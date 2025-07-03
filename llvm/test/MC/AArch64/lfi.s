# RUN: llvm-mc -filetype=obj -triple=aarch64-lfi-linux-musl %s \
# RUN:   | llvm-objdump --no-print-imm-hex -d -z --no-show-raw-insn - | FileCheck %s

.align 4
test_br:
	br x0
	blr x1
	ret
	ret x2
# CHECK-LABEL: <test_br>:
# CHECK:       add x18, x21, w0, uxtw
# CHECK-NEXT:  br x18
# CHECK-NEXT:  add x18, x21, w1, uxtw
# CHECK-NEXT:  blr x18
# CHECK-NEXT:  ret
# CHECK-NEXT:  add x18, x21, w2, uxtw
# CHECK-NEXT:  ret x18

test_sys:
	svc #0
	mrs x0, tpidr_el0
	msr tpidr_el0, x0
# CHECK-LABEL: <test_sys>:
# CHECK:       mov x22, x30
# CHECK-NEXT:  ldr x30, [x21]
# CHECK-NEXT:  blr x30
# CHECK-NEXT:  add x30, x21, w22, uxtw
# CHECK-NEXT:  mov x22, x30
# CHECK-NEXT:  ldr x30, [x21, #8]
# CHECK-NEXT:  blr x30
# CHECK-NEXT:  add x30, x21, w22, uxtw
# CHECK-NEXT:  mov x22, x30
# CHECK-NEXT:  ldr x30, [x21, #16]
# CHECK-NEXT:  blr x30
# CHECK-NEXT:  add x30, x21, w22, uxtw

test_spmod:
	add sp, sp, #8
	sub sp, sp, #8
	add sp, sp, x0
	sub sp, sp, x0
	mov sp, x0
	sub sp, sp, #1, lsl #12
# CHECK-LABEL: <test_spmod>:
# CHECK:       add x22, sp, #8
# CHECK-NEXT:  add sp, x21, w22, uxtw
# CHECK-NEXT:  sub x22, sp, #8
# CHECK-NEXT:  add sp, x21, w22, uxtw
# CHECK-NEXT:  add x22, sp, x0
# CHECK-NEXT:  add sp, x21, w22, uxtw
# CHECK-NEXT:  sub x22, sp, x0
# CHECK-NEXT:  add sp, x21, w22, uxtw
# CHECK-NEXT:  add sp, x21, w0, uxtw
# CHECK-NEXT:  sub x22, sp, #1, lsl #12
# CHECK-NEXT:  add sp, x21, w22, uxtw

test_ramod:
	mov x30, x29
	ldr x30, [sp]
	ldr x30, [sp], #8
	ldp x29, x30, [sp], #16
	ldp x30, x29, [sp], #16
# CHECK-LABEL: <test_ramod>:
# CHECK:       add x30, x21, w29, uxtw
# CHECK-NEXT:  ldr x22, [sp]
# CHECK-NEXT:  add x30, x21, w22, uxtw
# CHECK-NEXT:  ldr x22, [sp], #8
# CHECK-NEXT:  add x30, x21, w22, uxtw
# CHECK-NEXT:  ldp x29, x22, [sp], #16
# CHECK-NEXT:  add x30, x21, w22, uxtw
# CHECK-NEXT:  ldp x22, x29, [sp], #16
# CHECK-NEXT:  add x30, x21, w22, uxtw

