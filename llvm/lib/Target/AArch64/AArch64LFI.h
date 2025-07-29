#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64LFI_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64LFI_H

#include "MCTargetDesc/AArch64MCTargetDesc.h"

using namespace llvm;

namespace llvm {
  struct MemInstInfo {
    MemInstInfo() = default;

    int DestRegIdx;
    int BaseRegIdx;
    int OffsetIdx;
    bool IsPrePost = false;
    bool IsPair = false;
  };
static std::optional<MemInstInfo> getAtomicLdStInfo(const unsigned int Opcode) {
  int DestRegIdx;
  int BaseRegIdx;
  const int OffsetIdx = -1;
  const bool IsPrePost = false;
  bool IsPair = false;
  // (ST|LD)OPRegister: ld*, st*
  // CompareAndSwap(Pair)?(Unprvileged)?
  // Swap(LSUI)?
  // StoreRelease
  // LoadAcquire
  switch (Opcode) {
    // Compare-and-swap CAS(A)?(L)?(B|H|W|X)
    // MIR: Rs = op Rs, Rt, [Xn]
    case AArch64::CASB:
    case AArch64::CASH:
    case AArch64::CASW:
    case AArch64::CASX:
    case AArch64::CASAB:
    case AArch64::CASAH:
    case AArch64::CASAW:
    case AArch64::CASAX:
    case AArch64::CASALB:
    case AArch64::CASALH:
    case AArch64::CASALW:
    case AArch64::CASALX:
    case AArch64::CASLB:
    case AArch64::CASLH:
    case AArch64::CASLW:
    case AArch64::CASLX:
      DestRegIdx = 2;
      BaseRegIdx = 3;
      break;
    // rs1_rs2 = op rs1_rs2, rt1_rt2, [Xn]
    // case AArch64::CASPW:
    // case AArch64::CASPX:
    // case AArch64::CASPAW:
    // case AArch64::CASPAX:
    // case AArch64::CASPALW:
    // case AArch64::CASPALX:
    // case AArch64::CASPLW:
    // case AArch64::CASPLX:
    //   break;
    // swap SWP(A)?(L)?(B|H|W|X)
    // MIR: op Rs, Rt, [Xn]
    case AArch64::SWPB:
    case AArch64::SWPH:
    case AArch64::SWPW:
    case AArch64::SWPX:
    case AArch64::SWPAB:
    case AArch64::SWPAH:
    case AArch64::SWPAW:
    case AArch64::SWPAX:
    case AArch64::SWPALB:
    case AArch64::SWPALH:
    case AArch64::SWPALW:
    case AArch64::SWPALX:
    case AArch64::SWPLB:
    case AArch64::SWPLH:
    case AArch64::SWPLW:
    case AArch64::SWPLX:
      DestRegIdx = 1;
      BaseRegIdx = 2;
      break;
    // LD(ADD|CLR|EOR|SET)(A)?(L)?(B|H|W|X)
    // op Rs, Rt, [Xn]
    // if Rt == xzr, LD* aliases to ST*
    case AArch64::LDADDB:
    case AArch64::LDADDH:
    case AArch64::LDADDX:
    case AArch64::LDADDW:
    case AArch64::LDADDAB:
    case AArch64::LDADDAH:
    case AArch64::LDADDAW:
    case AArch64::LDADDAX:
    case AArch64::LDADDALB:
    case AArch64::LDADDALH:
    case AArch64::LDADDALW:
    case AArch64::LDADDALX:
    case AArch64::LDADDLB:
    case AArch64::LDADDLH:
    case AArch64::LDADDLX:
    case AArch64::LDADDLW:
      DestRegIdx = 1;
      BaseRegIdx = 2;
      break;
    case AArch64::LDCLRB:
    case AArch64::LDCLRH:
    case AArch64::LDCLRX:
    case AArch64::LDCLRW:
    case AArch64::LDCLRAB:
    case AArch64::LDCLRAH:
    case AArch64::LDCLRAW:
    case AArch64::LDCLRAX:
    case AArch64::LDCLRALB:
    case AArch64::LDCLRALH:
    case AArch64::LDCLRALW:
    case AArch64::LDCLRALX:
    case AArch64::LDCLRLB:
    case AArch64::LDCLRLH:
    case AArch64::LDCLRLX:
    case AArch64::LDCLRLW:
      DestRegIdx = 1;
      BaseRegIdx = 2;
      break;
    case AArch64::LDEORB:
    case AArch64::LDEORH:
    case AArch64::LDEORX:
    case AArch64::LDEORW:
    case AArch64::LDEORAB:
    case AArch64::LDEORAH:
    case AArch64::LDEORAW:
    case AArch64::LDEORAX:
    case AArch64::LDEORALB:
    case AArch64::LDEORALH:
    case AArch64::LDEORALW:
    case AArch64::LDEORALX:
    case AArch64::LDEORLB:
    case AArch64::LDEORLH:
    case AArch64::LDEORLX:
    case AArch64::LDEORLW:
      DestRegIdx = 1;
      BaseRegIdx = 2;
      break;
    case AArch64::LDSETB:
    case AArch64::LDSETH:
    case AArch64::LDSETX:
    case AArch64::LDSETW:
    case AArch64::LDSETAB:
    case AArch64::LDSETAH:
    case AArch64::LDSETAW:
    case AArch64::LDSETAX:
    case AArch64::LDSETALB:
    case AArch64::LDSETALH:
    case AArch64::LDSETALW:
    case AArch64::LDSETALX:
    case AArch64::LDSETLB:
    case AArch64::LDSETLH:
    case AArch64::LDSETLX:
    case AArch64::LDSETLW:
      DestRegIdx = 1;
      BaseRegIdx = 2;
      break;
    default:
      return std::nullopt;
  }
  return MemInstInfo { DestRegIdx, BaseRegIdx, OffsetIdx, IsPrePost, IsPair };
}

static inline std::optional<MemInstInfo> getLoadInfo(const unsigned int Opcode) {
  int DestRegIdx;
  int BaseRegIdx;
  int OffsetIdx;
  bool IsPrePost;
  bool IsPair = false;
  switch (Opcode) {
  default:
    return std::nullopt;

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
    DestRegIdx = 0;
    BaseRegIdx = 2;
    OffsetIdx = 3;
    IsPrePost = false;
    IsPair = true;
    break;

  case AArch64::LDPSWi:
  case AArch64::LDPWi:
  case AArch64::LDPXi:
    DestRegIdx = 0;
    BaseRegIdx = 2;
    OffsetIdx = 3;
    IsPrePost = false;
    IsPair = true;
    break;

  case AArch64::LDPQpost:
  case AArch64::LDPQpre:
  case AArch64::LDPDpost:
  case AArch64::LDPDpre:
  case AArch64::LDPSpost:
  case AArch64::LDPSpre:
    DestRegIdx = 1;
    BaseRegIdx = 3;
    OffsetIdx = 4;
    IsPrePost = true;
    IsPair = true;
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
    IsPair = true;
    break;

  // LD(A)?(X)?R(B|H|W|X)
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
    DestRegIdx = 0;
    BaseRegIdx = 1;
    OffsetIdx = -1;
    IsPrePost = false;
    break;

  // LD(A|X)P(W|X)
  case AArch64::LDXPW:
  case AArch64::LDXPX:
  case AArch64::LDAXPW:
  case AArch64::LDAXPX:
    DestRegIdx = 0;
    BaseRegIdx = 2;
    OffsetIdx = -1;
    IsPrePost = false;
    IsPair = true;
    break;
  }

  return MemInstInfo { DestRegIdx, BaseRegIdx, OffsetIdx, IsPrePost, IsPair };
}

static inline std::optional<MemInstInfo> getStoreInfo(const unsigned int Opcode) {
  int DestRegIdx;
  int BaseRegIdx;
  int OffsetIdx;
  bool IsPrePost;
  bool IsPair = false;
  switch (Opcode) {
  default:
    return std::nullopt;

  case AArch64::ST1i8:
  case AArch64::ST1i16:
  case AArch64::ST1i32:
  case AArch64::ST1i64:
  case AArch64::ST2i8:
  case AArch64::ST2i16:
  case AArch64::ST2i32:
  case AArch64::ST2i64:
  case AArch64::ST3i8:
  case AArch64::ST3i16:
  case AArch64::ST3i32:
  case AArch64::ST3i64:
  case AArch64::ST4i8:
  case AArch64::ST4i16:
  case AArch64::ST4i32:
  case AArch64::ST4i64:
    DestRegIdx = -1;
    BaseRegIdx = 2;
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

  // SIMDStSingleS
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
    BaseRegIdx = 3;
    OffsetIdx = 4;
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
    DestRegIdx = 0;
    BaseRegIdx = 2;
    OffsetIdx = 3;
    IsPrePost = false;
    IsPair = true;
    break;

  case AArch64::STPWi:
  case AArch64::STPXi:
    DestRegIdx = 0;
    BaseRegIdx = 2;
    OffsetIdx = 3;
    IsPrePost = false;
    IsPair = true;
    break;

  case AArch64::STPQpost:
  case AArch64::STPQpre:
  case AArch64::STPDpost:
  case AArch64::STPDpre:
  case AArch64::STPSpost:
  case AArch64::STPSpre:
    DestRegIdx = 1;
    BaseRegIdx = 3;
    OffsetIdx = 4;
    IsPrePost = true;
    IsPair = true;
    break;

  case AArch64::STPWpost:
  case AArch64::STPWpre:
  case AArch64::STPXpost:
  case AArch64::STPXpre:
    DestRegIdx = 1;
    BaseRegIdx = 3;
    OffsetIdx = 4;
    IsPrePost = true;
    IsPair = true;
    break;

  // op Dst, [Base]
  case AArch64::STLRB:
  case AArch64::STLRH:
  case AArch64::STLRW:
  case AArch64::STLRX:
    DestRegIdx = 0;
    BaseRegIdx = 1;
    OffsetIdx = -1;
    IsPrePost = false;
    break;

  // Base = op Dst, [Base, (-4|-8)]!
  // case AArch64::STLRWpre:
  // case AArch64::STLRXpre:
  //   DestRegIdx = 1;
  //   BaseRegIdx = 2;
  //   OffsetIdx = -1; // constant offset
  //   IsPrePost = true;
  //   break;

  case AArch64::STXRB:
  case AArch64::STXRH:
  case AArch64::STXRW:
  case AArch64::STXRX:
  case AArch64::STLXRB:
  case AArch64::STLXRH:
  case AArch64::STLXRW:
  case AArch64::STLXRX:
    DestRegIdx = 0;
    BaseRegIdx = 2;
    OffsetIdx = -1;
    IsPrePost = false;
    break;

  case AArch64::STXPW:
  case AArch64::STXPX:
  case AArch64::STLXPW:
  case AArch64::STLXPX:
    DestRegIdx = 1; // return the status result at 0
    BaseRegIdx = 3;
    OffsetIdx = -1;
    IsPrePost = false;
    IsPair = true;
    break;
  }

  return MemInstInfo { DestRegIdx, BaseRegIdx, OffsetIdx, IsPrePost, IsPair };
}

static std::optional<MemInstInfo> getMemInstInfo(unsigned Op) {
  auto MII = getLoadInfo(Op);
  if (MII.has_value())
    return MII;
  MII = getStoreInfo(Op);
  if (MII.has_value())
    return MII;
  MII = getAtomicLdStInfo(Op);
  if (MII.has_value())
    return MII;
  return std::nullopt;
}


static inline unsigned convertPrePostToBase(unsigned Op, bool &IsPre, bool &IsBaseNoOffset) {
  IsPre = false;
  IsBaseNoOffset = false;
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
  // case AArch64::STLRWpre:
  //   IsPre = true;
  //   IsBaseNoOffset = true;
  //   return AArch64::STPWi;
  // case AArch64::STLRXpre:
  //   IsPre = true;
  //   IsBaseNoOffset = true;
    // return AArch64::STLRX;
  case AArch64::LD1i64_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1i64;
  case AArch64::LD2i64_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2i64;
  case AArch64::LD1i8_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1i8;
  case AArch64::LD1i16_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1i16;
  case AArch64::LD1i32_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1i32;
  case AArch64::LD2i8_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2i8;
  case AArch64::LD2i16_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2i16;
  case AArch64::LD2i32_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2i32;
  case AArch64::LD3i8_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3i8;
  case AArch64::LD3i16_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3i16;
  case AArch64::LD3i32_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3i32;
  case AArch64::LD3i64_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3i64;
  case AArch64::LD4i8_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4i8;
  case AArch64::LD4i16_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4i16;
  case AArch64::LD4i32_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4i32;
  case AArch64::LD4i64_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4i64;
  case AArch64::LD1Onev1d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Onev1d;
  case AArch64::LD1Onev2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Onev2s;
  case AArch64::LD1Onev4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Onev4h;
  case AArch64::LD1Onev8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Onev8b;
  case AArch64::LD1Onev2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Onev2d;
  case AArch64::LD1Onev4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Onev4s;
  case AArch64::LD1Onev8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Onev8h;
  case AArch64::LD1Onev16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Onev16b;
  case AArch64::LD1Rv1d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Rv1d;
  case AArch64::LD1Rv2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Rv2s;
  case AArch64::LD1Rv4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Rv4h;
  case AArch64::LD1Rv8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Rv8b;
  case AArch64::LD1Rv2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Rv2d;
  case AArch64::LD1Rv4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Rv4s;
  case AArch64::LD1Rv8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Rv8h;
  case AArch64::LD1Rv16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Rv16b;
  case AArch64::LD1Twov1d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Twov1d;
  case AArch64::LD1Twov2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Twov2s;
  case AArch64::LD1Twov4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Twov4h;
  case AArch64::LD1Twov8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Twov8b;
  case AArch64::LD1Twov2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Twov2d;
  case AArch64::LD1Twov4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Twov4s;
  case AArch64::LD1Twov8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Twov8h;
  case AArch64::LD1Twov16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Twov16b;
  case AArch64::LD1Threev1d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Threev1d;
  case AArch64::LD1Threev2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Threev2s;
  case AArch64::LD1Threev4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Threev4h;
  case AArch64::LD1Threev8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Threev8b;
  case AArch64::LD1Threev2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Threev2d;
  case AArch64::LD1Threev4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Threev4s;
  case AArch64::LD1Threev8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Threev8h;
  case AArch64::LD1Threev16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Threev16b;
  case AArch64::LD1Fourv1d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Fourv1d;
  case AArch64::LD1Fourv2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Fourv2s;
  case AArch64::LD1Fourv4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Fourv4h;
  case AArch64::LD1Fourv8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Fourv8b;
  case AArch64::LD1Fourv2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Fourv2d;
  case AArch64::LD1Fourv4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Fourv4s;
  case AArch64::LD1Fourv8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Fourv8h;
  case AArch64::LD1Fourv16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD1Fourv16b;
  case AArch64::LD2Twov2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Twov2s;
  case AArch64::LD2Twov4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Twov4s;
  case AArch64::LD2Twov8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Twov8b;
  case AArch64::LD2Twov2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Twov2d;
  case AArch64::LD2Twov4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Twov4h;
  case AArch64::LD2Twov8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Twov8h;
  case AArch64::LD2Twov16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Twov16b;
  case AArch64::LD2Rv1d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Rv1d;
  case AArch64::LD2Rv2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Rv2s;
  case AArch64::LD2Rv4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Rv4s;
  case AArch64::LD2Rv8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Rv8b;
  case AArch64::LD2Rv2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Rv2d;
  case AArch64::LD2Rv4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Rv4h;
  case AArch64::LD2Rv8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Rv8h;
  case AArch64::LD2Rv16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD2Rv16b;
  case AArch64::LD3Threev2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Threev2s;
  case AArch64::LD3Threev4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Threev4h;
  case AArch64::LD3Threev8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Threev8b;
  case AArch64::LD3Threev2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Threev2d;
  case AArch64::LD3Threev4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Threev4s;
  case AArch64::LD3Threev8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Threev8h;
  case AArch64::LD3Threev16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Threev16b;
  case AArch64::LD3Rv1d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Rv1d;
  case AArch64::LD3Rv2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Rv2s;
  case AArch64::LD3Rv4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Rv4h;
  case AArch64::LD3Rv8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Rv8b;
  case AArch64::LD3Rv2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Rv2d;
  case AArch64::LD3Rv4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Rv4s;
  case AArch64::LD3Rv8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Rv8h;
  case AArch64::LD3Rv16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD3Rv16b;
  case AArch64::LD4Fourv2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Fourv2s;
  case AArch64::LD4Fourv4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Fourv4h;
  case AArch64::LD4Fourv8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Fourv8b;
  case AArch64::LD4Fourv2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Fourv2d;
  case AArch64::LD4Fourv4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Fourv4s;
  case AArch64::LD4Fourv8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Fourv8h;
  case AArch64::LD4Fourv16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Fourv16b;
  case AArch64::LD4Rv1d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Rv1d;
  case AArch64::LD4Rv2s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Rv2s;
  case AArch64::LD4Rv4h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Rv4h;
  case AArch64::LD4Rv8b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Rv8b;
  case AArch64::LD4Rv2d_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Rv2d;
  case AArch64::LD4Rv4s_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Rv4s;
  case AArch64::LD4Rv8h_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Rv8h;
  case AArch64::LD4Rv16b_POST:
    IsBaseNoOffset = true;
    return AArch64::LD4Rv16b;
  case AArch64::ST1i64_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1i64;
  case AArch64::ST2i64_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2i64;
  case AArch64::ST1i8_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1i8;
  case AArch64::ST1i16_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1i16;
  case AArch64::ST1i32_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1i32;
  case AArch64::ST2i8_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2i8;
  case AArch64::ST2i16_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2i16;
  case AArch64::ST2i32_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2i32;
  case AArch64::ST3i8_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3i8;
  case AArch64::ST3i16_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3i16;
  case AArch64::ST3i32_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3i32;
  case AArch64::ST3i64_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3i64;
  case AArch64::ST4i8_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4i8;
  case AArch64::ST4i16_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4i16;
  case AArch64::ST4i32_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4i32;
  case AArch64::ST4i64_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4i64;
  case AArch64::ST1Onev1d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Onev1d;
  case AArch64::ST1Onev2s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Onev2s;
  case AArch64::ST1Onev4h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Onev4h;
  case AArch64::ST1Onev8b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Onev8b;
  case AArch64::ST1Onev2d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Onev2d;
  case AArch64::ST1Onev4s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Onev4s;
  case AArch64::ST1Onev8h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Onev8h;
  case AArch64::ST1Onev16b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Onev16b;
  case AArch64::ST1Twov1d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Twov1d;
  case AArch64::ST1Twov2s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Twov2s;
  case AArch64::ST1Twov4h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Twov4h;
  case AArch64::ST1Twov8b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Twov8b;
  case AArch64::ST1Twov2d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Twov2d;
  case AArch64::ST1Twov4s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Twov4s;
  case AArch64::ST1Twov8h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Twov8h;
  case AArch64::ST1Twov16b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Twov16b;
  case AArch64::ST1Threev1d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Threev1d;
  case AArch64::ST1Threev2s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Threev2s;
  case AArch64::ST1Threev4h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Threev4h;
  case AArch64::ST1Threev8b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Threev8b;
  case AArch64::ST1Threev2d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Threev2d;
  case AArch64::ST1Threev4s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Threev4s;
  case AArch64::ST1Threev8h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Threev8h;
  case AArch64::ST1Threev16b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Threev16b;
  case AArch64::ST1Fourv1d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Fourv1d;
  case AArch64::ST1Fourv2s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Fourv2s;
  case AArch64::ST1Fourv4h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Fourv4h;
  case AArch64::ST1Fourv8b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Fourv8b;
  case AArch64::ST1Fourv2d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Fourv2d;
  case AArch64::ST1Fourv4s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Fourv4s;
  case AArch64::ST1Fourv8h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Fourv8h;
  case AArch64::ST1Fourv16b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST1Fourv16b;
  case AArch64::ST2Twov2s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2Twov2s;
  case AArch64::ST2Twov4s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2Twov4s;
  case AArch64::ST2Twov8b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2Twov8b;
  case AArch64::ST2Twov2d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2Twov2d;
  case AArch64::ST2Twov4h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2Twov4h;
  case AArch64::ST2Twov8h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2Twov8h;
  case AArch64::ST2Twov16b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST2Twov16b;
  case AArch64::ST3Threev2s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3Threev2s;
  case AArch64::ST3Threev4h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3Threev4h;
  case AArch64::ST3Threev8b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3Threev8b;
  case AArch64::ST3Threev2d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3Threev2d;
  case AArch64::ST3Threev4s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3Threev4s;
  case AArch64::ST3Threev8h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3Threev8h;
  case AArch64::ST3Threev16b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST3Threev16b;
  case AArch64::ST4Fourv2s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4Fourv2s;
  case AArch64::ST4Fourv4h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4Fourv4h;
  case AArch64::ST4Fourv8b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4Fourv8b;
  case AArch64::ST4Fourv2d_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4Fourv2d;
  case AArch64::ST4Fourv4s_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4Fourv4s;
  case AArch64::ST4Fourv8h_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4Fourv8h;
  case AArch64::ST4Fourv16b_POST:
    IsBaseNoOffset = true;
    return AArch64::ST4Fourv16b;
  }
  return AArch64::INSTRUCTION_LIST_END;
}

