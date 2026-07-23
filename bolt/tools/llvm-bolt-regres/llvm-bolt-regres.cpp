//===- bolt/tools/llvm-bolt-obj-cfg/llvm-bolt-obj-cfg.cpp ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Refactored object-file CFG printer and transformation engine using an
// ObjectRewriteInstance driver class.
//
//===----------------------------------------------------------------------===//

#include "bolt/Core/BinaryBasicBlock.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryEmitter.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Core/MCPlus.h"
#include "bolt/Core/MCPlusBuilder.h"
#include "bolt/Core/Relocation.h"
#include "bolt/Passes/DataflowInfoManager.h"
#include "bolt/Passes/RegAnalysis.h"
#include "bolt/Rewrite/RewriteInstance.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "bolt/Utils/Utils.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "SpareRegisters.h"

using namespace llvm;
using namespace object;
using namespace bolt;

namespace opts {

static cl::OptionCategory RegResCategory("llvm-bolt-regres Register Reallocation Options");

static cl::opt<std::string>
    InputFilename(cl::Positional, cl::desc("<input object file (.o)>"),
                  cl::Required, cl::cat(RegResCategory));

static cl::opt<std::string>
    FilterFunc("filter-func",
               cl::desc("Only print CFG for functions matching this string"),
               cl::init(""), cl::cat(RegResCategory));

static cl::opt<bool>
    NoSpareRegs("no-spare-regs",
              cl::desc("Disable attempting to spare a set of registers using liveness analysis"),
              cl::init(false), cl::cat(RegResCategory));

static cl::opt<bool>
    PrintCfg("print-asm-cfg",
                  cl::desc("Print All (Untouched) Control Flow Graphs"),
                  cl::init(false), cl::cat(RegResCategory));

static cl::opt<bool>
    PrintDiff("print-diff",
              cl::desc("Print disassembly diff for rewritten functions"),
              cl::init(false), cl::cat(RegResCategory));

static cl::opt<SpareStrategyMode> SpareStrategyOpt(
    "spare-strategy", cl::desc("Strategy for register sparing/reallocation"),
    cl::values(
        clEnumValN(SpareStrategyMode::DirectSwap, "direct-swap", "DirectRegRealloc: 0-cost local operand swap"),
        clEnumValN(SpareStrategyMode::ArgEviction, "arg-eviction", "ArgRegRealloc: Volatile Entry Eviction"),
        clEnumValN(SpareStrategyMode::CalleeShift, "callee-shift", "CalleeRegRealloc: Callee-Saved Shift"),
        clEnumValN(SpareStrategyMode::ArgCalleeEviction, "arg-callee-eviction", "ArgCalleeRegRealloc: Argument Callee Eviction"),
        clEnumValN(SpareStrategyMode::All, "all", "Run all strategies in cost order (default)")),
    cl::init(SpareStrategyMode::All), cl::cat(RegResCategory));

static cl::list<std::string> SpareTargetRegsOpt(
    "spare-target-regs", cl::desc("Target registers to spare (comma-separated or repeated)"),
    cl::CommaSeparated, cl::cat(RegResCategory));

} // namespace opts

static StringRef ToolName = "llvm-bolt-regres";

static void reportError(StringRef Message, Error E) {
  errs() << ToolName << ": '" << Message << "': " << toString(std::move(E))
         << ".\n";
  exit(1);
}

