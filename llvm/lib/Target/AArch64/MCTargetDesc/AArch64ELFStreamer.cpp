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
  unsigned scale(unsigned Op) {
    switch (Op) {
    case AArch64::LDPDpost:
    case AArch64::LDPDpre:
      return 8;
    case AArch64::LDPQpost:
    case AArch64::LDPQpre:
      return 16;
    case AArch64::LDPSWpost:
    case AArch64::LDPSWpre:
      return 4;
    case AArch64::LDPSpost:
    case AArch64::LDPSpre:
      return 4;
    case AArch64::LDPWpost:
    case AArch64::LDPWpre:
      return 4;
    case AArch64::LDPXpost:
    case AArch64::LDPXpre:
      return 8;
    case AArch64::STPDpost:
    case AArch64::STPDpre:
      return 8;
    case AArch64::STPQpost:
    case AArch64::STPQpre:
      return 16;
    case AArch64::STPSpost:
    case AArch64::STPSpre:
      return 4;
    case AArch64::STPWpost:
    case AArch64::STPWpre:
      return 4;
    case AArch64::STPXpost:
    case AArch64::STPXpre:
      return 8;
    }
    return 0;
  }

  unsigned demotePrePost(unsigned Op, bool &IsPre) {
    IsPre = false;
    switch (Op) {
    case AArch64::LDPDpost:
      return AArch64::LDPDi;
    case AArch64::LDPDpre:
      IsPre = true;
      return AArch64::LDPDi;
    case AArch64::LDPQpost:
      return AArch64::LDPQi;
    case AArch64::LDPQpre:
      IsPre = true;
      return AArch64::LDPQi;
    case AArch64::LDPSWpost:
      return AArch64::LDPSWi;
    case AArch64::LDPSWpre:
      IsPre = true;
      return AArch64::LDPSWi;
    case AArch64::LDPSpost:
      return AArch64::LDPSi;
    case AArch64::LDPSpre:
      IsPre = true;
      return AArch64::LDPSi;
    case AArch64::LDPWpost:
      return AArch64::LDPWi;
    case AArch64::LDPWpre:
      IsPre = true;
      return AArch64::LDPWi;
    case AArch64::LDPXpost:
      return AArch64::LDPXi;
    case AArch64::LDPXpre:
      IsPre = true;
      return AArch64::LDPXi;
    case AArch64::STPDpost:
      return AArch64::STPDi;
    case AArch64::STPDpre:
      IsPre = true;
      return AArch64::STPDi;
    case AArch64::STPQpost:
      return AArch64::STPQi;
    case AArch64::STPQpre:
      IsPre = true;
      return AArch64::STPQi;
    case AArch64::STPSpost:
      return AArch64::STPSi;
    case AArch64::STPSpre:
      IsPre = true;
      return AArch64::STPSi;
    case AArch64::STPWpost:
      return AArch64::STPWi;
    case AArch64::STPWpre:
      IsPre = true;
      return AArch64::STPWi;
    case AArch64::STPXpost:
      return AArch64::STPXi;
    case AArch64::STPXpre:
      IsPre = true;
      return AArch64::STPXi;
    case AArch64::LD1i64_POST:
      return AArch64::LD1i64;
    case AArch64::LD2i64_POST:
      return AArch64::LD2i64;
    case AArch64::LD1i8_POST:
      return AArch64::LD1i8;
    case AArch64::LD1i16_POST:
      return AArch64::LD1i16;
    case AArch64::LD1i32_POST:
      return AArch64::LD1i32;
    case AArch64::LD2i8_POST:
      return AArch64::LD2i8;
    case AArch64::LD2i16_POST:
      return AArch64::LD2i16;
    case AArch64::LD2i32_POST:
      return AArch64::LD2i32;
    case AArch64::LD3i8_POST:
      return AArch64::LD3i8;
    case AArch64::LD3i16_POST:
      return AArch64::LD3i16;
    case AArch64::LD3i32_POST:
      return AArch64::LD3i32;
    case AArch64::LD3i64_POST:
      return AArch64::LD3i64;
    case AArch64::LD4i8_POST:
      return AArch64::LD4i8;
    case AArch64::LD4i16_POST:
      return AArch64::LD4i16;
    case AArch64::LD4i32_POST:
      return AArch64::LD4i32;
    case AArch64::LD4i64_POST:
      return AArch64::LD4i64;
    case AArch64::LD1Onev1d_POST:
      return AArch64::LD1Onev1d;
    case AArch64::LD1Onev2s_POST:
      return AArch64::LD1Onev2s;
    case AArch64::LD1Onev4h_POST:
      return AArch64::LD1Onev4h;
    case AArch64::LD1Onev8b_POST:
      return AArch64::LD1Onev8b;
    case AArch64::LD1Onev2d_POST:
      return AArch64::LD1Onev2d;
    case AArch64::LD1Onev4s_POST:
      return AArch64::LD1Onev4s;
    case AArch64::LD1Onev8h_POST:
      return AArch64::LD1Onev8h;
    case AArch64::LD1Onev16b_POST:
      return AArch64::LD1Onev16b;
    case AArch64::LD1Rv1d_POST:
      return AArch64::LD1Rv1d;
    case AArch64::LD1Rv2s_POST:
      return AArch64::LD1Rv2s;
    case AArch64::LD1Rv4h_POST:
      return AArch64::LD1Rv4h;
    case AArch64::LD1Rv8b_POST:
      return AArch64::LD1Rv8b;
    case AArch64::LD1Rv2d_POST:
      return AArch64::LD1Rv2d;
    case AArch64::LD1Rv4s_POST:
      return AArch64::LD1Rv4s;
    case AArch64::LD1Rv8h_POST:
      return AArch64::LD1Rv8h;
    case AArch64::LD1Rv16b_POST:
      return AArch64::LD1Rv16b;
    case AArch64::LD1Twov1d_POST:
      return AArch64::LD1Twov1d;
    case AArch64::LD1Twov2s_POST:
      return AArch64::LD1Twov2s;
    case AArch64::LD1Twov4h_POST:
      return AArch64::LD1Twov4h;
    case AArch64::LD1Twov8b_POST:
      return AArch64::LD1Twov8b;
    case AArch64::LD1Twov2d_POST:
      return AArch64::LD1Twov2d;
    case AArch64::LD1Twov4s_POST:
      return AArch64::LD1Twov4s;
    case AArch64::LD1Twov8h_POST:
      return AArch64::LD1Twov8h;
    case AArch64::LD1Twov16b_POST:
      return AArch64::LD1Twov16b;
    case AArch64::LD1Threev1d_POST:
      return AArch64::LD1Threev1d;
    case AArch64::LD1Threev2s_POST:
      return AArch64::LD1Threev2s;
    case AArch64::LD1Threev4h_POST:
      return AArch64::LD1Threev4h;
    case AArch64::LD1Threev8b_POST:
      return AArch64::LD1Threev8b;
    case AArch64::LD1Threev2d_POST:
      return AArch64::LD1Threev2d;
    case AArch64::LD1Threev4s_POST:
      return AArch64::LD1Threev4s;
    case AArch64::LD1Threev8h_POST:
      return AArch64::LD1Threev8h;
    case AArch64::LD1Threev16b_POST:
      return AArch64::LD1Threev16b;
    case AArch64::LD1Fourv1d_POST:
      return AArch64::LD1Fourv1d;
    case AArch64::LD1Fourv2s_POST:
      return AArch64::LD1Fourv2s;
    case AArch64::LD1Fourv4h_POST:
      return AArch64::LD1Fourv4h;
    case AArch64::LD1Fourv8b_POST:
      return AArch64::LD1Fourv8b;
    case AArch64::LD1Fourv2d_POST:
      return AArch64::LD1Fourv2d;
    case AArch64::LD1Fourv4s_POST:
      return AArch64::LD1Fourv4s;
    case AArch64::LD1Fourv8h_POST:
      return AArch64::LD1Fourv8h;
    case AArch64::LD1Fourv16b_POST:
      return AArch64::LD1Fourv16b;
    case AArch64::LD2Twov2s_POST:
      return AArch64::LD2Twov2s;
    case AArch64::LD2Twov4s_POST:
      return AArch64::LD2Twov4s;
    case AArch64::LD2Twov8b_POST:
      return AArch64::LD2Twov8b;
    case AArch64::LD2Twov2d_POST:
      return AArch64::LD2Twov2d;
    case AArch64::LD2Twov4h_POST:
      return AArch64::LD2Twov4h;
    case AArch64::LD2Twov8h_POST:
      return AArch64::LD2Twov8h;
    case AArch64::LD2Twov16b_POST:
      return AArch64::LD2Twov16b;
    case AArch64::LD2Rv1d_POST:
      return AArch64::LD2Rv1d;
    case AArch64::LD2Rv2s_POST:
      return AArch64::LD2Rv2s;
    case AArch64::LD2Rv4s_POST:
      return AArch64::LD2Rv4s;
    case AArch64::LD2Rv8b_POST:
      return AArch64::LD2Rv8b;
    case AArch64::LD2Rv2d_POST:
      return AArch64::LD2Rv2d;
    case AArch64::LD2Rv4h_POST:
      return AArch64::LD2Rv4h;
    case AArch64::LD2Rv8h_POST:
      return AArch64::LD2Rv8h;
    case AArch64::LD2Rv16b_POST:
      return AArch64::LD2Rv16b;
    case AArch64::LD3Threev2s_POST:
      return AArch64::LD3Threev2s;
    case AArch64::LD3Threev4h_POST:
      return AArch64::LD3Threev4h;
    case AArch64::LD3Threev8b_POST:
      return AArch64::LD3Threev8b;
    case AArch64::LD3Threev2d_POST:
      return AArch64::LD3Threev2d;
    case AArch64::LD3Threev4s_POST:
      return AArch64::LD3Threev4s;
    case AArch64::LD3Threev8h_POST:
      return AArch64::LD3Threev8h;
    case AArch64::LD3Threev16b_POST:
      return AArch64::LD3Threev16b;
    case AArch64::LD3Rv1d_POST:
      return AArch64::LD3Rv1d;
    case AArch64::LD3Rv2s_POST:
      return AArch64::LD3Rv2s;
    case AArch64::LD3Rv4h_POST:
      return AArch64::LD3Rv4h;
    case AArch64::LD3Rv8b_POST:
      return AArch64::LD3Rv8b;
    case AArch64::LD3Rv2d_POST:
      return AArch64::LD3Rv2d;
    case AArch64::LD3Rv4s_POST:
      return AArch64::LD3Rv4s;
    case AArch64::LD3Rv8h_POST:
      return AArch64::LD3Rv8h;
    case AArch64::LD3Rv16b_POST:
      return AArch64::LD3Rv16b;
    case AArch64::LD4Fourv2s_POST:
      return AArch64::LD4Fourv2s;
    case AArch64::LD4Fourv4h_POST:
      return AArch64::LD4Fourv4h;
    case AArch64::LD4Fourv8b_POST:
      return AArch64::LD4Fourv8b;
    case AArch64::LD4Fourv2d_POST:
      return AArch64::LD4Fourv2d;
    case AArch64::LD4Fourv4s_POST:
      return AArch64::LD4Fourv4s;
    case AArch64::LD4Fourv8h_POST:
      return AArch64::LD4Fourv8h;
    case AArch64::LD4Fourv16b_POST:
      return AArch64::LD4Fourv16b;
    case AArch64::LD4Rv1d_POST:
      return AArch64::LD4Rv1d;
    case AArch64::LD4Rv2s_POST:
      return AArch64::LD4Rv2s;
    case AArch64::LD4Rv4h_POST:
      return AArch64::LD4Rv4h;
    case AArch64::LD4Rv8b_POST:
      return AArch64::LD4Rv8b;
    case AArch64::LD4Rv2d_POST:
      return AArch64::LD4Rv2d;
    case AArch64::LD4Rv4s_POST:
      return AArch64::LD4Rv4s;
    case AArch64::LD4Rv8h_POST:
      return AArch64::LD4Rv8h;
    case AArch64::LD4Rv16b_POST:
      return AArch64::LD4Rv16b;
    case AArch64::ST1i64_POST:
      return AArch64::ST1i64;
    case AArch64::ST2i64_POST:
      return AArch64::ST1i64;
    case AArch64::ST1i8_POST:
      return AArch64::ST1i64;
    case AArch64::ST1i16_POST:
      return AArch64::ST1i64;
    case AArch64::ST1i32_POST:
      return AArch64::ST1i64;
    case AArch64::ST2i8_POST:
      return AArch64::ST1i64;
    case AArch64::ST2i16_POST:
      return AArch64::ST1i64;
    case AArch64::ST2i32_POST:
      return AArch64::ST1i64;
    case AArch64::ST3i8_POST:
      return AArch64::ST1i64;
    case AArch64::ST3i16_POST:
      return AArch64::ST1i64;
    case AArch64::ST3i32_POST:
      return AArch64::ST1i64;
    case AArch64::ST3i64_POST:
      return AArch64::ST1i64;
    case AArch64::ST4i8_POST:
      return AArch64::ST4i8;
    case AArch64::ST4i16_POST:
      return AArch64::ST1i64;
    case AArch64::ST4i32_POST:
      return AArch64::ST1i64;
    case AArch64::ST4i64_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Onev1d_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Onev2s_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Onev4h_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Onev8b_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Onev2d_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Onev4s_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Onev8h_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Onev16b_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Twov1d_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Twov2s_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Twov4h_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Twov8b_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Twov2d_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Twov4s_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Twov8h_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Twov16b_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Threev1d_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Threev2s_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Threev4h_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Threev8b_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Threev2d_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Threev4s_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Threev8h_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Threev16b_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Fourv1d_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Fourv2s_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Fourv4h_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Fourv8b_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Fourv2d_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Fourv4s_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Fourv8h_POST:
      return AArch64::ST1i64;
    case AArch64::ST1Fourv16b_POST:
      return AArch64::ST1i64;
    case AArch64::ST2Twov2s_POST:
      return AArch64::ST1i64;
    case AArch64::ST2Twov4s_POST:
      return AArch64::ST1i64;
    case AArch64::ST2Twov8b_POST:
      return AArch64::ST1i64;
    case AArch64::ST2Twov2d_POST:
      return AArch64::ST1i64;
    case AArch64::ST2Twov4h_POST:
      return AArch64::ST1i64;
    case AArch64::ST2Twov8h_POST:
      return AArch64::ST1i64;
    case AArch64::ST2Twov16b_POST:
      return AArch64::ST1i64;
    case AArch64::ST3Threev2s_POST:
      return AArch64::ST1i64;
    case AArch64::ST3Threev4h_POST:
      return AArch64::ST1i64;
    case AArch64::ST3Threev8b_POST:
      return AArch64::ST1i64;
    case AArch64::ST3Threev2d_POST:
      return AArch64::ST1i64;
    case AArch64::ST3Threev4s_POST:
      return AArch64::ST1i64;
    case AArch64::ST3Threev8h_POST:
      return AArch64::ST1i64;
    case AArch64::ST3Threev16b_POST:
      return AArch64::ST1i64;
    case AArch64::ST4Fourv2s_POST:
      return AArch64::ST1i64;
    case AArch64::ST4Fourv4h_POST:
      return AArch64::ST1i64;
    case AArch64::ST4Fourv8b_POST:
      return AArch64::ST1i64;
    case AArch64::ST4Fourv2d_POST:
      return AArch64::ST1i64;
    case AArch64::ST4Fourv4s_POST:
      return AArch64::ST1i64;
    case AArch64::ST4Fourv8h_POST:
      return AArch64::ST1i64;
    case AArch64::ST4Fourv16b_POST:
      return AArch64::ST1i64;
    }
    return AArch64::INSTRUCTION_LIST_END;
  }

  bool getLoadInfo(const MCInst &Inst, int &DestRegIdx, int &BaseRegIdx, int &OffsetIdx, bool &IsPrePost) {
    switch (Inst.getOpcode()) {
    default:
      return false;

    case AArch64::LD1i64:
    case AArch64::LD2i64:
      DestRegIdx = 0;
      BaseRegIdx = 3;
      OffsetIdx = -1;
      IsPrePost = false;
      break;

    case AArch64::LD1i8:
    case AArch64::LD1i16:
    case AArch64::LD1i32:
    case AArch64::LD2i8:
    case AArch64::LD2i16:
    case AArch64::LD2i32:
    case AArch64::LD3i8:
    case AArch64::LD3i16:
    case AArch64::LD3i32:
    case AArch64::LD3i64:
    case AArch64::LD4i8:
    case AArch64::LD4i16:
    case AArch64::LD4i32:
    case AArch64::LD4i64:
      DestRegIdx = -1;
      BaseRegIdx = 3;
      OffsetIdx = -1;
      IsPrePost = false;
      break;

    case AArch64::LD1Onev1d:
    case AArch64::LD1Onev2s:
    case AArch64::LD1Onev4h:
    case AArch64::LD1Onev8b:
    case AArch64::LD1Onev2d:
    case AArch64::LD1Onev4s:
    case AArch64::LD1Onev8h:
    case AArch64::LD1Onev16b:
    case AArch64::LD1Rv1d:
    case AArch64::LD1Rv2s:
    case AArch64::LD1Rv4h:
    case AArch64::LD1Rv8b:
    case AArch64::LD1Rv2d:
    case AArch64::LD1Rv4s:
    case AArch64::LD1Rv8h:
    case AArch64::LD1Rv16b:
      DestRegIdx = 0;
      BaseRegIdx = 1;
      OffsetIdx = -1;
      IsPrePost = false;
      break;

    case AArch64::LD1Twov1d:
    case AArch64::LD1Twov2s:
    case AArch64::LD1Twov4h:
    case AArch64::LD1Twov8b:
    case AArch64::LD1Twov2d:
    case AArch64::LD1Twov4s:
    case AArch64::LD1Twov8h:
    case AArch64::LD1Twov16b:
    case AArch64::LD1Threev1d:
    case AArch64::LD1Threev2s:
    case AArch64::LD1Threev4h:
    case AArch64::LD1Threev8b:
    case AArch64::LD1Threev2d:
    case AArch64::LD1Threev4s:
    case AArch64::LD1Threev8h:
    case AArch64::LD1Threev16b:
    case AArch64::LD1Fourv1d:
    case AArch64::LD1Fourv2s:
    case AArch64::LD1Fourv4h:
    case AArch64::LD1Fourv8b:
    case AArch64::LD1Fourv2d:
    case AArch64::LD1Fourv4s:
    case AArch64::LD1Fourv8h:
    case AArch64::LD1Fourv16b:
    case AArch64::LD2Twov2s:
    case AArch64::LD2Twov4s:
    case AArch64::LD2Twov8b:
    case AArch64::LD2Twov2d:
    case AArch64::LD2Twov4h:
    case AArch64::LD2Twov8h:
    case AArch64::LD2Twov16b:
    case AArch64::LD2Rv1d:
    case AArch64::LD2Rv2s:
    case AArch64::LD2Rv4s:
    case AArch64::LD2Rv8b:
    case AArch64::LD2Rv2d:
    case AArch64::LD2Rv4h:
    case AArch64::LD2Rv8h:
    case AArch64::LD2Rv16b:
    case AArch64::LD3Threev2s:
    case AArch64::LD3Threev4h:
    case AArch64::LD3Threev8b:
    case AArch64::LD3Threev2d:
    case AArch64::LD3Threev4s:
    case AArch64::LD3Threev8h:
    case AArch64::LD3Threev16b:
    case AArch64::LD3Rv1d:
    case AArch64::LD3Rv2s:
    case AArch64::LD3Rv4h:
    case AArch64::LD3Rv8b:
    case AArch64::LD3Rv2d:
    case AArch64::LD3Rv4s:
    case AArch64::LD3Rv8h:
    case AArch64::LD3Rv16b:
    case AArch64::LD4Fourv2s:
    case AArch64::LD4Fourv4h:
    case AArch64::LD4Fourv8b:
    case AArch64::LD4Fourv2d:
    case AArch64::LD4Fourv4s:
    case AArch64::LD4Fourv8h:
    case AArch64::LD4Fourv16b:
    case AArch64::LD4Rv1d:
    case AArch64::LD4Rv2s:
    case AArch64::LD4Rv4h:
    case AArch64::LD4Rv8b:
    case AArch64::LD4Rv2d:
    case AArch64::LD4Rv4s:
    case AArch64::LD4Rv8h:
    case AArch64::LD4Rv16b:
      DestRegIdx = -1;
      BaseRegIdx = 1;
      OffsetIdx = -1;
      IsPrePost = false;
      break;

    case AArch64::LD1i64_POST:
    case AArch64::LD2i64_POST:
      DestRegIdx = 1;
      BaseRegIdx = 4;
      OffsetIdx = 5;
      IsPrePost = true;
      break;

    case AArch64::LD1i8_POST:
    case AArch64::LD1i16_POST:
    case AArch64::LD1i32_POST:
    case AArch64::LD2i8_POST:
    case AArch64::LD2i16_POST:
    case AArch64::LD2i32_POST:
    case AArch64::LD3i8_POST:
    case AArch64::LD3i16_POST:
    case AArch64::LD3i32_POST:
    case AArch64::LD3i64_POST:
    case AArch64::LD4i8_POST:
    case AArch64::LD4i16_POST:
    case AArch64::LD4i32_POST:
    case AArch64::LD4i64_POST:
      DestRegIdx = -1;
      BaseRegIdx = 4;
      OffsetIdx = 5;
      IsPrePost = true;
      break;

    case AArch64::LD1Onev1d_POST:
    case AArch64::LD1Onev2s_POST:
    case AArch64::LD1Onev4h_POST:
    case AArch64::LD1Onev8b_POST:
    case AArch64::LD1Onev2d_POST:
    case AArch64::LD1Onev4s_POST:
    case AArch64::LD1Onev8h_POST:
    case AArch64::LD1Onev16b_POST:
    case AArch64::LD1Rv1d_POST:
    case AArch64::LD1Rv2s_POST:
    case AArch64::LD1Rv4h_POST:
    case AArch64::LD1Rv8b_POST:
    case AArch64::LD1Rv2d_POST:
    case AArch64::LD1Rv4s_POST:
    case AArch64::LD1Rv8h_POST:
    case AArch64::LD1Rv16b_POST:
      DestRegIdx = 1;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = true;
      break;

    case AArch64::LD1Twov1d_POST:
    case AArch64::LD1Twov2s_POST:
    case AArch64::LD1Twov4h_POST:
    case AArch64::LD1Twov8b_POST:
    case AArch64::LD1Twov2d_POST:
    case AArch64::LD1Twov4s_POST:
    case AArch64::LD1Twov8h_POST:
    case AArch64::LD1Twov16b_POST:
    case AArch64::LD1Threev1d_POST:
    case AArch64::LD1Threev2s_POST:
    case AArch64::LD1Threev4h_POST:
    case AArch64::LD1Threev8b_POST:
    case AArch64::LD1Threev2d_POST:
    case AArch64::LD1Threev4s_POST:
    case AArch64::LD1Threev8h_POST:
    case AArch64::LD1Threev16b_POST:
    case AArch64::LD1Fourv1d_POST:
    case AArch64::LD1Fourv2s_POST:
    case AArch64::LD1Fourv4h_POST:
    case AArch64::LD1Fourv8b_POST:
    case AArch64::LD1Fourv2d_POST:
    case AArch64::LD1Fourv4s_POST:
    case AArch64::LD1Fourv8h_POST:
    case AArch64::LD1Fourv16b_POST:
    case AArch64::LD2Twov2s_POST:
    case AArch64::LD2Twov4s_POST:
    case AArch64::LD2Twov8b_POST:
    case AArch64::LD2Twov2d_POST:
    case AArch64::LD2Twov4h_POST:
    case AArch64::LD2Twov8h_POST:
    case AArch64::LD2Twov16b_POST:
    case AArch64::LD2Rv1d_POST:
    case AArch64::LD2Rv2s_POST:
    case AArch64::LD2Rv4s_POST:
    case AArch64::LD2Rv8b_POST:
    case AArch64::LD2Rv2d_POST:
    case AArch64::LD2Rv4h_POST:
    case AArch64::LD2Rv8h_POST:
    case AArch64::LD2Rv16b_POST:
    case AArch64::LD3Threev2s_POST:
    case AArch64::LD3Threev4h_POST:
    case AArch64::LD3Threev8b_POST:
    case AArch64::LD3Threev2d_POST:
    case AArch64::LD3Threev4s_POST:
    case AArch64::LD3Threev8h_POST:
    case AArch64::LD3Threev16b_POST:
    case AArch64::LD3Rv1d_POST:
    case AArch64::LD3Rv2s_POST:
    case AArch64::LD3Rv4h_POST:
    case AArch64::LD3Rv8b_POST:
    case AArch64::LD3Rv2d_POST:
    case AArch64::LD3Rv4s_POST:
    case AArch64::LD3Rv8h_POST:
    case AArch64::LD3Rv16b_POST:
    case AArch64::LD4Fourv2s_POST:
    case AArch64::LD4Fourv4h_POST:
    case AArch64::LD4Fourv8b_POST:
    case AArch64::LD4Fourv2d_POST:
    case AArch64::LD4Fourv4s_POST:
    case AArch64::LD4Fourv8h_POST:
    case AArch64::LD4Fourv16b_POST:
    case AArch64::LD4Rv1d_POST:
    case AArch64::LD4Rv2s_POST:
    case AArch64::LD4Rv4h_POST:
    case AArch64::LD4Rv8b_POST:
    case AArch64::LD4Rv2d_POST:
    case AArch64::LD4Rv4s_POST:
    case AArch64::LD4Rv8h_POST:
    case AArch64::LD4Rv16b_POST:
      DestRegIdx = -1;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = true;
      break;

    case AArch64::LDRBBroW:
    case AArch64::LDRBBroX:
    case AArch64::LDRBBui:
    case AArch64::LDRBroW:
    case AArch64::LDRBroX:
    case AArch64::LDRBui:
    case AArch64::LDRDl:
    case AArch64::LDRDroW:
    case AArch64::LDRDroX:
    case AArch64::LDRDui:
    case AArch64::LDRHHroW:
    case AArch64::LDRHHroX:
    case AArch64::LDRHHui:
    case AArch64::LDRHroW:
    case AArch64::LDRHroX:
    case AArch64::LDRHui:
    case AArch64::LDRQl:
    case AArch64::LDRQroW:
    case AArch64::LDRQroX:
    case AArch64::LDRQui:
    case AArch64::LDRSBWroW:
    case AArch64::LDRSBWroX:
    case AArch64::LDRSBWui:
    case AArch64::LDRSBXroW:
    case AArch64::LDRSBXroX:
    case AArch64::LDRSBXui:
    case AArch64::LDRSHWroW:
    case AArch64::LDRSHWroX:
    case AArch64::LDRSHWui:
    case AArch64::LDRSHXroW:
    case AArch64::LDRSHXroX:
    case AArch64::LDRSHXui:
    case AArch64::LDRSWl:
    case AArch64::LDRSWroW:
    case AArch64::LDRSWroX:
    case AArch64::LDRSWui:
    case AArch64::LDRSl:
    case AArch64::LDRSroW:
    case AArch64::LDRSroX:
    case AArch64::LDRSui:
    case AArch64::LDRWl:
    case AArch64::LDRWroW:
    case AArch64::LDRWroX:
    case AArch64::LDRWui:
    case AArch64::LDRXl:
    case AArch64::LDRXroW:
    case AArch64::LDRXroX:
    case AArch64::LDRXui:
    case AArch64::LDURBBi:
    case AArch64::LDURBi:
    case AArch64::LDURDi:
    case AArch64::LDURHHi:
    case AArch64::LDURHi:
    case AArch64::LDURQi:
    case AArch64::LDURSBWi:
    case AArch64::LDURSBXi:
    case AArch64::LDURSHWi:
    case AArch64::LDURSHXi:
    case AArch64::LDURSWi:
    case AArch64::LDURSi:
    case AArch64::LDURWi:
    case AArch64::LDURXi:
      DestRegIdx = 0;
      BaseRegIdx = 1;
      OffsetIdx = 2;
      IsPrePost = false;
      break;

    case AArch64::LDRBBpost:
    case AArch64::LDRBBpre:
    case AArch64::LDRBpost:
    case AArch64::LDRBpre:
    case AArch64::LDRDpost:
    case AArch64::LDRDpre:
    case AArch64::LDRHHpost:
    case AArch64::LDRHHpre:
    case AArch64::LDRHpost:
    case AArch64::LDRHpre:
    case AArch64::LDRQpost:
    case AArch64::LDRQpre:
    case AArch64::LDRSBWpost:
    case AArch64::LDRSBWpre:
    case AArch64::LDRSBXpost:
    case AArch64::LDRSBXpre:
    case AArch64::LDRSHWpost:
    case AArch64::LDRSHWpre:
    case AArch64::LDRSHXpost:
    case AArch64::LDRSHXpre:
    case AArch64::LDRSWpost:
    case AArch64::LDRSWpre:
    case AArch64::LDRSpost:
    case AArch64::LDRSpre:
    case AArch64::LDRWpost:
    case AArch64::LDRWpre:
    case AArch64::LDRXpost:
    case AArch64::LDRXpre:
      DestRegIdx = 1;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = true;
      break;

    case AArch64::LDNPDi:
    case AArch64::LDNPQi:
    case AArch64::LDNPSi:
    case AArch64::LDPQi:
    case AArch64::LDPDi:
    case AArch64::LDPSi:
      DestRegIdx = -1;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = false;
      break;

    case AArch64::LDPSWi:
    case AArch64::LDPWi:
    case AArch64::LDPXi:
      DestRegIdx = 0;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = false;
      break;

    case AArch64::LDPQpost:
    case AArch64::LDPQpre:
    case AArch64::LDPDpost:
    case AArch64::LDPDpre:
    case AArch64::LDPSpost:
    case AArch64::LDPSpre:
      DestRegIdx = -1;
      BaseRegIdx = 3;
      OffsetIdx = 4;
      IsPrePost = true;
      break;

    case AArch64::LDPSWpost:
    case AArch64::LDPSWpre:
    case AArch64::LDPWpost:
    case AArch64::LDPWpre:
    case AArch64::LDPXpost:
    case AArch64::LDPXpre:
      DestRegIdx = 1;
      BaseRegIdx = 3;
      OffsetIdx = 4;
      IsPrePost = true;
      break;

    case AArch64::LDXRB:
    case AArch64::LDXRH:
    case AArch64::LDXRW:
    case AArch64::LDXRX:
    case AArch64::LDAXRB:
    case AArch64::LDAXRH:
    case AArch64::LDAXRW:
    case AArch64::LDAXRX:
    case AArch64::LDARB:
    case AArch64::LDARH:
    case AArch64::LDARW:
    case AArch64::LDARX:
      DestRegIdx = -1;
      BaseRegIdx = 1;
      OffsetIdx = -1;
      break;

    case AArch64::LDXPW:
    case AArch64::LDXPX:
    case AArch64::LDAXPW:
    case AArch64::LDAXPX:
      DestRegIdx = -1;
      BaseRegIdx = 2;
      OffsetIdx = -1;
      break;
    }

    return true;
  }

  bool getStoreInfo(const MCInst &Inst, int &DestRegIdx, int &BaseRegIdx, int &OffsetIdx, bool &IsPrePost) {
    switch (Inst.getOpcode()) {
    default:
      return false;

    case AArch64::ST1i64:
    case AArch64::ST2i64:
      DestRegIdx = 0;
      BaseRegIdx = 3;
      OffsetIdx = -1;
      IsPrePost = false;
      break;

    case AArch64::ST1i8:
    case AArch64::ST1i16:
    case AArch64::ST1i32:
    case AArch64::ST2i8:
    case AArch64::ST2i16:
    case AArch64::ST2i32:
    case AArch64::ST3i8:
    case AArch64::ST3i16:
    case AArch64::ST3i32:
    case AArch64::ST3i64:
    case AArch64::ST4i8:
    case AArch64::ST4i16:
    case AArch64::ST4i32:
    case AArch64::ST4i64:
      DestRegIdx = -1;
      BaseRegIdx = 3;
      OffsetIdx = -1;
      IsPrePost = false;
      break;

    case AArch64::ST1Onev1d:
    case AArch64::ST1Onev2s:
    case AArch64::ST1Onev4h:
    case AArch64::ST1Onev8b:
    case AArch64::ST1Onev2d:
    case AArch64::ST1Onev4s:
    case AArch64::ST1Onev8h:
    case AArch64::ST1Onev16b:
      DestRegIdx = 0;
      BaseRegIdx = 1;
      OffsetIdx = -1;
      IsPrePost = false;
      break;

    case AArch64::ST1Twov1d:
    case AArch64::ST1Twov2s:
    case AArch64::ST1Twov4h:
    case AArch64::ST1Twov8b:
    case AArch64::ST1Twov2d:
    case AArch64::ST1Twov4s:
    case AArch64::ST1Twov8h:
    case AArch64::ST1Twov16b:
    case AArch64::ST1Threev1d:
    case AArch64::ST1Threev2s:
    case AArch64::ST1Threev4h:
    case AArch64::ST1Threev8b:
    case AArch64::ST1Threev2d:
    case AArch64::ST1Threev4s:
    case AArch64::ST1Threev8h:
    case AArch64::ST1Threev16b:
    case AArch64::ST1Fourv1d:
    case AArch64::ST1Fourv2s:
    case AArch64::ST1Fourv4h:
    case AArch64::ST1Fourv8b:
    case AArch64::ST1Fourv2d:
    case AArch64::ST1Fourv4s:
    case AArch64::ST1Fourv8h:
    case AArch64::ST1Fourv16b:
    case AArch64::ST2Twov2s:
    case AArch64::ST2Twov4s:
    case AArch64::ST2Twov8b:
    case AArch64::ST2Twov2d:
    case AArch64::ST2Twov4h:
    case AArch64::ST2Twov8h:
    case AArch64::ST2Twov16b:
    case AArch64::ST3Threev2s:
    case AArch64::ST3Threev4h:
    case AArch64::ST3Threev8b:
    case AArch64::ST3Threev2d:
    case AArch64::ST3Threev4s:
    case AArch64::ST3Threev8h:
    case AArch64::ST3Threev16b:
    case AArch64::ST4Fourv2s:
    case AArch64::ST4Fourv4h:
    case AArch64::ST4Fourv8b:
    case AArch64::ST4Fourv2d:
    case AArch64::ST4Fourv4s:
    case AArch64::ST4Fourv8h:
    case AArch64::ST4Fourv16b:
      DestRegIdx = -1;
      BaseRegIdx = 1;
      OffsetIdx = -1;
      IsPrePost = false;
      break;

    case AArch64::ST1i64_POST:
    case AArch64::ST2i64_POST:
      DestRegIdx = 1;
      BaseRegIdx = 4;
      OffsetIdx = 5;
      IsPrePost = true;
      break;

    case AArch64::ST1i8_POST:
    case AArch64::ST1i16_POST:
    case AArch64::ST1i32_POST:
    case AArch64::ST2i8_POST:
    case AArch64::ST2i16_POST:
    case AArch64::ST2i32_POST:
    case AArch64::ST3i8_POST:
    case AArch64::ST3i16_POST:
    case AArch64::ST3i32_POST:
    case AArch64::ST3i64_POST:
    case AArch64::ST4i8_POST:
    case AArch64::ST4i16_POST:
    case AArch64::ST4i32_POST:
    case AArch64::ST4i64_POST:
      DestRegIdx = -1;
      BaseRegIdx = 4;
      OffsetIdx = 5;
      IsPrePost = true;
      break;

    case AArch64::ST1Onev1d_POST:
    case AArch64::ST1Onev2s_POST:
    case AArch64::ST1Onev4h_POST:
    case AArch64::ST1Onev8b_POST:
    case AArch64::ST1Onev2d_POST:
    case AArch64::ST1Onev4s_POST:
    case AArch64::ST1Onev8h_POST:
    case AArch64::ST1Onev16b_POST:
      DestRegIdx = 1;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = true;
      break;

    case AArch64::ST1Twov1d_POST:
    case AArch64::ST1Twov2s_POST:
    case AArch64::ST1Twov4h_POST:
    case AArch64::ST1Twov8b_POST:
    case AArch64::ST1Twov2d_POST:
    case AArch64::ST1Twov4s_POST:
    case AArch64::ST1Twov8h_POST:
    case AArch64::ST1Twov16b_POST:
    case AArch64::ST1Threev1d_POST:
    case AArch64::ST1Threev2s_POST:
    case AArch64::ST1Threev4h_POST:
    case AArch64::ST1Threev8b_POST:
    case AArch64::ST1Threev2d_POST:
    case AArch64::ST1Threev4s_POST:
    case AArch64::ST1Threev8h_POST:
    case AArch64::ST1Threev16b_POST:
    case AArch64::ST1Fourv1d_POST:
    case AArch64::ST1Fourv2s_POST:
    case AArch64::ST1Fourv4h_POST:
    case AArch64::ST1Fourv8b_POST:
    case AArch64::ST1Fourv2d_POST:
    case AArch64::ST1Fourv4s_POST:
    case AArch64::ST1Fourv8h_POST:
    case AArch64::ST1Fourv16b_POST:
    case AArch64::ST2Twov2s_POST:
    case AArch64::ST2Twov4s_POST:
    case AArch64::ST2Twov8b_POST:
    case AArch64::ST2Twov2d_POST:
    case AArch64::ST2Twov4h_POST:
    case AArch64::ST2Twov8h_POST:
    case AArch64::ST2Twov16b_POST:
    case AArch64::ST3Threev2s_POST:
    case AArch64::ST3Threev4h_POST:
    case AArch64::ST3Threev8b_POST:
    case AArch64::ST3Threev2d_POST:
    case AArch64::ST3Threev4s_POST:
    case AArch64::ST3Threev8h_POST:
    case AArch64::ST3Threev16b_POST:
    case AArch64::ST4Fourv2s_POST:
    case AArch64::ST4Fourv4h_POST:
    case AArch64::ST4Fourv8b_POST:
    case AArch64::ST4Fourv2d_POST:
    case AArch64::ST4Fourv4s_POST:
    case AArch64::ST4Fourv8h_POST:
    case AArch64::ST4Fourv16b_POST:
      DestRegIdx = -1;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = true;
      break;

    case AArch64::STRBBroW:
    case AArch64::STRBBroX:
    case AArch64::STRBBui:
    case AArch64::STRBroW:
    case AArch64::STRBroX:
    case AArch64::STRBui:
    case AArch64::STRDroW:
    case AArch64::STRDroX:
    case AArch64::STRDui:
    case AArch64::STRHHroW:
    case AArch64::STRHHroX:
    case AArch64::STRHHui:
    case AArch64::STRHroW:
    case AArch64::STRHroX:
    case AArch64::STRHui:
    case AArch64::STRQroW:
    case AArch64::STRQroX:
    case AArch64::STRQui:
    case AArch64::STRSroW:
    case AArch64::STRSroX:
    case AArch64::STRSui:
    case AArch64::STRWroW:
    case AArch64::STRWroX:
    case AArch64::STRWui:
    case AArch64::STRXroW:
    case AArch64::STRXroX:
    case AArch64::STRXui:
    case AArch64::STURBBi:
    case AArch64::STURBi:
    case AArch64::STURDi:
    case AArch64::STURHHi:
    case AArch64::STURHi:
    case AArch64::STURQi:
    case AArch64::STURSi:
    case AArch64::STURWi:
    case AArch64::STURXi:
      DestRegIdx = 0;
      BaseRegIdx = 1;
      OffsetIdx = 2;
      IsPrePost = false;
      break;

    case AArch64::STRBBpost:
    case AArch64::STRBBpre:
    case AArch64::STRBpost:
    case AArch64::STRBpre:
    case AArch64::STRDpost:
    case AArch64::STRDpre:
    case AArch64::STRHHpost:
    case AArch64::STRHHpre:
    case AArch64::STRHpost:
    case AArch64::STRHpre:
    case AArch64::STRQpost:
    case AArch64::STRQpre:
    case AArch64::STRSpost:
    case AArch64::STRSpre:
    case AArch64::STRWpost:
    case AArch64::STRWpre:
    case AArch64::STRXpost:
    case AArch64::STRXpre:
      DestRegIdx = 1;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = true;
      break;

    case AArch64::STNPDi:
    case AArch64::STNPQi:
    case AArch64::STNPSi:
    case AArch64::STPQi:
    case AArch64::STPDi:
    case AArch64::STPSi:
      DestRegIdx = -1;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = false;
      break;

    case AArch64::STPWi:
    case AArch64::STPXi:
      DestRegIdx = 0;
      BaseRegIdx = 2;
      OffsetIdx = 3;
      IsPrePost = false;
      break;

    case AArch64::STPQpost:
    case AArch64::STPQpre:
    case AArch64::STPDpost:
    case AArch64::STPDpre:
    case AArch64::STPSpost:
    case AArch64::STPSpre:
      DestRegIdx = -1;
      BaseRegIdx = 3;
      OffsetIdx = 4;
      IsPrePost = true;
      break;

    case AArch64::STPWpost:
    case AArch64::STPWpre:
    case AArch64::STPXpost:
    case AArch64::STPXpre:
      DestRegIdx = 1;
      BaseRegIdx = 3;
      OffsetIdx = 4;
      IsPrePost = true;
      break;

    case AArch64::STXRB:
    case AArch64::STXRH:
    case AArch64::STXRW:
    case AArch64::STXRX:
    case AArch64::STLXRB:
    case AArch64::STLXRH:
    case AArch64::STLXRW:
    case AArch64::STLXRX:
      DestRegIdx = -1;
      BaseRegIdx = 2;
      OffsetIdx = -1;
      break;

    case AArch64::STXPW:
    case AArch64::STXPX:
    case AArch64::STLXPW:
    case AArch64::STLXPX:
      DestRegIdx = -1;
      BaseRegIdx = 3;
      OffsetIdx = -1;
      break;
    }

    return true;
  }

  unsigned int getMemPre(unsigned int Op) {
    switch (Op) {
    case AArch64::LDRBBpre:
      return AArch64::LDRBBroW;
    case AArch64::LDRBpre:
      return AArch64::LDRBroW;
    case AArch64::LDRDpre:
      return AArch64::LDRDroW;
    case AArch64::LDRHHpre:
      return AArch64::LDRHHroW;
    case AArch64::LDRHpre:
      return AArch64::LDRHroW;
    case AArch64::LDRQpre:
      return AArch64::LDRQroW;
    case AArch64::LDRSBWpre:
      return AArch64::LDRSBWroW;
    case AArch64::LDRSBXpre:
      return AArch64::LDRSBXroW;
    case AArch64::LDRSHWpre:
      return AArch64::LDRSHWroW;
    case AArch64::LDRSHXpre:
      return AArch64::LDRSHXroW;
    case AArch64::LDRSWpre:
      return AArch64::LDRSWroW;
    case AArch64::LDRSpre:
      return AArch64::LDRSroW;
    case AArch64::LDRWpre:
      return AArch64::LDRWroW;
    case AArch64::LDRXpre:
      return AArch64::LDRXroW;
    case AArch64::STRBBpre:
      return AArch64::STRBBroW;
    case AArch64::STRBpre:
      return AArch64::STRBroW;
    case AArch64::STRDpre:
      return AArch64::STRDroW;
    case AArch64::STRHHpre:
      return AArch64::STRHHroW;
    case AArch64::STRHpre:
      return AArch64::STRHroW;
    case AArch64::STRQpre:
      return AArch64::STRQroW;
    case AArch64::STRSpre:
      return AArch64::STRSroW;
    case AArch64::STRWpre:
      return AArch64::STRWroW;
    case AArch64::STRXpre:
      return AArch64::STRXroW;
    }
    return AArch64::INSTRUCTION_LIST_END;
  }

  unsigned int getMemPost(unsigned int Op) {
    switch (Op) {
    case AArch64::LDRBBpost:
      return AArch64::LDRBBroW;
    case AArch64::LDRBpost:
      return AArch64::LDRBroW;
    case AArch64::LDRDpost:
      return AArch64::LDRDroW;
    case AArch64::LDRHHpost:
      return AArch64::LDRHHroW;
    case AArch64::LDRHpost:
      return AArch64::LDRHroW;
    case AArch64::LDRQpost:
      return AArch64::LDRQroW;
    case AArch64::LDRSBWpost:
      return AArch64::LDRSBWroW;
    case AArch64::LDRSBXpost:
      return AArch64::LDRSBXroW;
    case AArch64::LDRSHWpost:
      return AArch64::LDRSHWroW;
    case AArch64::LDRSHXpost:
      return AArch64::LDRSHXroW;
    case AArch64::LDRSWpost:
      return AArch64::LDRSWroW;
    case AArch64::LDRSpost:
      return AArch64::LDRSroW;
    case AArch64::LDRWpost:
      return AArch64::LDRWroW;
    case AArch64::LDRXpost:
      return AArch64::LDRXroW;
    case AArch64::STRBBpost:
      return AArch64::STRBBroW;
    case AArch64::STRBpost:
      return AArch64::STRBroW;
    case AArch64::STRDpost:
      return AArch64::STRDroW;
    case AArch64::STRHHpost:
      return AArch64::STRHHroW;
    case AArch64::STRHpost:
      return AArch64::STRHroW;
    case AArch64::STRQpost:
      return AArch64::STRQroW;
    case AArch64::STRSpost:
      return AArch64::STRSroW;
    case AArch64::STRWpost:
      return AArch64::STRWroW;
    case AArch64::STRXpost:
      return AArch64::STRXroW;
    }
    return AArch64::INSTRUCTION_LIST_END;
  }

  unsigned int getMemRegX(unsigned int Op, unsigned &Shift) {
    Shift = 0;
    switch (Op) {
    case AArch64::LDRBBroX:
      return AArch64::LDRBBroW;
    case AArch64::LDRBroX:
      return AArch64::LDRBroW;
    case AArch64::LDRDroX:
      Shift = 3;
      return AArch64::LDRDroW;
    case AArch64::LDRHHroX:
      Shift = 1;
      return AArch64::LDRHHroW;
    case AArch64::LDRHroX:
      Shift = 1;
      return AArch64::LDRHroW;
    case AArch64::LDRQroX:
      Shift = 4;
      return AArch64::LDRQroW;
    case AArch64::LDRSBWroX:
      Shift = 1;
      return AArch64::LDRSBWroW;
    case AArch64::LDRSBXroX:
      Shift = 1;
      return AArch64::LDRSBXroW;
    case AArch64::LDRSHWroX:
      Shift = 1;
      return AArch64::LDRSHWroW;
    case AArch64::LDRSHXroX:
      Shift = 1;
      return AArch64::LDRSHXroW;
    case AArch64::LDRSWroX:
      Shift = 2;
      return AArch64::LDRSWroW;
    case AArch64::LDRSroX:
      Shift = 2;
      return AArch64::LDRSroW;
    case AArch64::LDRWroX:
      Shift = 2;
      return AArch64::LDRWroW;
    case AArch64::LDRXroX:
      Shift = 3;
      return AArch64::LDRXroW;
    case AArch64::STRBBroX:
      return AArch64::STRBBroW;
    case AArch64::STRBroX:
      return AArch64::STRBroW;
    case AArch64::STRDroX:
      Shift = 3;
      return AArch64::STRDroW;
    case AArch64::STRHHroX:
      Shift = 1;
      return AArch64::STRHHroW;
    case AArch64::STRHroX:
      Shift = 1;
      return AArch64::STRHroW;
    case AArch64::STRQroX:
      Shift = 4;
      return AArch64::STRQroW;
    case AArch64::STRSroX:
      Shift = 2;
      return AArch64::STRSroW;
    case AArch64::STRWroX:
      Shift = 2;
      return AArch64::STRWroW;
    case AArch64::STRXroX:
      Shift = 3;
      return AArch64::STRXroW;
    }
    return AArch64::INSTRUCTION_LIST_END;
  }

  unsigned int getMemRegW(unsigned int Op, unsigned &Shift) {
    Shift = 0;
    switch (Op) {
    case AArch64::LDRBBroW:
      return AArch64::LDRBBroW;
    case AArch64::LDRBroW:
      return AArch64::LDRBroW;
    case AArch64::LDRDroW:
      Shift = 3;
      return AArch64::LDRDroW;
    case AArch64::LDRHHroW:
      Shift = 1;
      return AArch64::LDRHHroW;
    case AArch64::LDRHroW:
      Shift = 1;
      return AArch64::LDRHroW;
    case AArch64::LDRQroW:
      Shift = 4;
      return AArch64::LDRQroW;
    case AArch64::LDRSBWroW:
      Shift = 1;
      return AArch64::LDRSBWroW;
    case AArch64::LDRSBXroW:
      Shift = 1;
      return AArch64::LDRSBXroW;
    case AArch64::LDRSHWroW:
      Shift = 1;
      return AArch64::LDRSHWroW;
    case AArch64::LDRSHXroW:
      Shift = 1;
      return AArch64::LDRSHXroW;
    case AArch64::LDRSWroW:
      Shift = 2;
      return AArch64::LDRSWroW;
    case AArch64::LDRSroW:
      Shift = 2;
      return AArch64::LDRSroW;
    case AArch64::LDRWroW:
      Shift = 2;
      return AArch64::LDRWroW;
    case AArch64::LDRXroW:
      Shift = 3;
      return AArch64::LDRXroW;
    case AArch64::STRBBroW:
      return AArch64::STRBBroW;
    case AArch64::STRBroW:
      return AArch64::STRBroW;
    case AArch64::STRDroW:
      Shift = 3;
      return AArch64::STRDroW;
    case AArch64::STRHHroW:
      Shift = 1;
      return AArch64::STRHHroW;
    case AArch64::STRHroW:
      Shift = 1;
      return AArch64::STRHroW;
    case AArch64::STRQroW:
      Shift = 4;
      return AArch64::STRQroW;
    case AArch64::STRSroW:
      Shift = 2;
      return AArch64::STRSroW;
    case AArch64::STRWroW:
      Shift = 2;
      return AArch64::STRWroW;
    case AArch64::STRXroW:
      Shift = 3;
      return AArch64::STRXroW;
    }
    return AArch64::INSTRUCTION_LIST_END;
  }

  unsigned int getMemSimple(unsigned int Op) {
    switch (Op) {
    case AArch64::LDRBBui:
      return AArch64::LDRBBroW;
    case AArch64::LDRBui:
      return AArch64::LDRBroW;
    case AArch64::LDRDui:
      return AArch64::LDRDroW;
    case AArch64::LDRHHui:
      return AArch64::LDRHHroW;
    case AArch64::LDRHui:
      return AArch64::LDRHroW;
    case AArch64::LDRQui:
      return AArch64::LDRQroW;
    case AArch64::LDRSBWui:
      return AArch64::LDRSBWroW;
    case AArch64::LDRSBXui:
      return AArch64::LDRSBXroW;
    case AArch64::LDRSHWui:
      return AArch64::LDRSHWroW;
    case AArch64::LDRSHXui:
      return AArch64::LDRSHXroW;
    case AArch64::LDRSWui:
      return AArch64::LDRSWroW;
    case AArch64::LDRSui:
      return AArch64::LDRSroW;
    case AArch64::LDRWui:
      return AArch64::LDRWroW;
    case AArch64::LDRXui:
      return AArch64::LDRXroW;
    case AArch64::STRBBui:
      return AArch64::STRBBroW;
    case AArch64::STRBui:
      return AArch64::STRBroW;
    case AArch64::STRDui:
      return AArch64::STRDroW;
    case AArch64::STRHHui:
      return AArch64::STRHHroW;
    case AArch64::STRHui:
      return AArch64::STRHroW;
    case AArch64::STRQui:
      return AArch64::STRQroW;
    case AArch64::STRSui:
      return AArch64::STRSroW;
    case AArch64::STRWui:
      return AArch64::STRWroW;
    case AArch64::STRXui:
      return AArch64::STRXroW;
    }
    return AArch64::INSTRUCTION_LIST_END;
  }

  bool isMemNoMaskAddr(unsigned Op) {
    unsigned Shift;
    return getMemSimple(Op) == AArch64::INSTRUCTION_LIST_END &&
      getMemPre(Op) == AArch64::INSTRUCTION_LIST_END &&
      getMemPost(Op) == AArch64::INSTRUCTION_LIST_END &&
      getMemRegX(Op, Shift) == AArch64::INSTRUCTION_LIST_END &&
      getMemRegW(Op, Shift) == AArch64::INSTRUCTION_LIST_END;
  }

  unsigned getMemN(unsigned Op) {
    switch (Op) {
    case AArch64::STXPW:
    case AArch64::STXPX:
      return 3;
    case AArch64::LDPDi:
    case AArch64::LDPDpost:
    case AArch64::LDPDpre:
    case AArch64::LDPQi:
    case AArch64::LDPQpost:
    case AArch64::LDPQpre:
    case AArch64::LDPSWi:
    case AArch64::LDPSWpost:
    case AArch64::LDPSWpre:
    case AArch64::LDPSi:
    case AArch64::LDPSpost:
    case AArch64::LDPSpre:
    case AArch64::LDPWi:
    case AArch64::LDPWpost:
    case AArch64::LDPWpre:
    case AArch64::LDPXi:
    case AArch64::LDPXpost:
    case AArch64::LDPXpre:
    case AArch64::STPDi:
    case AArch64::STPDpost:
    case AArch64::STPDpre:
    case AArch64::STPQi:
    case AArch64::STPQpost:
    case AArch64::STPQpre:
    case AArch64::STPSi:
    case AArch64::STPSpost:
    case AArch64::STPSpre:
    case AArch64::STPWi:
    case AArch64::STPWpost:
    case AArch64::STPWpre:
    case AArch64::STPXi:
    case AArch64::STPXpost:
    case AArch64::STPXpre:
    case AArch64::STXRB:
    case AArch64::STXRH:
    case AArch64::STXRW:
    case AArch64::STXRX:
      return 2;
    case AArch64::LDURBBi:
    case AArch64::LDURBi:
    case AArch64::LDURDi:
    case AArch64::LDURHHi:
    case AArch64::LDURHi:
    case AArch64::LDURQi:
    case AArch64::LDURSBWi:
    case AArch64::LDURSBXi:
    case AArch64::LDURSHWi:
    case AArch64::LDURSHXi:
    case AArch64::LDURSWi:
    case AArch64::LDURSi:
    case AArch64::LDURWi:
    case AArch64::LDURXi:
    case AArch64::STURBBi:
    case AArch64::STURBi:
    case AArch64::STURDi:
    case AArch64::STURHHi:
    case AArch64::STURHi:
    case AArch64::STURQi:
    case AArch64::STURSi:
    case AArch64::STURWi:
    case AArch64::STURXi:
    case AArch64::STTRBi:
    case AArch64::STTRHi:
    case AArch64::STTRWi:
    case AArch64::STTRXi:
    case AArch64::STTXRWr:
    case AArch64::STTXRXr:
      return 1;
    }
    return 0;
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
    SafeInst.setOpcode(demotePrePost(Inst.getOpcode(), IsPre));
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
          emitRRI0(AArch64::ADDXri, Base, Base, Inst.getOperand(OffsetIdx).getImm() * scale(Inst.getOpcode()), STI);
      } else {
        emitSafeMemN(BaseRegIdx, Inst, STI);
      }
      return;
    }

    unsigned MemOp;
    if ((MemOp = getMemSimple(Inst.getOpcode())) != AArch64::INSTRUCTION_LIST_END) {
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
    if ((MemOp = getMemPre(Inst.getOpcode())) != AArch64::INSTRUCTION_LIST_END) {
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
    if ((MemOp = getMemPost(Inst.getOpcode())) != AArch64::INSTRUCTION_LIST_END) {
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
    if ((MemOp = getMemRegX(Inst.getOpcode(), Shift)) != AArch64::INSTRUCTION_LIST_END) {
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
    if ((MemOp = getMemRegW(Inst.getOpcode(), Shift)) != AArch64::INSTRUCTION_LIST_END) {
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
