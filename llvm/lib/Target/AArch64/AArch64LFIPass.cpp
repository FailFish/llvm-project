#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64MCInstInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

#define DEBUG_TYPE "aarch64-lfi-pass"
#define AARCH64_LFI_PASS_NAME "AArch64 LFI Pass"
#define DBGLOC dbgs() << "[" << __FUNCTION__ << ":" << __LINE__ << "] "

using namespace llvm;

namespace {
class AArch64LFI : public MachineFunctionPass {
  const AArch64InstrInfo *TII;
  MachineLoopInfo *MLI;
  MachineFunction *MF;

  enum RTCallType {
    RT_Syscall,
    RT_TLSRead,
    RT_TLSWrite,
  };

private:
  bool handleIndBr(MachineInstr &MI);
  bool handleSyscall(MachineInstr &MI);
  bool handleTLSRead(MachineInstr &MI);
  bool handleTLSWrite(MachineInstr &MI);
  bool handleWriteX30(MachineInstr &MI);
  bool handleLoadStore(MachineInstr &MI);
  SmallVector<MachineInstr *, 4> createRTCall(RTCallType Ty, MachineInstr &MI);
  SmallVector<MachineInstr *, 3> createSwap(MachineInstr &MI, Register R1, Register R2);
  MachineInstr *createAddXrx(MachineInstr &MI, Register DestReg, Register BaseReg, Register OffsetReg);
  MachineInstr *createAddFromBase(MachineInstr &MI, Register DestReg, Register OffsetReg);
  MachineInstr *createLoadTemp(MachineInstr &MI);
  MachineInstr *createSafeMemDemoted(MachineInstr &MI, unsigned BaseRegIdx);
  SmallVector<MachineInstr *, 2> createSafeMemN(MachineInstr &MI, unsigned BaseRegIdx);
  SmallVector<MachineInstr *, 2> createMemMask(MachineInstr &MI, unsigned int NewOp, Register Rd, Register Rt, bool IsLoad);
  bool runOnMachineInstr(MachineInstr &MI);

public:
  static char ID;
  AArch64LFI() : MachineFunctionPass(ID) {
    initializeAArch64LFIPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  // WARN: Hard guarantee
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override {
    return AARCH64_LFI_PASS_NAME;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char AArch64LFI::ID = 0;

// Register the pass
INITIALIZE_PASS(AArch64LFI, DEBUG_TYPE,
                AARCH64_LFI_PASS_NAME, false, false)


static void insertMIs(MachineBasicBlock::iterator InsertBefore, ArrayRef<MachineInstr *> MIs) {
  auto *MBB = InsertBefore->getParent();
  for (MachineInstr *MI : MIs) {
    MBB->insert(InsertBefore, MI);
    LLVM_DEBUG(DBGLOC << "[+] "; MI->dump()); // NOTE: always print after insert.
  }
  LLVM_DEBUG(DBGLOC << "[*] "; InsertBefore->dump());
}

MachineInstr *AArch64LFI::createAddXrx(MachineInstr &MI, Register DestReg, Register BaseReg, Register OffsetReg) {
  return BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx), DestReg)
    .addReg(BaseReg)
    .addReg(getWRegFromXReg(OffsetReg))
    .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
}

MachineInstr *AArch64LFI::createAddFromBase(MachineInstr &MI, Register DestReg, Register OffsetReg) {
  return createAddXrx(MI, DestReg, AArch64::X21, OffsetReg);
}

MachineInstr *AArch64LFI::createLoadTemp(MachineInstr &MI) {
  assert(MI.getDesc().mayLoad());
  int DestRegIdx;
  int BaseRegIdx;
  int OffsetIdx;
  bool IsPrePost;

  if (!getLoadInfo(MI.getOpcode(), DestRegIdx, BaseRegIdx, OffsetIdx, IsPrePost)) {
    LLVM_DEBUG(dbgs() << "WARNING: MachineInstr(mayLoad), but getLoadInfo cannot recognize: ";
        MI.dump(););
    return nullptr;
  }
  assert(MI.getOperand(OffsetIdx).isImm());

  bool IsPair = DestRegIdx + 2 == BaseRegIdx; // WARN:

  if (!isAddrReg(MI.getOperand(BaseRegIdx).getReg()))
    return nullptr;

  MachineInstrBuilder MIB = BuildMI(*MF, MI.getDebugLoc(), MI.getDesc());
  bool LRUsed = false;

  // NOTE: AArch64 Load Instr layout
  // [PrePost_BaseReg,] DestReg[, DestReg2], BaseReg, Offset(Reg|Imm)
  if (IsPrePost)
    MIB.addDef(MI.getOperand(BaseRegIdx).getReg());

  Register DestReg = MI.getOperand(DestRegIdx).getReg();
  MIB.addDef(DestReg == AArch64::LR ? (LRUsed = true, AArch64::X22) : DestReg);
  if (IsPair) {
    Register DestReg2 = MI.getOperand(DestRegIdx + 1).getReg();
    MIB.addDef(DestReg2 == AArch64::LR ? (LRUsed = true, AArch64::X22) : DestReg2);
  }

  if (LRUsed == false)
    return nullptr;

  MIB.addReg(MI.getOperand(BaseRegIdx).getReg());
  MIB.addImm(MI.getOperand(OffsetIdx).getImm());

  return MIB;
}

MachineInstr *AArch64LFI::createSafeMemDemoted(MachineInstr &MI, unsigned BaseRegIdx) {
  bool IsPre;
  auto NewOpCode = convertPrePostToBase(MI.getOpcode(), IsPre);
  MachineInstrBuilder MIB = BuildMI(*MF, MI.getDebugLoc(), TII->get(NewOpCode));

  for (unsigned I = 1; I < BaseRegIdx; I++)
    MIB.add(MI.getOperand(I));
  MIB.addReg(AArch64::X18);
  if (IsPre)
    MIB.add(MI.getOperand(BaseRegIdx + 1));
  else if (isPairedLdSt(NewOpCode)) // CHECK: MCInst never had this
    MIB.addImm(0);

  return MIB;
}

SmallVector<MachineInstr *, 2> AArch64LFI::createSafeMemN(MachineInstr &MI, unsigned BaseRegIdx) {
  SmallVector<MachineInstr *, 2> Instrs;
  MachineInstrBuilder MIB = BuildMI(*MF, MI.getDebugLoc(), TII->get(MI.getOpcode()));
  bool PostGuard = false;
  for (unsigned I = 0; I < BaseRegIdx; I++) {
    auto MO = MI.getOperand(I);
    if (MO.isReg() && MO.getReg() == AArch64::LR) {
      PostGuard = true;
      MIB.addReg(AArch64::X22, MO.isDef() ? RegState::Define : 0);
    } else {
      MIB.add(MO);
    }
  }
  MIB.addReg(AArch64::X18);

  for (unsigned I = BaseRegIdx + 1; I < MI.getNumOperands(); I++)
    MIB.add(MI.getOperand(I));

  Instrs.push_back(MIB);
  if (PostGuard) {
    Instrs.push_back(createAddFromBase(MI, AArch64::LR, AArch64::X22));
  }

  return Instrs;
}
SmallVector<MachineInstr *, 2> AArch64LFI::createMemMask(MachineInstr &MI, unsigned int NewOp, Register Rd, Register Rt, bool IsLoad) {
  SmallVector<MachineInstr *, 2> Instrs;
  Register SafeRd = Rd;
  if (Rd == AArch64::LR)
    SafeRd = AArch64::X22;
  MachineInstr *NewMI = BuildMI(*MF, MI.getDebugLoc(), TII->get(NewOp))
    .addReg(SafeRd, IsLoad ? RegState::Define : 0)
    .addReg(AArch64::X21)
    .addReg(getWRegFromXReg(Rt))
    .addImm(AArch64_AM::getMemExtendImm(AArch64_AM::UXTW, 0))
    .addImm(0);
  Instrs.push_back(NewMI);

  if (Rd == AArch64::LR) {
    MachineInstr *PostGuard = createAddFromBase(MI, Rd, SafeRd);
    Instrs.push_back(PostGuard);
  }
  return Instrs;
}

SmallVector<MachineInstr *, 3> AArch64LFI::createSwap(MachineInstr &MI, Register R1, Register R2) {
  MachineInstr *Swap1 = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::EORXrs))
    .addReg(R1)
    .addReg(R1)
    .addReg(R2);
  MachineInstr *Swap2 = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::EORXrs))
    .addReg(R2)
    .addReg(R1)
    .addReg(R2);
  MachineInstr *Swap3 = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::EORXrs))
    .addReg(R1)
    .addReg(R1)
    .addReg(R2);

  return {Swap1, Swap2, Swap3};
}

