#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "AArch64LFI.h"
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

static cl::opt<bool> ClDisableLFIPass(
    "disable-lfi-pass", cl::Hidden, cl::init(false),
    cl::desc("disable LFI MIR pass"));

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
  bool handleLoadSpecialRegs(MachineInstr &MI);
  bool handleLoadStore(MachineInstr &MI);
  SmallVector<MachineInstr *, 4> createRTCall(RTCallType Ty, MachineInstr &MI);
  SmallVector<MachineInstr *, 3> createSwap(MachineInstr &MI, Register R1, Register R2);
  MachineInstr *createMov(MachineInstr &MI, Register DestReg, Register BaseReg);
  MachineInstr *createAddXrx(MachineInstr &MI, Register DestReg, Register BaseReg, Register OffsetReg);
  MachineInstr *createAddFromBase(MachineInstr &MI, Register DestReg, Register OffsetReg);
  MachineInstr *createLoadWithAltDestReg(MachineInstr &MI, const MemInstInfo &MII, Register From, Register To);
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
  return createAddXrx(MI, DestReg, LFIReg::BaseReg, OffsetReg);
}

MachineInstr *AArch64LFI::createSafeMemDemoted(MachineInstr &MI, unsigned BaseRegIdx) {
  bool IsPre, IsBaseNoOffset;
  auto NewOpCode = convertPrePostToBase(MI.getOpcode(), IsPre, IsBaseNoOffset);
  MachineInstrBuilder MIB = BuildMI(*MF, MI.getDebugLoc(), TII->get(NewOpCode));

  for (unsigned I = 1; I < BaseRegIdx; I++)
    MIB.add(MI.getOperand(I));
  MIB.addReg(LFIReg::AddrReg);
  if (IsPre)
    MIB.add(MI.getOperand(BaseRegIdx + 1));
  else if (!IsBaseNoOffset)
    // NOTE: some SIMD opcodes only have no-offset mode and Post-index mode with (imm|reg)
    // so this adds an offset 0 only if its base opcode needs an offset.
    MIB.addImm(0);

  return MIB;
}

SmallVector<MachineInstr *, 2> AArch64LFI::createSafeMemN(MachineInstr &MI, unsigned BaseRegIdx) {
  SmallVector<MachineInstr *, 2> Instrs;
  MachineInstrBuilder MIB = BuildMI(*MF, MI.getDebugLoc(), TII->get(MI.getOpcode()));
  for (unsigned I = 0; I < BaseRegIdx; I++) {
    auto MO = MI.getOperand(I);
    MIB.add(MO);
  }
  MIB.addReg(LFIReg::AddrReg);

  for (unsigned I = BaseRegIdx + 1; I < MI.getNumOperands(); I++)
    MIB.add(MI.getOperand(I));

  Instrs.push_back(MIB);

  return Instrs;
}
SmallVector<MachineInstr *, 2> AArch64LFI::createMemMask(MachineInstr &MI, unsigned int NewOp, Register Rd, Register Rt, bool IsLoad) {
  SmallVector<MachineInstr *, 2> Instrs;
  Register SafeRd = Rd;
  if (Rd == AArch64::LR)
    SafeRd = LFIReg::ScratchReg;
  MachineInstr *NewMI = BuildMI(*MF, MI.getDebugLoc(), TII->get(NewOp))
    .addReg(SafeRd, IsLoad ? RegState::Define : 0)
    .addReg(LFIReg::BaseReg)
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
  MachineInstr *Swap1 = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::EORXrs), R1)
    .addReg(R1)
    .addReg(R2)
    .addImm(0);
  MachineInstr *Swap2 = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::EORXrs), R2)
    .addReg(R1)
    .addReg(R2)
    .addImm(0);
  MachineInstr *Swap3 = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::EORXrs), R1)
    .addReg(R1)
    .addReg(R2)
    .addImm(0);

  return {Swap1, Swap2, Swap3};
}

MachineInstr *AArch64LFI::createMov(MachineInstr &MI, Register DestReg, Register BaseReg) {
  return BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ORRXrs), DestReg)
    .addReg(AArch64::XZR)
    .addReg(BaseReg)
    .addImm(0);
}

