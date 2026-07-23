//===- bolt/tools/llvm-bolt-regres/RegReallocEngine.cpp -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of RegReallocEngine and FunctionRegContext with candidate reservations
// and strategy-based candidate set partitioning.
//
//===----------------------------------------------------------------------===//

#include "RegReallocEngine.h"
#include "bolt/Core/MCPlus.h"
#include "bolt/Core/MCPlusBuilder.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include <algorithm>
#include <numeric>
#include <queue>
#include <tuple>

#define DEBUG_TYPE "spare-regs"

namespace llvm {
namespace bolt {

FunctionRegContext FunctionRegContext::create(
    const BinaryFunction &BF, ArrayRef<std::string> TargetRegNames) {
  const BinaryContext &BC = BF.getBinaryContext();
  FunctionRegContext Ctx;

  Ctx.GPRegs.resize(BC.MRI->getNumRegs(), false);
  BC.MIB->getGPRegs(Ctx.GPRegs);

  Ctx.CalleeSavedRegs.resize(BC.MRI->getNumRegs(), false);
  BC.MIB->getCalleeSavedRegs(Ctx.CalleeSavedRegs);

  BitVector SpareTargetRegs(BC.MRI->getNumRegs(), false);
  for (const std::string &TargetRegName : TargetRegNames) {
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(TargetRegName)) {
        SpareTargetRegs |= BC.MIB->getAliases(R, false);
        break;
      }
    }
  }

  Ctx.CandidatePool.resize(BC.MRI->getNumRegs(), false);
  BC.MIB->getGPRegs(Ctx.CandidatePool);
  Ctx.CandidatePool.flip();
  Ctx.CandidatePool |= SpareTargetRegs;
  Ctx.CandidatePool |= BC.MIB->getAliases(BC.MIB->getFramePointer(), false);
  Ctx.CandidatePool.flip();

  Ctx.PlannedReservedRegs.resize(BC.MRI->getNumRegs(), false);

  // ExpUsedInFunc: Physical registers explicitly used as instruction operands
  Ctx.ExpUsedInFunc.resize(BC.MRI->getNumRegs(), false);
  for (const BinaryBasicBlock &BB : BF) {
    for (const MCInst &Inst : BB) {
      for (const MCOperand &Op : MCPlus::primeOperands(Inst)) {
        if (Op.isReg())
          Ctx.ExpUsedInFunc |= BC.MIB->getAliases(Op.getReg(), false);
      }
    }
  }

  Ctx.ABIArgRegs = BC.MIB->getRegsUsedAsParams();

  // Rank candidate registers: Unused registers in function get top priority
  Ctx.RankedRegs.resize(BC.MRI->getNumRegs());
  std::iota(Ctx.RankedRegs.begin(), Ctx.RankedRegs.end(), 0);
  std::stable_sort(Ctx.RankedRegs.begin(), Ctx.RankedRegs.end(),
                   [&](size_t A, size_t B) {
                     BitVector AliasesA = BC.MIB->getAliases(A, false);
                     BitVector AliasesB = BC.MIB->getAliases(B, false);

                     bool UnusedA = !Ctx.ExpUsedInFunc.anyCommon(AliasesA);
                     bool UnusedB = !Ctx.ExpUsedInFunc.anyCommon(AliasesB);

                     if (UnusedA != UnusedB)
                       return UnusedA > UnusedB;

                     return A < B;
                   });

  return Ctx;
}

void RegReallocEngine::reserveCandidate(MCPhysReg CandidateReg) {
  const BinaryContext &BC = BF.getBinaryContext();
  RegCtx.PlannedReservedRegs |= BC.MIB->getAliases(CandidateReg, false);
}

