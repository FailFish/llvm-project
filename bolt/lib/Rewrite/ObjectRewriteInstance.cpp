//===- bolt/Rewrite/ObjectRewriteInstance.cpp - Object Rewriter -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier mechanism: Apache2.0-SHA256
//
//===----------------------------------------------------------------------===//

#include "bolt/Rewrite/ObjectRewriteInstance.h"
#include "bolt/Rewrite/ObjectEmitter.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryEmitter.h"
#include "bolt/Core/BinarySection.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "bolt/Rewrite/RewriteInstance.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"

using namespace llvm;
using namespace object;
using namespace bolt;

namespace opts {

extern cl::OptionCategory BoltCategory;

static cl::opt<std::string>
    FilterFunc("filter-func", cl::desc("Only rewrite specified function"),
               cl::cat(BoltCategory));

} // namespace opts




ObjectRewriteInstance::ObjectRewriteInstance(object::ObjectFile *ObjFile,
                                             std::unique_ptr<BinaryContext> BC)
    : ObjFile(ObjFile), BC(std::move(BC)) {
  this->BC->HasRelocations = true;
  this->BC->initializeTarget(std::unique_ptr<MCPlusBuilder>(
      createMCPlusBuilder(this->BC->TheTriple->getArch(), this->BC->MIA.get(),
                          this->BC->MII.get(), this->BC->MRI.get(),
                          this->BC->STI.get())));
}

ObjectRewriteInstance::~ObjectRewriteInstance() = default;

void ObjectRewriteInstance::processSectionMetadata() {
  for (const SectionRef &Section : ObjFile->sections()) {
    StringRef SectionName = cantFail(Section.getName());
    if (SectionName.starts_with(".rela") || SectionName.starts_with(".rel"))
      continue;
    BC->registerSection(Section);
  }

  for (const SectionRef &RelSec : ObjFile->sections()) {
    StringRef RelSecName = cantFail(RelSec.getName());
    if (!RelSecName.starts_with(".rela") && !RelSecName.starts_with(".rel"))
      continue;

    Expected<section_iterator> TargetSecOrErr = RelSec.getRelocatedSection();
    if (!TargetSecOrErr || *TargetSecOrErr == ObjFile->section_end())
      continue;

    SectionRef TargetSec = **TargetSecOrErr;
    StringRef TargetSecName = cantFail(TargetSec.getName());

    auto SecOrErr = BC->getUniqueSectionByName(TargetSecName);
    if (!SecOrErr)
      continue;
    BinarySection *BSection = &SecOrErr.get();

    for (const RelocationRef &Reloc : RelSec.relocations()) {
      uint64_t RelocOffset = Reloc.getOffset();
      uint64_t RelocType = Reloc.getType();

      symbol_iterator SymbolIt = Reloc.getSymbol();
      std::string SymbolName = "<unknown>";
      if (SymbolIt != ObjFile->symbol_end()) {
        Expected<StringRef> NameOrErr = SymbolIt->getName();
        if (NameOrErr) {
          StringRef SymName = NameOrErr.get();
          SymbolName = SymName.starts_with(BC->AsmInfo->getInternalSymbolPrefix())
                           ? "PG" + std::string(SymName)
                           : std::string(SymName);
        }
      }

      MCSymbol *Symbol = BC->Ctx->getOrCreateSymbol(SymbolName);

      int64_t Addend = 0;
      if (isa<ELFObjectFileBase>(ObjFile)) {
        ELFRelocationRef ELFRel(Reloc);
        auto AddendOrErr = ELFRel.getAddend();
        if (AddendOrErr)
          Addend = *AddendOrErr;
      }

      BSection->addRelocation(RelocOffset, Symbol, RelocType, Addend);
    }
  }
}

