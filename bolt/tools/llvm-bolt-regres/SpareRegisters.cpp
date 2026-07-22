//===- bolt/tools/llvm-bolt-regres/SpareRegisters.cpp ---------*- C++ -*-===//
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
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "spare-regs"

namespace llvm {
namespace bolt {

void SpareRegisters::printLiveness(BinaryFunction &BF, DataflowInfoManager &Info, raw_ostream &OS) {
  RegReallocPassBase::printLiveness(BF, Info, OS);
}

bool SpareRegisters::runOnFunction(BinaryFunction &Function, RegAnalysis &RA) {
  BinaryContext &BC = Function.getBinaryContext();

  BitVector SpareTargetRegs(BC.MRI->getNumRegs(), false);
  SmallVector<MCPhysReg, 4> TargetSparedRegs;
  for (const std::string &Name : TargetRegNames) {
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(Name)) {
        TargetSparedRegs.push_back(R);
        SpareTargetRegs |= BC.MIB->getAliases(R, false);
        break;
      }
    }
  }

  SmallVector<std::unique_ptr<RegReallocPassBase>, 4> EnabledPasses;

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

  LLVM_DEBUG({
    dbgs() << "BOLT-DEBUG: [Liveness Analysis] " << Function.getPrintName() << "\n";
    RegReallocPassBase::printLiveness(Function, *Info, dbgs());
  });

  std::unique_ptr<RegisterWebExtractor> Extractor =
      std::make_unique<RegisterWebExtractor>(Function, *Info);

  CachedWebsMap CachedWebs;
  for (MCPhysReg Reg : TargetSparedRegs)
    CachedWebs[Reg] = Extractor->extractWebs(Reg);

  bool AnyChanged = false;
  bool AnalysisDirty = false;

  for (auto &Pass : EnabledPasses) {
    // Refresh liveness analysis and webs ONLY if a previous pass modified code
    if (AnalysisDirty) {
      Info = std::make_unique<DataflowInfoManager>(Function, &RA, nullptr);

      LLVM_DEBUG({
        dbgs() << "BOLT-DEBUG: [Liveness Analysis REFRESH] " << Function.getPrintName() << "\n";
        RegReallocPassBase::printLiveness(Function, *Info, dbgs());
      });

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

  outs() << "\n=========================================================\n";
  outs() << "BOLT-INFO: SpareRegisters Finished\n";
  outs() << "=========================================================\n";

  return Error::success();
}

} // namespace bolt
} // namespace llvm
