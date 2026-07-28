# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-target-regs=R11 --spare-strategy=direct-swap -o %t.rewritten.o %t.o
# RUN: llvm-objdump -d %t.rewritten.o | FileCheck %s

# Multi-Basic-Block Test: DirectRegRealloc Candidate Types (0-cost local operand swap)

# Case 1: Volatile Candidate Swap (R11 -> RCX)
# R11 is dead at entry, used locally across loop body.
# Reallocated to unused volatile register RCX at 0 cost.
  .text
  .globl test_direct_swap_volatile
  .type test_direct_swap_volatile, @function
test_direct_swap_volatile:
# CHECK-LABEL: <test_direct_swap_volatile>:
# CHECK-NEXT:  movq $0x0, %rax
  movq $0, %rax             # Entry BB (does not touch R11)

.Lloop_header1:
# CHECK:       movq $0xa, %rcx
# CHECK-NEXT:  cmpq $0x0, %rcx
# CHECK-NEXT:  jle {{.*}}
  movq $10, %r11            # R11 initialized here
  cmpq $0, %r11
  jle .Lexit1

.Lloop_body1:
# CHECK:       addq (%rdi,%rcx,8), %rax
# CHECK-NEXT:  decq %rcx
# CHECK-NEXT:  jmp {{.*}}
  addq (%rdi, %r11, 8), %rax
  decq %r11
  jmp .Lloop_header1

.Lexit1:
# CHECK:       retq
  retq

# Case 2: Self-Preserving Pre-Saved Swap (R11 has prologue pushq %r11 / epilogue popq %r11)
# R11 is pre-saved in entry block. Self-preserving reallocation renames R11 -> RBX at 0 cost and updates popq in place.
  .globl test_direct_swap_presaved
  .type test_direct_swap_presaved, @function
test_direct_swap_presaved:
# CHECK-LABEL: <test_direct_swap_presaved>:
# CHECK-NEXT:  pushq %rbx
  pushq %r11                # Entry BB: pre-saved R11

.Lloop_header2:
# CHECK:       movq $0xa, %rbx
# CHECK-NEXT:  cmpq $0x0, %rbx
# CHECK-NEXT:  jle {{.*}}
  movq $10, %r11            # R11 initialized here
  cmpq $0, %r11
  jle .Lexit2

.Lloop_body2:
# CHECK:       addq (%rdi,%rbx,8), %rax
# CHECK-NEXT:  decq %rbx
# CHECK-NEXT:  jmp {{.*}}
  addq (%rdi, %r11, 8), %rax
  decq %r11
  jmp .Lloop_header2

.Lexit2:
# CHECK:       popq %rbx
# CHECK-NEXT:  retq
  popq %r11                 # Epilogue: restoring R11 -> popq %rbx
  retq
