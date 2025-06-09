#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64MCInstInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "aarch64-lfi-pass"

using namespace llvm;

namespace {
class AArch64LFI : public MachineFunctionPass {
  const TargetInstrInfo *TII;
  MachineLoopInfo *MLI;

public:
  static char ID;
  AArch64LFI() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "AArch64 LFI Pass";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

char AArch64LFI::ID = 0;

} // end anonymous namespace

bool AArch64LFI::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "Running AArch64 LFI Pass on function: " << MF.getName() << "\n");

  TII = MF.getSubtarget().getInstrInfo();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();

  for (auto &MBB : MF) {
    MachineLoop *L = MLI->getLoopFor(&MBB);
    for (auto &MI : llvm::make_early_inc_range(MBB)) {
      if (MI.getOpcode() == AArch64::LDRWui) {
        printf("hello\n");

        MachineInstr *NewMI = BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
          .addReg(AArch64::X18)
          .addReg(AArch64::X21)
          .add(MI.getOperand(1))
          .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));

        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(MI.getOpcode()))
          .add(MI.getOperand(0))
          .addReg(AArch64::X18)
          .add(MI.getOperand(2));

        MI.eraseFromParent();

        Changed = true;

        // MachineBasicBlock *Preheader = L->getLoopPreheader();
        // if (!Preheader)
        //   continue;
        // BuildMI(*Preheader, Preheader->getFirstTerminator(), NewMI->getDebugLoc(), TII->get(NewMI->getOpcode()))
        //   .add(NewMI->getOperand(0)) // x18
        //   .add(NewMI->getOperand(1)) // x21
        //   .add(NewMI->getOperand(2))
        //   .add(NewMI->getOperand(3)); // wN (with UXTW shift)
        //
        // NewMI->eraseFromParent();
      }
    }
  }

  return Changed;
}

// Register the pass
INITIALIZE_PASS(AArch64LFI, "aarch64-lfi-pass",
                "AArch64 LFI Pass", false, false)

FunctionPass *llvm::createAArch64LFIPass() {
  return new AArch64LFI();
}
