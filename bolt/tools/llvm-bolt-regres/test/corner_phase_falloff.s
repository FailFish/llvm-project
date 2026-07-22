# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-target-regs=RSI --spare-strategy=arg-eviction %t.o 2>&1 | FileCheck %s

# CHECK-NOT: [SUCCESS_REALLOCATED]

# Multi-Basic-Block Falloff Test: Strategy mismatch rejection (workload needs ArgCalleeRegRealloc)
# CFG: .Lentry -> .Lloop_header -> .Lcall_site -> ret
# Workload requires ArgCalleeRegRealloc (live at entry & crosses call), but --spare-strategy=arg-eviction is set.
  .text
  .globl test_corner_falloff_multi_bb
  .type test_corner_falloff_multi_bb, @function
test_corner_falloff_multi_bb:
  movq (%rsi), %rax         # Entry BB: RSI is Argument 2 (LiveAtEntry)
.Lloop:
  callq helper              # Call site: RSI live across callq!
  addq %rax, (%rsi)
  decq %rdi
  jnz .Lloop
  retq