bool AArch64LFI::handleWriteX30(MachineInstr &MI) {
  MachineInstr *Load = createLoadTemp(MI);
  if (Load == nullptr)
    return false;

  MachineInstr *Add = createAddFromBase(MI, AArch64::LR, AArch64::X22);

  insertMIs(MI, {Load, Add});
  MI.eraseFromParent();
  return true;
}

SmallVector<MachineInstr *, 4> AArch64LFI::createRTCall(RTCallType Ty, MachineInstr &MI) {
  MachineInstr *Mov = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ORRXrs), AArch64::X22)
    .addReg(AArch64::XZR)
    .addReg(AArch64::LR)
    .addImm(0);

  auto Offset = 0;
  switch (Ty) {
    case RT_Syscall: Offset = 0; break;
    case RT_TLSRead: Offset = 1; break;
    case RT_TLSWrite: Offset = 2; break;
  }

  // ldr x30, [x21]
  MachineInstr *Load = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDRXui), AArch64::LR)
    .addReg(AArch64::X21)
    .addImm(Offset);
  // blr x30
  MachineInstr *Blr = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::BLR))
    .addReg(AArch64::LR);
  // add x30, x21, w22, uxtw
  MachineInstr *Add = createAddFromBase(MI, AArch64::LR, AArch64::X22);

  return {Mov, Load, Blr, Add};
}


