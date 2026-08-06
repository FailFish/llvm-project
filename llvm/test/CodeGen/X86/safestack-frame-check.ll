; The positive side of the SP-relative frame check: code the policy accepts
; still compiles, and produces the stack-pointer-relative accesses the policy
; promised. The negative cases are handwritten .mir, because an alloca whose
; address escapes is moved to the unsafe stack by the SafeStack pass and so
; never reaches the check.
;
; The check is only scheduled on an LFI triple, which is the only target that
; turns the policy on, so that is what these run under. The policy itself is
; forced by the hidden flag rather than by the feature, so that this stays a
; test of the check and not of L4's gating.
;
; RUN: llc -mtriple=x86_64_lfi-linux-musl -safe-stack-force-sp-relative < %s \
; RUN:   | FileCheck %s
;
; With the policy off the check does not run, and classic SafeStack keeps the
; same allocas native for its own reasons.
; RUN: llc -mtriple=x86_64_lfi-linux-musl < %s | FileCheck %s --check-prefix=OFF

declare void @escape(ptr)

; Constant-offset accesses fold into [rsp + disp], so this stays native.
;
; CHECK-LABEL: kept_native:
; CHECK: movl $7, -8(%rsp)
; CHECK-NOT: __safestack_unsafe_stack_ptr
; OFF-LABEL: kept_native:
; OFF: movl $7, -8(%rsp)
define i32 @kept_native() safestack {
  %p = alloca [4 x i32], align 4
  %q = getelementptr [4 x i32], ptr %p, i32 0, i32 2
  store i32 7, ptr %q
  %v = load i32, ptr %q
  ret i32 %v
}

; An address that escapes is moved to the unsafe stack by the pass, so the
; check never sees it. This is why the violations have to be written in MIR.
;
; CHECK-LABEL: address_escapes:
; CHECK: __safestack_unsafe_stack_ptr
define void @address_escapes() safestack {
  %p = alloca [4 x i32], align 4
  call void @escape(ptr %p)
  ret void
}

; A variable index cannot fold into a constant displacement, so the predicate
; evicts it too -- the check and the predicate agree on the same boundary.
;
; CHECK-LABEL: variable_index:
; CHECK: __safestack_unsafe_stack_ptr
define i32 @variable_index(i32 %i) safestack {
  %p = alloca [4 x i32], align 4
  %q = getelementptr [4 x i32], ptr %p, i32 0, i32 %i
  %v = load i32, ptr %q
  ret i32 %v
}

; A function without the attribute is not instrumented at all, so the policy
; never applies and its allocas are nobody's business.
;
; CHECK-LABEL: uninstrumented:
; CHECK-NOT: __safestack_unsafe_stack_ptr
define void @uninstrumented() {
  %p = alloca [4 x i32], align 4
  call void @escape(ptr %p)
  ret void
}
