//===- bolt/tools/llvm-bolt-regres/SpareRegisters.cpp ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of SpareRegisters pipeline orchestrator with cross-pass
// cached web reuse.
//
//===----------------------------------------------------------------------===//

#include "SpareRegisters.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace bolt {

void SpareRegisters::printLiveness(BinaryFunction &BF, DataflowInfoManager &Info) {
  DirectRegRealloc Helper;
  Helper.printLiveness(BF, Info);
}

bool SpareRegisters::runOnFunction(BinaryFunction &Function, RegAnalysis &RA) {
  BinaryContext &BC = Function.getBinaryContext();

  BitVector SpareTargetRegs(BC.MRI->getNumRegs(), false);
  std::vector<MCPhysReg> TargetSparedRegs;
  for (const std::string &Name : TargetRegNames) {
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(Name)) {
        TargetSparedRegs.push_back(R);
        SpareTargetRegs |= BC.MIB->getAliases(R, false);
        break;
      }
    }
  }

  std::vector<std::unique_ptr<RegReallocPassBase>> EnabledPasses;

  if (StrategyMode == SpareStrategyMode::DirectSwap || StrategyMode == SpareStrategyMode::All)
    EnabledPasses.push_back(std::make_unique<DirectRegRealloc>(TargetRegNames));

  if (StrategyMode == SpareStrategyMode::ArgEviction || StrategyMode == SpareStrategyMode::All)
    EnabledPasses.push_back(std::make_unique<ArgRegRealloc>(TargetRegNames));

  if (StrategyMode == SpareStrategyMode::CalleeShift || StrategyMode == SpareStrategyMode::All)
    EnabledPasses.push_back(std::make_unique<CalleeRegRealloc>(TargetRegNames));

  if (StrategyMode == SpareStrategyMode::ArgCalleeEviction || StrategyMode == SpareStrategyMode::All)
    EnabledPasses.push_back(std::make_unique<ArgCalleeRegRealloc>(TargetRegNames));

  // 1. Initial DataflowInfoManager and Web Extraction ONCE per function
  std::unique_ptr<DataflowInfoManager> Info =
      std::make_unique<DataflowInfoManager>(Function, &RA, nullptr);
  std::unique_ptr<RegisterWebExtractor> Extractor =
      std::make_unique<RegisterWebExtractor>(Function, *Info);

  std::map<MCPhysReg, std::vector<RegisterWeb>> CachedWebs;
  for (MCPhysReg Reg : TargetSparedRegs)
    CachedWebs[Reg] = Extractor->extractWebs(Reg);

  bool AnyChanged = false;
  bool AnalysisDirty = false;

  for (auto &Pass : EnabledPasses) {
    // Refresh liveness analysis and webs ONLY if a previous pass modified code
    if (AnalysisDirty) {
      Info = std::make_unique<DataflowInfoManager>(Function, &RA, nullptr);
      Extractor = std::make_unique<RegisterWebExtractor>(Function, *Info);
      for (MCPhysReg Reg : TargetSparedRegs)
        CachedWebs[Reg] = Extractor->extractWebs(Reg);
      AnalysisDirty = false;
    }

    if (Pass->runWithCachedWebs(Function, CachedWebs, *Extractor)) {
      AnyChanged = true;
      AnalysisDirty = true; // Mark dirty so subsequent passes refresh if needed
    }
  }

  return AnyChanged;
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

  return Error::success();
}

} // namespace bolt
} // namespace llvm
