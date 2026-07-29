//===- bolt/Rewrite/ObjectEmitter.cpp - Object Emitter --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier mechanism: Apache2.0-SHA256
//
//===----------------------------------------------------------------------===//

#include "bolt/Rewrite/ObjectEmitter.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Core/JumpTable.h"
#include "bolt/Rewrite/ObjectRewriteInstance.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Object/ELFObjectFile.h"

using namespace llvm;
using namespace object;
using namespace bolt;

void ObjectEmitter::emitJumpTable(const JumpTable &JT) {
  for (MCSymbol *Entry : JT.Entries) {
    if (JT.Type == JumpTable::JTT_NORMAL) {
      Streamer.emitSymbolValue(Entry, JT.OutputEntrySize);
    } else { // JTT_PIC
      const MCSymbol *JTLabel = JT.Labels.begin()->second;
      const MCSymbolRefExpr *JTExpr =
          MCSymbolRefExpr::create(JTLabel, Streamer.getContext());
      const MCSymbolRefExpr *E =
          MCSymbolRefExpr::create(Entry, Streamer.getContext());
      const MCBinaryExpr *Value =
          MCBinaryExpr::createSub(E, JTExpr, Streamer.getContext());
      Streamer.emitValue(Value, JT.EntrySize);
    }
  }
}

void ObjectEmitter::emitFunction(BinaryFunction &BF) {
  if (!BF.isSimple() || !BF.hasCFG())
    return;

  StringRef SecName = BF.getOriginSectionName().value_or(".text");
  MCSection *Section = BC.Ctx->getELFSection(
      SecName, ELF::SHT_PROGBITS,
      ELF::SHF_ALLOC | ELF::SHF_EXECINSTR);
  Streamer.switchSection(Section);

  // Emit symbols for the function
  for (MCSymbol *Sym : BF.getSymbols()) {
    Streamer.emitLabel(Sym);
  }

  // Emit basic blocks and instructions
  for (BinaryBasicBlock *BB : BF.getLayout().blocks()) {
    Streamer.emitLabel(BB->getLabel());

    for (MCInst &Inst : *BB) {
      Streamer.emitInstruction(Inst, *BC.STI);
    }
  }

  // Emit jump tables for the function
  for (auto &JTI : BF.jumpTables()) {
    emitJumpTable(*JTI.second);
  }
}

void ObjectEmitter::emitFunctions() {
  for (BinaryFunction *BF : BC.getOutputBinaryFunctions()) {
    emitFunction(*BF);
  }
}

void ObjectEmitter::emitDataSections() {
  const auto &SectionSymbolsMap = RI.getSectionSymbolsMap();

  for (BinarySection &BSection : BC.sections()) {
    if (BSection.isText() || BSection.getName() == ".symtab" ||
        BSection.getName() == ".strtab" || BSection.getName() == ".shstrtab" ||
        BSection.getELFType() == ELF::SHT_RELA ||
        BSection.getELFType() == ELF::SHT_REL)
      continue;

    MCSection *ELFSection = BC.Ctx->getELFSection(
        BSection.getName(), BSection.getELFType(), BSection.getELFFlags());
    Streamer.switchSection(ELFSection);

    auto SymItMap = SectionSymbolsMap.find(BSection.getName().str());
    if (SymItMap != SectionSymbolsMap.end()) {
      for (const ObjectSymbolInfo &SymInfo : SymItMap->second) {
        if (SymInfo.IsSectionSymbol)
          continue;

        if (SymInfo.Flags & SymbolRef::SF_Global)
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Global);
        else if (SymInfo.Flags & SymbolRef::SF_Weak)
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Weak);
        else
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Local);

        if (SymInfo.Visibility == ELF::STV_HIDDEN)
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Hidden);
        else if (SymInfo.Visibility == ELF::STV_PROTECTED)
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Protected);

        if (SymInfo.Type == SymbolRef::ST_Data)
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_ELF_TypeObject);

        Streamer.emitLabel(SymInfo.Symbol);

        if (SymInfo.Size > 0)
          Streamer.emitELFSize(
              SymInfo.Symbol,
              MCConstantExpr::create(SymInfo.Size, *BC.Ctx));
      }
    }

    BSection.emitAsData(Streamer, BSection.getName());
    BSection.clearRelocations();
  }
}

void ObjectEmitter::emitTextSymbolAttributes() {
  ObjectFile *ObjFile = RI.getObjFile();
  const auto &SectionSymbolsMap = RI.getSectionSymbolsMap();

  for (const SectionRef &Section : ObjFile->sections()) {
    if (!Section.isText())
      continue;
    StringRef SecName = cantFail(Section.getName());
    auto SymItMap = SectionSymbolsMap.find(SecName.str());
    if (SymItMap != SectionSymbolsMap.end()) {
      for (const ObjectSymbolInfo &SymInfo : SymItMap->second) {
        if (SymInfo.IsSectionSymbol)
          continue;

        if (SymInfo.Flags & SymbolRef::SF_Global)
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Global);
        else if (SymInfo.Flags & SymbolRef::SF_Weak)
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Weak);
        else
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Local);

        if (SymInfo.Visibility == ELF::STV_HIDDEN)
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Hidden);
        else if (SymInfo.Visibility == ELF::STV_PROTECTED)
          Streamer.emitSymbolAttribute(SymInfo.Symbol, MCSA_Protected);
      }
    }
  }
}

void ObjectEmitter::emitObjectFile() {
  emitDataSections();
  emitTextSymbolAttributes();
  emitFunctions();
}
