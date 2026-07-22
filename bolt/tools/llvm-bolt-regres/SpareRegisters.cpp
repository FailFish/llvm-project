//===- bolt/tools/llvm-bolt-obj-cfg/SpareRegisters.cpp ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of SpareRegisters pipeline orchestrator.
//
//===----------------------------------------------------------------------===//

#include "SpareRegisters.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace bolt {

void SpareRegisters::printLiveness(BinaryFunction &BF,
                                   DataflowInfoManager &Info) {
  DirectRegRealloc Helper;
  Helper.printLiveness(BF, Info);
}

Error SpareRegisters::runOnFunctions(BinaryContext &BC) {
  outs() << "\n=========================================================\n";
  outs() << "BOLT-INFO: Running SpareRegisters Pipeline\n";
  outs() << "=========================================================\n";

  if (StrategyMode == SpareStrategyMode::DirectSwap ||
      StrategyMode == SpareStrategyMode::All) {
    DirectRegRealloc Pass(TargetRegNames);
    cantFail(Pass.runOnFunctions(BC));
  }

  if (StrategyMode == SpareStrategyMode::ArgEviction ||
      StrategyMode == SpareStrategyMode::All) {
    ArgRegRealloc Pass(TargetRegNames);
    cantFail(Pass.runOnFunctions(BC));
  }

  if (StrategyMode == SpareStrategyMode::CalleeShift ||
      StrategyMode == SpareStrategyMode::All) {
    CalleeRegRealloc Pass(TargetRegNames);
    cantFail(Pass.runOnFunctions(BC));
  }

  if (StrategyMode == SpareStrategyMode::ArgCalleeEviction ||
      StrategyMode == SpareStrategyMode::All) {
    ArgCalleeRegRealloc Pass(TargetRegNames);
    cantFail(Pass.runOnFunctions(BC));
  }

  return Error::success();
}

} // namespace bolt
} // namespace llvm