bool RegReallocEngine::isCandidateInterfering(
    MCPhysReg CandidateReg, const RegisterWeb &W,
    const FunctionPlan &Plan) const {
  const BinaryContext &BC = BF.getBinaryContext();
  BitVector CandAliases = BC.MIB->getAliases(CandidateReg, false);

  for (const ReallocPlanItem &Item : Plan.PlannedItems) {
    BitVector PlannedCandAliases = BC.MIB->getAliases(Item.CandidateReg, false);
    if (!CandAliases.anyCommon(PlannedCandAliases))
      continue; // Different physical candidate register, no conflict

    // Check basic block set intersection
    bool SharedBB = false;
    for (const BinaryBasicBlock *BB : W.Blocks) {
      if (Item.Web.Blocks.count(BB)) {
        SharedBB = true;
        break;
      }
    }

    if (!SharedBB)
      continue; // Completely disjoint basic blocks -> Safe to share candidate!

    // Shared basic block: check instruction-level overlap
    DenseSet<const MCInst *> ItemInsts(Item.Web.Instructions.begin(),
                                       Item.Web.Instructions.end());
    for (const MCInst *Inst : W.Instructions) {
      if (ItemInsts.count(Inst))
        return true; // Overlapping instruction -> Conflict!
    }
  }

  return false; // Safe to reuse!
}

MCPhysReg RegReallocEngine::findCandidate(
    const RegisterWeb &W, MCPhysReg TargetReg,
    const RegReallocOptions &Opts, const FunctionPlan &Plan) const {
  const BinaryContext &BC = BF.getBinaryContext();
  BitVector TargetAliases = BC.MIB->getAliases(TargetReg, false);

  if (W.LiveAtEntry && !Opts.EvictEntryArg)
    return 0;
  if (W.CrossesCallSite && !Opts.ShiftCalleeSaved)
    return 0;

  bool TargetIsCalleeSaved = RegCtx.CalleeSavedRegs.test(TargetReg);

  for (size_t RegIdx : RegCtx.RankedRegs) {
    if (!RegCtx.GPRegs[RegIdx] || BC.MIB->getRegSize(RegIdx) != 8)
      continue;

    BitVector CandAliases = BC.MIB->getAliases(RegIdx, false);

    if (TargetAliases.test(RegIdx))
      continue;

    if (!RegCtx.CandidatePool.anyCommon(CandAliases))
      continue;

    // Check interference against Plan state
    if (isCandidateInterfering(RegIdx, W, Plan))
      continue;

    bool CandIsCalleeSaved = RegCtx.CalleeSavedRegs.test(RegIdx);
    bool IsABIArg = RegCtx.ABIArgRegs.anyCommon(CandAliases);

    if (!Opts.ShiftCalleeSaved && !TargetIsCalleeSaved && CandIsCalleeSaved)
      continue;

    if (Opts.ShiftCalleeSaved && !CandIsCalleeSaved)
      continue;

    if (Opts.EvictEntryArg && IsABIArg)
      continue;

    bool IsUsedInFunc = RegCtx.ExpUsedInFunc.anyCommon(CandAliases);
    if (IsUsedInFunc && Extractor.isLiveDuringWeb(RegIdx, W))
      continue;

    return RegIdx;
  }

  return 0;
}