/// Encapsulates an object-file binary rewriting session, mirroring BOLT's
/// RewriteInstance pattern with standard symbol naming conventions.
class ObjectRewriteInstance {
public:
  /// Static factory method: Handles BinaryContext and MCPlusBuilder initialization
  static Expected<std::unique_ptr<ObjectRewriteInstance>>
  create(ObjectFile *ObjFile) {
    Triple TheTriple = ObjFile->makeTriple();
    Relocation::Arch = TheTriple.getArch();

    // Create BOLT BinaryContext
    auto BCOrErr = BinaryContext::createBinaryContext(
        TheTriple, std::make_shared<orc::SymbolStringPool>(),
        ObjFile->getFileName(), nullptr, true, DWARFContext::create(*ObjFile),
        JournalingStreams{outs(), errs()});

    if (Error E = BCOrErr.takeError())
      return std::move(E);

    std::unique_ptr<BinaryContext> BC = std::move(BCOrErr.get());

    // Initialize Target-specific MCPlusBuilder
    BC->initializeTarget(std::unique_ptr<MCPlusBuilder>(
        createMCPlusBuilder(TheTriple.getArch(), BC->MIA.get(), BC->MII.get(),
                            BC->MRI.get(), BC->STI.get())));

    BC->HasRelocations = true;

    return std::unique_ptr<ObjectRewriteInstance>(
        new ObjectRewriteInstance(ObjFile, std::move(BC)));
  }

  /// High-level entry point to execute the object rewrite pipeline
  Error run();

  /// Step 1: Register sections and parse section relocations into BinaryContext
  void processSectionMetadata();

  /// Step 2: Discover functions and labels from .symtab using BOLT rules
  void readSymbolTable();

  /// Step 3: Disassemble instructions across all functions
  void disassembleFunctions();

  /// Step 4: Build CFG basic blocks and control edges for all functions
  void buildFunctionsCFG();

  /// Step 5: Run optimization and transformation passes
  void runOptimizationPasses();

  /// Step 6: Dump formatted CFGs to output stream
  void printCFGs(raw_ostream &OS);

  /// Step 7: Emit rewritten binary functions to output object file (.o)
  void emitObjectFile(StringRef OutputFilename);

  /// Helper: Capture disassembly representation of a function
  std::vector<std::string> disassembleFunctionLines(const BinaryFunction &BF);

  /// Helper: Print disassembly diff for rewritten functions
  void printDiff(const std::map<const BinaryFunction *, std::vector<std::string>> &OriginalFuncLines);

private:
  ObjectRewriteInstance(ObjectFile *ObjFile, std::unique_ptr<BinaryContext> BC)
      : ObjFile(ObjFile), BC(std::move(BC)) {}

  ObjectFile *ObjFile;
  std::unique_ptr<BinaryContext> BC;
  uint64_t AnonymousId{0};

  struct SectionSym {
    std::string Name;
    uint64_t Addr;
    uint64_t Size;
  };
  std::map<SectionRef, std::vector<SectionSym>> SectionSymbols;
};

void ObjectRewriteInstance::processSectionMetadata() {
  // Derive alignment from input object file text section if not explicitly specified via CLI
  for (const SectionRef &Section : ObjFile->sections()) {
    if (Section.isText()) {
      uint64_t SecAlign = Section.getAlignment().value();
      if (SecAlign > 0) {
        if (!opts::AlignText.getNumOccurrences())
          opts::AlignText = SecAlign;
        if (!opts::AlignFunctions.getNumOccurrences())
          opts::AlignFunctions = SecAlign;
      }
    }
  }
  if (!opts::AlignText)
    opts::AlignText = 1;
  if (!opts::AlignFunctions)
    opts::AlignFunctions = 1;

  // Register sections in BinaryContext
  for (const SectionRef &Section : ObjFile->sections()) {
    BC->registerSection(Section);
  }

  // Parse relocations for text sections
  for (const SectionRef &Section : ObjFile->sections()) {
    if (!Section.isText())
      continue;

    uint64_t SectionAddr = Section.getAddress();
    auto SecOrErr = BC->getSectionForAddress(SectionAddr);
    BinarySection *BSection = SecOrErr ? &SecOrErr.get() : &BC->registerSection(Section);

    for (const RelocationRef &Reloc : Section.relocations()) {
      uint64_t RelocOffset = Reloc.getOffset();
      symbol_iterator SymbolIt = Reloc.getSymbol();
      std::string SymbolName = "<unknown>";
      if (SymbolIt != ObjFile->symbol_end()) {
        Expected<StringRef> NameOrErr = SymbolIt->getName();
        if (NameOrErr)
          SymbolName = std::string(NameOrErr.get());
      }
      MCSymbol *Symbol = BC->getOrCreateGlobalSymbol(0, SymbolName);
      BSection->addRelocation(RelocOffset, Symbol, Reloc.getType(), 0);
    }
  }
}

