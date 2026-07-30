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
#include "PseudoRewriter.h"
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

static BitVector discoverPreSavedRegisters(const BinaryFunction &BF) {
  const BinaryContext &BC = BF.getBinaryContext();
  BitVector PreSaved(BC.MRI->getNumRegs(), false);

  if (!BF.empty()) {
    const BinaryBasicBlock &EntryBB = *BF.begin();
    for (const MCInst &Inst : EntryBB) {
      if (BC.MIB->isPush(Inst)) {
        for (const MCOperand &Op : MCPlus::primeOperands(Inst)) {
          if (Op.isReg())
            PreSaved.set(Op.getReg());
        }
      }
    }
  }

  return PreSaved;
}

FunctionRegContext FunctionRegContext::create(
    const BinaryFunction &BF, ArrayRef<std::string> TargetRegNames) {
  const BinaryContext &BC = BF.getBinaryContext();
  FunctionRegContext Ctx;

  Ctx.GPRegs.resize(BC.MRI->getNumRegs(), false);
  BC.MIB->getGPRegs(Ctx.GPRegs);

  Ctx.CalleeSavedRegs.resize(BC.MRI->getNumRegs(), false);
  BC.MIB->getCalleeSavedRegs(Ctx.CalleeSavedRegs);
  Ctx.PreSavedRegs = discoverPreSavedRegisters(BF);

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

  // UsedInFunc: Physical registers used as explicit or implicit operands
  Ctx.UsedInFunc.resize(BC.MRI->getNumRegs(), false);
  for (const BinaryBasicBlock &BB : BF) {
    for (const MCInst &Inst : BB) {
      for (const MCOperand &Op : MCPlus::primeOperands(Inst)) {
        if (Op.isReg())
          Ctx.UsedInFunc |= BC.MIB->getAliases(Op.getReg(), false);
      }
      const MCInstrDesc &Desc = BC.MII->get(Inst.getOpcode());
      for (MCPhysReg ImpUse : Desc.implicit_uses())
        Ctx.UsedInFunc |= BC.MIB->getAliases(ImpUse, false);
      for (MCPhysReg ImpDef : Desc.implicit_defs())
        Ctx.UsedInFunc |= BC.MIB->getAliases(ImpDef, false);
    }
  }
  BC.MIB->getDefaultLiveOut(Ctx.UsedInFunc);

  Ctx.ABIArgRegs = BC.MIB->getRegsUsedAsParams();

  // Rank candidate registers: Unused registers in function get top priority
  Ctx.RankedRegs.resize(BC.MRI->getNumRegs());
  std::iota(Ctx.RankedRegs.begin(), Ctx.RankedRegs.end(), 0);
  std::stable_sort(Ctx.RankedRegs.begin(), Ctx.RankedRegs.end(),
                   [&](size_t A, size_t B) {
                     BitVector AliasesA = BC.MIB->getAliases(A, false);
                     BitVector AliasesB = BC.MIB->getAliases(B, false);

                     bool UnusedA = !Ctx.UsedInFunc.anyCommon(AliasesA);
                     bool UnusedB = !Ctx.UsedInFunc.anyCommon(AliasesB);

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
    for (const BinaryBasicBlock *BB : W.Blocks) {
      if (Item.Web.Blocks.count(BB))
        return true; // Shared basic block -> Conflict! Candidate cannot be shared within the same basic block.
    }
  }

  return false; // Completely disjoint basic blocks -> Safe to share candidate!
}

MCPhysReg RegReallocEngine::findCandidate(
    const RegisterWeb &W, MCPhysReg TargetReg,
    const RegReallocOptions &Opts, const FunctionPlan &Plan) const {
  const BinaryContext &BC = BF.getBinaryContext();
  BitVector TargetAliases = BC.MIB->getAliases(TargetReg, false);

  bool TargetIsCalleeSaved = RegCtx.CalleeSavedRegs.test(TargetReg);
  bool IsTargetPreSaved = RegCtx.PreSavedRegs.test(TargetReg);

  if (W.LiveAtEntry && !Opts.EvictEntryArg && !IsTargetPreSaved)
    return 0;
  if (W.CrossesCallSite && !Opts.ShiftCalleeSaved)
    return 0;

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

    // A web is self-preserving if target or candidate register is already pushed by the prologue
    bool IsTargetPreSaved = RegCtx.PreSavedRegs.test(TargetReg);
    bool IsCandPreSaved = RegCtx.PreSavedRegs.anyCommon(CandAliases);
    bool SelfPreserving = IsTargetPreSaved || IsCandPreSaved;

    if (!Opts.ShiftCalleeSaved && !TargetIsCalleeSaved && CandIsCalleeSaved && !SelfPreserving)
      continue;

    if (Opts.ShiftCalleeSaved && (!CandIsCalleeSaved || SelfPreserving))
      continue;

    if (Opts.EvictEntryArg && IsABIArg)
      continue;

    bool IsUsedInFunc = RegCtx.UsedInFunc.anyCommon(CandAliases);
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

void RegReallocEngine::applyFunctionPlan(FunctionPlan &Plan) {
  const BinaryContext &BC = BF.getBinaryContext();
  BinaryBasicBlock &EntryBB = *BF.begin();

  // 1. Rename operands across all planned webs
  for (const ReallocPlanItem &Item : Plan.PlannedItems) {
    applyReallocation(Item.PassName, Item.Web, Item.TargetReg, Item.CandidateReg, Item.Opts);
  }

  // 2. Insert Deduplicated PseudoSpillSave & PseudoSpillRestore for Callee-Saved Shift Candidates
  BitVector ProcessedPushes(BC.MRI->getNumRegs(), false);
  for (const ReallocPlanItem &Item : Plan.PlannedItems) {
    if (!Item.Opts.ShiftCalleeSaved)
      continue;

    MCPhysReg CandidateReg = Item.CandidateReg;
    if (ProcessedPushes.test(CandidateReg))
      continue; // Pseudo spill already created once for this candidate!

    ProcessedPushes.set(CandidateReg);
    Plan.HasPseudoInstructions = true;

    // Emit PseudoSpillSave at prologue entry
    createPseudoSpillSave(const_cast<BinaryContext &>(BC), EntryBB, CandidateReg, Item.TargetReg);

    // Emit PseudoSpillRestore on all return exit blocks
    for (BinaryBasicBlock &BB : BF) {
      if (BB.empty())
        continue;

      const MCInst &LastInst = *BB.rbegin();
      if (!BC.MIB->isReturn(LastInst) && !BC.MIB->isTailCall(LastInst))
        continue;

      auto ExitIt = std::prev(BB.end());
      createPseudoSpillRestore(const_cast<BinaryContext &>(BC), BB, ExitIt, CandidateReg, Item.TargetReg);
    }
  }

  // 3. Insert Deduplicated PseudoRegMove Entry Evictions
  DenseSet<std::pair<unsigned, unsigned>> ProcessedEvictions;
  for (const ReallocPlanItem &Item : Plan.PlannedItems) {
    if (!Item.Opts.EvictEntryArg)
      continue;

    auto Pair = std::make_pair(Item.TargetReg, Item.CandidateReg);
    if (ProcessedEvictions.count(Pair))
      continue; // Entry PseudoRegMove already created once for this target -> candidate pair!

    ProcessedEvictions.insert(Pair);
    Plan.HasPseudoInstructions = true;

    // Insert PseudoRegMove after prologue pushes if any exist
    auto InsertPos = EntryBB.begin();
    while (InsertPos != EntryBB.end() && BC.MIB->isPush(*InsertPos))
      ++InsertPos;

    createPseudoRegMove(const_cast<BinaryContext &>(BC), EntryBB, InsertPos, Item.CandidateReg, Item.TargetReg);
  }

  // 4. Automatically lower pseudo instructions if any were created
  if (Plan.HasPseudoInstructions) {
    PseudoRewriter Rewriter(BF);
    Rewriter.runOnFunction();
  }
}

} // namespace bolt
} // namespace llvm