bool AArch64LFI::handleTLSWrite(MachineInstr &MI) {
  Register Reg = MI.getOperand(1).getReg();
  SmallVector<MachineInstr *> Instrs;

  if (Reg != AArch64::X0) {
    Instrs.append(createSwap(MI, Reg, AArch64::X0));
  }

  auto CallSeq = createRTCall(RTCallType::RT_TLSWrite, MI);
  Instrs.append(CallSeq);

  if (Reg != AArch64::X0) {
    Instrs.append(createSwap(MI, Reg, AArch64::X0));
  }

  insertMIs(MI, Instrs);

  MI.eraseFromParent();
  return true;
}

bool AArch64LFI::handleTLSRead(MachineInstr &MI) {
  Register Reg = MI.getOperand(0).getReg();
  SmallVector<MachineInstr *> Instrs;

  if (Reg != AArch64::X0) {
    Instrs.append(createSwap(MI, Reg, AArch64::X0));
  }

  auto CallSeq = createRTCall(RTCallType::RT_TLSRead, MI);
  Instrs.append(CallSeq);

  if (Reg != AArch64::X0) {
    Instrs.append(createSwap(MI, Reg, AArch64::X0));
  }

  insertMIs(MI, Instrs);

  MI.eraseFromParent();
  return true;
}

bool AArch64LFI::handleSyscall(MachineInstr &MI) {
  auto CallSeq = createRTCall(RTCallType::RT_Syscall, MI);

  insertMIs(MI, CallSeq);

  MI.eraseFromParent();
  return true;
}

