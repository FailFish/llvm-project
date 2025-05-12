//===- lib/MC/AArch64ELFStreamer.cpp - ELF Object Output for AArch64 ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits AArch64 ELF .o object files. Different
// from generic ELF streamer in emitting mapping symbols ($x and $d) to delimit
// regions of data and code.
//
//===----------------------------------------------------------------------===//

#include "AArch64ELFStreamer.h"
#include "AArch64AddressingModes.h"
#include "AArch64MCInstInfo.h"
#include "AArch64MCTargetDesc.h"
#include "AArch64TargetStreamer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWinCOFFStreamer.h"
#include "llvm/Support/AArch64BuildAttributes.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class AArch64ELFStreamer;

class AArch64TargetAsmStreamer : public AArch64TargetStreamer {
  formatted_raw_ostream &OS;
  std::string VendorTag;

  void emitInst(uint32_t Inst) override;

  void emitDirectiveVariantPCS(MCSymbol *Symbol) override {
    OS << "\t.variant_pcs\t" << Symbol->getName() << "\n";
  }

  void emitARM64WinCFIAllocStack(unsigned Size) override {
    OS << "\t.seh_stackalloc\t" << Size << "\n";
  }
  void emitARM64WinCFISaveR19R20X(int Offset) override {
    OS << "\t.seh_save_r19r20_x\t" << Offset << "\n";
  }
  void emitARM64WinCFISaveFPLR(int Offset) override {
    OS << "\t.seh_save_fplr\t" << Offset << "\n";
  }
  void emitARM64WinCFISaveFPLRX(int Offset) override {
    OS << "\t.seh_save_fplr_x\t" << Offset << "\n";
  }
  void emitARM64WinCFISaveReg(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_reg\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveRegX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_reg_x\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveRegP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_regp\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveRegPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_regp_x\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveLRPair(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_lrpair\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveFReg(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_freg\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveFRegX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_freg_x\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveFRegP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_fregp\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveFRegPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_fregp_x\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISetFP() override { OS << "\t.seh_set_fp\n"; }
  void emitARM64WinCFIAddFP(unsigned Size) override {
    OS << "\t.seh_add_fp\t" << Size << "\n";
  }
  void emitARM64WinCFINop() override { OS << "\t.seh_nop\n"; }
  void emitARM64WinCFISaveNext() override { OS << "\t.seh_save_next\n"; }
  void emitARM64WinCFIPrologEnd() override { OS << "\t.seh_endprologue\n"; }
  void emitARM64WinCFIEpilogStart() override { OS << "\t.seh_startepilogue\n"; }
  void emitARM64WinCFIEpilogEnd() override { OS << "\t.seh_endepilogue\n"; }
  void emitARM64WinCFITrapFrame() override { OS << "\t.seh_trap_frame\n"; }
  void emitARM64WinCFIMachineFrame() override { OS << "\t.seh_pushframe\n"; }
  void emitARM64WinCFIContext() override { OS << "\t.seh_context\n"; }
  void emitARM64WinCFIECContext() override { OS << "\t.seh_ec_context\n"; }
  void emitARM64WinCFIClearUnwoundToCall() override {
    OS << "\t.seh_clear_unwound_to_call\n";
  }
  void emitARM64WinCFIPACSignLR() override {
    OS << "\t.seh_pac_sign_lr\n";
  }

  void emitARM64WinCFISaveAnyRegI(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegIP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_p\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegD(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegDP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_p\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegQ(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg\tq" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegQP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_p\tq" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegIX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_x\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegIPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_px\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegDX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_x\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegDPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_px\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegQX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_x\tq" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegQPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_px\tq" << Reg << ", " << Offset << "\n";
  }

  void emitAttribute(StringRef VendorName, unsigned Tag, unsigned Value,
                     std::string String) override {

    // AArch64 build attributes for assembly attribute form:
    // .aeabi_attribute tag, value
    if (unsigned(-1) == Value && "" == String) {
      assert(0 && "Arguments error");
      return;
    }

    unsigned VendorID = AArch64BuildAttributes::getVendorID(VendorName);

    switch (VendorID) {
    case AArch64BuildAttributes::VENDOR_UNKNOWN:
      if (unsigned(-1) != Value) {
        OS << "\t.aeabi_attribute" << "\t" << Tag << ", " << Value;
        AArch64TargetStreamer::emitAttribute(VendorName, Tag, Value, "");
      }
      if ("" != String) {
        OS << "\t.aeabi_attribute" << "\t" << Tag << ", " << String;
        AArch64TargetStreamer::emitAttribute(VendorName, Tag, unsigned(-1),
                                             String);
      }
      break;
    // Note: AEABI_FEATURE_AND_BITS takes only unsigned values
    case AArch64BuildAttributes::AEABI_FEATURE_AND_BITS:
      switch (Tag) {
      default: // allow emitting any attribute by number
        OS << "\t.aeabi_attribute" << "\t" << Tag << ", " << Value;
        // Keep the data structure consistent with the case of ELF emission
        // (important for llvm-mc asm parsing)
        AArch64TargetStreamer::emitAttribute(VendorName, Tag, Value, "");
        break;
      case AArch64BuildAttributes::TAG_FEATURE_BTI:
      case AArch64BuildAttributes::TAG_FEATURE_GCS:
      case AArch64BuildAttributes::TAG_FEATURE_PAC:
        OS << "\t.aeabi_attribute" << "\t" << Tag << ", " << Value << "\t// "
           << AArch64BuildAttributes::getFeatureAndBitsTagsStr(Tag);
        AArch64TargetStreamer::emitAttribute(VendorName, Tag, Value, "");
        break;
      }
      break;
    // Note: AEABI_PAUTHABI takes only unsigned values
    case AArch64BuildAttributes::AEABI_PAUTHABI:
      switch (Tag) {
      default: // allow emitting any attribute by number
        OS << "\t.aeabi_attribute" << "\t" << Tag << ", " << Value;
        // Keep the data structure consistent with the case of ELF emission
        // (important for llvm-mc asm parsing)
        AArch64TargetStreamer::emitAttribute(VendorName, Tag, Value, "");
        break;
      case AArch64BuildAttributes::TAG_PAUTH_PLATFORM:
      case AArch64BuildAttributes::TAG_PAUTH_SCHEMA:
        OS << "\t.aeabi_attribute" << "\t" << Tag << ", " << Value << "\t// "
           << AArch64BuildAttributes::getPauthABITagsStr(Tag);
        AArch64TargetStreamer::emitAttribute(VendorName, Tag, Value, "");
        break;
      }
      break;
    }
    OS << "\n";
  }

  void emitAtributesSubsection(
      StringRef SubsectionName,
      AArch64BuildAttributes::SubsectionOptional Optional,
      AArch64BuildAttributes::SubsectionType ParameterType) override {
    // The AArch64 build attributes assembly subsection header format:
    // ".aeabi_subsection name, optional, parameter type"
    // optional: required (0) optional (1)
    // parameter type: uleb128 or ULEB128 (0) ntbs or NTBS (1)
    unsigned SubsectionID = AArch64BuildAttributes::getVendorID(SubsectionName);

    assert((0 == Optional || 1 == Optional) &&
           AArch64BuildAttributes::getSubsectionOptionalUnknownError().data());
    assert((0 == ParameterType || 1 == ParameterType) &&
           AArch64BuildAttributes::getSubsectionTypeUnknownError().data());

    std::string SubsectionTag = ".aeabi_subsection";
    StringRef OptionalStr = getOptionalStr(Optional);
    StringRef ParameterStr = getTypeStr(ParameterType);

    switch (SubsectionID) {
    case AArch64BuildAttributes::VENDOR_UNKNOWN: {
      // Private subsection
      break;
    }
    case AArch64BuildAttributes::AEABI_PAUTHABI: {
      assert(AArch64BuildAttributes::REQUIRED == Optional &&
             "subsection .aeabi-pauthabi should be marked as "
             "required and not as optional");
      assert(AArch64BuildAttributes::ULEB128 == ParameterType &&
             "subsection .aeabi-pauthabi should be "
             "marked as uleb128 and not as ntbs");
      break;
    }
    case AArch64BuildAttributes::AEABI_FEATURE_AND_BITS: {
      assert(AArch64BuildAttributes::OPTIONAL == Optional &&
             "subsection .aeabi_feature_and_bits should be "
             "marked as optional and not as required");
      assert(AArch64BuildAttributes::ULEB128 == ParameterType &&
             "subsection .aeabi_feature_and_bits should "
             "be marked as uleb128 and not as ntbs");
      break;
    }
    }
    OS << "\t" << SubsectionTag << "\t" << SubsectionName << ", " << OptionalStr
       << ", " << ParameterStr;
    // Keep the data structure consistent with the case of ELF emission
    // (important for llvm-mc asm parsing)
    AArch64TargetStreamer::emitAtributesSubsection(SubsectionName, Optional,
                                                   ParameterType);
    OS << "\n";
  }

public:
  AArch64TargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);
};

AArch64TargetAsmStreamer::AArch64TargetAsmStreamer(MCStreamer &S,
                                                   formatted_raw_ostream &OS)
    : AArch64TargetStreamer(S), OS(OS) {}

void AArch64TargetAsmStreamer::emitInst(uint32_t Inst) {
  OS << "\t.inst\t0x" << Twine::utohexstr(Inst) << "\n";
}

/// Extend the generic ELFStreamer class so that it can emit mapping symbols at
/// the appropriate points in the object files. These symbols are defined in the
/// AArch64 ELF ABI:
///    infocenter.arm.com/help/topic/com.arm.doc.ihi0056a/IHI0056A_aaelf64.pdf
///
/// In brief: $x or $d should be emitted at the start of each contiguous region
/// of A64 code or data in a section. In practice, this emission does not rely
/// on explicit assembler directives but on inherent properties of the
/// directives doing the emission (e.g. ".byte" is data, "add x0, x0, x0" an
/// instruction).
///
/// As a result this system is orthogonal to the DataRegion infrastructure used
/// by MachO. Beware!
class AArch64ELFStreamer : public MCELFStreamer {
public:
  friend AArch64TargetELFStreamer;
  AArch64ELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                     std::unique_ptr<MCObjectWriter> OW,
                     std::unique_ptr<MCCodeEmitter> Emitter)
      : MCELFStreamer(Context, std::move(TAB), std::move(OW),
                      std::move(Emitter)),
        LastEMS(EMS_None) {
    auto *TO = getContext().getTargetOptions();
    ImplicitMapSyms = TO && TO->ImplicitMapSyms;
  }

  void changeSection(MCSection *Section, uint32_t Subsection = 0) override {
    // Save the mapping symbol state for potential reuse when revisiting the
    // section. When ImplicitMapSyms is true, the initial state is
    // EMS_A64 for text sections and EMS_Data for the others.
    LastMappingSymbols[getCurrentSection().first] = LastEMS;
    auto It = LastMappingSymbols.find(Section);
    if (It != LastMappingSymbols.end())
      LastEMS = It->second;
    else if (ImplicitMapSyms)
      LastEMS = Section->isText() ? EMS_A64 : EMS_Data;
    else
      LastEMS = EMS_None;

    MCELFStreamer::changeSection(Section, Subsection);

    // Section alignment of 4 to match GNU Assembler
    if (Section->isText())
      Section->ensureMinAlignment(Align(4));
  }

  // Reset state between object emissions
  void reset() override {
    MCELFStreamer::reset();
    LastMappingSymbols.clear();
    LastEMS = EMS_None;
  }

  /// This function is the one used to emit instruction data into the ELF
  /// streamer. We override it to add the appropriate mapping symbol if
  /// necessary.
  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override {
    emitA64MappingSymbol();
    MCELFStreamer::emitInstruction(Inst, STI);
  }

  /// Emit a 32-bit value as an instruction. This is only used for the .inst
  /// directive, EmitInstruction should be used in other cases.
  void emitInst(uint32_t Inst) {
    char Buffer[4];

    // We can't just use EmitIntValue here, as that will emit a data mapping
    // symbol, and swap the endianness on big-endian systems (instructions are
    // always little-endian).
    for (char &C : Buffer) {
      C = uint8_t(Inst);
      Inst >>= 8;
    }

    emitA64MappingSymbol();
    MCELFStreamer::emitBytes(StringRef(Buffer, 4));
  }

  /// This is one of the functions used to emit data into an ELF section, so the
  /// AArch64 streamer overrides it to add the appropriate mapping symbol ($d)
  /// if necessary.
  void emitBytes(StringRef Data) override {
    emitDataMappingSymbol();
    MCELFStreamer::emitBytes(Data);
  }

  /// This is one of the functions used to emit data into an ELF section, so the
  /// AArch64 streamer overrides it to add the appropriate mapping symbol ($d)
  /// if necessary.
  void emitValueImpl(const MCExpr *Value, unsigned Size, SMLoc Loc) override {
    emitDataMappingSymbol();
    MCELFStreamer::emitValueImpl(Value, Size, Loc);
  }

  void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                                  SMLoc Loc) override {
    emitDataMappingSymbol();
    MCObjectStreamer::emitFill(NumBytes, FillValue, Loc);
  }

private:
  enum ElfMappingSymbol {
    EMS_None,
    EMS_A64,
    EMS_Data
  };

  void emitDataMappingSymbol() {
    if (LastEMS == EMS_Data)
      return;
    emitMappingSymbol("$d");
    LastEMS = EMS_Data;
  }

  void emitA64MappingSymbol() {
    if (LastEMS == EMS_A64)
      return;
    emitMappingSymbol("$x");
    LastEMS = EMS_A64;
  }

  MCSymbol *emitMappingSymbol(StringRef Name) {
    auto *Symbol = cast<MCSymbolELF>(getContext().createLocalSymbol(Name));
    emitLabel(Symbol);
    return Symbol;
  }

  DenseMap<const MCSection *, ElfMappingSymbol> LastMappingSymbols;
  ElfMappingSymbol LastEMS;
  bool ImplicitMapSyms;
};

class AArch64LFIELFStreamer : public AArch64ELFStreamer {
public:
  AArch64LFIELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                      std::unique_ptr<MCObjectWriter> OW,
                      std::unique_ptr<MCCodeEmitter> Emitter)
      : AArch64ELFStreamer(Context, std::move(TAB), std::move(OW),
                        std::move(Emitter)) {}

  ~AArch64LFIELFStreamer() override = default;
private:
  // TODO: handle cas,swp,...

  bool isMemNoMaskAddr(unsigned Op) {
    unsigned Shift;
    return convertUiToRoW(Op) == AArch64::INSTRUCTION_LIST_END &&
      convertPreToRoW(Op) == AArch64::INSTRUCTION_LIST_END &&
      convertPostToRoW(Op) == AArch64::INSTRUCTION_LIST_END &&
      convertRoXToRoW(Op, Shift) == AArch64::INSTRUCTION_LIST_END &&
      convertRoWToRoW(Op, Shift) == AArch64::INSTRUCTION_LIST_END;
  }

  bool isAddrReg(MCRegister Reg) {
    return (Reg == AArch64::SP) || (Reg == AArch64::X18) || (Reg == AArch64::LR);
  }

  void emitRRRI0(unsigned int Opcode, MCRegister Rd, MCRegister Rt1, MCRegister Rt2, int64_t Imm, const MCSubtargetInfo &STI) {
    MCInst SafeInst;
    SafeInst.setOpcode(Opcode);
    SafeInst.addOperand(MCOperand::createReg(Rd));
    SafeInst.addOperand(MCOperand::createReg(Rt1));
    SafeInst.addOperand(MCOperand::createReg(Rt2));
    SafeInst.addOperand(MCOperand::createImm(Imm));
    SafeInst.addOperand(MCOperand::createImm(0));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
  }

  void emitRRRI(unsigned int Opcode, MCRegister Rd, MCRegister Rt1, MCRegister Rt2, int64_t Imm, const MCSubtargetInfo &STI) {
    MCInst SafeInst;
    SafeInst.setOpcode(Opcode);
    SafeInst.addOperand(MCOperand::createReg(Rd));
    SafeInst.addOperand(MCOperand::createReg(Rt1));
    SafeInst.addOperand(MCOperand::createReg(Rt2));
    SafeInst.addOperand(MCOperand::createImm(Imm));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
  }

  void emitRRRRI(unsigned int Opcode, MCRegister Rd, MCRegister Rt1, MCRegister Rt2, MCRegister Rt3, int64_t Imm, const MCSubtargetInfo &STI) {
    MCInst SafeInst;
    SafeInst.setOpcode(Opcode);
    SafeInst.addOperand(MCOperand::createReg(Rd));
    SafeInst.addOperand(MCOperand::createReg(Rt1));
    SafeInst.addOperand(MCOperand::createReg(Rt2));
    SafeInst.addOperand(MCOperand::createReg(Rt3));
    SafeInst.addOperand(MCOperand::createImm(Imm));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
  }

  void emitRRI(unsigned int Op, MCRegister Rd, MCRegister Rs, int64_t Imm, const MCSubtargetInfo &STI) {
    MCInst SafeInst;
    SafeInst.setOpcode(Op);
    SafeInst.addOperand(MCOperand::createReg(Rd));
    SafeInst.addOperand(MCOperand::createReg(Rs));
    SafeInst.addOperand(MCOperand::createImm(Imm));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
  }

  void emitRRI0(unsigned int Op, MCRegister Rd, MCRegister Rs, int64_t Imm, const MCSubtargetInfo &STI) {
    MCInst SafeInst;
    SafeInst.setOpcode(Op);
    SafeInst.addOperand(MCOperand::createReg(Rd));
    SafeInst.addOperand(MCOperand::createReg(Rs));
    SafeInst.addOperand(MCOperand::createImm(Imm));
    SafeInst.addOperand(MCOperand::createImm(0));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
  }

  void emitRRR(unsigned int Op, MCRegister Rd, MCRegister Rs1, MCRegister Rs2, const MCSubtargetInfo &STI) {
    MCInst SafeInst;
    SafeInst.setOpcode(Op);
    SafeInst.addOperand(MCOperand::createReg(Rd));
    SafeInst.addOperand(MCOperand::createReg(Rs1));
    SafeInst.addOperand(MCOperand::createReg(Rs2));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
  }

  void emitR(unsigned int Op, MCRegister Rd, const MCSubtargetInfo &STI) {
    MCInst SafeInst;
    SafeInst.setOpcode(Op);
    SafeInst.addOperand(MCOperand::createReg(Rd));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
  }

  void emitMemMask(unsigned int Opcode, MCRegister Rd, MCRegister Rt, const MCSubtargetInfo &STI) {
    MCRegister SafeRd = Rd;
    if (Rd == AArch64::LR)
      SafeRd = AArch64::X22;
    emitRRRI0(Opcode, SafeRd, AArch64::X21, Rt, AArch64_AM::getMemExtendImm(AArch64_AM::UXTW, 0), STI);
    if (Rd == AArch64::LR)
      emitRRRI0(AArch64::ADDXrx, Rd, AArch64::X21, SafeRd, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
  }

  void emitAddMask(MCRegister AddrReg, const MCSubtargetInfo &STI) {
    emitRRRI0(AArch64::ADDXrx, AArch64::X18, AArch64::X21, AddrReg, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
  }

  void emitSafeMemDemoted(unsigned N, const MCInst &Inst, const MCSubtargetInfo &STI) {
    MCInst SafeInst;
    bool IsPre;
    SafeInst.setOpcode(convertPrePostToBase(Inst.getOpcode(), IsPre));
    for (unsigned I = 1; I < N; I++)
      SafeInst.addOperand(Inst.getOperand(I));
    SafeInst.addOperand(MCOperand::createReg(AArch64::X18));
    if (IsPre)
      SafeInst.addOperand(Inst.getOperand(N + 1));
    else
      SafeInst.addOperand(MCOperand::createImm(0));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
  }

  void emitSafeMemN(unsigned N, const MCInst &Inst, const MCSubtargetInfo &STI) {
    MCInst SafeInst;
    SafeInst.setOpcode(Inst.getOpcode());
    bool PostGuard = false;
    for (unsigned I = 0; I < N; I++) {
      if (Inst.getOperand(I).isReg() && Inst.getOperand(I).getReg() == AArch64::LR) {
        PostGuard = true;
        SafeInst.addOperand(MCOperand::createReg(AArch64::X22));
      } else {
        SafeInst.addOperand(Inst.getOperand(I));
      }
    }
    SafeInst.addOperand(MCOperand::createReg(AArch64::X18));
    for (unsigned I = N + 1; I < Inst.getNumOperands(); I++)
      SafeInst.addOperand(Inst.getOperand(I));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
    if (PostGuard)
      emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
  }

  void emitSafeMem1(const MCInst &Inst, const MCSubtargetInfo &STI) {
    emitSafeMemN(1, Inst, STI);
  }

  void emitSafeMem2(const MCInst &Inst, const MCSubtargetInfo &STI) {
    emitSafeMemN(2, Inst, STI);
  }

  void emitIndirectBranch(unsigned int Opcode, MCRegister Rt, const MCInst &Inst, const MCSubtargetInfo &STI) {
    if (isAddrReg(Rt)) {
      AArch64ELFStreamer::emitInstruction(Inst, STI);
      return;
    }
    emitAddMask(Rt, STI);
    MCInst SafeInst;
    SafeInst.setOpcode(Opcode);
    SafeInst.addOperand(MCOperand::createReg(AArch64::X18));
    AArch64ELFStreamer::emitInstruction(SafeInst, STI);
  }

  void emitMov(MCRegister Dest, MCRegister Src, const MCSubtargetInfo &STI) {
    emitRRRI(AArch64::ORRXrs, Dest, AArch64::XZR, Src, 0, STI);
  }

  enum RTCallType {
    RT_Syscall,
    RT_TLSRead,
    RT_TLSWrite,
  };

  void emitRTCall(RTCallType CallType, const MCSubtargetInfo &STI) {
    emitMov(AArch64::X22, AArch64::LR, STI);
    unsigned Offset;
    switch (CallType) {
    case RT_Syscall: Offset = 0; break;
    case RT_TLSRead: Offset = 1; break;
    case RT_TLSWrite: Offset = 2; break;
    }
    emitRRI(AArch64::LDRXui, AArch64::LR, AArch64::X21, Offset, STI);
    emitR(AArch64::BLR, AArch64::LR, STI);
    emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
  }

  void emitSyscall(const MCSubtargetInfo &STI) {
    emitRTCall(RT_Syscall, STI);
  }

  void emitSwap(MCRegister Reg1, MCRegister Reg2, const MCSubtargetInfo &STI) {
    emitRRRI(AArch64::EORXrs, Reg1, Reg1, Reg2, 0, STI);
    emitRRRI(AArch64::EORXrs, Reg2, Reg1, Reg2, 0, STI);
    emitRRRI(AArch64::EORXrs, Reg1, Reg1, Reg2, 0, STI);
  }

  void emitTLSRead(const MCInst &Inst, const MCSubtargetInfo &STI) {
    MCRegister Reg = Inst.getOperand(0).getReg();
    if (Reg == AArch64::X0) {
      emitRTCall(RT_TLSRead, STI);
    } else {
      emitMov(Reg, AArch64::X0, STI);
      emitRTCall(RT_TLSRead, STI);
      emitSwap(AArch64::X0, Reg, STI);
    }
  }

  void emitTLSWrite(const MCInst &Inst, const MCSubtargetInfo &STI) {
    MCRegister Reg = Inst.getOperand(1).getReg();
    if (Reg == AArch64::X0) {
      emitRTCall(RT_TLSWrite, STI);
    } else {
      emitSwap(AArch64::X0, Reg, STI);
      emitRTCall(RT_TLSWrite, STI);
      emitSwap(AArch64::X0, Reg, STI);
    }
  }

public:
  /// This function is the one used to emit instruction data into the ELF
  /// streamer. We override it to mask dangerous instructions.
  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override {
    MCRegister Reg;
    switch (Inst.getOpcode()) {
    case AArch64::BR:
    case AArch64::BLR:
    case AArch64::RET:
      emitIndirectBranch(Inst.getOpcode(), Inst.getOperand(0).getReg(), Inst, STI);
      return;
    case AArch64::SVC:
      emitSyscall(STI);
      return;
    case AArch64::MRS:
      if (Inst.getOperand(1).getReg() == AArch64SysReg::TPIDR_EL0) {
        emitTLSRead(Inst, STI);
        return;
      }
      break;
    case AArch64::MSR:
      if (Inst.getOperand(0).getReg() == AArch64SysReg::TPIDR_EL0) {
        emitTLSWrite(Inst, STI);
        return;
      }
      break;
    case AArch64::ADDXri:
    case AArch64::SUBXri:
      Reg = Inst.getOperand(0).getReg();
      if (Reg != AArch64::SP)
        break;
      if (Inst.getOperand(2).getImm() == 0) {
        emitRRRI0(AArch64::ADDXrx, AArch64::SP, AArch64::X21, Inst.getOperand(1).getReg(), AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
      } else {
        emitRRI(Inst.getOpcode(), AArch64::X22, AArch64::SP, Inst.getOperand(2).getImm(), STI);
        emitRRRI0(AArch64::ADDXrx, AArch64::SP, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
      }
      return;
    case AArch64::ADDXrr:
    case AArch64::SUBXrr:
    case AArch64::ADDXrs:
    case AArch64::SUBXrs:
    case AArch64::ADDXrx:
    case AArch64::SUBXrx:
    case AArch64::ADDXrx64:
    case AArch64::SUBXrx64:
      Reg = Inst.getOperand(0).getReg();
      if (Reg != AArch64::SP)
        break;
      emitRRR(Inst.getOpcode(), AArch64::X22, AArch64::SP, Inst.getOperand(2).getReg(), STI);
      emitRRRI0(AArch64::ADDXrx, AArch64::SP, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
      return;
    case AArch64::ORRXrs:
      Reg = Inst.getOperand(0).getReg();
      if ((Reg == AArch64::SP) || (Reg == AArch64::LR)) {
        emitRRRI0(AArch64::ADDXrx, Reg, AArch64::X21, Inst.getOperand(2).getReg(), AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
        return;
      }
      break;
    case AArch64::LDRXui:
      if (isAddrReg(Inst.getOperand(1).getReg()) && Inst.getOperand(0).getReg() == AArch64::LR) {
        emitRRI(AArch64::LDRXui, AArch64::X22, Inst.getOperand(1).getReg(), Inst.getOperand(2).getImm(), STI);
        emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
        return;
      }
      break;
    case AArch64::LDRXpre:
      if (isAddrReg(Inst.getOperand(2).getReg()) && Inst.getOperand(1).getReg() == AArch64::LR) {
        emitRRRI(AArch64::LDRXpre, Inst.getOperand(0).getReg(), AArch64::X22, Inst.getOperand(2).getReg(), Inst.getOperand(3).getImm(), STI);
        emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
        return;
      }
      break;
    case AArch64::LDRXpost:
      if (isAddrReg(Inst.getOperand(2).getReg()) && Inst.getOperand(1).getReg() == AArch64::LR) {
        emitRRRI(AArch64::LDRXpost, Inst.getOperand(0).getReg(), AArch64::X22, Inst.getOperand(2).getReg(), Inst.getOperand(3).getImm(), STI);
        emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
        return;
      }
      break;
    case AArch64::LDPXi:
      if (isAddrReg(Inst.getOperand(2).getReg())) {
        if (Inst.getOperand(0).getReg() == AArch64::LR) {
          emitRRRI(AArch64::LDPXi, AArch64::X22, Inst.getOperand(1).getReg(), Inst.getOperand(2).getReg(), Inst.getOperand(3).getImm(), STI);
          emitRRRI(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
          return;
        }
        if (Inst.getOperand(1).getReg() == AArch64::LR) {
          emitRRRI0(AArch64::LDPXi, Inst.getOperand(0).getReg(), AArch64::X22, Inst.getOperand(2).getReg(), Inst.getOperand(3).getImm(), STI);
          emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
          return;
        }
      }
      break;
    case AArch64::LDPXpre:
      if (isAddrReg(Inst.getOperand(3).getReg())) {
        if (Inst.getOperand(1).getReg() == AArch64::LR) {
          emitRRRRI(AArch64::LDPXpre, Inst.getOperand(0).getReg(), AArch64::X22, Inst.getOperand(2).getReg(), Inst.getOperand(3).getReg(), Inst.getOperand(4).getImm(), STI);
          emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
          return;
        }
        if (Inst.getOperand(2).getReg() == AArch64::LR) {
          emitRRRRI(AArch64::LDPXpre, Inst.getOperand(0).getReg(), Inst.getOperand(1).getReg(), AArch64::X22, Inst.getOperand(3).getReg(), Inst.getOperand(4).getImm(), STI);
          emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
          return;
        }
      }
      break;
    case AArch64::LDPXpost:
      if (isAddrReg(Inst.getOperand(3).getReg())) {
        if (Inst.getOperand(1).getReg() == AArch64::LR) {
          emitRRRRI(AArch64::LDPXpost, Inst.getOperand(0).getReg(), AArch64::X22, Inst.getOperand(2).getReg(), Inst.getOperand(3).getReg(), Inst.getOperand(4).getImm(), STI);
          emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
          return;
        }
        if (Inst.getOperand(2).getReg() == AArch64::LR) {
          emitRRRRI(AArch64::LDPXpost, Inst.getOperand(0).getReg(), Inst.getOperand(1).getReg(), AArch64::X22, Inst.getOperand(3).getReg(), Inst.getOperand(4).getImm(), STI);
          emitRRRI0(AArch64::ADDXrx, AArch64::LR, AArch64::X21, AArch64::X22, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 0), STI);
          return;
        }
      }
      break;
    }

    int DestRegIdx, BaseRegIdx, OffsetIdx;
    bool IsPrePost;
    if (!getLoadInfo(Inst, DestRegIdx, BaseRegIdx, OffsetIdx, IsPrePost)) {
      if (!getStoreInfo(Inst, DestRegIdx, BaseRegIdx, OffsetIdx, IsPrePost)) {
        // not load or store, so emit as is.
        AArch64ELFStreamer::emitInstruction(Inst, STI);
        return;
      }
    }

    if (isMemNoMaskAddr(Inst.getOpcode())) {
      if (isAddrReg(Inst.getOperand(BaseRegIdx).getReg())) {
        AArch64ELFStreamer::emitInstruction(Inst, STI);
        return;
      }
      emitAddMask(Inst.getOperand(BaseRegIdx).getReg(), STI);
      if (IsPrePost) {
        emitSafeMemDemoted(BaseRegIdx, Inst, STI);
        MCRegister Base = Inst.getOperand(BaseRegIdx).getReg();
        if (Inst.getOperand(OffsetIdx).isReg())
          emitRRRI0(AArch64::ADDXrs, Base, Base, Inst.getOperand(OffsetIdx).getReg(), 0, STI);
        else
          emitRRI0(AArch64::ADDXri, Base, Base, Inst.getOperand(OffsetIdx).getImm() * getPrePostScale(Inst.getOpcode()), STI);
      } else {
        emitSafeMemN(BaseRegIdx, Inst, STI);
      }
      return;
    }

    unsigned MemOp;
    if ((MemOp = convertUiToRoW(Inst.getOpcode())) != AArch64::INSTRUCTION_LIST_END) {
      if (isAddrReg(Inst.getOperand(1).getReg())) {
        AArch64ELFStreamer::emitInstruction(Inst, STI);
        return;
      }
      if (Inst.getOperand(2).getImm() == 0) {
        emitMemMask(MemOp, Inst.getOperand(0).getReg(), Inst.getOperand(1).getReg(), STI);
      } else {
        emitAddMask(Inst.getOperand(1).getReg(), STI);
        emitSafeMem1(Inst, STI);
      }
      return;
    }
    if ((MemOp = convertPreToRoW(Inst.getOpcode())) != AArch64::INSTRUCTION_LIST_END) {
      if (isAddrReg(Inst.getOperand(2).getReg())) {
        AArch64ELFStreamer::emitInstruction(Inst, STI);
        return;
      }
      MCRegister Reg = Inst.getOperand(2).getReg();
      int64_t Imm = Inst.getOperand(3).getImm();
      if (Imm >= 0)
        emitRRI0(AArch64::ADDXri, Reg, Reg, Imm, STI);
      else
        emitRRI0(AArch64::SUBXri, Reg, Reg, -Imm, STI);
      emitMemMask(MemOp, Inst.getOperand(1).getReg(), Reg, STI);
      return;
    }
    if ((MemOp = convertPostToRoW(Inst.getOpcode())) != AArch64::INSTRUCTION_LIST_END) {
      if (isAddrReg(Inst.getOperand(2).getReg())) {
        AArch64ELFStreamer::emitInstruction(Inst, STI);
        return;
      }
      MCRegister Reg = Inst.getOperand(2).getReg();
      emitMemMask(MemOp, Inst.getOperand(1).getReg(), Reg, STI);
      int64_t Imm = Inst.getOperand(3).getImm();
      if (Imm >= 0)
        emitRRI0(AArch64::ADDXri, Reg, Reg, Imm, STI);
      else
        emitRRI0(AArch64::SUBXri, Reg, Reg, -Imm, STI);
      return;
    }
    unsigned Shift;
    if ((MemOp = convertRoXToRoW(Inst.getOpcode(), Shift)) != AArch64::INSTRUCTION_LIST_END) {
      MCRegister Reg1 = Inst.getOperand(1).getReg();
      MCRegister Reg2 = Inst.getOperand(2).getReg();
      int64_t Extend = Inst.getOperand(3).getImm();
      int64_t IsShift = Inst.getOperand(4).getImm();
      if (!IsShift)
        Shift = 0;
      if (Extend)
        emitRRRI0(AArch64::ADDXrx, AArch64::X22, Reg1, Reg2, AArch64_AM::getArithExtendImm(AArch64_AM::SXTX, Shift), STI);
      else
        emitRRRI0(AArch64::ADDXrs, AArch64::X22, Reg1, Reg2, AArch64_AM::getShifterImm(AArch64_AM::LSL, Shift), STI);
      emitMemMask(MemOp, Inst.getOperand(0).getReg(), AArch64::X22, STI);
      return;
    }
    if ((MemOp = convertRoWToRoW(Inst.getOpcode(), Shift)) != AArch64::INSTRUCTION_LIST_END) {
      MCRegister Reg1 = Inst.getOperand(1).getReg();
      MCRegister Reg2 = Inst.getOperand(2).getReg();
      int64_t S = Inst.getOperand(3).getImm();
      int64_t IsShift = Inst.getOperand(4).getImm();
      if (!IsShift)
        Shift = 0;
      if (S)
        emitRRRI0(AArch64::ADDXrx, AArch64::X22, Reg1, Reg2, AArch64_AM::getArithExtendImm(AArch64_AM::SXTW, Shift), STI);
      else
        emitRRRI0(AArch64::ADDXrx, AArch64::X22, Reg1, Reg2, AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, Shift), STI);
      emitMemMask(MemOp, Inst.getOperand(0).getReg(), AArch64::X22, STI);
      return;
    }

    AArch64ELFStreamer::emitInstruction(Inst, STI);
  }
};
} // end anonymous namespace

AArch64ELFStreamer &AArch64TargetELFStreamer::getStreamer() {
  return static_cast<AArch64ELFStreamer &>(Streamer);
}

void AArch64TargetELFStreamer::emitAtributesSubsection(
    StringRef VendorName, AArch64BuildAttributes::SubsectionOptional IsOptional,
    AArch64BuildAttributes::SubsectionType ParameterType) {
  AArch64TargetStreamer::emitAtributesSubsection(VendorName, IsOptional,
                                                 ParameterType);
}

void AArch64TargetELFStreamer::emitAttribute(StringRef VendorName, unsigned Tag,
                                             unsigned Value,
                                             std::string String) {
  if (unsigned(-1) != Value)
    AArch64TargetStreamer::emitAttribute(VendorName, Tag, Value, "");
  if ("" != String)
    AArch64TargetStreamer::emitAttribute(VendorName, Tag, unsigned(-1), String);
}

void AArch64TargetELFStreamer::emitInst(uint32_t Inst) {
  getStreamer().emitInst(Inst);
}

void AArch64TargetELFStreamer::emitDirectiveVariantPCS(MCSymbol *Symbol) {
  getStreamer().getAssembler().registerSymbol(*Symbol);
  cast<MCSymbolELF>(Symbol)->setOther(ELF::STO_AARCH64_VARIANT_PCS);
}

void AArch64TargetELFStreamer::finish() {
  AArch64TargetStreamer::finish();
  AArch64ELFStreamer &S = getStreamer();
  MCContext &Ctx = S.getContext();
  auto &Asm = S.getAssembler();

  S.emitAttributesSection(AttributeSection, ".ARM.attributes",
                          ELF::SHT_AARCH64_ATTRIBUTES, AttributeSubSections);

  // If ImplicitMapSyms is specified, ensure that text sections end with
  // the A64 state while non-text sections end with the data state. When
  // sections are combined by the linker, the subsequent section will start with
  // the right state. The ending mapping symbol is added right after the last
  // symbol relative to the section. When a dumb linker combines (.text.0; .word
  // 0) and (.text.1; .word 0), the ending $x of .text.0 precedes the $d of
  // .text.1, even if they have the same address.
  if (S.ImplicitMapSyms) {
    auto &Syms = Asm.getSymbols();
    const size_t NumSyms = Syms.size();
    DenseMap<MCSection *, std::pair<size_t, MCSymbol *>> EndMapSym;
    for (MCSection &Sec : Asm) {
      S.switchSection(&Sec);
      if (S.LastEMS == (Sec.isText() ? AArch64ELFStreamer::EMS_Data
                                     : AArch64ELFStreamer::EMS_A64))
        EndMapSym.insert(
            {&Sec, {NumSyms, S.emitMappingSymbol(Sec.isText() ? "$x" : "$d")}});
    }
    if (Syms.size() != NumSyms) {
      SmallVector<const MCSymbol *, 0> NewSyms;
      DenseMap<MCSection *, size_t> Cnt;
      Syms.truncate(NumSyms);
      // Find the last symbol index for each candidate section.
      for (auto [I, Sym] : llvm::enumerate(Syms)) {
        if (!Sym->isInSection())
          continue;
        auto It = EndMapSym.find(&Sym->getSection());
        if (It != EndMapSym.end())
          It->second.first = I;
      }
      SmallVector<size_t, 0> Idx;
      for (auto [I, Sym] : llvm::enumerate(Syms)) {
        NewSyms.push_back(Sym);
        if (!Sym->isInSection())
          continue;
        auto It = EndMapSym.find(&Sym->getSection());
        // If `Sym` is the last symbol relative to the section, add the ending
        // mapping symbol after `Sym`.
        if (It != EndMapSym.end() && I == It->second.first) {
          NewSyms.push_back(It->second.second);
          Idx.push_back(I);
        }
      }
      Syms = std::move(NewSyms);
      // F.second holds the number of symbols added before the FILE symbol.
      // Take into account the inserted mapping symbols.
      for (auto &F : S.getWriter().getFileNames())
        F.second += llvm::lower_bound(Idx, F.second) - Idx.begin();
    }
  }

  // The mix of execute-only and non-execute-only at link time is
  // non-execute-only. To avoid the empty implicitly created .text
  // section from making the whole .text section non-execute-only, we
  // mark it execute-only if it is empty and there is at least one
  // execute-only section in the object.
  if (any_of(Asm, [](const MCSection &Sec) {
        return cast<MCSectionELF>(Sec).getFlags() & ELF::SHF_AARCH64_PURECODE;
      })) {
    auto *Text =
        static_cast<MCSectionELF *>(Ctx.getObjectFileInfo()->getTextSection());
    bool Empty = true;
    for (auto &F : *Text) {
      if (auto *DF = dyn_cast<MCDataFragment>(&F)) {
        if (!DF->getContents().empty()) {
          Empty = false;
          break;
        }
      }
    }
    if (Empty)
      Text->setFlags(Text->getFlags() | ELF::SHF_AARCH64_PURECODE);
  }

  MCSectionELF *MemtagSec = nullptr;
  for (const MCSymbol &Symbol : Asm.symbols()) {
    const auto &Sym = cast<MCSymbolELF>(Symbol);
    if (Sym.isMemtag()) {
      MemtagSec = Ctx.getELFSection(".memtag.globals.static",
                                    ELF::SHT_AARCH64_MEMTAG_GLOBALS_STATIC, 0);
      break;
    }
  }
  if (!MemtagSec)
    return;

  // switchSection registers the section symbol and invalidates symbols(). We
  // need a separate symbols() loop.
  S.switchSection(MemtagSec);
  const auto *Zero = MCConstantExpr::create(0, Ctx);
  for (const MCSymbol &Symbol : Asm.symbols()) {
    const auto &Sym = cast<MCSymbolELF>(Symbol);
    if (!Sym.isMemtag())
      continue;
    auto *SRE = MCSymbolRefExpr::create(&Sym, Ctx);
    (void)S.emitRelocDirective(*Zero, "BFD_RELOC_NONE", SRE, SMLoc(),
                               *Ctx.getSubtargetInfo());
  }
}

MCTargetStreamer *
llvm::createAArch64AsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                                     MCInstPrinter *InstPrint) {
  return new AArch64TargetAsmStreamer(S, OS);
}

MCStreamer *
llvm::createAArch64ELFStreamer(const Triple &TheTriple, MCContext &Context,
                               std::unique_ptr<MCAsmBackend> &&TAB,
                               std::unique_ptr<MCObjectWriter> &&OW,
                               std::unique_ptr<MCCodeEmitter> &&Emitter) {
  if (TheTriple.isVendorLFI())
    return new AArch64LFIELFStreamer(Context, std::move(TAB), std::move(OW),
                                  std::move(Emitter));
  return new AArch64ELFStreamer(Context, std::move(TAB), std::move(OW),
                                std::move(Emitter));
}
