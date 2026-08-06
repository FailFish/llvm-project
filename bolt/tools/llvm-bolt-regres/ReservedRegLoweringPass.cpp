//===- bolt/tools/llvm-bolt-regres/ReservedRegLoweringPass.cpp --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of Single-Pass Reserved Physical Register Lowering.
//
//===----------------------------------------------------------------------===//

#include "ReservedRegLoweringPass.h"
#include "PseudoRewriter.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "bolt/Core/BinaryBasicBlock.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/MCPlus.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCLFIRewriter.h"

namespace llvm {
namespace bolt {

namespace {

/// Helper to find a dead scratch GPR at the current instruction position using liveness analysis.
MCPhysReg findDeadScratchGPR(const BinaryContext &BC, LivenessAnalysis *LA,
                             const MCInst &Inst, const BitVector &TargetAliases) {
  if (!LA)
    return 0;

  static const MCPhysReg ScratchCandidates[] = {
      X86::R10, X86::R11, X86::RAX, X86::RCX, X86::RDX, X86::RSI, X86::RDI, X86::R8, X86::R9};

  for (MCPhysReg Cand : ScratchCandidates) {
    if (TargetAliases.test(Cand))
      continue;

    BitVector CandAliases = BC.MIB->getAliases(Cand, /*OnlySmaller=*/false);
    bool MentionsCand = false;
    for (const MCOperand &Op : MCPlus::primeOperands(Inst)) {
      if (Op.isReg() && CandAliases.test(Op.getReg())) {
        MentionsCand = true;
        break;
      }
    }
    if (MentionsCand)
      continue;

    auto State = LA->getStateAt(Inst);
    if (!State)
      return 0;

    if (!State->anyCommon(BC.MIB->getAliases(Cand)))
      return Cand;
  }

  return 0;
}

/// Fallback helper to find a non-operand GPR candidate when liveness is unavailable.
MCPhysReg findNonOperandGPR(const BinaryContext &BC, const MCInst &Inst,
                            const BitVector &TargetAliases) {
  static const MCPhysReg FallbackCandidates[] = {
      X86::R10, X86::R11, X86::RAX, X86::RCX, X86::RDX, X86::RSI, X86::RDI, X86::R8, X86::R9};

  for (MCPhysReg Cand : FallbackCandidates) {
    if (TargetAliases.test(Cand))
      continue;

    BitVector CandAliases = BC.MIB->getAliases(Cand, /*OnlySmaller=*/false);
    bool MentionsCand = false;
    for (const MCOperand &Op : MCPlus::primeOperands(Inst)) {
      if (Op.isReg() && CandAliases.test(Op.getReg())) {
        MentionsCand = true;
        break;
      }
    }
    if (!MentionsCand)
      return Cand;
  }

  return X86::R10;
}

} // static namespace

MCInst ReservedRegLoweringPass::createSpillInst(MCPhysReg Reg, const VirtRegStorage &Storage) {
  MCInst SpillInst;
  SpillInst.setOpcode(X86::MOV64mr);

  if (Storage.isTLSSegment()) {
    SpillInst.addOperand(MCOperand::createReg(0));
    SpillInst.addOperand(MCOperand::createImm(1));
    SpillInst.addOperand(MCOperand::createReg(0));
    SpillInst.addOperand(MCOperand::createImm(Storage.getOffset()));
    SpillInst.addOperand(MCOperand::createReg(X86::FS));
  } else if (Storage.isStackSlot()) {
    SpillInst.addOperand(MCOperand::createReg(X86::RSP));
    SpillInst.addOperand(MCOperand::createImm(1));
    SpillInst.addOperand(MCOperand::createReg(0));
    SpillInst.addOperand(MCOperand::createImm(Storage.getOffset()));
    SpillInst.addOperand(MCOperand::createReg(0));
  } else {
    // Custom TLS Base Register
    SpillInst.addOperand(MCOperand::createReg(Storage.getBaseGPR()));
    SpillInst.addOperand(MCOperand::createImm(1));
    SpillInst.addOperand(MCOperand::createReg(0));
    SpillInst.addOperand(MCOperand::createImm(Storage.getOffset()));
    SpillInst.addOperand(MCOperand::createReg(0));
  }

  SpillInst.addOperand(MCOperand::createReg(Reg));
  return SpillInst;
}

MCInst ReservedRegLoweringPass::createRestoreInst(MCPhysReg Reg, const VirtRegStorage &Storage) {
  MCInst RestoreInst;
  RestoreInst.setOpcode(X86::MOV64rm);
  RestoreInst.addOperand(MCOperand::createReg(Reg));

  if (Storage.isTLSSegment()) {
    RestoreInst.addOperand(MCOperand::createReg(0));
    RestoreInst.addOperand(MCOperand::createImm(1));
    RestoreInst.addOperand(MCOperand::createReg(0));
    RestoreInst.addOperand(MCOperand::createImm(Storage.getOffset()));
    RestoreInst.addOperand(MCOperand::createReg(X86::FS));
  } else if (Storage.isStackSlot()) {
    RestoreInst.addOperand(MCOperand::createReg(X86::RSP));
    RestoreInst.addOperand(MCOperand::createImm(1));
    RestoreInst.addOperand(MCOperand::createReg(0));
    RestoreInst.addOperand(MCOperand::createImm(Storage.getOffset()));
    RestoreInst.addOperand(MCOperand::createReg(0));
  } else {
    // Custom TLS Base Register
    RestoreInst.addOperand(MCOperand::createReg(Storage.getBaseGPR()));
    RestoreInst.addOperand(MCOperand::createImm(1));
    RestoreInst.addOperand(MCOperand::createReg(0));
    RestoreInst.addOperand(MCOperand::createImm(Storage.getOffset()));
    RestoreInst.addOperand(MCOperand::createReg(0));
  }

  return RestoreInst;
}

void ReservedRegLoweringPass::lowerTargetRegisterInsts() {
  BinaryContext &BC = Function.getBinaryContext();

  for (const ReservedRegConfig &Config : TargetConfigs) {
    MCPhysReg TargetReg = 0;
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(Config.getTargetRegName())) {
        TargetReg = R;
        break;
      }
    }