bool AArch64LFI::handleIndBr(MachineInstr &MI) {
  MachineOperand &MO = MI.getOperand(0);
  Register Reg = MO.getReg();

  if (isAddrReg(Reg)) {
    return false;
  }

  MachineInstr *MaskMI = createAddFromBase(MI, AArch64::X18, Reg);
  MachineInstr *NewIndBr =
    BuildMI(*MF, MI.getDebugLoc(), MI.getDesc())
    .addReg(AArch64::X18);

  insertMIs(MI, {MaskMI, NewIndBr});

  MI.eraseFromParent();

  return true;
}

bool AArch64LFI::handleLoadStore(MachineInstr &MI) {
  LLVM_DEBUG(DBGLOC << '\n';);
  int DestRegIdx;
  int BaseRegIdx;
  int OffsetIdx;
  bool IsPrePost;

  if (!getLoadInfo(MI.getOpcode(), DestRegIdx, BaseRegIdx, OffsetIdx, IsPrePost)) {
    if (!getStoreInfo(MI.getOpcode(), DestRegIdx, BaseRegIdx, OffsetIdx, IsPrePost)) {
      return false;
    }
  }

  if (isMemNoMaskAddr(MI.getOpcode())) {
    Register BaseReg = MI.getOperand(BaseRegIdx).getReg();
    if (isAddrReg(BaseReg)) {
      return false;
    }
    SmallVector<MachineInstr *, 2> Instrs;
    MachineInstr *Mask = createAddFromBase(MI, AArch64::X18, BaseReg);
    Instrs.push_back(Mask);

    if (IsPrePost) {
      MachineInstr *SafeMem = createSafeMemDemoted(MI, BaseRegIdx);
      Instrs.push_back(SafeMem);

      MachineInstr *Add;
      if (MI.getOperand(OffsetIdx).isReg()) {
        Add = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrs), BaseReg)
          .addReg(BaseReg)
          .addReg(MI.getOperand(OffsetIdx).getReg())
          .addImm(0);
      } else {
        auto Offset = MI.getOperand(OffsetIdx).getImm() * getPrePostScale(MI.getOpcode());
        Add = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXri), BaseReg)
          .addReg(BaseReg)
          .addImm(Offset)
          .addImm(0);
      }
      Instrs.push_back(Add);
    } else {
      Instrs.append(createSafeMemN(MI, BaseRegIdx));
    }
    insertMIs(MI, Instrs);
    MI.eraseFromParent();
    return true;
  }

  SmallVector<MachineInstr *, 3> Instrs;
  unsigned MemOp;
  if ((MemOp = convertUiToRoW(MI.getOpcode())) != AArch64::INSTRUCTION_LIST_END) {
    Register BaseReg = MI.getOperand(BaseRegIdx).getReg();
    Register DestReg = MI.getOperand(DestRegIdx).getReg();
    bool IsDestDef = MI.getOperand(DestRegIdx).isDef();
    int64_t Offset = MI.getOperand(OffsetIdx).getImm();
    if (isAddrReg(BaseReg))
      return false;

    if (Offset == 0) {
      auto MaskChain = createMemMask(MI, MemOp, DestReg, BaseReg, IsDestDef);
      Instrs.append(MaskChain);
    } else {
      MachineInstr *Mask = createAddFromBase(MI, AArch64::X18, BaseReg);
      Instrs.push_back(Mask);
      auto SafeMem = createSafeMemN(MI, BaseRegIdx);
      Instrs.append(SafeMem);
    }
  }
  if ((MemOp = convertPreToRoW(MI.getOpcode())) != AArch64::INSTRUCTION_LIST_END) {
    Register BaseReg = MI.getOperand(BaseRegIdx).getReg();
    Register DestReg = MI.getOperand(DestRegIdx).getReg();
    bool IsDestDef = MI.getOperand(DestRegIdx).isDef();
    int64_t Offset = MI.getOperand(OffsetIdx).getImm();
    if (isAddrReg(BaseReg))
      return false;

    MachineInstr *Fixup;
    if (Offset >= 0) {
      Fixup = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXri), BaseReg)
          .addReg(BaseReg)
          .addImm(Offset)
          .addImm(0);
    } else {
      Fixup = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::SUBXri), BaseReg)
          .addReg(BaseReg)
          .addImm(-Offset)
          .addImm(0);
    }
    Instrs.push_back(Fixup);
    auto MaskChain = createMemMask(MI, MemOp, DestReg, BaseReg, IsDestDef);
    Instrs.append(MaskChain);
  }
  if ((MemOp = convertPostToRoW(MI.getOpcode())) != AArch64::INSTRUCTION_LIST_END) {
    Register BaseReg = MI.getOperand(BaseRegIdx).getReg();
    Register DestReg = MI.getOperand(DestRegIdx).getReg();
    bool IsDestDef = MI.getOperand(DestRegIdx).isDef();
    int64_t Offset = MI.getOperand(OffsetIdx).getImm();
    if (isAddrReg(BaseReg))
      return false;

    auto MaskChain = createMemMask(MI, MemOp, DestReg, BaseReg, IsDestDef);
    Instrs.append(MaskChain);

    MachineInstr *Fixup;
    if (Offset >= 0) {
      Fixup = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXri), BaseReg)
          .addReg(BaseReg)
          .addImm(Offset)
          .addImm(0);
    } else {
      Fixup = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::SUBXri), BaseReg)
          .addReg(BaseReg)
          .addImm(-Offset)
          .addImm(0);
    }
    Instrs.push_back(Fixup);
  }

  unsigned Shift;
  if ((MemOp = convertRoXToRoW(MI.getOpcode(), Shift)) != AArch64::INSTRUCTION_LIST_END) {
    Register DestReg = MI.getOperand(DestRegIdx).getReg();
    bool IsDestDef = MI.getOperand(DestRegIdx).isDef();
    Register BaseReg = MI.getOperand(BaseRegIdx).getReg();
    Register OffsetReg = MI.getOperand(OffsetIdx).getReg();
    int64_t Extend = MI.getOperand(OffsetIdx + 1).getImm();
    int64_t IsShift = MI.getOperand(OffsetIdx + 2).getImm();
    if (!IsShift)
      Shift = 0;

    MachineInstr *Fixup =
        BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
            .addReg(AArch64::X22)
            .addReg(BaseReg)
            .addReg(OffsetReg) // FIXME: WReg
            .addImm(
                Extend ? AArch64_AM::getArithExtendImm(AArch64_AM::SXTX, Shift)
                       : AArch64_AM::getArithExtendImm(AArch64_AM::LSL, Shift));
    Instrs.push_back(Fixup);
    auto MaskChain = createMemMask(MI, MemOp, DestReg, AArch64::X22, IsDestDef);
    Instrs.append(MaskChain);
  }
  if ((MemOp = convertRoWToRoW(MI.getOpcode(), Shift)) != AArch64::INSTRUCTION_LIST_END) {
    Register DestReg = MI.getOperand(DestRegIdx).getReg();
    bool IsDestDef = MI.getOperand(DestRegIdx).isDef();
    Register BaseReg = MI.getOperand(BaseRegIdx).getReg();
    Register OffsetReg = MI.getOperand(OffsetIdx).getReg();
    int64_t S = MI.getOperand(OffsetIdx + 1).getImm();
    int64_t IsShift = MI.getOperand(OffsetIdx + 2).getImm();
    if (!IsShift)
      Shift = 0;

    MachineInstr *Fixup =
        BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
            .addReg(AArch64::X22)
            .addReg(BaseReg)
            .addReg(OffsetReg)
            .addImm(
                S ? AArch64_AM::getArithExtendImm(AArch64_AM::SXTW, Shift)
                       : AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, Shift));
    Instrs.push_back(Fixup);
    auto MaskChain = createMemMask(MI, MemOp, DestReg, AArch64::X22, IsDestDef);
    Instrs.append(MaskChain);
  }

  insertMIs(MI, Instrs);
  MI.eraseFromParent();
  return true;
}
bool AArch64LFI::runOnMachineInstr(MachineInstr &MI) {
  LLVM_DEBUG(DBGLOC; MI.dump(););
  bool Changed = false;
  Register Reg;
  // TODO: need to check fallthrough
  switch (MI.getOpcode()) {
    case AArch64::BR:
    case AArch64::BLR:
    case AArch64::RET:
      Changed = handleIndBr(MI);
      break;
    case AArch64::SVC:
      Changed = handleSyscall(MI);
      break;
    case AArch64::MRS:
      // NOTE: In MIR level, Sysreg is an immediate.
      if (MI.getOperand(1).getImm() == AArch64SysReg::TPIDR_EL0) {
        Changed = handleTLSRead(MI);
      }
      break;
    case AArch64::MSR:
      if (MI.getOperand(0).getImm() == AArch64SysReg::TPIDR_EL0) {
        Changed = handleTLSWrite(MI);
      }
      break;
    case AArch64::ADDXri:
    case AArch64::SUBXri: // FIXME
      if (MI.getOperand(0).getReg() != AArch64::SP)
        break;
      // SP-updating ADD/SUB
      // TODO: refactor ADD/SUBs, handleSPUpdateADDSUB
      if (MI.getOperand(2).getImm() == 0) {
        MachineInstr *Guard =
          createAddFromBase(MI, AArch64::SP, MI.getOperand(1).getReg());
        insertMIs(MI, {Guard});
        MI.eraseFromParent();
      } else {
        MachineInstr *NewMI =
          BuildMI(*MF, MI.getDebugLoc(), MI.getDesc(), AArch64::X22)
          .addReg(AArch64::SP)
          .addImm(MI.getOperand(2).getImm())
          .addImm(MI.getOperand(3).getImm());
        MachineInstr *Guard =
          createAddFromBase(MI, AArch64::SP, AArch64::X22);
        insertMIs(MI, {NewMI, Guard});
        MI.eraseFromParent();
      }
      Changed = true;
      break;
    case AArch64::ADDXrr:
    case AArch64::SUBXrr:
    case AArch64::ADDXrs:
    case AArch64::SUBXrs:
    case AArch64::ADDXrx:
    case AArch64::SUBXrx:
    case AArch64::ADDXrx64:
    case AArch64::SUBXrx64:
      Reg = MI.getOperand(0).getReg();
      if (Reg != AArch64::SP)
        break;
      {
        MachineInstr *NewMI =
          BuildMI(*MF, MI.getDebugLoc(), MI.getDesc(), AArch64::X22)
          .addReg(AArch64::SP)
          .addReg(MI.getOperand(2).getReg())
          .addImm(MI.getOperand(3).getImm());
        MachineInstr *Guard =
          createAddFromBase(MI, AArch64::SP, AArch64::X22);
        insertMIs(MI, {NewMI, Guard});
        MI.eraseFromParent();
      }
      break;
    case AArch64::ORRXrs:
      Reg = MI.getOperand(0).getReg();
      if (Reg == AArch64::SP || Reg == AArch64::LR) {
        MachineInstr *NewMI =
          createAddFromBase(MI, Reg, MI.getOperand(2).getReg());
        insertMIs(MI, {NewMI});
        // keep the MI.
      }
      Changed = true;
      break;
    case TargetOpcode::BUNDLE:
      llvm_unreachable_internal("Didn't expect a bundle?");
      break;
      // NOTE: Two pseudo instrs are lowered by AArch64MCInstLower.
      // 1. CATCHRET, 2. CLEANUPRET
      // WARN: This may encounter pseudo instructions, which might be lowered
      // by later passes. (depending on relative pass order)
    case AArch64::LDRXui:
    case AArch64::LDRXpre:
    case AArch64::LDRXpost:
    case AArch64::LDPXi:
    case AArch64::LDPXpre:
    case AArch64::LDPXpost:
      if ((Changed = handleWriteX30(MI)))
          break;
      LLVM_FALLTHROUGH;
    default:
      // NOTE: MCID may be useful.
      if (MI.mayLoadOrStore()) {
        Changed |= handleLoadStore(MI);
        break;
      }
      if (MI.isIndirectBranch())
        llvm_unreachable_internal("missed AArch64 Indirect Branch Instr");
      LLVM_DEBUG(DBGLOC << "Skipping MachineInstr -- "; MI.dump(););
  }
  return Changed;
}

