//===- bolt/tools/llvm-bolt-obj-cfg/RegReallocEngine.cpp -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of RegReallocEngine.
//
//===----------------------------------------------------------------------===//

#include "RegReallocEngine.h"
#include "bolt/Core/MCPlus.h"
#include "bolt/Core/MCPlusBuilder.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "MCTargetDesc/X86MCTargetDesc.h"

#define DEBUG_TYPE "reg-realloc"

namespace llvm {
namespace bolt {

bool RegReallocEngine::reallocateWeb(
    BinaryFunction &BF, RegisterWeb &W, MCPhysReg TargetReg,
    const RegReallocOptions &Opts, RegisterWebExtractor &Extractor,
    const BitVector &GPRegs, const BitVector &CalleeSavedRegs,
    const BitVector &CandidatePool, const BitVector &UsedInFunction,
    const std::vector<size_t> &RankedRegs, const BitVector &ABIArgRegs) {

  BinaryContext &BC = BF.getBinaryContext();
  StringRef SparedName = BC.MRI->getName(TargetReg);
  BitVector TargetAliases = BC.MIB->getAliases(TargetReg, false);

  // Check if web demands match strategy options
  if (W.LiveAtEntry && !Opts.EvictEntryArg)
    return false;
  if (W.CrossesCallSite && !Opts.ShiftCalleeSaved)
    return false;

  // Search for candidate register
  MCPhysReg SelectedCandidate = 0;

  for (size_t RegIdx : RankedRegs) {
    if (!GPRegs[RegIdx] || BC.MIB->getRegSize(RegIdx) != 8)
      continue;

    BitVector CandAliases = BC.MIB->getAliases(RegIdx, false);

    if (TargetAliases.test(RegIdx))
      continue;

    if (!CandidatePool.anyCommon(CandAliases))
      continue;

    bool IsCalleeSaved = CalleeSavedRegs.test(RegIdx);
    bool IsABIArg = ABIArgRegs.anyCommon(CandAliases);

    // If strategy requires callee-saved, candidate must be callee-saved
    if (Opts.ShiftCalleeSaved && !IsCalleeSaved)
      continue;

    // If strategy does NOT shift callee-saved, candidate must be volatile or unused callee-saved
    if (!Opts.ShiftCalleeSaved && IsCalleeSaved && UsedInFunction.anyCommon(CandAliases))
      continue;

    // For entry eviction, candidate must not be an ABI parameter register carrying inputs
    if (Opts.EvictEntryArg && IsABIArg)
      continue;

    // Candidate must not be live during this web
    bool IsUsedInFunc = UsedInFunction.anyCommon(CandAliases);
    if (IsUsedInFunc && Extractor.isLiveDuringWeb(RegIdx, W))
      continue;

    SelectedCandidate = RegIdx;
    break;
  }

  if (SelectedCandidate == 0)
    return false;

  StringRef CandName = BC.MRI->getName(SelectedCandidate);

  // 1. Rename operands in web
  for (MCInst *Inst : W.Instructions) {
    for (MCOperand &Op : MCPlus::primeOperands(*Inst)) {
      if (!Op.isReg())
        continue;
      MCPhysReg OpReg = Op.getReg();
      if (TargetAliases.test(OpReg)) {
        unsigned Size = BC.MIB->getRegSize(OpReg);
        MCPhysReg SizedCand = BC.MIB->getAliasSized(SelectedCandidate, Size);
        Op.setReg(SizedCand);
      }
    }
  }

  // 2. Perform Entry Eviction if requested
  BinaryBasicBlock &EntryBB = *BF.begin();
  if (Opts.EvictEntryArg) {
    MCInst MovInst;
    MovInst.setOpcode(X86::MOV64rr);
    MovInst.addOperand(MCOperand::createReg(SelectedCandidate));
    MovInst.addOperand(MCOperand::createReg(TargetReg));

    if (!Opts.ShiftCalleeSaved) {
      EntryBB.insertInstruction(EntryBB.begin(), MovInst);
    }
  }

  // 3. Perform Callee-Saved Shift (prologue push & epilogue pop + DWARF CFI) if requested
  if (Opts.ShiftCalleeSaved) {
    // Prologue Push
    MCInst PushInst;
    BC.MIB->createPushRegister(PushInst, SelectedCandidate, 8);
    auto PushIt = EntryBB.insertInstruction(EntryBB.begin(), PushInst);
    auto CFIIt = std::next(PushIt);
    CFIIt = BF.addCFIInstruction(
        &EntryBB, CFIIt, MCCFIInstruction::createAdjustCfaOffset(nullptr, 8));
    CFIIt = BF.addCFIInstruction(
        &EntryBB, CFIIt,
        MCCFIInstruction::createOffset(
            nullptr, BC.MRI->getDwarfRegNum(SelectedCandidate, false), -8));

    if (Opts.EvictEntryArg) {
      MCInst MovInst;
      MovInst.setOpcode(X86::MOV64rr);
      MovInst.addOperand(MCOperand::createReg(SelectedCandidate));
      MovInst.addOperand(MCOperand::createReg(TargetReg));
      EntryBB.insertInstruction(CFIIt, MovInst);
    }

    // Epilogue Pop on all return exits
    for (BinaryBasicBlock &BB : BF) {
      if (BB.succ_empty() || (!BB.empty() && (BC.MIB->isReturn(*BB.rbegin()) || BC.MIB->isTailCall(*BB.rbegin())))) {
        auto ExitIt = BB.end();
        if (!BB.empty() && (BC.MIB->isReturn(*BB.rbegin()) || BC.MIB->isTailCall(*BB.rbegin())))
          ExitIt = std::prev(BB.end());

        MCInst PopInst;
        BC.MIB->createPopRegister(PopInst, SelectedCandidate, 8);
        auto PopIt = BB.insertInstruction(ExitIt, PopInst);
        auto PopCFIIt = std::next(PopIt);
        PopCFIIt = BF.addCFIInstruction(
            &BB, PopCFIIt, MCCFIInstruction::createAdjustCfaOffset(nullptr, -8));
        BF.addCFIInstruction(
            &BB, PopCFIIt,
            MCCFIInstruction::createSameValue(
                nullptr, BC.MRI->getDwarfRegNum(SelectedCandidate, false)));
      }
    }
  }

  outs() << "  -> [SUCCESS_REALLOCATED] Reallocated " << SparedName
         << " to " << CandName << "\n";

  return true;
}

} // namespace bolt
} // namespace llvm