void ObjectRewriteInstance::readSymbolTable() {
  for (const SectionRef &Section : ObjFile->sections()) {
    if (!Section.isText())
      continue;

    uint64_t SectionAddr = Section.getAddress();
    uint64_t SectionSize = Section.getSize();
    StringRef SectionName = cantFail(Section.getName());

    std::vector<SectionSym> &SecSymbols = SectionSymbols[Section];

    for (const SymbolRef &Symbol : ObjFile->symbols()) {
      Expected<section_iterator> SecOrErr = Symbol.getSection();
      if (!SecOrErr || *SecOrErr != Section)
        continue;

      Expected<SymbolRef::Type> TypeOrErr = Symbol.getType();
      if (!TypeOrErr || *TypeOrErr == SymbolRef::ST_File || *TypeOrErr == SymbolRef::ST_Debug)
        continue;

      Expected<StringRef> NameOrErr = Symbol.getName();
      if (!NameOrErr)
        continue;

      StringRef SymName = NameOrErr.get();
      uint64_t SymbolAddress = cantFail(Symbol.getAddress());
      uint64_t SymbolSize = ELFSymbolRef(Symbol).getSize();
      uint64_t SymbolAlignment = Symbol.getAlignment();
      uint32_t SymbolFlags = cantFail(Symbol.getFlags());

      // Following RewriteInstance.cpp naming rules,
      // 1. Convert internal assembler prefix (.L) to "PG.L" (Private Global)
      std::string Name =
          SymName.starts_with(BC->AsmInfo->getInternalSymbolPrefix())
              ? "PG" + std::string(SymName)
              : std::string(SymName);

      // 2. Assign name directly (single object file guarantees unique symbol names in .symtab)
      std::string UniqueName;
      if (Name.empty()) {
        UniqueName = "ANONYMOUS." + std::to_string(AnonymousId++);
      } else {
        UniqueName = Name;
      }

      // 3. Register names at address in BinaryContext
      BC->registerNameAtAddress(UniqueName, SymbolAddress, SymbolSize,
                                SymbolAlignment, SymbolFlags);

      bool IsFuncType = (*TypeOrErr == SymbolRef::ST_Function);
      bool IsGlobal = (SymbolFlags & SymbolRef::SF_Global);

      // Only treat ST_Function or Global symbols as function entry points
      if (!IsFuncType && !IsGlobal)
        continue;

      SecSymbols.push_back({UniqueName, SymbolAddress, SymbolSize});
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
      SecSymbols.push_back({std::string(SectionName), SectionAddr, SectionSize});
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
  }
}

void ObjectRewriteInstance::disassembleFunctions() {
  for (const SectionRef &Section : ObjFile->sections()) {
    if (!Section.isText())
      continue;

    uint64_t SectionAddr = Section.getAddress();
    uint64_t SectionSize = Section.getSize();
    auto SecOrErr = BC->getSectionForAddress(SectionAddr);
    BinarySection *BSection = SecOrErr ? &SecOrErr.get() : &BC->registerSection(Section);

    const std::vector<SectionSym> &SecSymbols = SectionSymbols[Section];

    for (const SectionSym &Sym : SecSymbols) {
      if (Sym.Addr < SectionAddr || Sym.Addr >= SectionAddr + SectionSize)
        continue;

      uint64_t MaxAvailableSize = (SectionAddr + SectionSize) - Sym.Addr;
      uint64_t SafeSize = std::min(Sym.Size, MaxAvailableSize);

      if (SafeSize == 0 || !BSection->containsRange(Sym.Addr, SafeSize))
        continue;

      if (!opts::FilterFunc.empty() && !StringRef(Sym.Name).contains(opts::FilterFunc))
        continue;

      // Create BOLT BinaryFunction
      BinaryFunction *BF = BC->createBinaryFunction(
          Sym.Name, *BSection, Sym.Addr, SafeSize);

      if (!BF)
        continue;

      BF->setMaxSize(SafeSize);

      // Disassemble raw instruction stream
      if (Error E = BF->disassemble()) {
        consumeError(std::move(E));
      }
    }
  }
}

void ObjectRewriteInstance::buildFunctionsCFG() {
  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &BF = BFI.second;
    if (Error E = BF.buildCFG(0)) {
      consumeError(std::move(E));
    }
  }
}