bool AArch64LFI::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "Running AArch64 LFI Pass on function: " << MF.getName()
      << "\n");
  bool Changed = false;

  TII = static_cast<const AArch64InstrInfo *>(MF.getSubtarget().getInstrInfo());
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  this->MF = &MF;

  for (auto &MBB : MF) {
    MachineLoop *L = MLI->getLoopFor(&MBB);
    for (auto &MI : llvm::make_early_inc_range(MBB)) {
      Changed |= runOnMachineInstr(MI);
    }
  }

  return Changed;
}

FunctionPass *llvm::createAArch64LFIPass() {
  return new AArch64LFI();
}

//       case AArch64::LDRXui:
//         if (isAddrReg(MI.getOperand(1).getReg()) && MI.getOperand(0).getReg() == AArch64::LR) {
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDRXui))
//             .addReg(AArch64::X22)
//             .addReg(MI.getOperand(1).getReg())
//             .addImm(MI.getOperand(2).getImm());
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//             .addReg(AArch64::LR)
//             .addReg(AArch64::X21)
//             .addReg(AArch64::X22)
//             .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//         }
//         break;
//       case AArch64::LDRXpre:
//         if (isAddrReg(MI.getOperand(2).getReg()) && MI.getOperand(1).getReg() == AArch64::LR) {
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDRXpre))
//             .addReg(MI.getOperand(0).getReg())
//             .addReg(AArch64::X22)
//             .addReg(MI.getOperand(2).getReg())
//             .addImm(MI.getOperand(3).getImm());
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//             .addReg(AArch64::LR)
//             .addReg(AArch64::X21)
//             .addReg(AArch64::X22)
//             .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//         }
//         break;
//       case AArch64::LDRXpost:
//         if (isAddrReg(MI.getOperand(2).getReg()) && MI.getOperand(1).getReg() == AArch64::LR) {
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDRXpost))
//             .addReg(MI.getOperand(0).getReg())
//             .addReg(AArch64::X22)
//             .addReg(MI.getOperand(2).getReg())
//             .addImm(MI.getOperand(3).getImm());
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//             .addReg(AArch64::LR)
//             .addReg(AArch64::X21)
//             .addReg(AArch64::X22)
//             .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//         }
//         break;
//       case AArch64::LDPXi:
//         if (isAddrReg(MI.getOperand(2).getReg())) {
//           if (MI.getOperand(0).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXi))
//               .addReg(AArch64::X22)
//               .addReg(MI.getOperand(1).getReg())
//               .addReg(MI.getOperand(2).getReg())
//               .addImm(MI.getOperand(3).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(AArch64::X21)
//               .addReg(AArch64::X22)
//               .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//           if (MI.getOperand(1).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXi))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(AArch64::X22)
//               .addReg(MI.getOperand(2).getReg())
//               .addImm(MI.getOperand(3).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(AArch64::X21)
//               .addReg(AArch64::X22)
//               .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//         }
//         break;
//       case AArch64::LDPXpre:
//         if (isAddrReg(MI.getOperand(3).getReg())) {
//           if (MI.getOperand(1).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXpre))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(AArch64::X22)
//               .addReg(MI.getOperand(2).getReg())
//               .addReg(MI.getOperand(3).getReg())
//               .addImm(MI.getOperand(4).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(AArch64::X21)
//               .addReg(AArch64::X22)
//               .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//           if (MI.getOperand(2).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXpre))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(MI.getOperand(1).getReg())
//               .addReg(AArch64::X22)
//               .addReg(MI.getOperand(3).getReg())
//               .addImm(MI.getOperand(4).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(AArch64::X21)
//               .addReg(AArch64::X22)
//               .addReg(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//         }
//         break;
//       case AArch64::LDPXpost:
//         if (isAddrReg(MI.getOperand(3).getReg())) {
//           if (MI.getOperand(1).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXpost))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(AArch64::X22)
//               .addReg(MI.getOperand(2).getReg())
//               .addReg(MI.getOperand(3).getReg())
//               .addImm(MI.getOperand(4).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(AArch64::X21)
//               .addReg(AArch64::X22)
//               .addReg(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//           if (MI.getOperand(2).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXpost))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(MI.getOperand(1).getReg())
//               .addReg(AArch64::X22)
//               .addReg(MI.getOperand(3).getReg())
//               .addImm(MI.getOperand(4).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(AArch64::X21)
//               .addReg(AArch64::X22)
//               .addReg(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//         }
//         break;