test_norewrite:
	ldr x0, [x18]
	str x0, [x18]
	br x18
	blr x18
	add x18, x21, w0, uxtw
	ldr x0, [sp]
	ldr x0, [sp, #16]
	ldr x0, [sp, #8]!
	ldr x0, [sp], #8
# CHECK-LABEL: <test_norewrite>:
# CHECK:       ldr x0, [x18]
# CHECK-NEXT:  str x0, [x18]
# CHECK-NEXT:  br x18
# CHECK-NEXT:  blr x18
# CHECK-NEXT:  add x18, x21, w0, uxtw
# CHECK-NEXT:  ldr x0, [sp]
# CHECK-NEXT:  ldr x0, [sp, #16]
# CHECK-NEXT:  ldr x0, [sp, #8]!
# CHECK-NEXT:  ldr x0, [sp], #8

test_x21_load:
  ldr x30, [x21, #0]
  ldr x30, [x21, #8]
  ldr x30, [x21, #256]

test_mem_basic:
	ldr x0, [x1]
	str x0, [x2]
	ldr x0, [x1, #16]
	ldr x0, [x1, #16]!
	ldr x0, [x1], #16
	ldr x0, [x1, #-8]!
	ldr x0, [x1], #-8
# CHECK-LABEL: <test_mem_basic>:
# CHECK:       ldr x0, [x21, w1, uxtw]
# CHECK-NEXT:  str x0, [x21, w2, uxtw]
# CHECK-NEXT:  add x18, x21, w1, uxtw
# CHECK-NEXT:  ldr x0, [x18, #16]
# CHECK-NEXT:  add x1, x1, #16
# CHECK-NEXT:  ldr x0, [x21, w1, uxtw]
# CHECK-NEXT:  ldr x0, [x21, w1, uxtw]
# CHECK-NEXT:  add x1, x1, #16
# CHECK-NEXT:  sub x1, x1, #8
# CHECK-NEXT:  ldr x0, [x21, w1, uxtw]
# CHECK-NEXT:  ldr x0, [x21, w1, uxtw]
# CHECK-NEXT:  sub x1, x1, #8

test_mem:
	ld1 { v0.s }[1], [x8]
	ld1r { v3.2d }, [x9]
	ld1 { v0.s }[1], [x8], x10
# CHECK-LABEL: <test_mem>:
# CHECK:       add x18, x21, w8, uxtw
# CHECK-NEXT:  ld1 { v0.s }[1], [x18]
# CHECK-NEXT:  add x18, x21, w9, uxtw
# CHECK-NEXT:  ld1r { v3.2d }, [x18]
# CHECK-NEXT:  add x18, x21, w8, uxtw
# CHECK-NEXT:  ld1 { v0.s }[1], [x18]
# CHECK-NEXT:  add x8, x8, x10

test_mem_pair:
	ldp x0, x1, [x2]
	stp x0, x1, [x2]
	ldp x0, x1, [x2, #16]
	ldp x0, x1, [x2, #16]!
	ldp x0, x1, [x2], #16
	ldp w0, w1, [x2, #16]!
	ldp w0, w1, [x2], #16
	ldaxp x0, x1, [x2]
	ldxp x0, x1, [x2]
	stxp w0, x1, x2, [x3]
	stlxp w0, x1, x2, [x3]
# CHECK-LABEL: <test_mem_pair>:
# CHECK:       add x18, x21, w2, uxtw
# CHECK-NEXT:  ldp x0, x1, [x18]

# CHECK-NEXT:  add x18, x21, w2, uxtw
# CHECK-NEXT:  stp x0, x1, [x18]

# CHECK-NEXT:  add x18, x21, w2, uxtw
# CHECK-NEXT:  ldp x0, x1, [x18, #16]

# CHECK-NEXT:  add x18, x21, w2, uxtw
# CHECK-NEXT:  ldp x0, x1, [x18, #16]
# CHECK-NEXT:  add x2, x2, #16

# CHECK-NEXT:  add x18, x21, w2, uxtw
# CHECK-NEXT:  ldp x0, x1, [x18]
# CHECK-NEXT:  add x2, x2, #16

# CHECK-NEXT:  add x18, x21, w2, uxtw
# CHECK-NEXT:  ldp w0, w1, [x18, #16]
# CHECK-NEXT:  add x2, x2, #16

# CHECK-NEXT:  add x18, x21, w2, uxtw
# CHECK-NEXT:  ldp w0, w1, [x18]
# CHECK-NEXT:  add x2, x2, #16

# CHECK-NEXT:  add x18, x21, w2, uxtw
# CHECK-NEXT:  ldaxp x0, x1, [x18]

# CHECK-NEXT:  add x18, x21, w2, uxtw
# CHECK-NEXT:  ldxp x0, x1, [x18]

# CHECK-NEXT:  add x18, x21, w3, uxtw
# CHECK-NEXT:  stxp w0, x1, x2, [x18]

# CHECK-NEXT:  add x18, x21, w3, uxtw
# CHECK-NEXT:  stlxp w0, x1, x2, [x18]

test_neon:
  ld1 { v0.b }[1], [x0]
  ld1 { v0.b }[1], [x0], x1
  ld1 { v0.b }[1], [x0], #1
  ld3 {v0.2d-v2.2d}, [x2]
  ld3 {v0.2d-v2.2d}, [x16], x1
  ld3 {v0.2d-v2.2d}, [x16], #48
  ld4 {v0.2s-v3.2s}, [x0], #32
  ld4 {v0.2s-v3.2s}, [sp], #32
  st2 {v0.8b-v1.8b}, [x1], #16
  st2 { v0.s, v1.s }[3], [x0], #8
  st2 { v0.s, v1.s }[3], [sp], #8
  LD1R { v0.8b }, [x0]
  LD1R { v0.8b }, [x0], #1
  LD1R { v0.4h }, [x0], #2
  LD1R { v0.2s }, [x0], #4
  LD1R { v0.1d }, [x0], #8
  LD1R { v0.8b }, [x0], x1

# CHECK-LABEL: <test_neon>:
# CHECK:      add     x18, x21, w0, uxtw
# CHECK-NEXT: ld1     { v0.b }[1], [x18]

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: ld1     { v0.b }[1], [x18]
# CHECK-NEXT: add     x0, x0, x1

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: ld1     { v0.b }[1], [x18]
# CHECK-NEXT: add     x0, x0, #0x1

# CHECK-NEXT: add     x18, x21, w2, uxtw
# CHECK-NEXT: ld3     { v0.2d, v1.2d, v2.2d }, [x18]

# CHECK-NEXT: add     x18, x21, w16, uxtw
# CHECK-NEXT: ld3     { v0.2d, v1.2d, v2.2d }, [x18]
# CHECK-NEXT: add     x16, x16, x1

# CHECK-NEXT: add     x18, x21, w16, uxtw
# CHECK-NEXT: ld3     { v0.2d, v1.2d, v2.2d }, [x18]
# CHECK-NEXT: add     x16, x16, #0x30

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: ld4     { v0.2s, v1.2s, v2.2s, v3.2s }, [x18]
# CHECK-NEXT: add     x0, x0, #0x20

# CHECK-NEXT: ld4     { v0.2s, v1.2s, v2.2s, v3.2s }, [sp], #32

# CHECK-NEXT: add     x18, x21, w1, uxtw
# CHECK-NEXT: st2     { v0.8b, v1.8b }, [x18]
# CHECK-NEXT: add     x1, x1, #0x10

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: st2     { v0.s, v1.s }[3], [x18]
# CHECK-NEXT: add     x0, x0, #0x8

# CHECK-NEXT: st2     { v0.s, v1.s }[3], [sp], #8

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: ld1r    { v0.8b }, [x18]

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: ld1r    { v0.8b }, [x18]
# CHECK-NEXT: add     x0, x0, #0x1

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: ld1r    { v0.4h }, [x18]
# CHECK-NEXT: add     x0, x0, #0x2

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: ld1r    { v0.2s }, [x18]
# CHECK-NEXT: add     x0, x0, #0x4

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: ld1r    { v0.1d }, [x18]
# CHECK-NEXT: add     x0, x0, #0x8

# CHECK-NEXT: add     x18, x21, w0, uxtw
# CHECK-NEXT: ld1r    { v0.8b }, [x18]
# CHECK-NEXT: add     x0, x0, x1
