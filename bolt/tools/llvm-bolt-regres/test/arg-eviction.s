# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-target-regs=R11 --spare-strategy=arg-eviction -o %t.rewritten.o %t.o
# RUN: llvm-objdump -d %t.rewritten.o | FileCheck %s

# Multi-Basic-Block Test: ArgRegRealloc (Volatile Entry Eviction across Branching CFG)
# CFG: .Lentry -> .Lthen / .Lelse -> .Lmerge -> ret
# R11 is read before write in entry BB (live at entry), used across conditional branches and merge block. No call sites.
  .text
  .globl test_arg_eviction
  .type test_arg_eviction, @function
test_arg_eviction:
# CHECK-LABEL: <test_arg_eviction>:
# CHECK-NEXT:  movq %r11, %r10
# CHECK-NEXT:  cmpq $0x0, %rdi
# CHECK-NEXT:  jg {{.*}}
  cmpq $0, %rdi             # Entry BB: R11 read before write (live at entry)
  jg .Lthen

.Lelse:
# CHECK:       movq (%r10), %rax
# CHECK-NEXT:  addq $0xa, %rax
# CHECK-NEXT:  jmp {{.*}}
  movq (%r11), %rax         # Else BB: reading from R11
  addq $10, %rax
  jmp .Lmerge

.Lthen:
# CHECK:       movq 0x8(%r10), %rax
# CHECK-NEXT:  subq $0x5, %rax
  movq 8(%r11), %rax        # Then BB: reading from R11
  subq $5, %rax

.Lmerge:
# CHECK:       addq %r10, %rax
# CHECK-NEXT:  retq
  addq %r11, %rax           # Merge BB: using R11
  retq
