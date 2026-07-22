//===- bolt/tools/llvm-bolt-obj-cfg/RegReallocPasses.h --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Independently Instantiable Register Reallocation Passes:
//   1. DirectRegRealloc       (0-cost local operand renaming)
//   2. ArgRegRealloc          (Argument entry eviction: mov r_cand, r_arg)
//   3. CalleeRegRealloc       (Callee-saved shift with prologue/epilogue CFI)
//   4. ArgCalleeRegRealloc    (Argument callee-saved eviction)
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCPASSES_H
#define BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCPASSES_H

#include "RegReallocEngine.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Passes/DataflowInfoManager.h"
#include "bolt/Passes/RegAnalysis.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace llvm {
namespace bolt {

// Base class for Register Reallocation Passes
class RegReallocPassBase {
protected:
  std::vector<std::string> TargetRegNames;
  RegReallocOptions Opts;

public:
  RegReallocPassBase(std::vector<std::string> TargetRegs, RegReallocOptions Options)
      : TargetRegNames(std::move(TargetRegs)), Opts(Options) {}

  virtual ~RegReallocPassBase() = default;

  virtual StringRef getName() const = 0;

  void printLiveness(BinaryFunction &BF, DataflowInfoManager &Info);
  bool runOnFunction(BinaryFunction &Function, RegAnalysis &RA);
  Error runOnFunctions(BinaryContext &BC);
};

// 1. DirectRegRealloc: 0-cost local operand swap (!LiveAtEntry, !CrossesCallSite)
class DirectRegRealloc : public RegReallocPassBase {
public:
  DirectRegRealloc(std::vector<std::string> TargetRegs = {"R11", "R14", "R15"})
      : RegReallocPassBase(std::move(TargetRegs), RegReallocOptions{false, false}) {}

  StringRef getName() const override { return "DirectRegRealloc"; }
};

// 2. ArgRegRealloc: Volatile Entry Eviction (LiveAtEntry, !CrossesCallSite)
class ArgRegRealloc : public RegReallocPassBase {
public:
  ArgRegRealloc(std::vector<std::string> TargetRegs = {"R11", "R14", "R15"})
      : RegReallocPassBase(std::move(TargetRegs), RegReallocOptions{true, false}) {}

  StringRef getName() const override { return "ArgRegRealloc"; }
};

// 3. CalleeRegRealloc: Callee-Saved Register Shift (!LiveAtEntry, CrossesCallSite)
class CalleeRegRealloc : public RegReallocPassBase {
public:
  CalleeRegRealloc(std::vector<std::string> TargetRegs = {"R11", "R14", "R15"})
      : RegReallocPassBase(std::move(TargetRegs), RegReallocOptions{false, true}) {}

  StringRef getName() const override { return "CalleeRegRealloc"; }
};

// 4. ArgCalleeRegRealloc: Argument Callee-Saved Eviction (LiveAtEntry, CrossesCallSite)
class ArgCalleeRegRealloc : public RegReallocPassBase {
public:
  ArgCalleeRegRealloc(std::vector<std::string> TargetRegs = {"R11", "R14", "R15"})
      : RegReallocPassBase(std::move(TargetRegs), RegReallocOptions{true, true}) {}

  StringRef getName() const override { return "ArgCalleeRegRealloc"; }
};

} // namespace bolt
} // namespace llvm

#endif // BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGREALLOCPASSES_H
