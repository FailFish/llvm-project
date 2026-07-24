//===- bolt/tools/llvm-bolt-regres/PseudoRewriter.cpp ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of PseudoRewriter lowering pass.
//
//===----------------------------------------------------------------------===//

#include "PseudoRewriter.h"
#include "bolt/Core/BinaryBasicBlock.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/MCPlusBuilder.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include "MCTargetDesc/X86MCTargetDesc.h"

namespace llvm {
namespace bolt {

void createPseudoRegMove(BinaryContext &BC, BinaryBasicBlock &EntryBB,
                         BinaryBasicBlock::iterator InsertPos,
                         MCPhysReg CandidateReg, MCPhysReg TargetReg) {
  MCInst Inst;
  Inst.setOpcode(X86::MOV64rr);
  Inst.addOperand(MCOperand::createReg(CandidateReg));
  Inst.addOperand(MCOperand::createReg(TargetReg));

  BC.MIB->addAnnotation(Inst, "PseudoRegMove", true);
  BC.MIB->addAnnotation(Inst, "TargetReg", static_cast<int64_t>(TargetReg));

  EntryBB.insertInstruction(InsertPos, Inst);
}

void createPseudoSpillSave(BinaryContext &BC, BinaryBasicBlock &EntryBB,
                          MCPhysReg CandidateReg, MCPhysReg TargetReg) {
  MCInst Inst;
  BC.MIB->createPushRegister(Inst, CandidateReg, 8);

  BC.MIB->addAnnotation(Inst, "PseudoSpillSave", true);
  BC.MIB->addAnnotation(Inst, "TargetReg", static_cast<int64_t>(TargetReg));

  EntryBB.insertInstruction(EntryBB.begin(), Inst);
}

void createPseudoSpillRestore(BinaryContext &BC, BinaryBasicBlock &BB,
                             BinaryBasicBlock::iterator ExitIt,
                             MCPhysReg CandidateReg, MCPhysReg TargetReg) {
  MCInst Inst;
  BC.MIB->createPopRegister(Inst, CandidateReg, 8);

  BC.MIB->addAnnotation(Inst, "PseudoSpillRestore", true);
  BC.MIB->addAnnotation(Inst, "TargetReg", static_cast<int64_t>(TargetReg));

  BB.insertInstruction(ExitIt, Inst);
}

bool PseudoRewriter::runOnFunction() {
  BinaryContext &BC = Function.getBinaryContext();
  bool Changed = false;

  // Process Prologue Pushes (PseudoSpillSave) & Entry Evictions (PseudoRegMove)
  if (!Function.empty()) {
    BinaryBasicBlock &EntryBB = *Function.begin();
    for (size_t i = 0; i < EntryBB.size(); ++i) {
      MCInst &Inst = *(EntryBB.begin() + i);
      if (BC.MIB->isCFI(Inst))
        continue;

      if (BC.MIB->hasAnnotation(Inst, "PseudoSpillSave")) {
        MCPhysReg CandidateReg = Inst.getOperand(0).getReg();
        BC.MIB->removeAnnotation(Inst, "PseudoSpillSave");
        BC.MIB->removeAnnotation(Inst, "TargetReg");

        // Add CFI instructions for prologue push
        auto InsertPos = EntryBB.begin() + i + 1;
        InsertPos = Function.addCFIInstruction(
            &EntryBB, InsertPos, MCCFIInstruction::createAdjustCfaOffset(nullptr, 8));
        Function.addCFIInstruction(
            &EntryBB, InsertPos,
            MCCFIInstruction::createOffset(
                nullptr, BC.MRI->getDwarfRegNum(CandidateReg, false), -8));

        i += 2; // Advance index past the 2 inserted CFI instructions
        Changed = true;
      } else if (BC.MIB->hasAnnotation(Inst, "PseudoRegMove")) {
        BC.MIB->removeAnnotation(Inst, "PseudoRegMove");
        BC.MIB->removeAnnotation(Inst, "TargetReg");
        Changed = true;
      }
    }
  }

  // Process Return Epilogue Pops (PseudoSpillRestore)
  for (BinaryBasicBlock &BB : Function) {
    for (size_t i = 0; i < BB.size(); ++i) {
      MCInst &Inst = *(BB.begin() + i);
      if (BC.MIB->isCFI(Inst))
        continue;

      if (BC.MIB->hasAnnotation(Inst, "PseudoSpillRestore")) {
        MCPhysReg CandidateReg = Inst.getOperand(0).getReg();
        BC.MIB->removeAnnotation(Inst, "PseudoSpillRestore");
        BC.MIB->removeAnnotation(Inst, "TargetReg");

        // Add CFI instructions for epilogue pop
        auto InsertPos = BB.begin() + i + 1;
        InsertPos = Function.addCFIInstruction(
            &BB, InsertPos, MCCFIInstruction::createAdjustCfaOffset(nullptr, -8));
        Function.addCFIInstruction(
            &BB, InsertPos,
            MCCFIInstruction::createSameValue(
                nullptr, BC.MRI->getDwarfRegNum(CandidateReg, false)));

        i += 2; // Advance index past the 2 inserted CFI instructions
        Changed = true;
      }
    }
  }

  return Changed;
}

} // namespace bolt
} // namespace llvm
