//===- bolt/tools/llvm-bolt-regres/PseudoRewriter.h ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Dedicated lowering and expansion pass for pseudo instructions (PseudoRegMove,
// PseudoSpillSave, PseudoSpillRestore) emitted by RegReallocEngine.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_PSEUDOREWRITER_H
#define BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_PSEUDOREWRITER_H

#include "bolt/Core/BinaryFunction.h"

namespace llvm {
namespace bolt {

/// Helper functions to create pseudo instructions with metadata annotations
void createPseudoRegMove(BinaryContext &BC, BinaryBasicBlock &EntryBB,
                         BinaryBasicBlock::iterator InsertPos,
                         MCPhysReg CandidateReg, MCPhysReg TargetReg);

void createPseudoSpillSave(BinaryContext &BC, BinaryBasicBlock &EntryBB,
                          MCPhysReg CandidateReg, MCPhysReg TargetReg);

void createPseudoSpillRestore(BinaryContext &BC, BinaryBasicBlock &BB,
                             BinaryBasicBlock::iterator ExitIt,
                             MCPhysReg CandidateReg, MCPhysReg TargetReg);

/// Lowers and expands abstract pseudo instructions into physical machine instructions.
class PseudoRewriter {
private:
  BinaryFunction &Function;

public:
  explicit PseudoRewriter(BinaryFunction &Function) : Function(Function) {}

  /// Executes pseudo instruction lowering across all basic blocks in Function.
  bool runOnFunction();
};

} // namespace bolt
} // namespace llvm

#endif // BOLT_TOOLS_LLVM_BOLT_OBJ_CFG_PSEUDOREWRITER_H
