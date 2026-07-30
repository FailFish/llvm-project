# RUN: llvm-mc -filetype=obj -triple x86_64-unknown-unknown %s -o %t.o
# RUN: llvm-bolt-regres --spare-target-regs=R11 --spare-strategy=direct-swap -o %t.rewritten.o %t.o
# RUN: llvm-objdump -d %t.rewritten.o | FileCheck %s

# Test Fallback Reserved Register Eliminator:
# Target register R11 crosses a call site while direct-swap is specified.
# Physical GPR candidates cannot be allocated without callee-shift, so
# ReservedRegLoweringPass virtualizes R11 -> vreg_r11 and lowers to custom TLS base offset 40(%r15) -> 0x28(%r15)
# with scratch slot 32(%r15) -> 0x20(%r15) (0 stack frame accesses).

  .text
  .globl test_virt_reg_tls_fallback
  .type test_virt_reg_tls_fallback, @function
test_virt_reg_tls_fallback:
# CHECK-LABEL: <test_virt_reg_tls_fallback>:
  movq $0, %rax

.Lloop_header:
# CHECK:       movq %r10, 0x20(%r15)
# CHECK-NEXT:  movq $0xa, %r10
# CHECK-NEXT:  movq %r10, 0x28(%r15)
# CHECK-NEXT:  movq 0x20(%r15), %r10
  movq $10, %r11

# CHECK:       movq %r10, 0x20(%r15)
# CHECK-NEXT:  movq 0x28(%r15), %r10
# CHECK-NEXT:  cmpq $0x0, %r10
# CHECK-NEXT:  movq 0x20(%r15), %r10
  cmpq $0, %r11
  jle .Lexit

.Lloop_body:
  callq external_func
# CHECK:       movq %r10, 0x20(%r15)
# CHECK-NEXT:  movq 0x28(%r15), %r10
# CHECK-NEXT:  decq %r10
# CHECK-NEXT:  movq %r10, 0x28(%r15)
# CHECK-NEXT:  movq 0x20(%r15), %r10
  decq %r11
  jmp .Lloop_header

.Lexit:
  retq
