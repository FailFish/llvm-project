//===- bolt/tools/llvm-bolt-obj-cfg/RegReallocEngine.h ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unified Core Engine for Register Reallocation / Sparing Passes.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCENGINE_H
#define BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCENGINE_H

#include "RegisterWebExtractor.h"
#include "bolt/Core/BinaryBasicBlock.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Passes/RegAnalysis.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/MC/MCRegister.h"
#include <string>
#include <vector>

namespace llvm {
namespace bolt {

struct RegReallocOptions {
  bool EvictEntryArg = false;    // Insert 'mov CandReg, TargetReg' at entry
  bool ShiftCalleeSaved = false; // Insert prologue push / epilogue pop + CFI
};

class RegReallocEngine {
public:
  // Attempts to reallocate a single def-use web W of TargetReg using Options.
  // Returns true if reallocation succeeded.
  static bool reallocateWeb(BinaryFunction &BF, RegisterWeb &W,
                            MCPhysReg TargetReg, const RegReallocOptions &Opts,
                            RegisterWebExtractor &Extractor,
                            const BitVector &GPRegs,
                            const BitVector &CalleeSavedRegs,
                            const BitVector &CandidatePool,
                            const BitVector &UsedInFunction,
                            const std::vector<size_t> &RankedRegs,
                            const BitVector &ABIArgRegs);
};

} // namespace bolt
} // namespace llvm

#endif // BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCENGINE_H
