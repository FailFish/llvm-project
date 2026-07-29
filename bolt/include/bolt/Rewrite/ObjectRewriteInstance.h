//===- bolt/Rewrite/ObjectRewriteInstance.h - Object Rewriter --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier mechanism: Apache2.0-SHA256
//
//===----------------------------------------------------------------------===//
//
// Declarations for ObjectRewriteInstance, providing first-class object file
// (.o) rewriting capabilities in LLVM/BOLT.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_REWRITE_OBJECTREWRITEINSTANCE_H
#define BOLT_REWRITE_OBJECTREWRITEINSTANCE_H

#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryFunction.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace bolt {

/// Symbol information preserved from input relocatable object files (.o).
struct ObjectSymbolInfo {
  std::string Name;
  MCSymbol *Symbol;
  uint64_t SectionOffset;
  uint64_t Size;
  uint32_t Flags;
  object::SymbolRef::Type Type;
  bool IsSectionSymbol;
  uint8_t Visibility;
};

/// First-class object file rewriter instance in LLVM/BOLT.
class ObjectRewriteInstance {
public:
  ObjectRewriteInstance(object::ObjectFile *ObjFile,
                        std::unique_ptr<BinaryContext> BC);
  ~ObjectRewriteInstance();

  /// Process section metadata and parse relocations into BinarySection.
  void processSectionMetadata();

  /// Read symbol table and populate symbol maps.
  Error readSymbolTable();

  /// Disassemble functions in text sections into BinaryFunctions.
  void disassembleFunctions();

  /// Build control flow graphs for disassembled functions.
  void buildFunctionsCFG();

  /// Print CFGs of disassembled functions.
  void printCFGs(raw_ostream &OS);

  /// Emit the rewritten object file to \p OutputFilename.
  void emitObjectFile(StringRef OutputFilename);

  /// Return reference to the BinaryContext.
  BinaryContext &getBinaryContext() { return *BC; }
  const BinaryContext &getBinaryContext() const { return *BC; }

  object::ObjectFile *getObjFile() { return ObjFile; }
  const std::map<std::string, std::vector<ObjectSymbolInfo>> &
  getSectionSymbolsMap() const {
    return SectionSymbolsMap;
  }

private:
  object::ObjectFile *ObjFile;
  std::unique_ptr<BinaryContext> BC;

  /// Map from section name to a list of symbols in that section.
  std::map<std::string, std::vector<ObjectSymbolInfo>> SectionSymbolsMap;

  struct SectionSym {
    std::string Name;
    uint64_t Addr;
    uint64_t Size;
  };
  std::map<std::string, std::vector<SectionSym>> SectionSymbols;
};

} // namespace bolt
} // namespace llvm

#endif // BOLT_REWRITE_OBJECTREWRITEINSTANCE_H
