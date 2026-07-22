//===- bolt/tools/llvm-bolt-regres/RegReallocEngine.cpp -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of RegReallocEngine and FunctionRegContext with candidate reservations
// and strategy-based candidate set partitioning.
//
//===----------------------------------------------------------------------===//

#include "RegReallocEngine.h"
#include "bolt/Core/MCPlus.h"
#include "bolt/Core/MCPlusBuilder.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include <algorithm>
#include <numeric>

#define DEBUG_TYPE "spare-regs"

namespace llvm {
namespace bolt {

FunctionRegContext FunctionRegContext::create(
    const BinaryFunction &BF, ArrayRef<std::string> TargetRegNames) {
  const BinaryContext &BC = BF.getBinaryContext();
  FunctionRegContext Ctx;

  Ctx.GPRegs.resize(BC.MRI->getNumRegs(), false);
  BC.MIB->getGPRegs(Ctx.GPRegs);

  Ctx.CalleeSavedRegs.resize(BC.MRI->getNumRegs(), false);
  BC.MIB->getCalleeSavedRegs(Ctx.CalleeSavedRegs);

  BitVector SpareTargetRegs(BC.MRI->getNumRegs(), false);
  for (const std::string &TargetRegName : TargetRegNames) {
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(TargetRegName)) {
        SpareTargetRegs |= BC.MIB->getAliases(R, false);
        break;
      }
    }
  }

  Ctx.CandidatePool.resize(BC.MRI->getNumRegs(), false);
  BC.MIB->getGPRegs(Ctx.CandidatePool);
  Ctx.CandidatePool.flip();
  Ctx.CandidatePool |= SpareTargetRegs;
  Ctx.CandidatePool |= BC.MIB->getAliases(BC.MIB->getFramePointer(), false);
  Ctx.CandidatePool.flip();

  Ctx.PlannedReservedRegs.resize(BC.MRI->getNumRegs(), false);

  Ctx.UsedInFunction.resize(BC.MRI->getNumRegs(), false);
  for (const BinaryBasicBlock &BB : BF) {
    for (const MCInst &Inst : BB) {
      for (const MCOperand &Op : MCPlus::primeOperands(Inst)) {
        if (Op.isReg())
          Ctx.UsedInFunction |= BC.MIB->getAliases(Op.getReg(), false);
      }
      const MCInstrDesc &Desc = BC.MII->get(Inst.getOpcode());
      for (MCPhysReg ImpUse : Desc.implicit_uses())
        Ctx.UsedInFunction |= BC.MIB->getAliases(ImpUse, false);
      for (MCPhysReg ImpDef : Desc.implicit_defs())
        Ctx.UsedInFunction |= BC.MIB->getAliases(ImpDef, false);
    }
  }

  Ctx.ABIArgRegs.resize(BC.MRI->getNumRegs(), false);
  for (const char *ArgName : {"RDI", "RSI", "RDX", "RCX", "R8", "R9"}) {
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(ArgName)) {
        Ctx.ABIArgRegs |= BC.MIB->getAliases(R, false);
        break;
      }
    }
  }

  // Rank candidate registers: Unused registers in function get top priority
  Ctx.RankedRegs.resize(BC.MRI->getNumRegs());
  std::iota(Ctx.RankedRegs.begin(), Ctx.RankedRegs.end(), 0);
  std::stable_sort(Ctx.RankedRegs.begin(), Ctx.RankedRegs.end(),
                   [&](size_t A, size_t B) {
                     BitVector AliasesA = BC.MIB->getAliases(A, false);
                     BitVector AliasesB = BC.MIB->getAliases(B, false);

                     bool UnusedA = !Ctx.UsedInFunction.anyCommon(AliasesA);
                     bool UnusedB = !Ctx.UsedInFunction.anyCommon(AliasesB);

                     if (UnusedA != UnusedB)
                       return UnusedA > UnusedB;

                     return A < B;
                   });

  return Ctx;
}

void RegReallocEngine::reserveCandidate(MCPhysReg CandidateReg) {
  const BinaryContext &BC = BF.getBinaryContext();
  RegCtx.PlannedReservedRegs |= BC.MIB->getAliases(CandidateReg, false);
}

MCPhysReg RegReallocEngine::findCandidate(const RegisterWeb &W,
                                          MCPhysReg TargetReg,
                                          const RegReallocOptions &Opts) const {
  const BinaryContext &BC = BF.getBinaryContext();
  BitVector TargetAliases = BC.MIB->getAliases(TargetReg, false);

  // Check if web demands match strategy options
  if (W.LiveAtEntry && !Opts.EvictEntryArg)
    return 0;
  if (W.CrossesCallSite && !Opts.ShiftCalleeSaved)
    return 0;

  bool TargetIsCalleeSaved = RegCtx.CalleeSavedRegs.test(TargetReg);

  for (size_t RegIdx : RegCtx.RankedRegs) {
    if (!RegCtx.GPRegs[RegIdx] || BC.MIB->getRegSize(RegIdx) != 8)
      continue;

    BitVector CandAliases = BC.MIB->getAliases(RegIdx, false);

    if (TargetAliases.test(RegIdx))
      continue;

    if (!RegCtx.CandidatePool.anyCommon(CandAliases))
      continue;

    // Check if candidate register is already reserved by a planned web in this batch
    if (RegCtx.PlannedReservedRegs.anyCommon(CandAliases))
      continue;

    bool CandIsCalleeSaved = RegCtx.CalleeSavedRegs.test(RegIdx);
    bool IsABIArg = RegCtx.ABIArgRegs.anyCommon(CandAliases);

    // Direct Swap (0 added cost): Volatile -> Callee adds new push/pop (Not allowed in 0-cost swap)
    if (!Opts.ShiftCalleeSaved && !TargetIsCalleeSaved && CandIsCalleeSaved)
      continue;

    // Phase 2 (CalleeShift / ArgCalleeEviction): Callee-saved candidates ONLY
    if (Opts.ShiftCalleeSaved && !CandIsCalleeSaved)
      continue;

    if (Opts.EvictEntryArg && IsABIArg)
      continue;

    bool IsUsedInFunc = RegCtx.UsedInFunction.anyCommon(CandAliases);
    if (IsUsedInFunc && Extractor.isLiveDuringWeb(RegIdx, W))
      continue;

    return RegIdx;
  }

  return 0;
}

void RegReallocEngine::applyReallocation(const RegisterWeb &W,
                                         MCPhysReg TargetReg,
                                         MCPhysReg CandidateReg,
                                         const RegReallocOptions &Opts) {
  const BinaryContext &BC = BF.getBinaryContext();
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
