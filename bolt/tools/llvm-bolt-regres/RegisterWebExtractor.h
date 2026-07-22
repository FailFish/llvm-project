//===- bolt/tools/llvm-bolt-regres/RegisterWebExtractor.h -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Def-use web extraction and liveness analysis for physical registers in BOLT.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGISTERWEBEXTRACTOR_H
#define BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGISTERWEBEXTRACTOR_H

#include "bolt/Core/BinaryBasicBlock.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Passes/DataflowInfoManager.h"
#include "bolt/Passes/LivenessAnalysis.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegister.h"
#include <set>
#include <vector>

namespace llvm {
namespace bolt {

/// Represents a def-use web for a specific physical register across basic blocks.
struct RegisterWeb {
  MCRegister Reg;
  std::set<const BinaryBasicBlock *> Blocks;
  std::vector<MCInst *> Instructions;
  bool CrossesCallSite = false;
  bool LiveAtEntry = false;
};

/// Extracts def-use webs and analyzes liveness for physical registers in a BinaryFunction.
class RegisterWebExtractor {
public:
  RegisterWebExtractor(BinaryFunction &BF, LivenessAnalysis &LA)
      : BF(BF), LA(LA), BC(BF.getBinaryContext()) {}

  RegisterWebExtractor(BinaryFunction &BF, DataflowInfoManager &Info)
      : BF(BF), LA(Info.getLivenessAnalysis()), BC(BF.getBinaryContext()) {}

  /// Builds def-use webs for target physical register Reg across basic blocks in BF.
  std::vector<RegisterWeb> extractWebs(MCRegister Reg);

  /// Checks if CandReg (or its aliases) is live at any ProgramPoint in W.
  bool isLiveDuringWeb(MCRegister CandReg, const RegisterWeb &W) const;

private:
  BinaryFunction &BF;
  LivenessAnalysis &LA;
  BinaryContext &BC;

  bool isRegActiveInBB(const BinaryBasicBlock &BB,
                      const BitVector &RegAliases) const;
  bool isRegLiveAcrossEdge(const BinaryBasicBlock &From,
                          const BinaryBasicBlock &To,
                          const BitVector &RegAliases) const;
};

} // namespace bolt
} // namespace llvm

#endif // BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_REGISTERWEBEXTRACTOR_H
