# `llvm-bolt-regres`: Post-Binary Register Reallocation & Sparing Tool

`llvm-bolt-regres` is a standalone LLVM BOLT tool designed for **post-codegen register reallocation, register sparing, and binary CFG rewriting** on relocatable object files (`.o`).

By leveraging BOLT's `BinaryContext`, `BinaryFunction`, and `LivenessAnalysis` infrastructure, `llvm-bolt-regres` disassembles post-assembly binaries, constructs Control Flow Graphs (CFGs), extracts physical register def-use webs, and executes post-binary register reallocation passes to free specified target registers.

---

## 🚀 Key Capabilities

### 1. Physical Register Web Analysis (`RegisterWebExtractor`)
Unlike traditional LLVM CodeGen passes that operate on virtual registers before assembly lowering, `llvm-bolt-regres` operates on physical registers in relocatable `.o` binaries.

The `RegisterWebExtractor` engine:
* Traces def-use chains across multi-basic-block CFG control flow (loops, branches, multi-returns).
* Isolates connected def-use subgraphs (**`RegisterWeb`**s) for target physical registers.
* Computes web-level traits:
  * **`LiveAtEntry`**: Indicates if the target register is an incoming ABI parameter or live-in at instruction 0.
  * **`CrossesCallSite`**: Indicates if any instruction within the web's lifetime spans an external function call (`callq`).

### 2. Unified Reallocation Engine (`RegReallocEngine`)
`RegReallocEngine` provides a DRY (Don't Repeat Yourself), parameter-driven core for:
* **Candidate Register Search**: Finds clean, non-interfering 64-bit candidate registers while enforcing ABI parameter protection and REX-prefix constraints (e.g., blacklisting high 8-bit registers `%ah`/`%bh`).
* **Operand Mutation**: Renames all uses and definitions of target registers in `MCInst` operands across the web.
* **DWARF CFI Generation**: Generates correct DWARF Call Frame Information (`.cfi_adjust_cfa_offset`, `.cfi_offset`, `.cfi_same_value`) when shifting webs to callee-saved registers.

---

## 📦 Modular Pass Pipeline (`RegReallocPasses`)

Each reallocation strategy is encapsulated in an independently instantiable pass class:

| Pass Class | Strategy Flag | Cost | Transformation Description |
| :--- | :--- | :---: | :--- |
| **`DirectRegRealloc`** | `--spare-strategy=direct-swap` | 0 insns | **Direct Register Swap**: 0-cost local operand renaming when target is dead at entry and does not cross calls. |
| **`ArgRegRealloc`** | `--spare-strategy=arg-eviction` | 1 mov | **Argument Register Eviction**: Evicts live parameter registers at function entry (`mov r_scratch, r_arg`) into scratch registers. |
| **`CalleeRegRealloc`** | `--spare-strategy=callee-shift` | push/pop | **Callee-Saved Register Shift**: Shifts call-crossing webs to callee-saved registers with prologue `push` & epilogue `pop` + DWARF CFI. |
| **`ArgCalleeRegRealloc`** | `--spare-strategy=arg-callee-eviction` | push/mov/pop | **Argument Callee-Saved Eviction**: Evicts live argument registers across calls by combining entry `mov` with prologue/epilogue `push`/`pop` + DWARF CFI. |

---

## 🛠️ Command-Line Interface

### Syntax
```bash
llvm-bolt-regres [options] <input object file (.o)>
```

### Options

| Flag | Description | Default |
| :--- | :--- | :--- |
| `--spare-regs` | Enables post-binary register reallocation pass pipeline. | `true` |
| `--spare-target-regs=<REG>` | Target registers to spare (comma-separated, e.g. `--spare-target-regs=RSI,R11`). | `R11, R14, R15` |
| `--spare-strategy=<STRATEGY>` | Selection mode: `direct-swap`, `arg-eviction`, `callee-shift`, `arg-callee-eviction`, or `all`. | `all` |
| `--print-liveness` | Prints `LiveIn` and `LiveOut` physical register sets for each basic block. | `false` |
| `--filter-func=<NAME>` | Filters execution and dumping to functions matching the specified regex/substring. | `""` (all functions) |

---

## 💡 Usage Examples

### 1. Run full cost-hierarchy pipeline to spare `%rsi`
```bash
llvm-bolt-regres --spare-regs --spare-target-regs=RSI input.o
```

### 2. Run ONLY 0-cost `DirectRegRealloc`
```bash
llvm-bolt-regres --spare-regs --spare-target-regs=RSI --spare-strategy=direct-swap input.o
```

### 3. Print basic block liveness sets
```bash
llvm-bolt-regres --print-liveness --filter-func=my_func input.o
```

---

## 🏗️ Building & Verification

### Build Target
```bash
ninja -C build llvm-bolt-regres
```

### Run Automated Lit Test Suite
```bash
# Run the entire test suite with LLVM Lit (0.07s)
build/bin/llvm-lit -v bolt/tools/llvm-bolt-regres/test

# Run a specific Lit test case
build/bin/llvm-lit -v bolt/tools/llvm-bolt-regres/test/phase1a_pure_swap.s
```

---

## 📁 Source File Structure

```text
bolt/tools/llvm-bolt-regres/
├── CMakeLists.txt              # CMake build targets for llvm-bolt-regres
├── README.md                   # Tool documentation
├── llvm-bolt-regres.cpp        # Command-line driver and ObjectRewriteInstance
├── RegisterWebExtractor.h/.cpp # Def-use web extraction and liveness engine
├── RegReallocEngine.h/.cpp     # Core parameter-driven register reallocation engine
├── RegReallocPasses.h/.cpp     # Independent pass classes (DirectRegRealloc, ArgRegRealloc, etc.)
├── SpareRegisters.h/.cpp       # Pipeline orchestrator pass
└── test/                       # Multi-basic-block Lit / FileCheck assembly test suite
```
