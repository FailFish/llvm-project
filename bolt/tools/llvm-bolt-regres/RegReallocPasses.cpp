//===- bolt/tools/llvm-bolt-regres/RegReallocPasses.cpp ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of RegReallocPassBase.
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
#include <algorithm>
#include <numeric>

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
      OS << BC.MRI->getName(Reg);
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
  BinaryContext &BC = Function.getBinaryContext();

  BitVector GPRegs(BC.MRI->getNumRegs(), false);
  BC.MIB->getGPRegs(GPRegs);

  BitVector CalleeSavedRegs(BC.MRI->getNumRegs(), false);
  BC.MIB->getCalleeSavedRegs(CalleeSavedRegs);

  BitVector SpareTargetRegs(BC.MRI->getNumRegs(), false);
  for (const std::string &TargetRegName : TargetRegNames) {
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(TargetRegName)) {
        SpareTargetRegs |= BC.MIB->getAliases(R, false);
        break;
      }
    }
  }

  BitVector CandidatePool(BC.MRI->getNumRegs(), false);
  BC.MIB->getGPRegs(CandidatePool);
  CandidatePool.flip();
  CandidatePool |= SpareTargetRegs;
  CandidatePool |= BC.MIB->getAliases(BC.MIB->getFramePointer(), false);
  CandidatePool.flip();

  SmallVector<size_t, 16> RankedRegs(BC.MRI->getNumRegs());
  std::iota(RankedRegs.begin(), RankedRegs.end(), 0);

  BitVector UsedInFunction(BC.MRI->getNumRegs(), false);
  for (BinaryBasicBlock &BB : Function) {
    for (MCInst &Inst : BB) {
      for (MCOperand &Op : MCPlus::primeOperands(Inst)) {
        if (Op.isReg())
          UsedInFunction |= BC.MIB->getAliases(Op.getReg(), false);
      }
      const MCInstrDesc &Desc = BC.MII->get(Inst.getOpcode());
      for (MCPhysReg ImpUse : Desc.implicit_uses())
        UsedInFunction |= BC.MIB->getAliases(ImpUse, false);
      for (MCPhysReg ImpDef : Desc.implicit_defs())
        UsedInFunction |= BC.MIB->getAliases(ImpDef, false);
    }
  }

  BitVector ABIArgRegs(BC.MRI->getNumRegs(), false);
  for (const char *ArgName : {"RDI", "RSI", "RDX", "RCX", "R8", "R9"}) {
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(ArgName)) {
        ABIArgRegs |= BC.MIB->getAliases(R, false);
        break;
      }
    }
  }

  SmallVector<MCPhysReg, 4> TargetSparedRegs;
  for (const std::string &Name : TargetRegNames) {
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(Name)) {
        TargetSparedRegs.push_back(R);
        break;
      }
    }
  }

  // 1. Single-Analysis Planning Phase: Build ReallocPlan for all eligible webs
  SmallVector<ReallocPlanItem, 4> Plan;

  for (MCPhysReg SparedReg : TargetSparedRegs) {
    BitVector SparedAliases = BC.MIB->getAliases(SparedReg, false);
    if (!UsedInFunction.anyCommon(SparedAliases))
      continue;

    auto It = CachedWebs.find(SparedReg);
    if (It == CachedWebs.end())
      continue;

    for (const RegisterWeb &W : It->second) {
      MCPhysReg CandReg = RegReallocEngine::findCandidate(
          Function, W, SparedReg, Opts, Extractor, GPRegs, CalleeSavedRegs,
          CandidatePool, UsedInFunction, RankedRegs, ABIArgRegs);

      if (CandReg != 0) {
        Plan.push_back({W, SparedReg, CandReg});

        LLVM_DEBUG({
          dbgs() << "BOLT-DEBUG: [" << getName() << "] Planned reallocation: "
                 << BC.MRI->getName(SparedReg) << " -> "
                 << BC.MRI->getName(CandReg)
                 << " (LiveAtEntry=" << W.LiveAtEntry
                 << ", CrossesCallSite=" << W.CrossesCallSite << ")\n";
        });
      }
    }
  }

  // If no webs match strategy criteria, return false (0 changes made)
  if (Plan.empty())
    return false;

  LLVM_DEBUG({
    dbgs() << "BOLT-DEBUG: [" << getName() << "] Executing " << Plan.size()
           << " planned reallocation(s) on " << Function.getPrintName() << "\n";
  });

  // 2. Batch Mutation Phase: Apply planned reallocations
  for (const ReallocPlanItem &Item : Plan) {
    RegReallocEngine::applyReallocation(Function, Item.Web, Item.TargetReg,
                                        Item.CandidateReg, Opts);
  }

  return true;
}

bool RegReallocPassBase::runOnFunction(BinaryFunction &Function, RegAnalysis &RA) {
  DataflowInfoManager Info(Function, &RA, nullptr);
  RegisterWebExtractor Extractor(Function, Info);

  LLVM_DEBUG({
    dbgs() << "BOLT-DEBUG: [Liveness Analysis] " << Function.getPrintName() << "\n";
    printLiveness(Function, Info, dbgs());
  });

  BitVector SpareTargetRegs(Function.getBinaryContext().MRI->getNumRegs(), false);
  for (const std::string &TargetRegName : TargetRegNames) {
    for (unsigned R = 1; R < Function.getBinaryContext().MRI->getNumRegs(); ++R) {
      if (StringRef(Function.getBinaryContext().MRI->getName(R)).equals_insensitive(TargetRegName)) {
        SpareTargetRegs |= Function.getBinaryContext().MIB->getAliases(R, false);
        break;
      }
    }
  }

  CachedWebsMap CachedWebs;
  for (unsigned R = 1; R < Function.getBinaryContext().MRI->getNumRegs(); ++R) {
    if (SpareTargetRegs.test(R) && Function.getBinaryContext().MIB->getRegSize(R) == 8) {
      CachedWebs[R] = Extractor.extractWebs(R);
    }
  }

  return runWithCachedWebs(Function, CachedWebs, Extractor);
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