Error ObjectRewriteInstance::readSymbolTable() {

  for (const SymbolRef &Symbol : ObjFile->symbols()) {
    Expected<StringRef> NameOrErr = Symbol.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    StringRef SymName = *NameOrErr;

    Expected<uint64_t> AddressOrErr = Symbol.getAddress();
    if (!AddressOrErr) {
      consumeError(AddressOrErr.takeError());
      continue;
    }
    uint64_t SymbolOffset = *AddressOrErr;

    Expected<section_iterator> SecOrErr = Symbol.getSection();
    if (!SecOrErr || *SecOrErr == ObjFile->section_end()) {
      if (!SecOrErr)
        consumeError(SecOrErr.takeError());
      continue;
    }
    section_iterator Sec = *SecOrErr;
    StringRef SecName = cantFail(Sec->getName());
    BinarySection *BSection = &BC->getUniqueSectionByName(SecName).get();
    uint64_t SymbolAddress = BSection->getAddress() + SymbolOffset;

    uint64_t SymbolSize = ELFSymbolRef(Symbol).getSize();
    uint32_t SymbolAlignment = Symbol.getAlignment();
    uint32_t SymbolFlags = cantFail(Symbol.getFlags());

    std::string Name =
        SymName.starts_with(BC->AsmInfo->getInternalSymbolPrefix())
            ? "PG" + std::string(SymName)
            : std::string(SymName);
    std::string UniqueName = Name.empty() ? Sec->getName()->str() : Name;

    MCSymbol *MCSym = BC->Ctx->getOrCreateSymbol(UniqueName);

    if (SecOrErr && *SecOrErr != ObjFile->section_end()) {
      StringRef SecName = cantFail((*SecOrErr)->getName());
      Expected<SymbolRef::Type> TypeOrErr = Symbol.getType();
      SymbolRef::Type SymbolType =
          TypeOrErr ? *TypeOrErr : SymbolRef::ST_Unknown;
      bool IsSecSym = (ELFSymbolRef(Symbol).getELFType() == ELF::STT_SECTION);
      uint8_t Visibility = ELFSymbolRef(Symbol).getOther() & 0x3;
      SectionSymbolsMap[SecName.str()].push_back(
          {UniqueName, MCSym, SymbolOffset, SymbolSize, SymbolFlags, SymbolType,
           IsSecSym, Visibility});
    }

    BC->registerNameAtAddress(UniqueName, SymbolAddress, SymbolSize,
                              SymbolAlignment, SymbolFlags);
  }

  for (const SectionRef &Section : ObjFile->sections()) {
    if (!Section.isText())
      continue;

    StringRef SectionName = cantFail(Section.getName());
    auto SecOrErr = BC->getUniqueSectionByName(SectionName);
    if (!SecOrErr)
      continue;
    BinarySection *BSection = &SecOrErr.get();

    uint64_t SectionAddr = BSection->getAddress();
    uint64_t SectionSize = BSection->getSize();

    std::vector<SectionSym> SecSymbols;

    auto SymItMap = SectionSymbolsMap.find(SectionName.str());
    if (SymItMap != SectionSymbolsMap.end()) {
      for (const ObjectSymbolInfo &SymInfo : SymItMap->second) {
        if (SymInfo.IsSectionSymbol)
          continue;

        uint64_t SymbolAddress =
            BSection->getAddress() + SymInfo.SectionOffset;
        bool IsFuncType = (SymInfo.Type == SymbolRef::ST_Function);
        bool IsGlobal = (SymInfo.Flags & SymbolRef::SF_Global);

        if (!IsFuncType && !IsGlobal)
          continue;

        uint64_t SymbolSize = SymInfo.Size;
        SecSymbols.push_back({SymInfo.Name, SymbolAddress, SymbolSize});
      }
    }

    std::sort(SecSymbols.begin(), SecSymbols.end(),
              [](const SectionSym &A, const SectionSym &B) {
                if (A.Addr != B.Addr)
                  return A.Addr < B.Addr;
                return A.Size > B.Size;
              });

    auto UniqueIt = std::unique(SecSymbols.begin(), SecSymbols.end(),
                                [](const SectionSym &A, const SectionSym &B) {
                                  return A.Addr == B.Addr;
                                });
    SecSymbols.erase(UniqueIt, SecSymbols.end());

    if (SecSymbols.empty()) {
      SecSymbols.push_back(
          {std::string(SectionName), SectionAddr, SectionSize});
    } else {
      for (size_t i = 0; i < SecSymbols.size(); ++i) {
        if (SecSymbols[i].Size == 0) {
          uint64_t NextAddr = (i + 1 < SecSymbols.size())
                                  ? SecSymbols[i + 1].Addr
                                  : (SectionAddr + SectionSize);
          if (NextAddr > SecSymbols[i].Addr)
            SecSymbols[i].Size = NextAddr - SecSymbols[i].Addr;
        }
      }
    }

    SectionSymbols[SectionName.str()] = std::move(SecSymbols);
  }

  return Error::success();
}

