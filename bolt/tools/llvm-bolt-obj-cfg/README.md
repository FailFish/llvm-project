# Object-File CFG & Binary Transformation Engine (`llvm-bolt-obj-cfg`)

## 1. Executive Summary & Core Rationale

`llvm-bolt-obj-cfg` is a lightweight, specialized binary analysis and transformation tool built on top of LLVM's BOLT core libraries (`LLVMBOLTCore`, `LLVMBOLTPasses`, `LLVMBOLTRewrite`, `LLVMBOLTUtils`). 

The primary goal of this tool is to perform **intra-module Control Flow Graph (CFG) reconstruction, instruction analysis, relocation matching, and binary transformation directly on unlinked relocatable object files (`.o`)**, rather than post-link executables.

---

## 2. Why Target `.o` Files? Key Advantages & Problem Bypasses Over Linked ELF Executables

Operating at the unlinked relocatable object file (`.o`) level provides fundamental architectural advantages that **bypass major limitations inherent to post-link ELF binary rewriting**:

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                     LINKED ELF EXECUTABLE (Standard BOLT Target)                       │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ ❌ Hardcoded VMAs (0x401000) require complex global address rewriting & segment growth  │
│ ❌ Stripped relocation tables force disassembly pattern-matching for jump tables        │
│ ❌ Stripped symbol tables (.symtab) force heuristic function boundary scanning         │
│ ❌ Requires Linux perf LBR profile data (perf.data) for layout optimization            │
└────────────────────────────────────────────────────────────────────────────────────────┘
                                           │
                                           │  BYPASSED BY TARGETING .o FILES
                                           ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                   UNLINKED RELOCATABLE OBJECT FILE (.o) (Our Target)                   │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ ✅ Zero Virtual Address Space (sh_addr=0): Address allocation deferred to ld/lld       │
│ ✅ Preserved .rela.rodata Relocations: 100% exact jump table target discovery         │
│ ✅ Preserved .symtab Symbol Tables: Exact function boundaries without heuristics       │
│ ✅ Profile-Independent: Operates purely on static structural CFGs without perf.data   │
│ ✅ Deferred Cross-Module Symbols: External calls (call printf) use standard PLT32     │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### A. Deferred Memory Allocation & Address Binding (`sh_addr = 0`)
- **Linked Binary Problem**: Linked executables have fixed Virtual Memory Addresses (VMAs, e.g. `0x401000`). Inserting instructions or reordering basic blocks breaks absolute address offsets, forcing standard BOLT to recalculate global memory maps and allocate new ELF segments (`.bolt.text`).
- **Object File Advantage**: In `.o` files, all section VMA addresses are `0` (`sh_addr = 0`). All internal references are section-relative offsets. Inserting or deleting instructions simply updates `MCInst` lists without breaking global memory bindings. The final physical address allocation is **deferred to the system linker (`ld.lld` / `mold`)**.

### B. Guaranteed Relocation Records for Jump Tables (`.rela.rodata`)
- **Linked Binary Problem**: Standard linkers strip relocation tables (`.rela.dyn`, `.rela.plt`) by default. Standard BOLT must use pattern-matching disassembly heuristics (`MCPlusBuilder::analyzeIndirectBranch`) to guess jump table targets. If pattern matching fails, BOLT marks the function `IsSimple = false` and skips optimizing it.
- **Object File Advantage**: Relocatable `.o` files **natively preserve `.rela.rodata` relocation tables**. Every jump table entry in `.rodata` targeting a `.text` basic block has an explicit ELF relocation record. BOLT reads `.rela.rodata` directly, achieving **100% deterministic target basic-block discovery** without pattern guessing.

### C. Exact Function Boundaries via Preserved Symbol Tables (`.symtab`)
- **Linked Binary Problem**: Executable binaries are frequently stripped (`strip --strip-all`), removing `.symtab`. BOLT must rely on DWARF `.eh_frame` unwinding tables, LBR profile traces, or heuristic entry-point scanning to guess where functions begin and end.
- **Object File Advantage**: Object files retain full `.symtab` symbol tables with explicit function entry point types (`SymbolRef::ST_Function`) and sizes (`st_size`), providing **exact function boundaries without heuristic guessing**.

### D. Performance Profile Independence (`perf.data`)
- **Linked Binary Problem**: Standard BOLT is designed for Profile-Guided Optimization (PGO), requiring Linux `perf` Branch Stack (LBR) sampling records to identify hot/cold functions.
- **Object File Advantage**: Static hardening, control-flow integrity, or instruction transformation passes analyze structural CFGs directly, completely eliminating the requirement for `perf` data collection.

---

## 3. Fundamental Assembler Resolution Principle & Relocation Behavior

To accurately understand how binary analysis engines (like BOLT) process relocatable object files (`.o`), we must establish the core rule governing assemblers (GAS, LLVM `llvm-mc`, NASM, YASM):

> **The Assembler Resolution Rule**:  
> An assembler resolves an expression (such as a branch target or symbol difference $A - B$) **statically at assembly time into raw bytes—emitting NO ELF relocations—if and only if** both symbols $A$ and $B$ reside in the **same section** of the same translation unit, making their relative distance known at assembly time.  
>  
> Conversely, an assembler **MUST emit ELF relocation entries** whenever an expression involves symbols across **different sections** (e.g. `.text` to `.rodata`), **external/global symbols**, or data pointers whose final relative positions are determined at link time.

