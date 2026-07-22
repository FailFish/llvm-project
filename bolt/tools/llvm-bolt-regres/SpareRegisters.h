//===- bolt/tools/llvm-bolt-regres/SpareRegisters.h -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pipeline Manager Pass orchestrating register reallocation strategies in cost
// hierarchy order with cross-pass cached web reuse:
// DirectRegRealloc -> ArgRegRealloc -> CalleeRegRealloc -> ArgCalleeRegRealloc.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_SPAREREGISTERS_H
#define BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_SPAREREGISTERS_H

#include "RegReallocPasses.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Passes/DataflowInfoManager.h"
#include "bolt/Passes/RegAnalysis.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <string>

namespace llvm {
namespace bolt {

enum class SpareStrategyMode {
  DirectSwap,        // DirectRegRealloc
  ArgEviction,       // ArgRegRealloc
  CalleeShift,       // CalleeRegRealloc
  ArgCalleeEviction, // ArgCalleeRegRealloc
  All                // Run all strategies in cost order
};

class SpareRegisters {
private:
  SmallVector<std::string, 4> TargetRegNames;
  SpareStrategyMode StrategyMode;

public:
  SpareRegisters(ArrayRef<std::string> TargetRegs = {"R11", "R14", "R15"},
                 SpareStrategyMode Mode = SpareStrategyMode::All)
      : TargetRegNames(TargetRegs.begin(), TargetRegs.end()), StrategyMode(Mode) {}

  void printLiveness(BinaryFunction &BF, DataflowInfoManager &Info);
  bool runOnFunction(BinaryFunction &Function, RegAnalysis &RA);
  Error runOnFunctions(BinaryContext &BC);
};

} // namespace bolt
} // namespace llvm

#endif // BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_SPAREREGISTERS_H