void ObjectRewriteInstance::runOptimizationPasses() {
  if (opts::NoSpareRegs)
    return;

  SmallVector<std::string, 4> TargetRegs(opts::SpareTargetRegsOpt.begin(),
                                         opts::SpareTargetRegsOpt.end());
  if (TargetRegs.empty())
    TargetRegs = {"R11", "R14", "R15"};

  SpareRegisters Pass(TargetRegs, opts::SpareStrategyOpt);
  cantFail(Pass.runOnFunctions(*BC));
}

void ObjectRewriteInstance::printCFGs(raw_ostream &OS) {
  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &BF = BFI.second;
    if (!opts::FilterFunc.empty() && !BF.hasNameRegex(opts::FilterFunc))
      continue;
    BF.print(OS);
  }
}

std::vector<std::string>
ObjectRewriteInstance::disassembleFunctionLines(const BinaryFunction &BF) {
  std::vector<std::string> Lines;
  for (const BinaryBasicBlock &BB : BF) {
    Lines.push_back(BB.getName().str() + ":");
    for (const MCInst &Inst : BB) {
      std::string InstStr;
      raw_string_ostream SS(InstStr);
      BC->printInstruction(SS, Inst, 0, &BF, /*PrintMCInst=*/false,
                           /*PrintMemData=*/false, /*PrintRelocations=*/false,
                           /*Endl=*/"");
      Lines.push_back("  " + SS.str());
    }
  }
  return Lines;
}

void ObjectRewriteInstance::printDiff(
    const std::map<const BinaryFunction *, std::vector<std::string>>
        &OriginalFuncLines) {
  for (auto &BFI : BC->getBinaryFunctions()) {
    const BinaryFunction &BF = BFI.second;
    if (!opts::FilterFunc.empty() && !BF.hasNameRegex(opts::FilterFunc))
      continue;

    auto It = OriginalFuncLines.find(&BF);
    if (It == OriginalFuncLines.end())
      continue;

    const std::vector<std::string> &Before = It->second;
    std::vector<std::string> After = disassembleFunctionLines(BF);

    if (Before == After)
      continue;

    outs() << "--- a/" << BF.getPrintName() << "\n";
    outs() << "+++ b/" << BF.getPrintName() << "\n";
    outs() << "@@ -1," << Before.size() << " +1," << After.size() << " @@\n";

    size_t i = 0, j = 0;
    while (i < Before.size() || j < After.size()) {
      if (i < Before.size() && j < After.size() && Before[i] == After[j]) {
        outs() << "  " << Before[i] << "\n";
        i++;
        j++;
      } else {
        size_t MatchI = i, MatchJ = j;
        bool FoundMatch = false;
        for (size_t lookI = i; lookI < Before.size() && !FoundMatch; ++lookI) {
          for (size_t lookJ = j; lookJ < After.size() && !FoundMatch; ++lookJ) {
            if (Before[lookI] == After[lookJ]) {
              MatchI = lookI;
              MatchJ = lookJ;
              FoundMatch = true;
            }
          }
        }
        if (FoundMatch) {
          while (i < MatchI) {
            outs() << "- " << Before[i++] << "\n";
          }
          while (j < MatchJ) {
            outs() << "+ " << After[j++] << "\n";
          }
        } else {
          while (i < Before.size()) {
            outs() << "- " << Before[i++] << "\n";
          }
          while (j < After.size()) {
            outs() << "+ " << After[j++] << "\n";
          }
        }
      }
    }
  }
}

