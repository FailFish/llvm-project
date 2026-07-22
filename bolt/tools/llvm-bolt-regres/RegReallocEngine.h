//===- bolt/tools/llvm-bolt-regres/RegReallocEngine.h ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Per-function lightweight RegReallocEngine and FunctionRegContext.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCENGINE_H
#define BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCENGINE_H

#include "RegisterWebExtractor.h"
#include "bolt/Core/BinaryFunction.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCRegister.h"
#include <string>

namespace llvm {
namespace bolt {

struct RegReallocOptions {
  bool EvictEntryArg = false;
  bool ShiftCalleeSaved = false;
};

/// Encapsulates per-function register classification state.
struct FunctionRegContext {
  BitVector GPRegs;
  BitVector CalleeSavedRegs;
  BitVector CandidatePool;
  BitVector ExpUsedInFunc;
  BitVector ABIArgRegs;
  BitVector PlannedReservedRegs;
  SmallVector<size_t, 16> RankedRegs;

  static FunctionRegContext create(const BinaryFunction &BF,
                                   ArrayRef<std::string> TargetRegNames);
};

/// Per-function Register Reallocation Engine.
class RegReallocEngine {
private:
  BinaryFunction &BF;
  RegisterWebExtractor &Extractor;
  FunctionRegContext RegCtx;

public:
  RegReallocEngine(BinaryFunction &BF, RegisterWebExtractor &Extractor,
                   ArrayRef<std::string> TargetRegNames)
      : BF(BF), Extractor(Extractor),
        RegCtx(FunctionRegContext::create(BF, TargetRegNames)) {}

  const FunctionRegContext &getRegContext() const { return RegCtx; }

  /// Marks a candidate register as reserved for a planned web in this batch.
  void reserveCandidate(MCPhysReg CandidateReg);

  /// Planning Phase: Finds an available candidate register for W excluding reserved candidates.
  MCPhysReg findCandidate(const RegisterWeb &W, MCPhysReg TargetReg,
                          const RegReallocOptions &Opts) const;

  /// Mutation Phase: Applies register swapping, entry move, and prologue/epilogue CFI.
  void applyReallocation(StringRef PassName, const RegisterWeb &W, MCPhysReg TargetReg,
                         MCPhysReg CandidateReg, const RegReallocOptions &Opts);
};

} // namespace bolt
} // namespace llvm

#endif // BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCENGINE_H
