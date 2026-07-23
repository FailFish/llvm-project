//===- bolt/tools/llvm-bolt-regres/RegReallocPasses.cpp ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of RegReallocPassBase using single-analysis planning.
//
//===----------------------------------------------------------------------===//

#include "RegReallocPasses.h"
#include "bolt/Core/BinaryBasicBlock.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Core/MCPlus.h"
#include "bolt/Core/MCPlusBuilder.h"
#include "bolt/Passes/DataflowAnalysis.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "spare-regs"

namespace llvm {
namespace bolt {

static void formatCleanRegSet(raw_ostream &OS, const BinaryContext &BC,
                              const BitVector &State) {
  BitVector GPRegs(BC.MRI->getNumRegs(), false);
  BC.MIB->getGPRegs(GPRegs);

  bool First = true;
  for (unsigned Reg : State.set_bits()) {
    if (GPRegs.test(Reg) && BC.MIB->getRegSize(Reg) == 8) {
      if (!First)
        OS << ", ";
      OS << StringRef(BC.MRI->getName(Reg)).upper();
      First = false;
    }
  }
  if (First)
    OS << "(none)";
}

void RegReallocPassBase::printLiveness(BinaryFunction &BF, DataflowInfoManager &Info, raw_ostream &OS) {
  const BinaryContext &BC = BF.getBinaryContext();
  LivenessAnalysis &LA = Info.getLivenessAnalysis();

  OS << "Function: " << BF.getPrintName() << "\n";
  for (BinaryBasicBlock &BB : BF) {
    OS << "  " << BB.getName() << ":\n";

    ProgramPoint FirstPP = ProgramPoint::getFirstPointAt(BB);
    ErrorOr<const BitVector &> LiveIn = LA.getStateAt(FirstPP);
    OS << "    LiveIn : ";
    if (LiveIn)
      formatCleanRegSet(OS, BC, *LiveIn);
    else
      OS << "(unknown)";
    OS << "\n";

    ProgramPoint LastPP = ProgramPoint::getLastPointAt(BB);
    ErrorOr<const BitVector &> LiveOut = LA.getStateAt(LastPP);
    OS << "    LiveOut: ";
    if (LiveOut)
      formatCleanRegSet(OS, BC, *LiveOut);
    else
      OS << "(unknown)";
    OS << "\n";
  }
}

bool RegReallocPassBase::runWithCachedWebs(
    BinaryFunction &Function,
    const CachedWebsMap &CachedWebs,
    RegisterWebExtractor &Extractor) {
  RegReallocEngine Engine(Function, Extractor, TargetRegNames);
  FunctionPlan Plan;
  Engine.planFunction(Plan, TargetRegNames, Opts);

  if (Plan.PlannedItems.empty())
    return false;

  Engine.applyFunctionPlan(Plan);
  return true;
}

bool RegReallocPassBase::runOnFunction(BinaryFunction &Function, RegAnalysis &RA) {
  DataflowInfoManager Info(Function, &RA, nullptr);

  LLVM_DEBUG({
    dbgs() << "BOLT-DEBUG: [Liveness Analysis] " << Function.getPrintName() << "\n";
    printLiveness(Function, Info, dbgs());
  });

  RegisterWebExtractor Extractor(Function, Info);
  RegReallocEngine Engine(Function, Extractor, TargetRegNames);

  FunctionPlan Plan;
  Engine.planFunction(Plan, TargetRegNames, Opts);

  if (Plan.PlannedItems.empty())
    return false;

  Engine.applyFunctionPlan(Plan);
  return true;
}

Error RegReallocPassBase::runOnFunctions(BinaryContext &BC) {
  outs() << "\n=========================================================\n";
  outs() << "BOLT-INFO: Running " << getName() << "\n";
  outs() << "=========================================================\n";

  RegAnalysis RA(BC, &BC.getBinaryFunctions(), nullptr);

  for (auto &BFI : BC.getBinaryFunctions()) {
    BinaryFunction &Function = BFI.second;
    if (!Function.isSimple() || Function.isIgnored() || Function.empty())
      continue;

    runOnFunction(Function, RA);
  }

  return Error::success();
}

} // namespace bolt
} // namespace llvm
