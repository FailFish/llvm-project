; Lowering of the SafeStack varargs handshake intrinsics. The register save
; area is addressed through the %slot operand instead of a frame object, so no
; safe-stack address reaches the va_list.
;
; RUN: llc -mtriple=x86_64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=x86_64-unknown-linux-gnu -O0 < %s | FileCheck %s --check-prefix=O0

declare void @llvm.safestack.vararg.save.regs(ptr)
declare void @llvm.va_start.safestack.p0(ptr, ptr, ptr)
declare void @llvm.va_start.p0(ptr)

; %ap, %slot and %base take rdi, rsi and rdx, so the variadic GPRs start at
; rcx and gp_offset is 3*8 = 24.

; CHECK-LABEL: f:
; The spills go through the slot pointer in %rsi, not through a frame index.
; CHECK-DAG: movq %rcx, 24(%rsi)
; CHECK-DAG: movq %r8, 32(%rsi)
; CHECK-DAG: movq %r9, 40(%rsi)
; The XMM spills stay gated on %al.
; CHECK: testb %al, %al
; CHECK: je
; CHECK-DAG: movaps %xmm0, 48(%rsi)
; CHECK-DAG: movaps %xmm7, 160(%rsi)
; reg_save_area (offset 16 in the va_list) is the slot.
; CHECK: movq %rsi, 16(%rdi)

; The same holds without optimization, where the pseudo is expanded by
; FastRegAlloc rather than the usual allocator.
; O0-LABEL: f:
; O0: testb %al, %al
; O0: movq %rsi, 16(%rdi)
define void @f(ptr %ap, ptr %slot, ptr %base, ...) safestack {
entry:
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  call void @llvm.va_start.safestack.p0(ptr %ap, ptr %slot, ptr %base)
  ret void
}

; overflow_arg_area (offset 8) still points into the caller's frame: Phase 1
; leaves the position convention off, so %base is ignored.
; CHECK-LABEL: overflow_area_is_caller_frame:
; CHECK: leaq {{[0-9]+}}(%rsp), %[[OVF:[a-z0-9]+]]
; CHECK: movq %[[OVF]], 8(%rdi)
define void @overflow_area_is_caller_frame(ptr %ap, ptr %slot, ptr %base, ...) safestack {
entry:
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  call void @llvm.va_start.safestack.p0(ptr %ap, ptr %slot, ptr %base)
  ret void
}

; va_start.safestack may appear in a block other than the entry block; only
; save.regs is pinned there.
; CHECK-LABEL: va_start_in_successor:
; CHECK: movq %rcx, 24(%rsi)
; CHECK: movq %rsi, 16(%rdi)
define void @va_start_in_successor(ptr %ap, ptr %slot, i1 %c, ...) safestack {
entry:
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  br i1 %c, label %do, label %skip

do:
  call void @llvm.va_start.safestack.p0(ptr %ap, ptr %slot, ptr null)
  ret void

skip:
  ret void
}

; Without SSE the save area is 48 bytes of GPRs only: no %al gate and no
; VASTART_SAVE_XMM_REGS pseudo, matching getVarArgsSaveAreaInfo's smaller size.
; CHECK-LABEL: nosse:
; CHECK: movq %rcx, 24(%rsi)
; CHECK-NOT: testb %al, %al
; CHECK-NOT: movaps
; CHECK: movq %rsi, 16(%rdi)
define void @nosse(ptr %ap, ptr %slot, ptr %base, ...) safestack nounwind
    "target-features"="-sse,-sse2" {
entry:
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  call void @llvm.va_start.safestack.p0(ptr %ap, ptr %slot, ptr %base)
  ret void
}

; musttail variadic forwarding uses the register-forwarding path: no save area,
; no va_start, so the handshake never engages and lowering is unaffected.
declare void @callee(ptr, ...)

; CHECK-LABEL: musttail_thunk:
; CHECK-NOT: movq %r{{[a-z0-9]+}}, {{[0-9]+}}(%rsi)
; CHECK: jmp callee
define void @musttail_thunk(ptr %p, ...) safestack nounwind {
entry:
  musttail call void (ptr, ...) @callee(ptr %p, ...)
  ret void
}

; Without the handshake, lowering is untouched. This is the behaviour the
; feature exists to change: the save area is a native frame object, so the
; spills are rsp-relative and reg_save_area is a raw safe-stack address
; materialised by an leaq -- which is what must not end up in a va_list.
; CHECK-LABEL: plain_va_start:
; CHECK: subq ${{[0-9]+}}, %rsp
; CHECK: movq %rsi, -{{[0-9]+}}(%rsp)
; CHECK: leaq -{{[0-9]+}}(%rsp), %[[RSA:[a-z0-9]+]]
; CHECK: movq %[[RSA]], 16(%rdi)
define void @plain_va_start(ptr %ap, ...) {
entry:
  call void @llvm.va_start.p0(ptr %ap)
  ret void
}