    if (TargetReg == 0)
      continue;

    BitVector TargetAliases = BC.MIB->getAliases(TargetReg, /*OnlySmaller=*/false);
    const VirtRegStorage &Storage = Config.getStorage();

    for (BinaryBasicBlock &BB : Function) {
      for (size_t i = 0; i < BB.size(); ++i) {
        MCInst &Inst = *(BB.begin() + i);

        if (BC.MIB->hasAnnotation(Inst, "PseudoRegMove") ||
            BC.MIB->hasAnnotation(Inst, "PseudoSpillSave") ||
            BC.MIB->hasAnnotation(Inst, "PseudoSpillRestore"))
          continue;

        bool IsUse = BC.MIB->hasUseOfPhysReg(Inst, TargetReg);
        bool IsDef = BC.MIB->hasDefOfPhysReg(Inst, TargetReg);
        if (!IsUse && !IsDef)
          continue;

        MCPhysReg DeadScratch = findDeadScratchGPR(BC, LA, Inst, TargetAliases);
        MCPhysReg ScratchReg = DeadScratch ? DeadScratch : findNonOperandGPR(BC, Inst, TargetAliases);
        bool NeedOuterSpill = (DeadScratch == 0);

        for (MCOperand &Op : MCPlus::primeOperands(Inst)) {
          if (Op.isReg() && TargetAliases.test(Op.getReg()))
            Op.setReg(ScratchReg);
        }

        SmallVector<MCInst, 5> LoweredSequence;

        auto addSafeInst = [&](MCInst InstToWrap) {
          if (BC.TheTriple->isLFI())
            InstToWrap.setFlags(InstToWrap.getFlags() | IP_SKIP_REWRITE);
          LoweredSequence.push_back(InstToWrap);
        };

        // 1. Save live scratch GPR if dead scratch was unavailable
        if (NeedOuterSpill)
          addSafeInst(createSpillInst(ScratchReg, TempScratchStorage));

        // 2. Load virtual register value from TLS memory backing if instruction reads target register
        if (IsUse)
          addSafeInst(createRestoreInst(ScratchReg, Storage));

        // 3. Original instruction (now operating on ScratchReg)
        LoweredSequence.push_back(Inst);

        // 4. Store updated virtual register value back to TLS memory backing if instruction defines target register
        if (IsDef)
          addSafeInst(createSpillInst(ScratchReg, Storage));

        // 5. Restore live scratch GPR if saved
        if (NeedOuterSpill)
          addSafeInst(createRestoreInst(ScratchReg, TempScratchStorage));

        auto ReplacePos = BB.begin() + i;
        BB.replaceInstruction(ReplacePos, LoweredSequence.begin(), LoweredSequence.end());
        i += LoweredSequence.size() - 1;
      }
    }
  }
}

void ReservedRegLoweringPass::expandPseudosAndCFI() {
  PseudoRewriter Rewriter(Function);
  Rewriter.runOnFunction();
}

bool ReservedRegLoweringPass::runOnFunction() {
  lowerTargetRegisterInsts();
  expandPseudosAndCFI();
  return true;
}

} // namespace bolt
} // namespace llvm
