//===- bolt/tools/llvm-bolt-regres/RegReallocPasses.h --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Independently Instantiable Register Reallocation Passes supporting
// single-analysis batch planning and cross-pass cached web reuse.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCPASSES_H
#define BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCPASSES_H

#include "RegReallocEngine.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Passes/DataflowInfoManager.h"
#include "bolt/Passes/RegAnalysis.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace llvm {
namespace bolt {

struct ReallocPlanItem {
  RegisterWeb Web;
  MCPhysReg TargetReg;
  MCPhysReg CandidateReg;
};

using CachedWebsMap = DenseMap<MCPhysReg, std::vector<RegisterWeb>>;

// Base class for Register Reallocation Passes
class RegReallocPassBase {
protected:
  SmallVector<std::string, 4> TargetRegNames;
  RegReallocOptions Opts;

public:
  RegReallocPassBase(ArrayRef<std::string> TargetRegs, RegReallocOptions Options)
      : TargetRegNames(TargetRegs.begin(), TargetRegs.end()), Opts(Options) {}

  virtual ~RegReallocPassBase() = default;

  virtual StringRef getName() const = 0;

  void printLiveness(BinaryFunction &BF, DataflowInfoManager &Info);

  // Single-Analysis Batch Planning: Accepts pre-extracted cached webs map
  bool runWithCachedWebs(BinaryFunction &Function,
                         const CachedWebsMap &CachedWebs,
                         RegisterWebExtractor &Extractor);

  bool runOnFunction(BinaryFunction &Function, RegAnalysis &RA);
  Error runOnFunctions(BinaryContext &BC);
};

// 1. DirectRegRealloc: 0-cost local operand swap (!LiveAtEntry, !CrossesCallSite)
class DirectRegRealloc : public RegReallocPassBase {
public:
  DirectRegRealloc(ArrayRef<std::string> TargetRegs = {"R11", "R14", "R15"})
      : RegReallocPassBase(TargetRegs, RegReallocOptions{false, false}) {}

  StringRef getName() const override { return "DirectRegRealloc"; }
};

// 2. ArgRegRealloc: Volatile Entry Eviction (LiveAtEntry, !CrossesCallSite)
class ArgRegRealloc : public RegReallocPassBase {
public:
  ArgRegRealloc(ArrayRef<std::string> TargetRegs = {"R11", "R14", "R15"})
      : RegReallocPassBase(TargetRegs, RegReallocOptions{true, false}) {}

  StringRef getName() const override { return "ArgRegRealloc"; }
};

// 3. CalleeRegRealloc: Callee-Saved Register Shift (!LiveAtEntry, CrossesCallSite)
class CalleeRegRealloc : public RegReallocPassBase {
public:
  CalleeRegRealloc(ArrayRef<std::string> TargetRegs = {"R11", "R14", "R15"})
      : RegReallocPassBase(TargetRegs, RegReallocOptions{false, true}) {}

  StringRef getName() const override { return "CalleeRegRealloc"; }
};

// 4. ArgCalleeRegRealloc: Argument Callee-Saved Eviction (LiveAtEntry, CrossesCallSite)
class ArgCalleeRegRealloc : public RegReallocPassBase {
public:
  ArgCalleeRegRealloc(ArrayRef<std::string> TargetRegs = {"R11", "R14", "R15"})
      : RegReallocPassBase(TargetRegs, RegReallocOptions{true, true}) {}

  StringRef getName() const override { return "ArgCalleeRegRealloc"; }
};

} // namespace bolt
} // namespace llvm

#endif // BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCPASSES_H
