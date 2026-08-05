; Under the varargs position convention the backend bumps the unsafe stack
; pointer around each variadic call, so a function containing one needs the
; pass's restore-point machinery even when it has nothing of its own on the
; unsafe stack.
;
; RUN: opt -safe-stack -S -mtriple=x86_64-pc-linux-gnu < %s -o - \
; RUN:   | FileCheck %s --check-prefix=OFF
; RUN: opt -safe-stack -safestack-vararg-position-convention -S \
; RUN:   -mtriple=x86_64-pc-linux-gnu < %s -o - | FileCheck %s

declare void @sink(i32, ...)
declare void @plain(i32)
declare void @cleanup()
declare i32 @__gxx_personality_v0(...)

; A frameless caller that unwinds past a variadic call must not leave the bump
; in place: each catch-and-retry iteration would leak it, without bound. The
; landing pad gets a restore point putting the entry-time pointer back.
; Nothing here allocates, so there is no frame -- only the base pointer and
; the restores.
;
; The landing pad is a restore point in its own right, so this shape is
; already handled with the convention off. It is checked here because the
; convention is what makes the restore load-bearing rather than incidental.
;
; CHECK-LABEL: define void @frameless_invoke
; CHECK: %[[BASE:.*]] = load ptr, ptr @__safestack_unsafe_stack_ptr
; CHECK-NOT: getelementptr
; CHECK: invoke void (i32, ...) @sink
; CHECK: store ptr %[[BASE]], ptr @__safestack_unsafe_stack_ptr
; CHECK: landingpad
; CHECK-NEXT: cleanup
; CHECK-NEXT: store ptr %[[BASE]], ptr @__safestack_unsafe_stack_ptr
;
; OFF-LABEL: define void @frameless_invoke
; OFF: %[[OBASE:.*]] = load ptr, ptr @__safestack_unsafe_stack_ptr
; OFF: landingpad
; OFF-NEXT: cleanup
; OFF-NEXT: store ptr %[[OBASE]], ptr @__safestack_unsafe_stack_ptr
define void @frameless_invoke(i32 %a) safestack personality ptr @__gxx_personality_v0 {
  invoke void (i32, ...) @sink(i32 %a, i64 1, i64 2, i64 3, i64 4, i64 5,
                               i64 6, i64 7)
          to label %ok unwind label %lpad
ok:
  ret void
lpad:
  %l = landingpad { ptr, i32 } cleanup
  call void @cleanup()
  resume { ptr, i32 } %l
}

; A variadic call with no restore point of its own is what the convention adds
; to the pass's trigger. The backend already un-bumps on the normal return
; path, so the store this produces at the return is redundant; the trigger is
; there to keep the rule "a function containing a variadic call is a function
; the pass has processed" true without depending on which shapes happen to
; have a landing pad.
;
; CHECK-LABEL: define void @frameless_call
; CHECK: %[[BASE:.*]] = load ptr, ptr @__safestack_unsafe_stack_ptr
; CHECK: call void (i32, ...) @sink
; CHECK-NEXT: store ptr %[[BASE]], ptr @__safestack_unsafe_stack_ptr
;
; OFF-LABEL: define void @frameless_call
; OFF-NOT: __safestack_unsafe_stack_ptr
define void @frameless_call(i32 %a) safestack {
  call void (i32, ...) @sink(i32 %a, i64 1, i64 2, i64 3, i64 4, i64 5,
                             i64 6, i64 7)
  ret void
}

; A musttail variadic call is never bumped -- the callee is meant to see the
; area this function's own caller staged -- so there is nothing to restore and
; the thunk is left alone in both modes.
;
; CHECK-LABEL: define void @musttail_thunk
; CHECK-NOT: __safestack_unsafe_stack_ptr
; OFF-LABEL: define void @musttail_thunk
; OFF-NOT: __safestack_unsafe_stack_ptr
define void @musttail_thunk(i32 %a, ...) safestack {
  musttail call void (i32, ...) @sink(i32 %a, ...)
  ret void
}

; A call to a variadic function is what triggers this, not a call in a
; variadic function: nothing is staged for a non-variadic callee.
;
; CHECK-LABEL: define void @nonvariadic_call
; CHECK-NOT: __safestack_unsafe_stack_ptr
; OFF-LABEL: define void @nonvariadic_call
; OFF-NOT: __safestack_unsafe_stack_ptr
define void @nonvariadic_call(i32 %a) safestack {
  call void @plain(i32 %a)
  ret void
}

; A caller with an unsafe frame of its own is processed either way; the
; convention adds nothing to it, because the frame already carries the base
; pointer and the restores the bump relies on.
;
; CHECK-LABEL: define void @framed_caller
; CHECK: %[[BASE:.*]] = load ptr, ptr @__safestack_unsafe_stack_ptr
; CHECK: getelementptr i8, ptr %[[BASE]], i32 -16
; OFF-LABEL: define void @framed_caller
; OFF: %[[OBASE:.*]] = load ptr, ptr @__safestack_unsafe_stack_ptr
; OFF: getelementptr i8, ptr %[[OBASE]], i32 -16
define void @framed_caller(i32 %a) safestack {
  %buf = alloca [16 x i8], align 1
  call void (i32, ...) @sink(i32 %a, ptr %buf)
  ret void
}
