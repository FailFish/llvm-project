//===- bolt/tools/llvm-bolt-regres/SpareRegisters.cpp ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of Pipeline Manager Pass for Register Sparing.
//
//===----------------------------------------------------------------------===//

#include "SpareRegisters.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "RegReallocEngine.h"
#include "RegisterWebExtractor.h"
#include "ReservedRegLoweringPass.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Core/MCPlus.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "spare-regs"

namespace llvm {
namespace bolt {

SpareRegisters::SpareRegisters(ArrayRef<ReservedRegConfig> TargetConfigs,
                               VirtRegStorage TempScratchStorage,
                               SpareStrategyMode Mode)
    : TargetConfigs(TargetConfigs.begin(), TargetConfigs.end()),
      TempScratchStorage(TempScratchStorage), StrategyMode(Mode) {}

SmallVector<std::string, 4> SpareRegisters::getTargetRegNames() const {
  SmallVector<std::string, 4> Names;
  for (const ReservedRegConfig &Cfg : TargetConfigs)
    Names.push_back(Cfg.getTargetRegName().str());
  return Names;
}

SmallVector<ReservedRegConfig, 4>
SpareRegisters::getNotFullySparedConfigs(const BinaryFunction &BF,
                                         const FunctionPlan &Plan) const {
  const BinaryContext &BC = BF.getBinaryContext();
  SmallVector<ReservedRegConfig, 4> NotFullySpared;

  for (const ReservedRegConfig &Config : TargetConfigs) {
    MCPhysReg TargetReg = 0;
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(Config.getTargetRegName())) {
        TargetReg = R;
        break;
      }
    }

    if (TargetReg == 0)
      continue;

    BitVector TargetAliases = BC.MIB->getAliases(TargetReg, /*OnlySmaller=*/false);
    bool FullySparedByGPR = false;
    for (const ReallocPlanItem &Item : Plan.PlannedItems) {
      if (TargetAliases.test(Item.TargetReg)) {
        FullySparedByGPR = true;
        break;
      }
    }

    if (!FullySparedByGPR)
      NotFullySpared.push_back(Config);
  }

  return NotFullySpared;
}

void SpareRegisters::printLiveness(BinaryFunction &BF, DataflowInfoManager &Info, raw_ostream &OS) {
  RegReallocPassBase::printLiveness(BF, Info, OS);
}

bool SpareRegisters::runOnFunction(BinaryFunction &Function, RegAnalysis &RA) {
  DataflowInfoManager Info(Function, &RA, nullptr);

  LLVM_DEBUG({
    dbgs() << "BOLT-DEBUG: [Liveness Analysis] " << Function.getPrintName() << "\n";
    RegReallocPassBase::printLiveness(Function, Info, dbgs());
  });

  SmallVector<std::string, 4> TargetRegNames = getTargetRegNames();
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

  bool PhysicalChanged = false;
  if (!Plan.PlannedItems.empty()) {
    Engine.applyFunctionPlan(Plan);
    PhysicalChanged = true;
  }

  // Fallback Reserved Register Eliminator for target registers not fully spared by GPR reallocations
  SmallVector<ReservedRegConfig, 4> NotFullySparedConfigs = getNotFullySparedConfigs(Function, Plan);
  bool FallbackChanged = false;
  if (!NotFullySparedConfigs.empty()) {
    ReservedRegLoweringPass LoweringPass(Function, NotFullySparedConfigs, TempScratchStorage,
                                         /*AllowStackSpill=*/false, &Info.getLivenessAnalysis());
    FallbackChanged = LoweringPass.runOnFunction();
  }

  return PhysicalChanged || FallbackChanged;
}

Error SpareRegisters::runOnFunctions(BinaryContext &BC) {
  RegAnalysis RA(BC, &BC.getBinaryFunctions(), nullptr);

  for (auto &BFI : BC.getBinaryFunctions()) {
    BinaryFunction &BF = BFI.second;
    if (!BF.isSimple() || BF.isIgnored() || BF.empty())
      continue;
    runOnFunction(BF, RA);
  }

  return Error::success();
}

} // namespace bolt
} // namespace llvm
