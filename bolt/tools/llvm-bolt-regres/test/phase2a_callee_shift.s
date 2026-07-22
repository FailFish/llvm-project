# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-regs --spare-target-regs=RSI --spare-strategy=callee-shift %t.o 2>&1 | FileCheck %s

# CHECK: [SUCCESS_REALLOCATED] Reallocated RSI

# Multi-Basic-Block Test: CalleeRegRealloc (Callee-Saved Shift across Branching CFG with Call & Multiple Returns)
# CFG: .Lentry -> .Learly_exit (ret) / .Lmain_path (call helper) -> .Lexit (ret)
# RSI is dead at entry, initialized in entry BB, but spans across callq helper in .Lmain_path.
# Requires callee-saved R12 shift + push %r12 at entry & pop %r12 at both return exits.
  .text
  .globl test_phase2a_multi_bb
  .type test_phase2a_multi_bb, @function
test_phase2a_multi_bb:
  movq $100, %rsi           # Entry BB: RSI initialized (dead at entry)
  cmpq $0, %rdi
  je .Learly_exit
.Lmain_path:
  movq %rdi, %rax
  callq helper              # Call site: RSI is live across callq!
  addq %rsi, %rax           # Main path BB: using RSI after call
  retq                      # Exit 1 (main ret)
.Learly_exit:
  xorq %rax, %rax           # Early Exit BB
  retq                      # Exit 2 (early ret)