void RegReallocEngine::planFunction(FunctionPlan &Plan,
                                    ArrayRef<std::string> TargetRegNames,
                                    const RegReallocOptions &AllowedOpts) {
  const BinaryContext &BC = BF.getBinaryContext();
  Plan.PlannedProloguePushedRegs.resize(BC.MRI->getNumRegs(), false);

  SmallVector<MCPhysReg, 4> TargetSparedRegs;
  for (const std::string &Name : TargetRegNames) {
    for (unsigned R = 1; R < BC.MRI->getNumRegs(); ++R) {
      if (StringRef(BC.MRI->getName(R)).equals_insensitive(Name)) {
        TargetSparedRegs.push_back(R);
        break;
      }
    }
  }

  // 1. Extract webs ONCE per target register
  DenseMap<MCPhysReg, std::vector<RegisterWeb>> CachedTargetWebs;
  for (MCPhysReg TargetReg : TargetSparedRegs) {
    std::vector<RegisterWeb> Webs = Extractor.extractWebs(TargetReg);
    if (Webs.empty()) {
      outs() << "  -> [UNUSED] Target register "
             << StringRef(BC.MRI->getName(TargetReg)).upper()
             << " is unused in " << BF.getPrintName() << "\n";
      continue;
    }
    CachedTargetWebs[TargetReg] = std::move(Webs);
  }

  // 2. Enqueue ALL webs across target registers into a single Global Priority Queue
  struct CompWebPriority {
    bool operator()(const RegisterWeb *A, const RegisterWeb *B) const {
      return std::tuple(A->Priority, A->Reg) < std::tuple(B->Priority, B->Reg);
    }
  };
  std::priority_queue<const RegisterWeb *, std::vector<const RegisterWeb *>,
                      CompWebPriority>
      WorkList;

  for (const auto &Entry : CachedTargetWebs) {
    for (const RegisterWeb &W : Entry.second) {
      WorkList.push(&W);
    }
  }

  // Define strategy waterfall phases to evaluate for each web
  struct StrategyPhase {
    StringRef Name;
    RegReallocOptions Opts;
  };
  SmallVector<StrategyPhase, 4> Phases;
  Phases.push_back({"DirectRegRealloc", {false, false}});
  if (AllowedOpts.EvictEntryArg)
    Phases.push_back({"ArgRegRealloc", {true, false}});
  if (AllowedOpts.ShiftCalleeSaved)
    Phases.push_back({"CalleeRegRealloc", {false, true}});
  if (AllowedOpts.EvictEntryArg && AllowedOpts.ShiftCalleeSaved)
    Phases.push_back({"ArgCalleeRegRealloc", {true, true}});

  // 3. Process webs in global priority order
  while (!WorkList.empty()) {
    const RegisterWeb *W = WorkList.top();
    WorkList.pop();

    MCPhysReg AllocatedCand = 0;
    StrategyPhase ChosenPhase;

    for (const StrategyPhase &Phase : Phases) {
      MCPhysReg CandReg = findCandidate(*W, W->Reg, Phase.Opts, Plan);
      if (CandReg != 0) {
        AllocatedCand = CandReg;
        ChosenPhase = Phase;
        break;
      }
    }

    if (AllocatedCand != 0) {
      Plan.PlannedItems.push_back(
          {*W, static_cast<MCPhysReg>(W->Reg), AllocatedCand, ChosenPhase.Opts, ChosenPhase.Name.str()});
      reserveCandidate(AllocatedCand);
      if (ChosenPhase.Opts.ShiftCalleeSaved)
        Plan.PlannedProloguePushedRegs.set(AllocatedCand);
    } else {
      // Print [FAILED] status once per web that could not be allocated
      outs() << "  -> [FAILED] Web #" << W->WebID << " ("
             << StringRef(BC.MRI->getName(W->Reg)).upper()
             << ") failed in " << BF.getPrintName() << "\n";
    }
  }
}