### Impact on CFG Discovery & Symbolization

1. **Intra-Section Branches (`je .Llabel` inside `.text`)**:
   - Both `je` and `.Llabel` reside in `.text`.
   - The assembler computes the PC-relative byte offset statically into opcode bytes (`74 51`, `eb 3c`). **No ELF relocation is created.**
   - **BOLT Symbolization**: BOLT's disassembler decodes `TargetAddress = PC + Offset` using `evaluateBranch()`, creates local basic block labels (`getOrCreateLocalLabel()`), and symbolizes the branch operands into `MCSymbolRefExpr`.

2. **Intra-Section Relative Jump Tables (`.long .Llabel - .Lfunc_start`)**:
   - Both `.Llabel` and `.Lfunc_start` reside in `.text`.
   - The assembler computes the raw integer differences statically into data bytes (`[24, 48, 96]`). **No ELF relocation is created.**
   - **BOLT Symbolization**: BOLT's `MCPlusBuilder::analyzeIndirectBranch` inspects the indirect jump instruction pattern (`jmp *%rcx`), reads table base address + raw offsets, and computes target basic block addresses.

3. **Inter-Section & Data-to-Code References (`.rodata` Jump Tables & External Calls)**:
   - Target `.Llabel` is in `.text`, but the jump table array is in `.rodata` (or external function call).
   - Because section distances are unknown at assembly time, the **assembler MUST emit explicit ELF relocations (`.rela.rodata` / `.rela.text`)**.
   - **BOLT Symbolization**: In `.o` files, BOLT reads `.rela.rodata` relocations directly, guaranteeing 100% exact jump table basic-block target discovery without pattern guessing.

---

## 4. Architecture & Pipeline Overview

The `llvm-bolt-obj-cfg` tool combines LLVM object inspection patterns (`llvm::object::ObjectFile`) with BOLT's native `BinaryContext` and `BinaryFunction` APIs:

```text
┌────────────────────────────────────────────────────────────────────────────────┐
│                         INPUT RELOCATABLE OBJECT (.o)                          │
└───────────────────────┬────────────────────────────────────────────────────────┘
                        │
                        ▼
┌────────────────────────────────────────────────────────────────────────────────┐
│ 1. SECTION DISCOVERY (`Section.isText()`)                                      │
│    Uses `llvm::object::ObjectFile` to discover all executable code sections.   │
└───────────────────────┬────────────────────────────────────────────────────────┘
                        │
                        ▼
┌────────────────────────────────────────────────────────────────────────────────┐
│ 2. RELOCATION TABLE REGISTRATION (`Section.relocations()`)                     │
│    Registers section relocations into `BinarySection` to associate global      │
│    data and external call symbols with disassembled MCInsts.                   │
└───────────────────────┬────────────────────────────────────────────────────────┘
                        │
                        ▼
┌────────────────────────────────────────────────────────────────────────────────┐
│ 3. BINARYFUNCTION CREATION & BOUNDS CONFIGURATION                              │
│    Instantiates `BinaryFunction` objects for symbols in `.text` and sets       │
│    `BF->setMaxSize(SafeSize)` to prevent section boundary assertion failures.  │
└───────────────────────┬────────────────────────────────────────────────────────┘
                        │
                        ▼
┌────────────────────────────────────────────────────────────────────────────────┐
│ 4. DISASSEMBLY, SYMBOLIZATION & CFG CONSTRUCTION                               │
│    • `BF->disassemble()` decodes instructions and symbolizes intra-function    │
│      relative branch targets into `MCSymbolRefExpr`.                           │
│    • `BF->buildCFG(0)` partitions instructions into basic blocks (.LBB0, etc.) │
│      and links predecessor/successor control-flow edges.                       │
└───────────────────────┬────────────────────────────────────────────────────────┘
                        │
                        ▼
┌────────────────────────────────────────────────────────────────────────────────┐
│ 5. CFG PRINTING / CUSTOM PASS PROCESSING                                       │
│    Outputs official BOLT-formatted CFGs (`BF->print(outs())`) with basic       │
│    blocks, layout hashes, tail call notes, and instruction disassemblies.      │
└────────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. Build & Usage Guide

### Building In-Tree
From the root of your `llvm-project` repository:

```bash
mkdir -p build && cd build

cmake -G Ninja -S ../llvm -B . \
  -DLLVM_ENABLE_PROJECTS="bolt" \
  -DLLVM_TARGETS_TO_BUILD="X86;AArch64" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_USE_LINKER=lld

ninja llvm-bolt-obj-cfg
```

### Running the Tool
```bash
# 1. Dump Control Flow Graphs across all object files
libjpeg-asm/dump_all_cfgs.sh

# 2. Run directly on a single object file
build/bin/llvm-bolt-obj-cfg libjpeg-asm/x86_64/jccolor-avx2.asm.o

# 3. Filter specific function names
build/bin/llvm-bolt-obj-cfg --filter-func=rowloop libjpeg-asm/x86_64/jccolor-avx2.asm.o
```
