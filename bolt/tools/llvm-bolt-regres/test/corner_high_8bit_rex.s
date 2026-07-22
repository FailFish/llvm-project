# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-regs --spare-target-regs=RSI --spare-strategy=all %t.o 2>&1 | FileCheck %s

# CHECK: Running DirectRegRealloc

# Multi-Basic-Block Corner Case Test: High 8-bit Register REX Blacklisting
# CFG: .Lentry (%ah usage) -> .Lbranch1 / .Lbranch2 -> .Lexit
# High 8-bit register %ah used in entry block. Prevents any REX-prefix register (R8-R15, SIL, DIL)
# from being used as replacement candidate anywhere in the function.
  .text
  .globl test_corner_high_8bit_multi_bb
  .type test_corner_high_8bit_multi_bb, @function
test_corner_high_8bit_multi_bb:
  movb %ah, %al             # Entry BB: High 8-bit %ah usage (NO REX allowed!)
  movq $42, %rsi            # Initializing RSI
  cmpb $0, %al
  je .Lelse
.Lthen:
  addq $1, %rsi
  jmp .Lexit
.Lelse:
  addq $2, %rsi
.Lexit:
  movq (%rsi), %rax         # Exit BB: using RSI
  retq