MachineInstr *AArch64LFI::createLoadWithAltDestReg(MachineInstr &MI, const MemInstInfo &MII, Register From, Register To) {
  MachineInstrBuilder MIB = BuildMI(*MF, MI.getDebugLoc(), MI.getDesc());
  Register BaseReg = MI.getOperand(MII.BaseRegIdx).getReg();
  Register DestReg = MI.getOperand(MII.DestRegIdx).getReg();

  // NOTE: AArch64 Load Instr layout
  // [PrePost_BaseReg,] DestReg[, DestReg2], BaseReg, Offset(Reg|Imm)
  if (MII.IsPrePost)
    MIB.addDef(BaseReg);

  MIB.addDef(DestReg == From ? To : DestReg);
  if (MII.IsPair) {
    Register DestReg2 = MI.getOperand(MII.DestRegIdx + 1).getReg();
    MIB.addDef(DestReg2 == From ? To : DestReg2);
  }

  MIB.addReg(BaseReg);
  MIB.add(MI.getOperand(MII.OffsetIdx));

  return MIB;
}

static bool definesReg(const MachineInstr &MI, const MemInstInfo &MII, const MCRegister Reg) {
  return MI.definesRegister(Reg, /*TRI=*/nullptr);
}

// If LoadInst writes X30 and basereg is not x21 => replace X30 with X22 and add a postguard
// If LoadInst writes X18, X21 => replace them with XZR (discard)
bool AArch64LFI::handleLoadSpecialRegs(MachineInstr &MI) {
  std::optional<MemInstInfo> MII = getLoadInfo(MI.getOpcode());
  SmallVector<MachineInstr *> Insts;

  if (definesReg(MI, *MII, AArch64::LR) && MI.getOperand(MII->BaseRegIdx).getReg() != LFIReg::BaseReg) {
    MachineInstr *NewMI = createLoadWithAltDestReg(MI, *MII, AArch64::LR, LFIReg::ScratchReg);
    MachineInstr *Add = createAddFromBase(MI, AArch64::LR, LFIReg::ScratchReg);
    Insts.append({NewMI, Add});
  }
  if (definesReg(MI, *MII, LFIReg::AddrReg)) {
    MachineInstr *NewMI = createLoadWithAltDestReg(MI, *MII, LFIReg::AddrReg, AArch64::XZR);
    Insts.push_back(NewMI);
  }
  if (definesReg(MI, *MII, LFIReg::BaseReg)) {
    MachineInstr *NewMI = createLoadWithAltDestReg(MI, *MII, LFIReg::BaseReg, AArch64::XZR);
    Insts.push_back(NewMI);
  }
  if (Insts.empty()) {
    return false;
  }
  insertMIs(MI, Insts);
  MI.eraseFromParent();
  handleLoadStore(*Insts[0]); // call LFI rewriter again
  return true;
}

SmallVector<MachineInstr *, 4> AArch64LFI::createRTCall(RTCallType Ty, MachineInstr &MI) {
  MachineInstr *Mov = createMov(MI, LFIReg::ScratchReg, AArch64::LR);

  auto Offset = 0;
  switch (Ty) {
    case RT_Syscall: Offset = 0; break;
    case RT_TLSRead: Offset = 1; break;
    case RT_TLSWrite: Offset = 2; break;
  }

  // ldr x30, [x21]
  MachineInstr *Load = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDRXui), AArch64::LR)
    .addReg(LFIReg::BaseReg)
    .addImm(Offset);
  // blr x30
  MachineInstr *Blr = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::BLR))
    .addReg(AArch64::LR);
  // add x30, x21, w22, uxtw
  MachineInstr *Add = createAddFromBase(MI, AArch64::LR, LFIReg::ScratchReg);

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
    Instrs.push_back(createMov(MI, Reg, AArch64::X0));
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

  if (isSafeIndBr(MI.getOpcode(), Reg)) {
    return false;
  }

  MachineInstr *MaskMI = createAddFromBase(MI, LFIReg::AddrReg, Reg);
  MO.setReg(LFIReg::AddrReg);

  insertMIs(MI, {MaskMI});

  return true;
}

