# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-target-regs=RSI --spare-strategy=arg-eviction %t.o 2>&1 | FileCheck %s

# CHECK: [SUCCESS_REALLOCATED] Reallocated RSI

# Multi-Basic-Block Test: ArgRegRealloc (Argument Entry Eviction across Branching CFG)
# CFG: .Lentry -> .Lthen / .Lelse -> .Lmerge -> ret
# RSI is Argument 2 (live at entry), used across conditional branches and merge block. No call sites.
  .text
  .globl test_phase1b_multi_bb
  .type test_phase1b_multi_bb, @function
test_phase1b_multi_bb:
  cmpq $0, %rdi             # Entry BB: checking Arg 1 (%rdi)
  jg .Lthen
.Lelse:
  movq (%rsi), %rax         # Else BB: reading from RSI (%rsi)
  addq $10, %rax
  jmp .Lmerge
.Lthen:
  movq 8(%rsi), %rax        # Then BB: reading from RSI (%rsi)
  subq $5, %rax
.Lmerge:
  addq %rsi, %rax           # Merge BB: using RSI (%rsi)
  retq
