//===- bolt/tools/llvm-bolt-regres/ReservedRegLoweringPass.h --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Single-pass reserved physical register lowering pass using custom TLS base register
// offsets and integrated pseudo instruction rewriting.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_TOOLS_LLVM_BOLT_REGRES_RESERVEDREGLOWERINGPASS_H
#define BOLT_TOOLS_LLVM_BOLT_REGRES_RESERVEDREGLOWERINGPASS_H

#include "bolt/Core/BinaryFunction.h"
#include "bolt/Passes/LivenessAnalysis.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <string>

namespace llvm {
namespace bolt {

/// Discriminator for virtual register physical storage backing.
enum class VirtStorageKind {
  TLS_BaseReg,    // TLS via Base Register offset (e.g. offset(%r15))
  TLS_Segment,    // TLS via Segment register offset (e.g. %fs:offset)
  StackSlot       // Dedicated stack frame slot [rsp + offset]
};

/// Type-safe, immutable physical storage backing abstraction for virtual registers.
class VirtRegStorage {
private:
  VirtStorageKind Kind;
  int32_t Offset;
  MCPhysReg BaseGPR;
  std::string SegRegName;

  VirtRegStorage(VirtStorageKind Kind, int32_t Offset, MCPhysReg BaseGPR, StringRef Segment)
      : Kind(Kind), Offset(Offset), BaseGPR(BaseGPR), SegRegName(Segment.str()) {}

public:
  /// Factory: Custom TLS Base Register backing [Offset(%TLSBaseReg)]
  static VirtRegStorage createTLSBaseReg(int32_t Offset, MCPhysReg TLSBaseReg) {
    assert(TLSBaseReg != 0 && "TLS base register cannot be 0!");
    return VirtRegStorage(VirtStorageKind::TLS_BaseReg, Offset, TLSBaseReg, "");
  }

  /// Factory: Segment register backing (%fs:Offset)
  static VirtRegStorage createTLSSegment(int32_t Offset, StringRef Segment) {
    return VirtRegStorage(VirtStorageKind::TLS_Segment, Offset, 0, Segment);
  }

  /// Factory: Dedicated stack frame slot backing [rsp + Offset]
  static VirtRegStorage createStackSlot(int32_t Offset) {
    return VirtRegStorage(VirtStorageKind::StackSlot, Offset, 0, "");
  }

  // Kind Queries
  VirtStorageKind getKind() const { return Kind; }
  bool isTLSBaseReg() const { return Kind == VirtStorageKind::TLS_BaseReg; }
  bool isTLSSegment() const { return Kind == VirtStorageKind::TLS_Segment; }
  bool isStackSlot() const { return Kind == VirtStorageKind::StackSlot; }

  // Type-safe Read-only Accessors
  int32_t getOffset() const { return Offset; }

  MCPhysReg getBaseGPR() const {
    assert(isTLSBaseReg() && "Storage is not TLSBaseReg!");
    assert(BaseGPR != 0 && "BaseGPR cannot be 0!");
    return BaseGPR;
  }

  StringRef getSegmentName() const {
    assert(isTLSSegment() && "Storage is not TLSSegment!");
    return SegRegName;
  }
};

/// Configuration binding a target physical register name to its storage backing.
class ReservedRegConfig {
private:
  std::string TargetRegName;
  VirtRegStorage Storage;

public:
  ReservedRegConfig(StringRef TargetRegName, VirtRegStorage Storage)
      : TargetRegName(TargetRegName.str()), Storage(Storage) {}

  StringRef getTargetRegName() const { return TargetRegName; }
  const VirtRegStorage &getStorage() const { return Storage; }
};

/// Single-pass reserved register lowering transformer replacing target physical
/// registers with custom TLS base register offsets in a single linear sweep.
class ReservedRegLoweringPass {
private:
  BinaryFunction &Function;
  SmallVector<ReservedRegConfig, 4> TargetConfigs;
  VirtRegStorage TempScratchStorage;
  bool AllowStackSpill;
  LivenessAnalysis *LA;

  static MCInst createSpillInst(MCPhysReg Reg, const VirtRegStorage &Storage);
  static MCInst createRestoreInst(MCPhysReg Reg, const VirtRegStorage &Storage);

  void lowerTargetRegisterInsts();
  void expandPseudosAndCFI();

public:
  ReservedRegLoweringPass(
      BinaryFunction &Function,
      ArrayRef<ReservedRegConfig> TargetConfigs,
      VirtRegStorage TempScratchStorage,
      bool AllowStackSpill = false,
      LivenessAnalysis *LA = nullptr)
      : Function(Function),
        TargetConfigs(TargetConfigs.begin(), TargetConfigs.end()),
        TempScratchStorage(TempScratchStorage),
        AllowStackSpill(AllowStackSpill), LA(LA) {}

  /// Executes single-pass target register elimination, custom TLS lowering, and pseudo expansion.
  bool runOnFunction();
};

} // namespace bolt
} // namespace llvm

#endif // BOLT_TOOLS_LLVM_BOLT_REGRES_RESERVEDREGLOWERINGPASS_H
