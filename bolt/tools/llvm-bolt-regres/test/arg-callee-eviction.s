# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-target-regs=R15 --spare-strategy=arg-callee-eviction -o %t.rewritten.o %t.o
# RUN: llvm-objdump -d %t.rewritten.o | FileCheck %s

# Multi-Basic-Block Test: ArgCalleeRegRealloc (Argument Callee Eviction across Loop with Calls & Multiple Returns)
# CFG: .Lentry -> .Lloop_header -> .Lcall_site -> .Learly_err (ret) / .Lloop_latch -> .Lexit (ret)
# R15 is read before write in entry BB (LiveAtEntry) AND spans across callq helper inside loop.
# Requires push %r12 + mov %r12, %r15 at entry, and pop %r12 at both exit blocks (.Learly_err & .Lexit).
  .text
  .globl test_arg_callee_eviction
  .type test_arg_callee_eviction, @function
test_arg_callee_eviction:
# CHECK-LABEL: <test_arg_callee_eviction>:
# CHECK-NEXT:  pushq %r12
# CHECK-NEXT:  movq %r15, %r12
# CHECK-NEXT:  movq $0x0, %rbx
  movq $0, %rbx             # Entry BB: R15 read before write (live at entry)

.Lloop_header:
# CHECK:       cmpq $0x0, (%r12)
# CHECK-NEXT:  je {{.*}}
  cmpq $0, (%r15)           # Loop Header BB: checking dereferenced R15
  je .Learly_err

.Lcall_site:
# CHECK:       callq {{.*}}
# CHECK-NEXT:  addq %rax, (%r12)
# CHECK-NEXT:  incq %rbx
# CHECK-NEXT:  cmpq $0x5, %rbx
# CHECK-NEXT:  jl {{.*}}
  callq helper              # Call site: R15 live across call!
  addq %rax, (%r15)         # Latch BB: modifying dereferenced R15
  incq %rbx
  cmpq $5, %rbx
  jl .Lloop_header

.Lexit:
# CHECK:       movq %rbx, %rax
# CHECK-NEXT:  popq %r12
# CHECK-NEXT:  retq
  movq %rbx, %rax           # Normal Exit BB
  retq                      # Exit 1 (normal ret)

.Learly_err:
# CHECK:       movq $-0x1, %rax
# CHECK-NEXT:  popq %r12
# CHECK-NEXT:  retq
  movq $-1, %rax            # Error Exit BB
  retq                      # Exit 2 (error ret)
