//===- bolt/tools/llvm-bolt-regres/llvm-bolt-regres.cpp ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CLI tool for object-file transformation using ObjectRewriteInstance.
//
//===----------------------------------------------------------------------===//

#include "SpareRegisters.h"
#include "bolt/Core/MCPlusBuilder.h"
#include "bolt/Core/Relocation.h"
#include "bolt/Rewrite/ObjectRewriteInstance.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "bolt/Utils/Utils.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace object;
using namespace bolt;

namespace opts {

extern cl::OptionCategory BoltCategory;
extern cl::opt<bool> PrintCFG;

static cl::opt<std::string>
    InputFilename(cl::Positional, cl::desc("<input object file>"), cl::Required,
                  cl::cat(BoltCategory));

static cl::opt<bool> NoSpareRegs("no-spare-regs",
                                 cl::desc("Disable spare registers pass"),
                                 cl::cat(BoltCategory));

static cl::list<std::string> SpareTargetRegsOpt(
    "spare-target-regs", cl::CommaSeparated,
    cl::desc("Target registers to vacate (e.g. R11, R14, R15)"),
    cl::cat(BoltCategory));

static cl::opt<SpareStrategyMode> SpareStrategyOpt(
    "spare-strategy", cl::desc("Strategy for vacating target register"),
    cl::init(SpareStrategyMode::All),
    cl::values(
        clEnumValN(SpareStrategyMode::All, "all",
                   "Run all strategies in cost order"),
        clEnumValN(SpareStrategyMode::DirectSwap, "direct-swap",
                   "Swap target reg with available scratch reg directly"),
        clEnumValN(SpareStrategyMode::ArgEviction, "arg-eviction",
                   "Evict target reg to argument reg"),
        clEnumValN(SpareStrategyMode::CalleeShift, "callee-shift",
                   "Shift target reg to callee-saved reg"),
        clEnumValN(SpareStrategyMode::ArgCalleeEviction, "arg-callee-eviction",
                   "Evict argument reg to callee-saved reg")),
    cl::cat(BoltCategory));

static cl::opt<bool> PrintDiff("print-diff",
                               cl::desc("Print disassembly diff"),
                               cl::cat(BoltCategory));

} // namespace opts

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

  cl::HideUnrelatedOptions(opts::BoltCategory);
  cl::ParseCommandLineOptions(
      argc, argv, "Object-file register sparing and rewriting tool\n");

  Expected<OwningBinary<Binary>> BinaryOrErr =
      createBinary(opts::InputFilename);
  if (!BinaryOrErr) {
    report_error(opts::InputFilename, BinaryOrErr.takeError());
  }

  Binary *Bin = BinaryOrErr->getBinary();
  ObjectFile *ObjFile = dyn_cast<ObjectFile>(Bin);
  if (!ObjFile) {
    errs() << "BOLT-ERROR: input file is not an object file\n";
    return 1;
  }

  outs() << "BOLT-INFO: Disassembling object file: " << opts::InputFilename
         << " (" << ObjFile->getArch() << ")\n";

  Triple TheTriple = ObjFile->makeTriple();

  Relocation::Arch = TheTriple.getArch();
  auto BCOrErr = BinaryContext::createBinaryContext(
      TheTriple, std::make_shared<orc::SymbolStringPool>(), opts::InputFilename,
      nullptr, /*IsPIC=*/true, nullptr,
      JournalingStreams{outs(), errs()});
  if (!BCOrErr) {
    report_error(opts::InputFilename, BCOrErr.takeError());
  }

  std::unique_ptr<BinaryContext> BC = std::move(BCOrErr.get());

  ObjectRewriteInstance ORI(ObjFile, std::move(BC));

  ORI.processSectionMetadata();

  if (Error E = ORI.readSymbolTable()) {
    report_error(opts::InputFilename, std::move(E));
  }

  ORI.disassembleFunctions();
  ORI.buildFunctionsCFG();

  if (opts::PrintCFG) {
    ORI.printCFGs(outs());
  }

  if (!opts::NoSpareRegs) {
    SmallVector<std::string, 4> TargetRegs(opts::SpareTargetRegsOpt.begin(),
                                           opts::SpareTargetRegsOpt.end());
    if (TargetRegs.empty())
      TargetRegs = {"R11", "R14", "R15"};

    SpareRegisters Pass(TargetRegs, opts::SpareStrategyOpt);
    cantFail(Pass.runOnFunctions(ORI.getBinaryContext()));
  }

  if (!opts::OutputFilename.empty()) {
    ORI.emitObjectFile(opts::OutputFilename);
  }

  return 0;
}