void RegReallocEngine::applyReallocation(StringRef PassName,
                                         const RegisterWeb &W,
                                         MCPhysReg TargetReg,
                                         MCPhysReg CandidateReg,
                                         const RegReallocOptions &Opts) {
  const BinaryContext &BC = BF.getBinaryContext();
  StringRef SparedName = BC.MRI->getName(TargetReg);
  StringRef CandName = BC.MRI->getName(CandidateReg);
  BitVector TargetAliases = BC.MIB->getAliases(TargetReg, false);

  // 1. Rename operands in web
  for (MCInst *Inst : W.Instructions) {
    for (MCOperand &Op : MCPlus::primeOperands(*Inst)) {
      if (!Op.isReg())
        continue;
      MCPhysReg OpReg = Op.getReg();
      if (TargetAliases.test(OpReg)) {
        unsigned Size = BC.MIB->getRegSize(OpReg);
        MCPhysReg SizedCand = BC.MIB->getAliasSized(CandidateReg, Size);
        Op.setReg(SizedCand);
      }
    }
  }

  // 2. Perform Entry Eviction if requested
  BinaryBasicBlock &EntryBB = *BF.begin();
  if (Opts.EvictEntryArg && !Opts.ShiftCalleeSaved) {
    MCInst MovInst;
    MovInst.setOpcode(X86::MOV64rr);
    MovInst.addOperand(MCOperand::createReg(CandidateReg));
    MovInst.addOperand(MCOperand::createReg(TargetReg));
    EntryBB.insertInstruction(EntryBB.begin(), MovInst);
  }

  // 3. Perform Callee-Saved Shift (prologue push & epilogue pop + DWARF CFI) if requested
  if (Opts.ShiftCalleeSaved) {
    // Prologue Push
    MCInst PushInst;
    BC.MIB->createPushRegister(PushInst, CandidateReg, 8);
    auto PushIt = EntryBB.insertInstruction(EntryBB.begin(), PushInst);
    auto CFIIt = std::next(PushIt);
    CFIIt = BF.addCFIInstruction(
        &EntryBB, CFIIt, MCCFIInstruction::createAdjustCfaOffset(nullptr, 8));
    CFIIt = BF.addCFIInstruction(
        &EntryBB, CFIIt,
        MCCFIInstruction::createOffset(
            nullptr, BC.MRI->getDwarfRegNum(CandidateReg, false), -8));

    if (Opts.EvictEntryArg) {
      MCInst MovInst;
      MovInst.setOpcode(X86::MOV64rr);
      MovInst.addOperand(MCOperand::createReg(CandidateReg));
      MovInst.addOperand(MCOperand::createReg(TargetReg));
      EntryBB.insertInstruction(CFIIt, MovInst);
    }

    // Epilogue Pop on all return exits
    for (BinaryBasicBlock &BB : BF) {
      if (BB.succ_empty() || (!BB.empty() && (BC.MIB->isReturn(*BB.rbegin()) || BC.MIB->isTailCall(*BB.rbegin())))) {
        auto ExitIt = BB.end();
        if (!BB.empty() && (BC.MIB->isReturn(*BB.rbegin()) || BC.MIB->isTailCall(*BB.rbegin())))
          ExitIt = std::prev(BB.end());

        MCInst PopInst;
        BC.MIB->createPopRegister(PopInst, CandidateReg, 8);
        auto PopIt = BB.insertInstruction(ExitIt, PopInst);
        auto PopCFIIt = std::next(PopIt);
        PopCFIIt = BF.addCFIInstruction(
            &BB, PopCFIIt, MCCFIInstruction::createAdjustCfaOffset(nullptr, -8));
        BF.addCFIInstruction(
            &BB, PopCFIIt,
            MCCFIInstruction::createSameValue(
                nullptr, BC.MRI->getDwarfRegNum(CandidateReg, false)));
      }
    }
  }

  std::string ExtraOps = "";
  if (Opts.ShiftCalleeSaved && Opts.EvictEntryArg)
    ExtraOps = " [+push/pop, +mov]";
  else if (Opts.ShiftCalleeSaved)
    ExtraOps = " [+push/pop]";
  else if (Opts.EvictEntryArg)
    ExtraOps = " [+mov]";

  outs() << "  -> [SUCCESS] [" << PassName << "] Web #" << W.WebID << " (" << SparedName
         << ") -> " << CandName << ExtraOps << " in " << BF.getPrintName() << "\n";
}

void RegReallocEngine::applyFunctionPlan(const FunctionPlan &Plan) {
  for (const ReallocPlanItem &Item : Plan.PlannedItems) {
    applyReallocation(Item.PassName, Item.Web, Item.TargetReg, Item.CandidateReg, Item.Opts);
  }
}

} // namespace bolt
} // namespace llvm
