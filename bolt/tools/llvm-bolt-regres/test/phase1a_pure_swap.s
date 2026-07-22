# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-target-regs=RSI --spare-strategy=direct-swap %t.o 2>&1 | FileCheck %s

# CHECK: [SUCCESS_REALLOCATED] Reallocated RSI

# Multi-Basic-Block Test: DirectRegRealloc (0-cost local operand swap across Loop CFG)
# CFG: .Lentry -> .Lloop_header -> .Lloop_body -> .Lexit
# RSI is initialized inside function (dead at entry), used across loop body, and dead after .Lexit.
# No call sites inside function.
  .text
  .globl test_phase1a_multi_bb
  .type test_phase1a_multi_bb, @function
test_phase1a_multi_bb:
  movq $0, %rax             # Entry BB
  movq $10, %rsi            # Initializing RSI (dead at entry)
.Lloop_header:
  cmpq $0, %rsi             # Loop Header BB
  jle .Lexit
.Lloop_body:
  addq (%rdi, %rsi, 8), %rax # Loop Body BB: using RSI as index
  decq %rsi
  jmp .Lloop_header
.Lexit:
  retq                      # Exit BB