bool AArch64LFI::handleLoadStore(MachineInstr &MI) {
  LLVM_DEBUG(DBGLOC << '\n';);
  std::optional<MemInstInfo> MII = getMemInstInfo(MI.getOpcode());
  if (!MII.has_value()) {
    return false;
  }
  const auto [DestRegIdx, BaseRegIdx, OffsetIdx, IsPrePost, IsPair] = MII.value();

  // LFI Specification 1.9, 1.15.6
  // handling `ldr x30, [x21, #i]`
  if (MI.getOpcode() == AArch64::LDRXui) {
    auto DestReg = MI.getOperand(DestRegIdx).getReg();
    auto BaseReg = MI.getOperand(BaseRegIdx).getReg();
    auto OffsetMO = MI.getOperand(OffsetIdx);
    // NOTE: Implicit scale 8 for LDRXui <imm12> field
    if (DestReg == AArch64::LR && BaseReg == LFIReg::BaseReg &&
        (OffsetMO.isImm() && OffsetMO.getImm() < 32)) {
      return false;
    }
  }

  if (isMemNoMaskAddr(MI.getOpcode())) {
    Register BaseReg = MI.getOperand(BaseRegIdx).getReg();
    if (isAddrReg(BaseReg)) {
      return false;
    }
    SmallVector<MachineInstr *, 2> Instrs;
    MachineInstr *Mask = createAddFromBase(MI, LFIReg::AddrReg, BaseReg);
    Instrs.push_back(Mask);

    if (IsPrePost) {
      MachineInstr *SafeMem = createSafeMemDemoted(MI, BaseRegIdx);
      Instrs.push_back(SafeMem);

      MachineInstr *Fixup;
      MachineOperand &OffsetMO = MI.getOperand(OffsetIdx);
      if (OffsetMO.isReg()) {
        // The immediate offset of post-indexed addressing NEON Instrs has a fixed value, and
        // it is encoded as a post-index addressing with XZR register operand.
        // e.g., LD3Threev3d_POST can only have #48 as its operand and its offset
        // MachineOperand holds XZR, which is a *Register* kind, not Imm.
        Register OffReg = OffsetMO.getReg();
        if (OffReg == AArch64::XZR) {
          const LdStNInstrDesc *Info = getLdStNInstrDesc(MI.getOpcode());
          assert(Info && Info->NaturalOffset >= 0);
          Fixup = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXri), BaseReg)
            .addReg(BaseReg)
            .addImm(Info->NaturalOffset)
            .addImm(0);
        } else {
          assert(OffReg != AArch64::WZR);
          Fixup = BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrs), BaseReg)
            .addReg(BaseReg)
            .addReg(MI.getOperand(OffsetIdx).getReg())
            .addImm(0);
        }
      } else {
        auto Offset = MI.getOperand(OffsetIdx).getImm() * getPrePostScale(MI.getOpcode());
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
      }
      Instrs.push_back(Fixup);
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
    MachineOperand OffsetMO = MI.getOperand(OffsetIdx);
    if (isAddrReg(BaseReg))
      return false;

    // NOTE: ADRP + LDR has a global variable as MO.
    if (OffsetMO.isImm() && OffsetMO.getImm() == 0) {
      auto MaskChain = createMemMask(MI, MemOp, DestReg, BaseReg, IsDestDef);
      Instrs.append(MaskChain);
    } else {
      MachineInstr *Mask = createAddFromBase(MI, LFIReg::AddrReg, BaseReg);
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
        BuildMI(*MF, MI.getDebugLoc(), TII->get(Extend ? AArch64::ADDXrx : AArch64::ADDXrs))
            .addReg(LFIReg::ScratchReg)
            .addReg(BaseReg)
            .addReg(OffsetReg)
            .addImm(
                Extend ? AArch64_AM::getArithExtendImm(AArch64_AM::SXTX, Shift)
                       : AArch64_AM::getShifterImm(AArch64_AM::LSL, Shift));
    Instrs.push_back(Fixup);
    auto MaskChain = createMemMask(MI, MemOp, DestReg, LFIReg::ScratchReg, IsDestDef);
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
            .addReg(LFIReg::ScratchReg)
            .addReg(BaseReg)
            .addReg(OffsetReg)
            .addImm(
                S ? AArch64_AM::getArithExtendImm(AArch64_AM::SXTW, Shift)
                       : AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, Shift));
    Instrs.push_back(Fixup);
    auto MaskChain = createMemMask(MI, MemOp, DestReg, LFIReg::ScratchReg, IsDestDef);
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
  // TODO: lift atomic instruction handling from mcinst rewriter
  switch (MI.getOpcode()) {
    case AArch64::TCRETURNri:
    // NOTE: tailcall instrs below will be expanded by AArch64LFIELFStreamer.
    // case AArch64::TCRETURNrix16x17:
    // case AArch64::TCRETURNrix17:
    // case AArch64::TCRETURNrinotx16:
    // case AArch64::TCRETURNriALL:
    // case AArch64::AUTH_TCRETURN:
    // case AArch64::AUTH_TCRETURN_BTI:
    case AArch64::BR:
    case AArch64::BLR:
    case AArch64::RET:
      Changed = handleIndBr(MI);
      break;
    case AArch64::SVC:
      Changed = handleSyscall(MI);
      break;
    case AArch64::MRS:
      if (MI.getOperand(1).getImm() == AArch64SysReg::TPIDR_EL0) {
        Changed = handleTLSRead(MI);
      }
      break;
    case AArch64::MSR:
      if (MI.getOperand(0).getImm() == AArch64SysReg::TPIDR_EL0) {
        Changed = handleTLSWrite(MI);
      }
      break;
    case AArch64::ANDXri:
      // and sp, x0, #0xfffffffffffffff0
      if (MI.getOperand(0).getReg() == AArch64::SP) {
        MachineInstr *NewMI = BuildMI(*MF, MI.getDebugLoc(), MI.getDesc(), LFIReg::ScratchReg)
          .add(MI.getOperand(1))
          .add(MI.getOperand(2));
        MachineInstr *Guard = createAddFromBase(MI, AArch64::SP, LFIReg::ScratchReg);
        insertMIs(MI, {NewMI, Guard});
        MI.eraseFromParent();
      }
      break;
    case AArch64::MOVZXi:
      // movz x30, #imm, lsl #shift
      // an alias of mov x30, #imm (AArch64::MOVXi doesn't exist)
      if (MI.getOperand(0).getReg() == AArch64::LR) {
        MachineInstr *NewMI = BuildMI(*MF, MI.getDebugLoc(), MI.getDesc(), LFIReg::ScratchReg)
          .add(MI.getOperand(1))
          .add(MI.getOperand(2));
        MachineInstr *Guard = createAddFromBase(MI, AArch64::SP, LFIReg::ScratchReg);
        insertMIs(MI, {NewMI, Guard});
        MI.eraseFromParent();
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
          BuildMI(*MF, MI.getDebugLoc(), MI.getDesc(), LFIReg::ScratchReg)
          .addReg(MI.getOperand(1).getReg())
          .addImm(MI.getOperand(2).getImm())
          .addImm(MI.getOperand(3).getImm());
        MachineInstr *Guard =
          createAddFromBase(MI, AArch64::SP, LFIReg::ScratchReg);
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
          BuildMI(*MF, MI.getDebugLoc(), MI.getDesc(), LFIReg::ScratchReg)
          .addReg(AArch64::SP)
          .addReg(MI.getOperand(2).getReg())
          .addImm(MI.getOperand(3).getImm());
        MachineInstr *Guard =
          createAddFromBase(MI, AArch64::SP, LFIReg::ScratchReg);
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
      // NOTE: some loads must be re-written twice
      // 1) rewriting DestReg to prevent memory writes to illegal regs.
      // 2) rewriting BaseReg/memory operand.
      if ((Changed = handleLoadSpecialRegs(MI)))
        /* internally calls LFI rewriter again */
        break;
      LLVM_FALLTHROUGH;
    default:
      // NOTE: MCID may be useful.
      if (MI.mayLoadOrStore()) {
        Changed |= handleLoadStore(MI);
        break;
      }
      // WARN: this may be too restrictive with a secondary rewriter.
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
  if (ClDisableLFIPass)
    return Changed;

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
//             .addReg(LFIReg::ScratchReg)
//             .addReg(MI.getOperand(1).getReg())
//             .addImm(MI.getOperand(2).getImm());
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//             .addReg(AArch64::LR)
//             .addReg(LFIReg::BaseReg)
//             .addReg(LFIReg::ScratchReg)
//             .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//         }
//         break;
//       case AArch64::LDRXpre:
//         if (isAddrReg(MI.getOperand(2).getReg()) && MI.getOperand(1).getReg() == AArch64::LR) {
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDRXpre))
//             .addReg(MI.getOperand(0).getReg())
//             .addReg(LFIReg::ScratchReg)
//             .addReg(MI.getOperand(2).getReg())
//             .addImm(MI.getOperand(3).getImm());
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//             .addReg(AArch64::LR)
//             .addReg(LFIReg::BaseReg)
//             .addReg(LFIReg::ScratchReg)
//             .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//         }
//         break;
//       case AArch64::LDRXpost:
//         if (isAddrReg(MI.getOperand(2).getReg()) && MI.getOperand(1).getReg() == AArch64::LR) {
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDRXpost))
//             .addReg(MI.getOperand(0).getReg())
//             .addReg(LFIReg::ScratchReg)
//             .addReg(MI.getOperand(2).getReg())
//             .addImm(MI.getOperand(3).getImm());
//           BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//             .addReg(AArch64::LR)
//             .addReg(LFIReg::BaseReg)
//             .addReg(LFIReg::ScratchReg)
//             .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//         }
//         break;
//       case AArch64::LDPXi:
//         if (isAddrReg(MI.getOperand(2).getReg())) {
//           if (MI.getOperand(0).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXi))
//               .addReg(LFIReg::ScratchReg)
//               .addReg(MI.getOperand(1).getReg())
//               .addReg(MI.getOperand(2).getReg())
//               .addImm(MI.getOperand(3).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(LFIReg::BaseReg)
//               .addReg(LFIReg::ScratchReg)
//               .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//           if (MI.getOperand(1).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXi))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(LFIReg::ScratchReg)
//               .addReg(MI.getOperand(2).getReg())
//               .addImm(MI.getOperand(3).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(LFIReg::BaseReg)
//               .addReg(LFIReg::ScratchReg)
//               .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//         }
//         break;
//       case AArch64::LDPXpre:
//         if (isAddrReg(MI.getOperand(3).getReg())) {
//           if (MI.getOperand(1).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXpre))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(LFIReg::ScratchReg)
//               .addReg(MI.getOperand(2).getReg())
//               .addReg(MI.getOperand(3).getReg())
//               .addImm(MI.getOperand(4).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(LFIReg::BaseReg)
//               .addReg(LFIReg::ScratchReg)
//               .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//           if (MI.getOperand(2).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXpre))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(MI.getOperand(1).getReg())
//               .addReg(LFIReg::ScratchReg)
//               .addReg(MI.getOperand(3).getReg())
//               .addImm(MI.getOperand(4).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(LFIReg::BaseReg)
//               .addReg(LFIReg::ScratchReg)
//               .addReg(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//         }
//         break;
//       case AArch64::LDPXpost:
//         if (isAddrReg(MI.getOperand(3).getReg())) {
//           if (MI.getOperand(1).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXpost))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(LFIReg::ScratchReg)
//               .addReg(MI.getOperand(2).getReg())
//               .addReg(MI.getOperand(3).getReg())
//               .addImm(MI.getOperand(4).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(LFIReg::BaseReg)
//               .addReg(LFIReg::ScratchReg)
//               .addReg(AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0));
//           }
//           if (MI.getOperand(2).getReg() == AArch64::LR) {
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::LDPXpost))
//               .addReg(MI.getOperand(0).getReg())
//               .addReg(MI.getOperand(1).getReg())
//               .addReg(LFIReg::ScratchReg)
//               .addReg(MI.getOperand(3).getReg())
//               .addImm(MI.getOperand(4).getImm());
//             BuildMI(*MF, MI.getDebugLoc(), TII->get(AArch64::ADDXrx))
//               .addReg(AArch64::LR)
//               .addReg(LFIReg::BaseReg)
//               .addReg(LFIReg::ScratchReg)
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
      .addReg(LFIReg::ScratchReg)
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
        .addReg(LFIReg::ScratchReg)
        .addReg(MI.getOperand(DestRegIdx + 1).getReg())
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addImm(MI.getOperand(OffsetIdx).getImm());
      Changed = true;
    }
    if (MI.getOperand(DestRegIdx + 1).getReg() == AArch64::LR) {
      BuildMI(*MF, MI.getDebugLoc(), MI.getDesc())
        .addReg(MI.getOperand(DestRegIdx).getReg())
        .addReg(LFIReg::ScratchReg)
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
      .addReg(LFIReg::ScratchReg)
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
        .addReg(LFIReg::ScratchReg)
        .addReg(MI.getOperand(DestRegIdx + 1).getReg())
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addImm(MI.getOperand(OffsetIdx).getImm());
    }
    if (MI.getOperand(DestRegIdx + 1).getReg() == AArch64::LR) {
      BuildMI(*MF, MI.getDebugLoc(), MI.getDesc())
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addReg(MI.getOperand(DestRegIdx).getReg())
        .addReg(LFIReg::ScratchReg)
        .addReg(MI.getOperand(BaseRegIdx).getReg())
        .addImm(MI.getOperand(OffsetIdx).getImm());
    }
  } */
