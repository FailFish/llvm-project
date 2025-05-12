#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCINSTINFO_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCINSTINFO_H

#include "AArch64MCTargetDesc.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;

namespace llvm {

static inline bool getLoadInfo(const MCInst &Inst, int &DestRegIdx, int &BaseRegIdx, int &OffsetIdx, bool &IsPrePost) {
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

static inline bool getStoreInfo(const MCInst &Inst, int &DestRegIdx, int &BaseRegIdx, int &OffsetIdx, bool &IsPrePost) {
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

static inline unsigned convertPrePostToBase(unsigned Op, bool &IsPre) {
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

} // end namespace llvm

#endif
