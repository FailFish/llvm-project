//===- bolt/tools/llvm-bolt-regres/RegReallocPasses.h ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pass driver base and derived pass definitions for register reallocation.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCPASSES_H
#define BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCPASSES_H

#include "RegReallocEngine.h"
#include "RegisterWebExtractor.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Passes/DataflowInfoManager.h"
#include "bolt/Passes/RegAnalysis.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <queue>
#include <string>
#include <tuple>
#include <vector>

namespace llvm {
namespace bolt {

using CachedWebsMap = DenseMap<MCPhysReg, std::vector<RegisterWeb>>;

/// LLVM-style Priority Queue Comparator for RegisterWeb pointers
struct CompWebPriority {
  bool operator()(const RegisterWeb *A, const RegisterWeb *B) const {
    // Compare by LLVM priority score first, with Reg ID as a stable tie-breaker
    return std::tuple(A->Priority, A->Reg) <
           std::tuple(B->Priority, B->Reg);
  }
};

using WebPriorityQueue =
    std::priority_queue<const RegisterWeb *, std::vector<const RegisterWeb *>,
                        CompWebPriority>;

/// Abstract base class for single-analysis register reallocation passes.
class RegReallocPassBase {
protected:
  SmallVector<std::string, 4> TargetRegNames;
  RegReallocOptions Opts;

public:
  RegReallocPassBase(ArrayRef<std::string> TargetRegs, RegReallocOptions Opts)
      : TargetRegNames(TargetRegs.begin(), TargetRegs.end()), Opts(Opts) {}

  virtual ~RegReallocPassBase() = default;

  virtual StringRef getName() const = 0;

  static void printLiveness(BinaryFunction &BF, DataflowInfoManager &Info, raw_ostream &OS);

  /// Executes reallocation planning and batch mutation using cached def-use webs.
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
