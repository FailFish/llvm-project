//===- bolt/tools/llvm-bolt-regres/RegReallocEngine.cpp -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of RegReallocEngine (planning vs mutation separation).
//
//===----------------------------------------------------------------------===//

#include "RegReallocEngine.h"
#include "bolt/Core/MCPlus.h"
#include "bolt/Core/MCPlusBuilder.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "MCTargetDesc/X86MCTargetDesc.h"

namespace llvm {
namespace bolt {

MCPhysReg RegReallocEngine::findCandidate(
    BinaryFunction &BF, const RegisterWeb &W, MCPhysReg TargetReg,
    const RegReallocOptions &Opts, RegisterWebExtractor &Extractor,
    const BitVector &GPRegs, const BitVector &CalleeSavedRegs,
    const BitVector &CandidatePool, const BitVector &UsedInFunction,
    ArrayRef<size_t> RankedRegs, const BitVector &ABIArgRegs) {

  BinaryContext &BC = BF.getBinaryContext();
  BitVector TargetAliases = BC.MIB->getAliases(TargetReg, false);

  // Check if web demands match strategy options
  if (W.LiveAtEntry && !Opts.EvictEntryArg)
    return 0;
  if (W.CrossesCallSite && !Opts.ShiftCalleeSaved)
    return 0;

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

    if (Opts.ShiftCalleeSaved && !IsCalleeSaved)
      continue;

    if (!Opts.ShiftCalleeSaved && IsCalleeSaved && UsedInFunction.anyCommon(CandAliases))
      continue;

    if (Opts.EvictEntryArg && IsABIArg)
      continue;

    bool IsUsedInFunc = UsedInFunction.anyCommon(CandAliases);
    if (IsUsedInFunc && Extractor.isLiveDuringWeb(RegIdx, W))
      continue;

    return RegIdx;
  }

  return 0;
}

void RegReallocEngine::applyReallocation(BinaryFunction &BF, const RegisterWeb &W,
                                         MCPhysReg TargetReg,
                                         MCPhysReg CandidateReg,
                                         const RegReallocOptions &Opts) {
  BinaryContext &BC = BF.getBinaryContext();
  StringRef SparedName = BC.MRI->getName(TargetReg);
  StringRef CandName = BC.MRI->getName(CandidateReg);
  BitVector TargetAliases = BC.MIB->getAliases(TargetReg, false);

  // 1. Rename operands in web
  for (MCInst *Inst : W.Instructions) {
    for (MCOperand &Op : MCPlus::primeOperands(*Inst)) {
      if (!Op.isReg())
        continue;
      MCPhysReg OpReg = Op.getReg();
      if (TargetAliases.test(OpReg)) {
        unsigned Size = BC.MIB->getRegSize(OpReg);
        MCPhysReg SizedCand = BC.MIB->getAliasSized(CandidateReg, Size);
        Op.setReg(SizedCand);
      }
    }
  }

  // 2. Perform Entry Eviction if requested
  BinaryBasicBlock &EntryBB = *BF.begin();
  if (Opts.EvictEntryArg && !Opts.ShiftCalleeSaved) {
    MCInst MovInst;
    MovInst.setOpcode(X86::MOV64rr);
    MovInst.addOperand(MCOperand::createReg(CandidateReg));
    MovInst.addOperand(MCOperand::createReg(TargetReg));
    EntryBB.insertInstruction(EntryBB.begin(), MovInst);
  }

  // 3. Perform Callee-Saved Shift (prologue push & epilogue pop + DWARF CFI) if requested
  if (Opts.ShiftCalleeSaved) {
    // Prologue Push
    MCInst PushInst;
    BC.MIB->createPushRegister(PushInst, CandidateReg, 8);
    auto PushIt = EntryBB.insertInstruction(EntryBB.begin(), PushInst);
    auto CFIIt = std::next(PushIt);
    CFIIt = BF.addCFIInstruction(
        &EntryBB, CFIIt, MCCFIInstruction::createAdjustCfaOffset(nullptr, 8));
    CFIIt = BF.addCFIInstruction(
        &EntryBB, CFIIt,
        MCCFIInstruction::createOffset(
            nullptr, BC.MRI->getDwarfRegNum(CandidateReg, false), -8));

    if (Opts.EvictEntryArg) {
      MCInst MovInst;
      MovInst.setOpcode(X86::MOV64rr);
      MovInst.addOperand(MCOperand::createReg(CandidateReg));
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
        BC.MIB->createPopRegister(PopInst, CandidateReg, 8);
        auto PopIt = BB.insertInstruction(ExitIt, PopInst);
        auto PopCFIIt = std::next(PopIt);
        PopCFIIt = BF.addCFIInstruction(
            &BB, PopCFIIt, MCCFIInstruction::createAdjustCfaOffset(nullptr, -8));
        BF.addCFIInstruction(
            &BB, PopCFIIt,
            MCCFIInstruction::createSameValue(
                nullptr, BC.MRI->getDwarfRegNum(CandidateReg, false)));
      }
    }
  }

  outs() << "  -> [SUCCESS_REALLOCATED] Reallocated " << SparedName
         << " to " << CandName << "\n";
}

} // namespace bolt
} // namespace llvm
