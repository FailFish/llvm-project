//===- bolt/Rewrite/ObjectEmitter.h - Object Emitter -*- C++ -------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier mechanism: Apache2.0-SHA256
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_REWRITE_OBJECTEMITTER_H
#define BOLT_REWRITE_OBJECTEMITTER_H

#include "bolt/Core/BinaryContext.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {
namespace bolt {

class ObjectRewriteInstance;

/// Dedicated emitter for emitting relocatable object files (.o).
class ObjectEmitter {
  ObjectRewriteInstance &RI;
  BinaryContext &BC;
  MCStreamer &Streamer;

  void emitFunction(BinaryFunction &BF);
  void emitJumpTable(const JumpTable &JT);
  void emitDataSections();
  void emitTextSymbolAttributes();

public:
  ObjectEmitter(ObjectRewriteInstance &RI, BinaryContext &BC,
                MCStreamer &Streamer)
      : RI(RI), BC(BC), Streamer(Streamer) {}

  void emitFunctions();
  void emitObjectFile();
};

} // namespace bolt
} // namespace llvm

#endif
