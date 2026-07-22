//===- bolt/tools/llvm-bolt-obj-cfg/RegisterWebExtractor.cpp ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of RegisterWebExtractor for def-use web extraction and LLVM priority calculation.
//
//===----------------------------------------------------------------------===//

#include "RegisterWebExtractor.h"
#include "bolt/Core/BinaryLoop.h"
#include "bolt/Core/MCPlus.h"
#include "bolt/Core/MCPlusBuilder.h"
#include <cmath>
#include <map>
#include <queue>
#include <set>

namespace llvm {
namespace bolt {

bool RegisterWebExtractor::isRegActiveInBB(const BinaryBasicBlock &BB,
                                           const BitVector &RegAliases) const {
  // Check LiveIn
  ProgramPoint FirstPP =
      ProgramPoint::getFirstPointAt(const_cast<BinaryBasicBlock &>(BB));
  ErrorOr<const BitVector &> FirstState = LA.getStateAt(FirstPP);
  if (FirstState && FirstState->anyCommon(RegAliases))
    return true;

  // Check LiveOut
  ProgramPoint LastPP =
      ProgramPoint::getLastPointAt(const_cast<BinaryBasicBlock &>(BB));
  ErrorOr<const BitVector &> LastState = LA.getStateAt(LastPP);
  if (LastState && LastState->anyCommon(RegAliases))
    return true;

  // Check instructions in BB
  for (const MCInst &Inst : BB) {
    ErrorOr<const BitVector &> StateBefore = LA.getStateAt(Inst);
    if (StateBefore && StateBefore->anyCommon(RegAliases))
      return true;

    ErrorOr<const BitVector &> StateAfter = LA.getStateBefore(Inst);
    if (StateAfter && StateAfter->anyCommon(RegAliases))
      return true;

    for (const MCOperand &Op : MCPlus::primeOperands(Inst)) {
      if (Op.isReg() && RegAliases.test(Op.getReg()))
        return true;
    }

    const MCInstrDesc &Desc = BC.MII->get(Inst.getOpcode());
    for (MCPhysReg ImpUse : Desc.implicit_uses()) {
      if (RegAliases.test(ImpUse))
        return true;
    }
    for (MCPhysReg ImpDef : Desc.implicit_defs()) {
      if (RegAliases.test(ImpDef))
        return true;
    }
  }

  return false;
}

bool RegisterWebExtractor::isRegLiveAcrossEdge(
    const BinaryBasicBlock &From, const BinaryBasicBlock &To,
    const BitVector &RegAliases) const {
  ProgramPoint FromLast =
      ProgramPoint::getLastPointAt(const_cast<BinaryBasicBlock &>(From));
  ErrorOr<const BitVector &> FromState = LA.getStateAt(FromLast);
  if (!FromState || !FromState->anyCommon(RegAliases))
    return false;

  ProgramPoint ToFirst =
      ProgramPoint::getFirstPointAt(const_cast<BinaryBasicBlock &>(To));
  ErrorOr<const BitVector &> ToState = LA.getStateAt(ToFirst);
  if (!ToState || !ToState->anyCommon(RegAliases))
    return false;

  return true;
}

std::vector<RegisterWeb> RegisterWebExtractor::extractWebs(MCRegister Reg) {
  std::vector<RegisterWeb> Webs;
  if (BF.empty())
    return Webs;

  BitVector RegAliases = BC.MIB->getAliases(Reg, /*OnlySmaller=*/false);

  // 1. Identify active basic blocks for Reg
  std::vector<const BinaryBasicBlock *> ActiveBlocks;
  std::set<const BinaryBasicBlock *> ActiveSet;
  for (const BinaryBasicBlock &BB : BF) {
    if (isRegActiveInBB(BB, RegAliases)) {
      ActiveBlocks.push_back(&BB);
      ActiveSet.insert(&BB);
    }
  }

  if (ActiveBlocks.empty())
    return Webs;

  // 2. Build adjacency map among active blocks
  std::map<const BinaryBasicBlock *, std::set<const BinaryBasicBlock *>> Adj;
  for (const BinaryBasicBlock *BBA : ActiveBlocks) {
    for (const BinaryBasicBlock *BBB : BBA->successors()) {
      if (ActiveSet.count(BBB) && isRegLiveAcrossEdge(*BBA, *BBB, RegAliases)) {
        Adj[BBA].insert(BBB);
        Adj[BBB].insert(BBA);
      }
    }
  }

  // 3. Find connected components (webs)
  std::set<const BinaryBasicBlock *> Visited;
  for (const BinaryBasicBlock *StartBB : ActiveBlocks) {
    if (Visited.count(StartBB))
      continue;

    RegisterWeb W;
    W.Reg = Reg;

    std::queue<const BinaryBasicBlock *> Q;
    Q.push(StartBB);
    Visited.insert(StartBB);

    while (!Q.empty()) {
      const BinaryBasicBlock *Curr = Q.front();
      Q.pop();
      W.Blocks.insert(Curr);

      auto Iter = Adj.find(Curr);
      if (Iter != Adj.end()) {
        for (const BinaryBasicBlock *Neighbor : Iter->second) {
          if (!Visited.count(Neighbor)) {
            Visited.insert(Neighbor);
            Q.push(Neighbor);
          }
        }
      }
    }

    // 4. Compute LiveAtEntry, CrossesCallSite, and Instructions for web W
    for (const BinaryBasicBlock *BB : W.Blocks) {
      if (BB == &*BF.begin() || BB->pred_size() == 0) {
        ProgramPoint FirstPP =
            ProgramPoint::getFirstPointAt(const_cast<BinaryBasicBlock &>(*BB));
        if (LA.isAlive(FirstPP, Reg)) {
          W.LiveAtEntry = true;
          break;
        }
      }
    }

    double ExecutionCost = 0.0;
    BF.calculateLoopInfo();
    const BinaryLoopInfo &BLI = BF.getLoopInfo();

    for (const BinaryBasicBlock *BB : W.Blocks) {
      unsigned MentionCount = 0;
      for (MCInst &Inst : const_cast<BinaryBasicBlock &>(*BB)) {
        bool MentionsReg = false;
        for (const MCOperand &Op : MCPlus::primeOperands(Inst)) {
          if (Op.isReg() && RegAliases.test(Op.getReg())) {
            MentionsReg = true;
            break;
          }
        }
        if (!MentionsReg) {
          const MCInstrDesc &Desc = BC.MII->get(Inst.getOpcode());
          for (MCPhysReg ImpUse : Desc.implicit_uses()) {
            if (RegAliases.test(ImpUse)) {
              MentionsReg = true;
              break;
            }
          }
          if (!MentionsReg) {
            for (MCPhysReg ImpDef : Desc.implicit_defs()) {
              if (RegAliases.test(ImpDef)) {
                MentionsReg = true;
                break;
              }
            }
          }
        }
        if (MentionsReg) {
          W.Instructions.push_back(&Inst);
          MentionCount++;
        }

        if (BC.MIB->isCall(Inst)) {
          ErrorOr<const BitVector &> StateBefore = LA.getStateAt(Inst);
          ErrorOr<const BitVector &> StateAfter = LA.getStateBefore(Inst);
          if ((StateBefore && StateBefore->anyCommon(RegAliases)) ||
              (StateAfter && StateAfter->anyCommon(RegAliases))) {
            W.CrossesCallSite = true;
          }
        }
      }
      const BinaryLoop *L = BLI.getLoopFor(BB);
      unsigned Depth = L ? L->getLoopDepth() : 0;
      ExecutionCost += MentionCount * std::pow(10.0, Depth);
    }

    W.Priority = ExecutionCost / std::max<size_t>(1, W.Instructions.size());
    Webs.push_back(std::move(W));
  }

  return Webs;
}

bool RegisterWebExtractor::isLiveDuringWeb(MCRegister CandReg,
                                           const RegisterWeb &W) const {
  BitVector CandAliases = BC.MIB->getAliases(CandReg, /*OnlySmaller=*/false);

  for (const BinaryBasicBlock *BB : W.Blocks) {
    for (const MCInst &Inst : *BB) {
      ErrorOr<const BitVector &> StateBefore = LA.getStateAt(Inst);
      if (StateBefore && StateBefore->anyCommon(CandAliases))
        return true;

      ErrorOr<const BitVector &> StateAfter = LA.getStateBefore(Inst);
      if (StateAfter && StateAfter->anyCommon(CandAliases))
        return true;

      for (const MCOperand &Op : MCPlus::primeOperands(Inst)) {
        if (Op.isReg() && CandAliases.test(Op.getReg()))
          return true;
      }

      const MCInstrDesc &Desc = BC.MII->get(Inst.getOpcode());
      for (MCPhysReg ImpUse : Desc.implicit_uses()) {
        if (CandAliases.test(ImpUse))
          return true;
      }
      for (MCPhysReg ImpDef : Desc.implicit_defs()) {
        if (CandAliases.test(ImpDef))
          return true;
      }
    }
  }

  return false;
}

} // namespace bolt
} // namespace llvm