/*
  DestRegIdx = 0;
  BaseRegIdx = 1;
  OffsetIdx = 2;
  IsPrePost = false;

  // LDRXui
  if (isAddrReg(MI.getOperand(BaseRegIdx).getReg())
      && MI.getOperand(DestRegIdx).getReg() == AArch64::LR) {
    BuildMI(*MF, MI.getDebugLoc(), MI.getDesc())
      .addReg(AArch64::X22)
      .addReg(MI.getOperand(BaseRegIdx).getReg())
      .addImm(MI.getOperand(OffsetIdx).getImm());
    Changed = true;
  }

  // LDPXi
  DestRegIdx = 0;
  BaseRegIdx = 2;
  OffsetIdx = 3;
  IsPrePost = false;
  if (isAddrReg(MI.getOperand(BaseRegIdx).getReg())) {
    if (MI.getOperand(DestRegIdx).getReg() == AArch64::LR) {
      BuildMI(*MF, MI.getDebugLoc(), MI.getDesc())
        .addReg(AArch64::X22)
        .addReg(MI.getOperand(DestRegIdx + 1).getReg())
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addImm(MI.getOperand(OffsetIdx).getImm());
      Changed = true;
    }
    if (MI.getOperand(DestRegIdx + 1).getReg() == AArch64::LR) {
      BuildMI(*MF, MI.getDebugLoc(), MI.getDesc())
        .addReg(MI.getOperand(DestRegIdx).getReg())
        .addReg(AArch64::X22)
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addImm(MI.getOperand(OffsetIdx).getImm());
      Changed = true;
    }
  }

  // LDRXpre/post
  DestRegIdx = 1;
  BaseRegIdx = 2;
  OffsetIdx = 3;
  IsPrePost = true;
  if (isAddrReg(MI.getOperand(BaseRegIdx).getReg())
      && MI.getOperand(1).getReg() == AArch64::LR) {
    BuildMI(*MF, MI.getDebugLoc(), MI.getDesc())
      .addReg(MI.getOperand(BaseRegIdx).getReg())
      .addReg(AArch64::X22)
      .addReg(MI.getOperand(BaseRegIdx).getReg())
      .addImm(MI.getOperand(OffsetIdx).getImm());
  }

  // LDPXpre/post
  DestRegIdx = 1;
  BaseRegIdx = 3;
  OffsetIdx = 4;
  IsPrePost = true;
  if (isAddrReg(MI.getOperand(BaseRegIdx).getReg())) {
    if (MI.getOperand(DestRegIdx).getReg() == AArch64::LR) {
      BuildMI(*MF, MI.getDebugLoc(), MI.getDesc())
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addReg(AArch64::X22)
        .addReg(MI.getOperand(DestRegIdx + 1).getReg())
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addImm(MI.getOperand(OffsetIdx).getImm());
    }
    if (MI.getOperand(DestRegIdx + 1).getReg() == AArch64::LR) {
      BuildMI(*MF, MI.getDebugLoc(), MI.getDesc())
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addReg(MI.getOperand(DestRegIdx).getReg())
        .addReg(AArch64::X22)
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addImm(MI.getOperand(OffsetIdx).getImm());
    }
  } */