void ObjectRewriteInstance::emitObjectFile(StringRef OutputFilename) {
  outs() << "BOLT-INFO: Emitting rewritten object file: " << OutputFilename
         << "\n";
  std::error_code EC;
  raw_fd_ostream OS(OutputFilename, EC, sys::fs::OF_None);
  if (EC) {
    errs() << ToolName << ": cannot open output file '" << OutputFilename
           << "': " << EC.message() << "\n";
    exit(1);
  }

  BC->getOutputBinaryFunctions().clear();
  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &BF = BFI.second;
    if (BC->shouldEmit(BF))
      BC->getOutputBinaryFunctions().push_back(&BF);
  }

  std::unique_ptr<MCStreamer> Streamer = BC->createStreamer(OS);
  emitBinaryContext(*Streamer, *BC);
  Streamer->finish();
}

Error ObjectRewriteInstance::run() {
  outs() << "BOLT-INFO: Disassembling object file: " << ObjFile->getFileName()
         << " (" << BC->TheTriple->str() << ")\n";

  processSectionMetadata();
  readSymbolTable();
  disassembleFunctions();
  buildFunctionsCFG();

  if (opts::PrintCfg)
    printCFGs(outs());

  std::map<const BinaryFunction *, std::vector<std::string>> OriginalFuncLines;
  if (opts::PrintDiff) {
    for (auto &BFI : BC->getBinaryFunctions()) {
      OriginalFuncLines[&BFI.second] = disassembleFunctionLines(BFI.second);
    }
  }

  runOptimizationPasses();

  if (opts::PrintDiff)
    printDiff(OriginalFuncLines);

  if (!opts::OutputFilename.empty())
    emitObjectFile(opts::OutputFilename);

  return Error::success();
}

int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;

  // Initialize targets supported by BOLT
#define BOLT_TARGET(target)                                                    \
  LLVMInitialize##target##TargetInfo();                                        \
  LLVMInitialize##target##TargetMC();                                          \
  LLVMInitialize##target##AsmParser();                                         \
  LLVMInitialize##target##Disassembler();                                      \
  LLVMInitialize##target##Target();                                            \
  LLVMInitialize##target##AsmPrinter();

#include "bolt/Core/TargetConfig.def"

  cl::ParseCommandLineOptions(argc, argv,
                              "BOLT Object File CFG Printer & Transformation Engine\n");

  if (!sys::fs::exists(opts::InputFilename)) {
    errs() << ToolName << ": Input file '" << opts::InputFilename
           << "' does not exist.\n";
    return 1;
  }

  // Load input object file
  Expected<OwningBinary<Binary>> BinaryOrErr =
      createBinary(opts::InputFilename);
  if (Error E = BinaryOrErr.takeError())
    reportError(opts::InputFilename, std::move(E));

  Binary &Bin = *BinaryOrErr.get().getBinary();
  ObjectFile *ObjFile = dyn_cast<ObjectFile>(&Bin);
  if (!ObjFile) {
    errs() << ToolName << ": Input file is not a valid object file.\n";
    return 1;
  }

  // Instantiate ObjectRewriteInstance via static factory method
  auto RIOrErr = ObjectRewriteInstance::create(ObjFile);
  if (Error E = RIOrErr.takeError())
    reportError(opts::InputFilename, std::move(E));

  // Execute ObjectRewriteInstance pipeline
  if (Error E = RIOrErr.get()->run())
    reportError(opts::InputFilename, std::move(E));

  return 0;
}
