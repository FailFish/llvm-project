//===- bolt/tools/llvm-bolt-regres/SpareRegisters.cpp ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of SpareRegisters pipeline orchestrator using single-analysis planning.
//
//===----------------------------------------------------------------------===//

#include "SpareRegisters.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "spare-regs"

namespace llvm {
namespace bolt {

void SpareRegisters::printLiveness(BinaryFunction &BF, DataflowInfoManager &Info, raw_ostream &OS) {
  RegReallocPassBase::printLiveness(BF, Info, OS);
}

bool SpareRegisters::runOnFunction(BinaryFunction &Function, RegAnalysis &RA) {
  DataflowInfoManager Info(Function, &RA, nullptr);

  LLVM_DEBUG({
    dbgs() << "BOLT-DEBUG: [Liveness Analysis] " << Function.getPrintName() << "\n";
    RegReallocPassBase::printLiveness(Function, Info, dbgs());
  });

  RegisterWebExtractor Extractor(Function, Info);
  RegReallocEngine Engine(Function, Extractor, TargetRegNames);

  RegReallocOptions AllowedOpts;
  if (StrategyMode == SpareStrategyMode::ArgEviction || StrategyMode == SpareStrategyMode::All)
    AllowedOpts.EvictEntryArg = true;
  if (StrategyMode == SpareStrategyMode::CalleeShift || StrategyMode == SpareStrategyMode::All)
    AllowedOpts.ShiftCalleeSaved = true;
  if (StrategyMode == SpareStrategyMode::ArgCalleeEviction || StrategyMode == SpareStrategyMode::All) {
    AllowedOpts.EvictEntryArg = true;
    AllowedOpts.ShiftCalleeSaved = true;
  }

  FunctionPlan Plan;
  Engine.planFunction(Plan, TargetRegNames, AllowedOpts);

  if (Plan.PlannedItems.empty())
    return false;

  Engine.applyFunctionPlan(Plan);
  return true;
}

Error SpareRegisters::runOnFunctions(BinaryContext &BC) {
  outs() << "\n=========================================================\n";
  outs() << "BOLT-INFO: Running SpareRegisters Pipeline\n";
  outs() << "=========================================================\n";

  RegAnalysis RA(BC, &BC.getBinaryFunctions(), nullptr);

  for (auto &BFI : BC.getBinaryFunctions()) {
    BinaryFunction &Function = BFI.second;
    if (!Function.isSimple() || Function.isIgnored() || Function.empty())
      continue;

    runOnFunction(Function, RA);
  }

  outs() << "\n=========================================================\n";
  outs() << "BOLT-INFO: SpareRegisters Finished\n";
  outs() << "=========================================================\n";

  return Error::success();
}

} // namespace bolt
} // namespace llvm
