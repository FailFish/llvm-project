//===- SafeStackFrameCheck.cpp - Enforce SafeStack's SP-relative policy ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Under SafeStack's SP-relative policy the pass keeps an object on the native
// stack only if it can see that every access folds into a stack-pointer-plus-
// constant memory operand. That is a promise about generated code made from
// the IR, one instruction selection and the machine optimizers could break
// without anything else noticing: on the targets that want this policy, the
// safe stack is reachable only that way, so an address that reaches a register
// is not slower, it is wrong.
//
// This turns the promise into a build failure. It is a standalone pass rather
// than a MachineVerifier check because it has to run in release builds -- it
// is a guarantee about shipped code, not a debugging aid.
//
// It runs as late as possible before register allocation: after the machine-SSA
// optimizers, so that anything they fold is inside the checked region, and
// before allocation, because the spill slots that introduces are not IR objects
// and are addressed however the target likes. Frame indices stay symbolic until
// prologue/epilogue insertion, so the form is still visible here.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "safe-stack-frame-check"

namespace {

class SafeStackFrameCheck : public MachineFunctionPass {
public:
  static char ID;

  SafeStackFrameCheck() : MachineFunctionPass(ID) {
    initializeSafeStackFrameCheckPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "SafeStack SP-relative frame check";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

char SafeStackFrameCheck::ID = 0;

INITIALIZE_PASS(SafeStackFrameCheck, DEBUG_TYPE,
                "SafeStack SP-relative frame check", false, false)

FunctionPass *llvm::createSafeStackFrameCheckPass() {
  return new SafeStackFrameCheck();
}

bool SafeStackFrameCheck::runOnMachineFunction(MachineFunction &MF) {
  const TargetSubtargetInfo &STI = MF.getSubtarget();

  // Self-gating, the same way the SafeStack pass is, and on both of the same
  // conditions. The check is only meaningful where the policy was actually
  // applied: a function the pass never instrumented takes the address of its
  // allocas as freely as any other, and flagging that would be nonsense.
  const Function &F = MF.getFunction();
  if (!F.hasFnAttribute(Attribute::SafeStack))
    return false;
  if (!STI.getTargetLowering()->getSPRelativePolicy(F))
    return false;

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetInstrInfo &TII = *STI.getInstrInfo();

  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
        const MachineOperand &MO = MI.getOperand(I);
        if (!MO.isFI())
          continue;

        // Only objects that came from an IR alloca are covered. Everything the
        // backend invented for itself -- spill slots, instruction selection
        // temporaries -- carries a null allocation and is none of the policy's
        // business.
        const AllocaInst *AI = MFI.getObjectAllocation(MO.getIndex());
        if (!AI)
          continue;

        if (TII.isSPRelativeFrameIndexUse(MI, I))
          continue;

        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "SafeStack: the address of '" << AI->getName() << "' in '"
           << MF.getName()
           << "' is not reachable stack-pointer-relative, but the SP-relative "
              "policy kept it on the native stack: ";
        MI.print(OS, /*IsStandalone=*/true, /*SkipOpers=*/false,
                 /*SkipDebugLoc=*/true);
        report_fatal_error(StringRef(Msg));
      }
    }
  }

  return false;
}