void ObjectRewriteInstance::disassembleFunctions() {
  // Pass 1: Create all BinaryFunction objects
  for (const SectionRef &Section : ObjFile->sections()) {
    if (!Section.isText())
      continue;

    StringRef SectionName = cantFail(Section.getName());
    auto SecOrErr = BC->getUniqueSectionByName(SectionName);
    if (!SecOrErr)
      continue;
    BinarySection *BSection = &SecOrErr.get();

    uint64_t SectionAddr = BSection->getAddress();
    uint64_t SectionSize = BSection->getSize();

    auto SymIt = SectionSymbols.find(SectionName.str());
    if (SymIt == SectionSymbols.end())
      continue;
    const std::vector<SectionSym> &SecSymbols = SymIt->second;

    for (const SectionSym &Sym : SecSymbols) {
      if (Sym.Addr < SectionAddr || Sym.Addr >= SectionAddr + SectionSize)
        continue;

      uint64_t MaxAvailableSize = (SectionAddr + SectionSize) - Sym.Addr;
      uint64_t SymSize = Sym.Size;
      if (SymSize == 0) {
        auto NextIt = std::upper_bound(
            SecSymbols.begin(), SecSymbols.end(), Sym.Addr,
            [](uint64_t Addr, const SectionSym &S) { return Addr < S.Addr; });
        if (NextIt != SecSymbols.end())
          SymSize = NextIt->Addr - Sym.Addr;
        else
          SymSize = MaxAvailableSize;
      }
      uint64_t SafeSize = std::min(SymSize, MaxAvailableSize);

      if (SafeSize == 0 || !BSection->containsRange(Sym.Addr, SafeSize))
        continue;

      if (!opts::FilterFunc.empty() &&
          !StringRef(Sym.Name).contains(opts::FilterFunc))
        continue;

      BinaryFunction *BF =
          BC->createBinaryFunction(Sym.Name, *BSection, Sym.Addr, SafeSize);

      if (!BF)
        continue;

      BF->setMaxSize(SafeSize);
    }
  }

  // Pass 2: Attach relocations to BinaryFunction objects before disassembly
  for (const SectionRef &RelSec : ObjFile->sections()) {
    StringRef RelSecName = cantFail(RelSec.getName());
    if (!RelSecName.starts_with(".rela") && !RelSecName.starts_with(".rel"))
      continue;

    Expected<section_iterator> TargetSecOrErr = RelSec.getRelocatedSection();
    if (!TargetSecOrErr || *TargetSecOrErr == ObjFile->section_end())
      continue;

    SectionRef TargetSec = **TargetSecOrErr;
    if (!TargetSec.isText())
      continue;

    StringRef TargetSecName = cantFail(TargetSec.getName());
    auto SecOrErr = BC->getUniqueSectionByName(TargetSecName);
    if (!SecOrErr)
      continue;

    BinarySection *BSection = &SecOrErr.get();

    for (const RelocationRef &Reloc : RelSec.relocations()) {
      uint64_t RelocOffset = Reloc.getOffset();
      uint64_t RelocType = Reloc.getType();

      symbol_iterator SymbolIt = Reloc.getSymbol();
      std::string SymbolName = "<unknown>";
      MCSymbol *Symbol = nullptr;

      if (SymbolIt != ObjFile->symbol_end()) {
        Expected<StringRef> NameOrErr = SymbolIt->getName();
        if (NameOrErr && !NameOrErr.get().empty()) {
          StringRef SymName = NameOrErr.get();
          SymbolName = SymName.starts_with(BC->AsmInfo->getInternalSymbolPrefix())
                           ? "PG" + std::string(SymName)
                           : std::string(SymName);
          Symbol = BC->Ctx->getOrCreateSymbol(SymbolName);
        } else {
          Expected<section_iterator> SymSecOrErr = SymbolIt->getSection();
          if (SymSecOrErr && *SymSecOrErr != ObjFile->section_end()) {
            StringRef SymSecName = cantFail((*SymSecOrErr)->getName());
            Symbol = BC->Ctx->getOrCreateSymbol(SymSecName);
          }
        }
      }

      int64_t Addend = 0;
      if (isa<ELFObjectFileBase>(ObjFile)) {
        ELFRelocationRef ELFRel(Reloc);
        auto AddendOrErr = ELFRel.getAddend();
        if (AddendOrErr)
          Addend = *AddendOrErr;
      }

      uint64_t FullAddr = BSection->getAddress() + RelocOffset;
      if (BinaryFunction *BF =
              BC->getBinaryFunctionContainingAddress(FullAddr)) {
        BF->addRelocation(FullAddr, Symbol, static_cast<uint32_t>(RelocType),
                          Addend, 0);
      }
    }
  }

  // Pass 3: Disassemble all functions now that relocations are present
  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &BF = BFI.second;
    if (Error E = BF.disassemble()) {
      outs() << "BOLT-INFO: disassemble failed for " << BF.getPrintName()
             << ": " << toString(std::move(E)) << "\n";
    } else {
      outs() << "BOLT-INFO: disassembled " << BF.getPrintName()
             << " size=" << BF.getSize() << "\n";
    }
  }

  // Register function alias symbols into BinaryFunction symbols list
  for (const SectionRef &Section : ObjFile->sections()) {
    if (!Section.isText())
      continue;
    StringRef SectionName = cantFail(Section.getName());
    auto It = SectionSymbolsMap.find(SectionName.str());
    if (It == SectionSymbolsMap.end())
      continue;

    BinarySection *BSection = &BC->getUniqueSectionByName(SectionName).get();

    for (const ObjectSymbolInfo &SymInfo : It->second) {
      if (SymInfo.IsSectionSymbol)
        continue;
      uint64_t SymbolAddr =
          BSection->getAddress() + SymInfo.SectionOffset;
      if (BinaryFunction *BF =
              BC->getBinaryFunctionContainingAddress(SymbolAddr)) {
        if (SymbolAddr == BF->getAddress()) {
          auto &Syms = BF->getSymbols();
          if (Syms.size() == 1 &&
              Syms[0]->getName().starts_with(".local.text")) {
            Syms[0] = SymInfo.Symbol;
          } else if (std::find(Syms.begin(), Syms.end(), SymInfo.Symbol) ==
                     Syms.end()) {
            Syms.push_back(SymInfo.Symbol);
          }
        }
      }
    }
  }
}