static inline unsigned convertPreToRoW(unsigned Op) {
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

static inline unsigned convertPostToRoW(unsigned Op) {
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

static inline unsigned getPrePostScale(unsigned Op) {
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

static inline unsigned convertRoXToRoW(unsigned Op, unsigned &Shift) {
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

static inline unsigned convertRoWToRoW(unsigned Op, unsigned &Shift) {
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

static inline unsigned convertUiToRoW(unsigned Op) {
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

// Copied from AArch64InstrInfo.cpp
static bool isPairedLdSt(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
    // TODO: STXPX
  case AArch64::LDPSi:
  case AArch64::LDPSWi:
  case AArch64::LDPDi:
  case AArch64::LDPQi:
  case AArch64::LDPWi:
  case AArch64::LDPXi:
  case AArch64::STPSi:
  case AArch64::STPDi:
  case AArch64::STPQi:
  case AArch64::STPWi:
  case AArch64::STPXi:
  case AArch64::STGPi:
    return true;
  }
}

// copied from AArch64InstPrinter.cpp
struct LdStNInstrDesc {
  unsigned Opcode;
  const char *Mnemonic;
  const char *Layout;
  int ListOperand;
  bool HasLane;
  int NaturalOffset;
};

static const LdStNInstrDesc LdStNInstInfo[] = {
  { AArch64::LD1i8,             "ld1",  ".b",     1, true,  0  },
  { AArch64::LD1i16,            "ld1",  ".h",     1, true,  0  },
  { AArch64::LD1i32,            "ld1",  ".s",     1, true,  0  },
  { AArch64::LD1i64,            "ld1",  ".d",     1, true,  0  },
  { AArch64::LD1i8_POST,        "ld1",  ".b",     2, true,  1  },
  { AArch64::LD1i16_POST,       "ld1",  ".h",     2, true,  2  },
  { AArch64::LD1i32_POST,       "ld1",  ".s",     2, true,  4  },
  { AArch64::LD1i64_POST,       "ld1",  ".d",     2, true,  8  },
  { AArch64::LD1Rv16b,          "ld1r", ".16b",   0, false, 0  },
  { AArch64::LD1Rv8h,           "ld1r", ".8h",    0, false, 0  },
  { AArch64::LD1Rv4s,           "ld1r", ".4s",    0, false, 0  },
  { AArch64::LD1Rv2d,           "ld1r", ".2d",    0, false, 0  },
  { AArch64::LD1Rv8b,           "ld1r", ".8b",    0, false, 0  },
  { AArch64::LD1Rv4h,           "ld1r", ".4h",    0, false, 0  },
  { AArch64::LD1Rv2s,           "ld1r", ".2s",    0, false, 0  },
  { AArch64::LD1Rv1d,           "ld1r", ".1d",    0, false, 0  },
  { AArch64::LD1Rv16b_POST,     "ld1r", ".16b",   1, false, 1  },
  { AArch64::LD1Rv8h_POST,      "ld1r", ".8h",    1, false, 2  },
  { AArch64::LD1Rv4s_POST,      "ld1r", ".4s",    1, false, 4  },
  { AArch64::LD1Rv2d_POST,      "ld1r", ".2d",    1, false, 8  },
  { AArch64::LD1Rv8b_POST,      "ld1r", ".8b",    1, false, 1  },
  { AArch64::LD1Rv4h_POST,      "ld1r", ".4h",    1, false, 2  },
  { AArch64::LD1Rv2s_POST,      "ld1r", ".2s",    1, false, 4  },
  { AArch64::LD1Rv1d_POST,      "ld1r", ".1d",    1, false, 8  },
  { AArch64::LD1Onev16b,        "ld1",  ".16b",   0, false, 0  },
  { AArch64::LD1Onev8h,         "ld1",  ".8h",    0, false, 0  },
  { AArch64::LD1Onev4s,         "ld1",  ".4s",    0, false, 0  },
  { AArch64::LD1Onev2d,         "ld1",  ".2d",    0, false, 0  },
  { AArch64::LD1Onev8b,         "ld1",  ".8b",    0, false, 0  },
  { AArch64::LD1Onev4h,         "ld1",  ".4h",    0, false, 0  },
  { AArch64::LD1Onev2s,         "ld1",  ".2s",    0, false, 0  },
  { AArch64::LD1Onev1d,         "ld1",  ".1d",    0, false, 0  },
  { AArch64::LD1Onev16b_POST,   "ld1",  ".16b",   1, false, 16 },
  { AArch64::LD1Onev8h_POST,    "ld1",  ".8h",    1, false, 16 },
  { AArch64::LD1Onev4s_POST,    "ld1",  ".4s",    1, false, 16 },
  { AArch64::LD1Onev2d_POST,    "ld1",  ".2d",    1, false, 16 },
  { AArch64::LD1Onev8b_POST,    "ld1",  ".8b",    1, false, 8  },
  { AArch64::LD1Onev4h_POST,    "ld1",  ".4h",    1, false, 8  },
  { AArch64::LD1Onev2s_POST,    "ld1",  ".2s",    1, false, 8  },
  { AArch64::LD1Onev1d_POST,    "ld1",  ".1d",    1, false, 8  },
  { AArch64::LD1Twov16b,        "ld1",  ".16b",   0, false, 0  },
  { AArch64::LD1Twov8h,         "ld1",  ".8h",    0, false, 0  },
  { AArch64::LD1Twov4s,         "ld1",  ".4s",    0, false, 0  },
  { AArch64::LD1Twov2d,         "ld1",  ".2d",    0, false, 0  },
  { AArch64::LD1Twov8b,         "ld1",  ".8b",    0, false, 0  },
  { AArch64::LD1Twov4h,         "ld1",  ".4h",    0, false, 0  },
  { AArch64::LD1Twov2s,         "ld1",  ".2s",    0, false, 0  },
  { AArch64::LD1Twov1d,         "ld1",  ".1d",    0, false, 0  },
  { AArch64::LD1Twov16b_POST,   "ld1",  ".16b",   1, false, 32 },
  { AArch64::LD1Twov8h_POST,    "ld1",  ".8h",    1, false, 32 },
  { AArch64::LD1Twov4s_POST,    "ld1",  ".4s",    1, false, 32 },
  { AArch64::LD1Twov2d_POST,    "ld1",  ".2d",    1, false, 32 },
  { AArch64::LD1Twov8b_POST,    "ld1",  ".8b",    1, false, 16 },
  { AArch64::LD1Twov4h_POST,    "ld1",  ".4h",    1, false, 16 },
  { AArch64::LD1Twov2s_POST,    "ld1",  ".2s",    1, false, 16 },
  { AArch64::LD1Twov1d_POST,    "ld1",  ".1d",    1, false, 16 },
  { AArch64::LD1Threev16b,      "ld1",  ".16b",   0, false, 0  },
  { AArch64::LD1Threev8h,       "ld1",  ".8h",    0, false, 0  },
  { AArch64::LD1Threev4s,       "ld1",  ".4s",    0, false, 0  },
  { AArch64::LD1Threev2d,       "ld1",  ".2d",    0, false, 0  },
  { AArch64::LD1Threev8b,       "ld1",  ".8b",    0, false, 0  },
  { AArch64::LD1Threev4h,       "ld1",  ".4h",    0, false, 0  },
  { AArch64::LD1Threev2s,       "ld1",  ".2s",    0, false, 0  },
  { AArch64::LD1Threev1d,       "ld1",  ".1d",    0, false, 0  },
  { AArch64::LD1Threev16b_POST, "ld1",  ".16b",   1, false, 48 },
  { AArch64::LD1Threev8h_POST,  "ld1",  ".8h",    1, false, 48 },
  { AArch64::LD1Threev4s_POST,  "ld1",  ".4s",    1, false, 48 },
  { AArch64::LD1Threev2d_POST,  "ld1",  ".2d",    1, false, 48 },
  { AArch64::LD1Threev8b_POST,  "ld1",  ".8b",    1, false, 24 },
  { AArch64::LD1Threev4h_POST,  "ld1",  ".4h",    1, false, 24 },
  { AArch64::LD1Threev2s_POST,  "ld1",  ".2s",    1, false, 24 },
  { AArch64::LD1Threev1d_POST,  "ld1",  ".1d",    1, false, 24 },
  { AArch64::LD1Fourv16b,       "ld1",  ".16b",   0, false, 0  },
  { AArch64::LD1Fourv8h,        "ld1",  ".8h",    0, false, 0  },
  { AArch64::LD1Fourv4s,        "ld1",  ".4s",    0, false, 0  },
  { AArch64::LD1Fourv2d,        "ld1",  ".2d",    0, false, 0  },
  { AArch64::LD1Fourv8b,        "ld1",  ".8b",    0, false, 0  },
  { AArch64::LD1Fourv4h,        "ld1",  ".4h",    0, false, 0  },
  { AArch64::LD1Fourv2s,        "ld1",  ".2s",    0, false, 0  },
  { AArch64::LD1Fourv1d,        "ld1",  ".1d",    0, false, 0  },
  { AArch64::LD1Fourv16b_POST,  "ld1",  ".16b",   1, false, 64 },
  { AArch64::LD1Fourv8h_POST,   "ld1",  ".8h",    1, false, 64 },
  { AArch64::LD1Fourv4s_POST,   "ld1",  ".4s",    1, false, 64 },
  { AArch64::LD1Fourv2d_POST,   "ld1",  ".2d",    1, false, 64 },
  { AArch64::LD1Fourv8b_POST,   "ld1",  ".8b",    1, false, 32 },
  { AArch64::LD1Fourv4h_POST,   "ld1",  ".4h",    1, false, 32 },
  { AArch64::LD1Fourv2s_POST,   "ld1",  ".2s",    1, false, 32 },
  { AArch64::LD1Fourv1d_POST,   "ld1",  ".1d",    1, false, 32 },
  { AArch64::LD2i8,             "ld2",  ".b",     1, true,  0  },
  { AArch64::LD2i16,            "ld2",  ".h",     1, true,  0  },
  { AArch64::LD2i32,            "ld2",  ".s",     1, true,  0  },
  { AArch64::LD2i64,            "ld2",  ".d",     1, true,  0  },
  { AArch64::LD2i8_POST,        "ld2",  ".b",     2, true,  2  },
  { AArch64::LD2i16_POST,       "ld2",  ".h",     2, true,  4  },
  { AArch64::LD2i32_POST,       "ld2",  ".s",     2, true,  8  },
  { AArch64::LD2i64_POST,       "ld2",  ".d",     2, true,  16  },
  { AArch64::LD2Rv16b,          "ld2r", ".16b",   0, false, 0  },
  { AArch64::LD2Rv8h,           "ld2r", ".8h",    0, false, 0  },
  { AArch64::LD2Rv4s,           "ld2r", ".4s",    0, false, 0  },
  { AArch64::LD2Rv2d,           "ld2r", ".2d",    0, false, 0  },
  { AArch64::LD2Rv8b,           "ld2r", ".8b",    0, false, 0  },
  { AArch64::LD2Rv4h,           "ld2r", ".4h",    0, false, 0  },
  { AArch64::LD2Rv2s,           "ld2r", ".2s",    0, false, 0  },
  { AArch64::LD2Rv1d,           "ld2r", ".1d",    0, false, 0  },
  { AArch64::LD2Rv16b_POST,     "ld2r", ".16b",   1, false, 2  },
  { AArch64::LD2Rv8h_POST,      "ld2r", ".8h",    1, false, 4  },
  { AArch64::LD2Rv4s_POST,      "ld2r", ".4s",    1, false, 8  },
  { AArch64::LD2Rv2d_POST,      "ld2r", ".2d",    1, false, 16 },
  { AArch64::LD2Rv8b_POST,      "ld2r", ".8b",    1, false, 2  },
  { AArch64::LD2Rv4h_POST,      "ld2r", ".4h",    1, false, 4  },
  { AArch64::LD2Rv2s_POST,      "ld2r", ".2s",    1, false, 8  },
  { AArch64::LD2Rv1d_POST,      "ld2r", ".1d",    1, false, 16 },
  { AArch64::LD2Twov16b,        "ld2",  ".16b",   0, false, 0  },
  { AArch64::LD2Twov8h,         "ld2",  ".8h",    0, false, 0  },
  { AArch64::LD2Twov4s,         "ld2",  ".4s",    0, false, 0  },
  { AArch64::LD2Twov2d,         "ld2",  ".2d",    0, false, 0  },
  { AArch64::LD2Twov8b,         "ld2",  ".8b",    0, false, 0  },
  { AArch64::LD2Twov4h,         "ld2",  ".4h",    0, false, 0  },
  { AArch64::LD2Twov2s,         "ld2",  ".2s",    0, false, 0  },
  { AArch64::LD2Twov16b_POST,   "ld2",  ".16b",   1, false, 32 },
  { AArch64::LD2Twov8h_POST,    "ld2",  ".8h",    1, false, 32 },
  { AArch64::LD2Twov4s_POST,    "ld2",  ".4s",    1, false, 32 },
  { AArch64::LD2Twov2d_POST,    "ld2",  ".2d",    1, false, 32 },
  { AArch64::LD2Twov8b_POST,    "ld2",  ".8b",    1, false, 16 },
  { AArch64::LD2Twov4h_POST,    "ld2",  ".4h",    1, false, 16 },
  { AArch64::LD2Twov2s_POST,    "ld2",  ".2s",    1, false, 16 },
  { AArch64::LD3i8,             "ld3",  ".b",     1, true,  0  },
  { AArch64::LD3i16,            "ld3",  ".h",     1, true,  0  },
  { AArch64::LD3i32,            "ld3",  ".s",     1, true,  0  },
  { AArch64::LD3i64,            "ld3",  ".d",     1, true,  0  },
  { AArch64::LD3i8_POST,        "ld3",  ".b",     2, true,  3  },
  { AArch64::LD3i16_POST,       "ld3",  ".h",     2, true,  6  },
  { AArch64::LD3i32_POST,       "ld3",  ".s",     2, true,  12 },
  { AArch64::LD3i64_POST,       "ld3",  ".d",     2, true,  24 },
  { AArch64::LD3Rv16b,          "ld3r", ".16b",   0, false, 0  },
  { AArch64::LD3Rv8h,           "ld3r", ".8h",    0, false, 0  },
  { AArch64::LD3Rv4s,           "ld3r", ".4s",    0, false, 0  },
  { AArch64::LD3Rv2d,           "ld3r", ".2d",    0, false, 0  },
  { AArch64::LD3Rv8b,           "ld3r", ".8b",    0, false, 0  },
  { AArch64::LD3Rv4h,           "ld3r", ".4h",    0, false, 0  },
  { AArch64::LD3Rv2s,           "ld3r", ".2s",    0, false, 0  },
  { AArch64::LD3Rv1d,           "ld3r", ".1d",    0, false, 0  },
  { AArch64::LD3Rv16b_POST,     "ld3r", ".16b",   1, false, 3  },
  { AArch64::LD3Rv8h_POST,      "ld3r", ".8h",    1, false, 6  },
  { AArch64::LD3Rv4s_POST,      "ld3r", ".4s",    1, false, 12 },
  { AArch64::LD3Rv2d_POST,      "ld3r", ".2d",    1, false, 24 },
  { AArch64::LD3Rv8b_POST,      "ld3r", ".8b",    1, false, 3  },
  { AArch64::LD3Rv4h_POST,      "ld3r", ".4h",    1, false, 6  },
  { AArch64::LD3Rv2s_POST,      "ld3r", ".2s",    1, false, 12 },
  { AArch64::LD3Rv1d_POST,      "ld3r", ".1d",    1, false, 24 },
  { AArch64::LD3Threev16b,      "ld3",  ".16b",   0, false, 0  },
  { AArch64::LD3Threev8h,       "ld3",  ".8h",    0, false, 0  },
  { AArch64::LD3Threev4s,       "ld3",  ".4s",    0, false, 0  },
  { AArch64::LD3Threev2d,       "ld3",  ".2d",    0, false, 0  },
  { AArch64::LD3Threev8b,       "ld3",  ".8b",    0, false, 0  },
  { AArch64::LD3Threev4h,       "ld3",  ".4h",    0, false, 0  },
  { AArch64::LD3Threev2s,       "ld3",  ".2s",    0, false, 0  },
  { AArch64::LD3Threev16b_POST, "ld3",  ".16b",   1, false, 48 },
  { AArch64::LD3Threev8h_POST,  "ld3",  ".8h",    1, false, 48 },
  { AArch64::LD3Threev4s_POST,  "ld3",  ".4s",    1, false, 48 },
  { AArch64::LD3Threev2d_POST,  "ld3",  ".2d",    1, false, 48 },
  { AArch64::LD3Threev8b_POST,  "ld3",  ".8b",    1, false, 24 },
  { AArch64::LD3Threev4h_POST,  "ld3",  ".4h",    1, false, 24 },
  { AArch64::LD3Threev2s_POST,  "ld3",  ".2s",    1, false, 24 },
  { AArch64::LD4i8,             "ld4",  ".b",     1, true,  0  },
  { AArch64::LD4i16,            "ld4",  ".h",     1, true,  0  },
  { AArch64::LD4i32,            "ld4",  ".s",     1, true,  0  },
  { AArch64::LD4i64,            "ld4",  ".d",     1, true,  0  },
  { AArch64::LD4i8_POST,        "ld4",  ".b",     2, true,  4  },
  { AArch64::LD4i16_POST,       "ld4",  ".h",     2, true,  8  },
  { AArch64::LD4i32_POST,       "ld4",  ".s",     2, true,  16 },
  { AArch64::LD4i64_POST,       "ld4",  ".d",     2, true,  32 },
  { AArch64::LD4Rv16b,          "ld4r", ".16b",   0, false, 0  },
  { AArch64::LD4Rv8h,           "ld4r", ".8h",    0, false, 0  },
  { AArch64::LD4Rv4s,           "ld4r", ".4s",    0, false, 0  },
  { AArch64::LD4Rv2d,           "ld4r", ".2d",    0, false, 0  },
  { AArch64::LD4Rv8b,           "ld4r", ".8b",    0, false, 0  },
  { AArch64::LD4Rv4h,           "ld4r", ".4h",    0, false, 0  },
  { AArch64::LD4Rv2s,           "ld4r", ".2s",    0, false, 0  },
  { AArch64::LD4Rv1d,           "ld4r", ".1d",    0, false, 0  },
  { AArch64::LD4Rv16b_POST,     "ld4r", ".16b",   1, false, 4  },
  { AArch64::LD4Rv8h_POST,      "ld4r", ".8h",    1, false, 8  },
  { AArch64::LD4Rv4s_POST,      "ld4r", ".4s",    1, false, 16 },
  { AArch64::LD4Rv2d_POST,      "ld4r", ".2d",    1, false, 32 },
  { AArch64::LD4Rv8b_POST,      "ld4r", ".8b",    1, false, 4  },
  { AArch64::LD4Rv4h_POST,      "ld4r", ".4h",    1, false, 8  },
  { AArch64::LD4Rv2s_POST,      "ld4r", ".2s",    1, false, 16 },
  { AArch64::LD4Rv1d_POST,      "ld4r", ".1d",    1, false, 32 },
  { AArch64::LD4Fourv16b,       "ld4",  ".16b",   0, false, 0  },
  { AArch64::LD4Fourv8h,        "ld4",  ".8h",    0, false, 0  },
  { AArch64::LD4Fourv4s,        "ld4",  ".4s",    0, false, 0  },
  { AArch64::LD4Fourv2d,        "ld4",  ".2d",    0, false, 0  },
  { AArch64::LD4Fourv8b,        "ld4",  ".8b",    0, false, 0  },
  { AArch64::LD4Fourv4h,        "ld4",  ".4h",    0, false, 0  },
  { AArch64::LD4Fourv2s,        "ld4",  ".2s",    0, false, 0  },
  { AArch64::LD4Fourv16b_POST,  "ld4",  ".16b",   1, false, 64 },
  { AArch64::LD4Fourv8h_POST,   "ld4",  ".8h",    1, false, 64 },
  { AArch64::LD4Fourv4s_POST,   "ld4",  ".4s",    1, false, 64 },
  { AArch64::LD4Fourv2d_POST,   "ld4",  ".2d",    1, false, 64 },
  { AArch64::LD4Fourv8b_POST,   "ld4",  ".8b",    1, false, 32 },
  { AArch64::LD4Fourv4h_POST,   "ld4",  ".4h",    1, false, 32 },
  { AArch64::LD4Fourv2s_POST,   "ld4",  ".2s",    1, false, 32 },
  { AArch64::ST1i8,             "st1",  ".b",     0, true,  0  },
  { AArch64::ST1i16,            "st1",  ".h",     0, true,  0  },
  { AArch64::ST1i32,            "st1",  ".s",     0, true,  0  },
  { AArch64::ST1i64,            "st1",  ".d",     0, true,  0  },
  { AArch64::ST1i8_POST,        "st1",  ".b",     1, true,  1  },
  { AArch64::ST1i16_POST,       "st1",  ".h",     1, true,  2  },
  { AArch64::ST1i32_POST,       "st1",  ".s",     1, true,  4  },
  { AArch64::ST1i64_POST,       "st1",  ".d",     1, true,  8  },
  { AArch64::ST1Onev16b,        "st1",  ".16b",   0, false, 0  },
  { AArch64::ST1Onev8h,         "st1",  ".8h",    0, false, 0  },
  { AArch64::ST1Onev4s,         "st1",  ".4s",    0, false, 0  },
  { AArch64::ST1Onev2d,         "st1",  ".2d",    0, false, 0  },
  { AArch64::ST1Onev8b,         "st1",  ".8b",    0, false, 0  },
  { AArch64::ST1Onev4h,         "st1",  ".4h",    0, false, 0  },
  { AArch64::ST1Onev2s,         "st1",  ".2s",    0, false, 0  },
  { AArch64::ST1Onev1d,         "st1",  ".1d",    0, false, 0  },
  { AArch64::ST1Onev16b_POST,   "st1",  ".16b",   1, false, 16 },
  { AArch64::ST1Onev8h_POST,    "st1",  ".8h",    1, false, 16 },
  { AArch64::ST1Onev4s_POST,    "st1",  ".4s",    1, false, 16 },
  { AArch64::ST1Onev2d_POST,    "st1",  ".2d",    1, false, 16 },
  { AArch64::ST1Onev8b_POST,    "st1",  ".8b",    1, false, 8  },
  { AArch64::ST1Onev4h_POST,    "st1",  ".4h",    1, false, 8  },
  { AArch64::ST1Onev2s_POST,    "st1",  ".2s",    1, false, 8  },
  { AArch64::ST1Onev1d_POST,    "st1",  ".1d",    1, false, 8  },
  { AArch64::ST1Twov16b,        "st1",  ".16b",   0, false, 0  },
  { AArch64::ST1Twov8h,         "st1",  ".8h",    0, false, 0  },
  { AArch64::ST1Twov4s,         "st1",  ".4s",    0, false, 0  },
  { AArch64::ST1Twov2d,         "st1",  ".2d",    0, false, 0  },
  { AArch64::ST1Twov8b,         "st1",  ".8b",    0, false, 0  },
  { AArch64::ST1Twov4h,         "st1",  ".4h",    0, false, 0  },
  { AArch64::ST1Twov2s,         "st1",  ".2s",    0, false, 0  },
  { AArch64::ST1Twov1d,         "st1",  ".1d",    0, false, 0  },
  { AArch64::ST1Twov16b_POST,   "st1",  ".16b",   1, false, 32 },
  { AArch64::ST1Twov8h_POST,    "st1",  ".8h",    1, false, 32 },
  { AArch64::ST1Twov4s_POST,    "st1",  ".4s",    1, false, 32 },
  { AArch64::ST1Twov2d_POST,    "st1",  ".2d",    1, false, 32 },
  { AArch64::ST1Twov8b_POST,    "st1",  ".8b",    1, false, 16 },
  { AArch64::ST1Twov4h_POST,    "st1",  ".4h",    1, false, 16 },
  { AArch64::ST1Twov2s_POST,    "st1",  ".2s",    1, false, 16 },
  { AArch64::ST1Twov1d_POST,    "st1",  ".1d",    1, false, 16 },
  { AArch64::ST1Threev16b,      "st1",  ".16b",   0, false, 0  },
  { AArch64::ST1Threev8h,       "st1",  ".8h",    0, false, 0  },
  { AArch64::ST1Threev4s,       "st1",  ".4s",    0, false, 0  },
  { AArch64::ST1Threev2d,       "st1",  ".2d",    0, false, 0  },
  { AArch64::ST1Threev8b,       "st1",  ".8b",    0, false, 0  },
  { AArch64::ST1Threev4h,       "st1",  ".4h",    0, false, 0  },
  { AArch64::ST1Threev2s,       "st1",  ".2s",    0, false, 0  },
  { AArch64::ST1Threev1d,       "st1",  ".1d",    0, false, 0  },
  { AArch64::ST1Threev16b_POST, "st1",  ".16b",   1, false, 48 },
  { AArch64::ST1Threev8h_POST,  "st1",  ".8h",    1, false, 48 },
  { AArch64::ST1Threev4s_POST,  "st1",  ".4s",    1, false, 48 },
  { AArch64::ST1Threev2d_POST,  "st1",  ".2d",    1, false, 48 },
  { AArch64::ST1Threev8b_POST,  "st1",  ".8b",    1, false, 24 },
  { AArch64::ST1Threev4h_POST,  "st1",  ".4h",    1, false, 24 },
  { AArch64::ST1Threev2s_POST,  "st1",  ".2s",    1, false, 24 },
  { AArch64::ST1Threev1d_POST,  "st1",  ".1d",    1, false, 24 },
  { AArch64::ST1Fourv16b,       "st1",  ".16b",   0, false, 0  },
  { AArch64::ST1Fourv8h,        "st1",  ".8h",    0, false, 0  },
  { AArch64::ST1Fourv4s,        "st1",  ".4s",    0, false, 0  },
  { AArch64::ST1Fourv2d,        "st1",  ".2d",    0, false, 0  },
  { AArch64::ST1Fourv8b,        "st1",  ".8b",    0, false, 0  },
  { AArch64::ST1Fourv4h,        "st1",  ".4h",    0, false, 0  },
  { AArch64::ST1Fourv2s,        "st1",  ".2s",    0, false, 0  },
  { AArch64::ST1Fourv1d,        "st1",  ".1d",    0, false, 0  },
  { AArch64::ST1Fourv16b_POST,  "st1",  ".16b",   1, false, 64 },
  { AArch64::ST1Fourv8h_POST,   "st1",  ".8h",    1, false, 64 },
  { AArch64::ST1Fourv4s_POST,   "st1",  ".4s",    1, false, 64 },
  { AArch64::ST1Fourv2d_POST,   "st1",  ".2d",    1, false, 64 },
  { AArch64::ST1Fourv8b_POST,   "st1",  ".8b",    1, false, 32 },
  { AArch64::ST1Fourv4h_POST,   "st1",  ".4h",    1, false, 32 },
  { AArch64::ST1Fourv2s_POST,   "st1",  ".2s",    1, false, 32 },
  { AArch64::ST1Fourv1d_POST,   "st1",  ".1d",    1, false, 32 },
  { AArch64::ST2i8,             "st2",  ".b",     0, true,  0  },
  { AArch64::ST2i16,            "st2",  ".h",     0, true,  0  },
  { AArch64::ST2i32,            "st2",  ".s",     0, true,  0  },
  { AArch64::ST2i64,            "st2",  ".d",     0, true,  0  },
  { AArch64::ST2i8_POST,        "st2",  ".b",     1, true,  2  },
  { AArch64::ST2i16_POST,       "st2",  ".h",     1, true,  4  },
  { AArch64::ST2i32_POST,       "st2",  ".s",     1, true,  8  },
  { AArch64::ST2i64_POST,       "st2",  ".d",     1, true,  16 },
  { AArch64::ST2Twov16b,        "st2",  ".16b",   0, false, 0  },
  { AArch64::ST2Twov8h,         "st2",  ".8h",    0, false, 0  },
  { AArch64::ST2Twov4s,         "st2",  ".4s",    0, false, 0  },
  { AArch64::ST2Twov2d,         "st2",  ".2d",    0, false, 0  },
  { AArch64::ST2Twov8b,         "st2",  ".8b",    0, false, 0  },
  { AArch64::ST2Twov4h,         "st2",  ".4h",    0, false, 0  },
  { AArch64::ST2Twov2s,         "st2",  ".2s",    0, false, 0  },
  { AArch64::ST2Twov16b_POST,   "st2",  ".16b",   1, false, 32 },
  { AArch64::ST2Twov8h_POST,    "st2",  ".8h",    1, false, 32 },
  { AArch64::ST2Twov4s_POST,    "st2",  ".4s",    1, false, 32 },
  { AArch64::ST2Twov2d_POST,    "st2",  ".2d",    1, false, 32 },
  { AArch64::ST2Twov8b_POST,    "st2",  ".8b",    1, false, 16 },
  { AArch64::ST2Twov4h_POST,    "st2",  ".4h",    1, false, 16 },
  { AArch64::ST2Twov2s_POST,    "st2",  ".2s",    1, false, 16 },
  { AArch64::ST3i8,             "st3",  ".b",     0, true,  0  },
  { AArch64::ST3i16,            "st3",  ".h",     0, true,  0  },
  { AArch64::ST3i32,            "st3",  ".s",     0, true,  0  },
  { AArch64::ST3i64,            "st3",  ".d",     0, true,  0  },
  { AArch64::ST3i8_POST,        "st3",  ".b",     1, true,  3  },
  { AArch64::ST3i16_POST,       "st3",  ".h",     1, true,  6  },
  { AArch64::ST3i32_POST,       "st3",  ".s",     1, true,  12 },
  { AArch64::ST3i64_POST,       "st3",  ".d",     1, true,  24 },
  { AArch64::ST3Threev16b,      "st3",  ".16b",   0, false, 0  },
  { AArch64::ST3Threev8h,       "st3",  ".8h",    0, false, 0  },
  { AArch64::ST3Threev4s,       "st3",  ".4s",    0, false, 0  },
  { AArch64::ST3Threev2d,       "st3",  ".2d",    0, false, 0  },
  { AArch64::ST3Threev8b,       "st3",  ".8b",    0, false, 0  },
  { AArch64::ST3Threev4h,       "st3",  ".4h",    0, false, 0  },
  { AArch64::ST3Threev2s,       "st3",  ".2s",    0, false, 0  },
  { AArch64::ST3Threev16b_POST, "st3",  ".16b",   1, false, 48 },
  { AArch64::ST3Threev8h_POST,  "st3",  ".8h",    1, false, 48 },
  { AArch64::ST3Threev4s_POST,  "st3",  ".4s",    1, false, 48 },
  { AArch64::ST3Threev2d_POST,  "st3",  ".2d",    1, false, 48 },
  { AArch64::ST3Threev8b_POST,  "st3",  ".8b",    1, false, 24 },
  { AArch64::ST3Threev4h_POST,  "st3",  ".4h",    1, false, 24 },
  { AArch64::ST3Threev2s_POST,  "st3",  ".2s",    1, false, 24 },
  { AArch64::ST4i8,             "st4",  ".b",     0, true,  0  },
  { AArch64::ST4i16,            "st4",  ".h",     0, true,  0  },
  { AArch64::ST4i32,            "st4",  ".s",     0, true,  0  },
  { AArch64::ST4i64,            "st4",  ".d",     0, true,  0  },
  { AArch64::ST4i8_POST,        "st4",  ".b",     1, true,  4  },
  { AArch64::ST4i16_POST,       "st4",  ".h",     1, true,  8  },
  { AArch64::ST4i32_POST,       "st4",  ".s",     1, true,  16 },
  { AArch64::ST4i64_POST,       "st4",  ".d",     1, true,  32 },
  { AArch64::ST4Fourv16b,       "st4",  ".16b",   0, false, 0  },
  { AArch64::ST4Fourv8h,        "st4",  ".8h",    0, false, 0  },
  { AArch64::ST4Fourv4s,        "st4",  ".4s",    0, false, 0  },
  { AArch64::ST4Fourv2d,        "st4",  ".2d",    0, false, 0  },
  { AArch64::ST4Fourv8b,        "st4",  ".8b",    0, false, 0  },
  { AArch64::ST4Fourv4h,        "st4",  ".4h",    0, false, 0  },
  { AArch64::ST4Fourv2s,        "st4",  ".2s",    0, false, 0  },
  { AArch64::ST4Fourv16b_POST,  "st4",  ".16b",   1, false, 64 },
  { AArch64::ST4Fourv8h_POST,   "st4",  ".8h",    1, false, 64 },
  { AArch64::ST4Fourv4s_POST,   "st4",  ".4s",    1, false, 64 },
  { AArch64::ST4Fourv2d_POST,   "st4",  ".2d",    1, false, 64 },
  { AArch64::ST4Fourv8b_POST,   "st4",  ".8b",    1, false, 32 },
  { AArch64::ST4Fourv4h_POST,   "st4",  ".4h",    1, false, 32 },
  { AArch64::ST4Fourv2s_POST,   "st4",  ".2s",    1, false, 32 },
};

static const LdStNInstrDesc *getLdStNInstrDesc(unsigned Opcode) {
  for (const auto &Info : LdStNInstInfo)
    if (Info.Opcode == Opcode)
      return &Info;

  return nullptr;
}

static bool isMemNoMaskAddr(unsigned Op) {
  unsigned Shift;
  return convertUiToRoW(Op) == AArch64::INSTRUCTION_LIST_END &&
    convertPreToRoW(Op) == AArch64::INSTRUCTION_LIST_END &&
    convertPostToRoW(Op) == AArch64::INSTRUCTION_LIST_END &&
    convertRoXToRoW(Op, Shift) == AArch64::INSTRUCTION_LIST_END &&
    convertRoWToRoW(Op, Shift) == AArch64::INSTRUCTION_LIST_END;
}

static bool isAddrReg(MCRegister Reg) {
  return (Reg == AArch64::SP) || (Reg == AArch64::X18) || (Reg == AArch64::LR);
}

static bool isSafeIndBr(unsigned Opcode, MCRegister Reg) {
  return (Opcode == AArch64::BR && Reg == AArch64::X18)
    || (Opcode == AArch64::BLR && (Reg == AArch64::LR || Reg == AArch64::X18))
    || (Opcode == AArch64::RET && Reg == AArch64::LR);
}

} // end namespace llvm
#endif
