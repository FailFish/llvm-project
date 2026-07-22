# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-target-regs=RSI --spare-strategy=arg-callee-eviction %t.o 2>&1 | FileCheck %s

# CHECK: [SUCCESS_REALLOCATED] Reallocated RSI

# Multi-Basic-Block Test: ArgCalleeRegRealloc (Argument Callee Eviction across Loop with Calls & Multiple Returns)
# CFG: .Lentry -> .Lloop_header -> .Lcall_site -> .Learly_err (ret) / .Lloop_latch -> .Lexit (ret)
# RSI is Argument 2 (LiveAtEntry) AND spans across callq helper inside loop.
# Requires push %r12 + mov %r12, %rsi at entry, and pop %r12 at both exit blocks (.Learly_err & .Lexit).
  .text
  .globl test_phase2b_multi_bb
  .type test_phase2b_multi_bb, @function
test_phase2b_multi_bb:
  movq $0, %rbx             # Entry BB: RSI is Argument 2 (live at entry)
.Lloop_header:
  cmpq $0, (%rsi)           # Loop Header BB: checking dereferenced RSI
  je .Learly_err
.Lcall_site:
  callq helper              # Call site: RSI is live across callq!
  addq %rax, (%rsi)         # Latch BB: modifying dereferenced RSI
  incq %rbx
  cmpq $5, %rbx
  jl .Lloop_header
.Lexit:
  movq %rbx, %rax           # Normal Exit BB
  retq                      # Exit 1 (normal ret)
.Learly_err:
  movq $-1, %rax            # Error Exit BB
  retq                      # Exit 2 (error ret)