void ObjectRewriteInstance::buildFunctionsCFG() {
  BC->getOutputBinaryFunctions().clear();
  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &BF = BFI.second;
    outs() << "BOLT-INFO: Processing function " << BF.getPrintName()
           << " size=" << BF.getSize() << "\n";
    if (Error E = BF.buildCFG(0)) {
      outs() << "BOLT-INFO: buildCFG failed for " << BF.getPrintName() << ": "
             << toString(std::move(E)) << "\n";
    } else {
      BF.setSimple(true);
      BF.getLayout().update(
          ArrayRef<BinaryBasicBlock *>(BF.pbegin(), BF.pend()));
      BF.postProcessCFG();
      BC->getOutputBinaryFunctions().push_back(&BF);
      outs() << "BOLT-INFO: Function " << BF.getPrintName() << " has "
             << BF.size() << " basic blocks.\n";
    }
  }
}

void ObjectRewriteInstance::printCFGs(raw_ostream &OS) {
  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &BF = BFI.second;
    if (!opts::FilterFunc.empty() && !BF.hasNameRegex(opts::FilterFunc))
      continue;

    OS << "Binary Function \"" << BF.getPrintName() << "\"";
    if (BF.isFragment())
      OS << " (fragment)";
    OS << " {\n";
    BF.print(OS, "");
    OS << "}\n\n";
  }
}

void ObjectRewriteInstance::emitObjectFile(StringRef OutputFilename) {
  std::error_code EC;
  raw_fd_ostream OS(OutputFilename, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "BOLT-ERROR: cannot open output file " << OutputFilename << ": "
           << EC.message() << "\n";
    return;
  }

  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(*BC->TheTriple, Error);
  if (!TheTarget) {
    errs() << "BOLT-ERROR: " << Error << "\n";
    return;
  }

  std::unique_ptr<MCCodeEmitter> CE(
      TheTarget->createMCCodeEmitter(*BC->MII, *BC->Ctx));
  std::unique_ptr<MCAsmBackend> TAB(
      TheTarget->createMCAsmBackend(*BC->STI, *BC->MRI, MCTargetOptions()));

  bool HasAnyCFI = false;
  for (auto &BFI : BC->getBinaryFunctions()) {
    if (BFI.second.hasCFI()) {
      HasAnyCFI = true;
      break;
    }
  }

  std::unique_ptr<MCObjectWriter> OW = TAB->createObjectWriter(OS);
  std::unique_ptr<MCStreamer> Streamer(TheTarget->createMCObjectStreamer(
      *BC->TheTriple, *BC->Ctx, std::move(TAB), std::move(OW), std::move(CE),
      *BC->STI));

  if (!HasAnyCFI)
    Streamer->emitCFISections(/*EH=*/false, /*Debug=*/false, /*SFrame=*/false);

  Streamer->initSections(*BC->STI);

  ObjectEmitter OE(*this, *BC, *Streamer);
  OE.emitObjectFile();

  Streamer->finish();
  OS.flush();

  outs() << "BOLT-INFO: Emitted rewritten object file: " << OutputFilename
         << "\n";
}
