# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-target-regs=R14 --spare-strategy=callee-shift -o %t.rewritten.o %t.o
# RUN: llvm-objdump -d %t.rewritten.o | FileCheck %s

# Multi-Basic-Block Test: CalleeRegRealloc (Callee-Saved Shift across Branching CFG with Call & Multiple Returns)
# CFG: .Lentry -> .Learly_exit (ret) / .Lmain_path (call helper) -> .Lexit (ret)
# R14 is initialized after entry BB (!LiveAtEntry), but spans across callq helper in .Lmain_path.
# Requires callee-saved RBX shift + push %rbx at entry & pop %rbx at both return exits.
  .text
  .globl test_callee_shift
  .type test_callee_shift, @function
test_callee_shift:
# CHECK-LABEL: <test_callee_shift>:
# CHECK-NEXT:  pushq %rbx
# CHECK-NEXT:  cmpq $0x0, %rdi
# CHECK-NEXT:  je {{.*}}
  cmpq $0, %rdi             # Entry BB (does not touch R14)
  je .Learly_exit

.Lmain_path:
# CHECK:       movq $0x64, %rbx
# CHECK-NEXT:  callq {{.*}}
# CHECK-NEXT:  addq %rbx, %rax
# CHECK-NEXT:  popq %rbx
# CHECK-NEXT:  retq
  movq $100, %r14           # Main path BB: R14 initialized here
  callq helper              # Call site: R14 live across call!
  addq %r14, %rax           # Main path BB: using R14 after call
  retq                      # Exit 1 (main ret)

.Learly_exit:
# CHECK:       xorq %rax, %rax
# CHECK-NEXT:  popq %rbx
# CHECK-NEXT:  retq
  xorq %rax, %rax           # Early Exit BB
  retq                      # Exit 2 (early ret)
