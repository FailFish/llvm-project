; A frameless variadic musttail thunk forwards the staging area its own caller
; opened, because nothing moves the unsafe stack pointer between the thunk's
; entry and the forwarded call. Give the thunk a frame and that stops holding:
; its prologue publishes a lowered pointer, so the callee's entry-time value no
; longer names the area's base. A musttail call cannot be un-tail-called to
; repair it, hence the hard error.
;
; RUN: not --crash opt -safe-stack -safestack-vararg-position-convention -S \
; RUN:   -mtriple=x86_64-pc-linux-gnu < %s -o - 2>&1 | FileCheck %s
;
; Without the convention there is no staging area and the thunk is fine.
; RUN: opt -safe-stack -S -mtriple=x86_64-pc-linux-gnu < %s -o - \
; RUN:   | FileCheck %s --check-prefix=OFF

declare void @sink(i32, ...)
declare void @escape(ptr)

; CHECK: LLVM ERROR: musttail variadic call in 'framed_thunk', which needs an
; CHECK-SAME: unsafe stack frame, is incompatible with the SafeStack varargs
; CHECK-SAME: position convention

; OFF-LABEL: define void @framed_thunk
; OFF: __safestack_unsafe_stack_ptr
; OFF: musttail call void (i32, ...) @sink
define void @framed_thunk(i32 %a, ...) safestack {
  %buf = alloca [64 x i8], align 1
  call void @escape(ptr %buf)
  musttail call void (i32, ...) @sink(i32 %a, ...)
  ret void
}
