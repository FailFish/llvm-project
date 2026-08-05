; The SafeStack varargs position convention: an instrumented caller stages a
; variadic call's stack arguments on its own unsafe stack and leaves the unsafe
; stack pointer bumped past them, so the callee's entry-time value is the base
; of the area and its va_list holds no safe-stack address at all.
;
; RUN: llc -mtriple=x86_64-unknown-linux-gnu -safestack-vararg-position-convention < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=x86_64-unknown-linux-gnu -safestack-vararg-position-convention -O0 < %s \
; RUN:   | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu < %s | FileCheck %s --check-prefix=OFF

declare void @sink(i32, ...)
declare void @sink7(i32, i32, i32, i32, i32, i32, i32, ...)
declare void @escape(ptr)
declare void @llvm.va_start.p0(ptr)

; The three arguments past the six integer registers are staged on the unsafe
; stack. The bumped pointer reaches the home cell BEFORE any of them is
; written: a signal handler running in between then allocates its unsafe frames
; below the area rather than on top of the staged arguments.
;
; CHECK-LABEL: bump:
; CHECK: leaq -32([[T0:%r[a-z0-9]+]]), [[BUMPED:%r[a-z0-9]+]]
; CHECK-NEXT: movq [[BUMPED]], %fs:([[HOME:%[a-z0-9]+]])
; CHECK-NEXT: movq $8, -16([[T0]])
; CHECK-NEXT: movq $7, -24([[T0]])
; CHECK-NEXT: movq $6, -32([[T0]])
; CHECK: callq sink@PLT
; CHECK-NEXT: movq {{%r[a-z0-9]+}}, %fs:([[HOME]])
;
; The same without optimization, where FastISel would otherwise lower the call
; itself and write the arguments to the native outgoing area. It has to hand
; an in-mode variadic call to SelectionDAG, which is the only place the
; staging exists.
;
; O0-LABEL: bump:
; O0: addq $-32, [[BUMPED:%r[a-z0-9]+]]
; O0-NEXT: movq [[BUMPED]], %fs:({{%[a-z0-9]+}})
; O0-NEXT: movq $8, -16([[T0:%r[a-z0-9]+]])
; O0-NEXT: movq $7, -24([[T0]])
; O0-NEXT: movq $6, -32([[T0]])
; O0-NOT: (%rsp)
; O0: callq sink@PLT
;
; With the convention off the same call writes its stack arguments to the
; native outgoing area and never touches the unsafe stack pointer.
;
; OFF-LABEL: bump:
; OFF-NOT: __safestack_unsafe_stack_ptr
; OFF: pushq $8
; OFF: pushq $7
; OFF: pushq $6
; OFF: callq sink@PLT
define void @bump(i32 %a) safestack {
  call void (i32, ...) @sink(i32 %a, i64 1, i64 2, i64 3, i64 4, i64 5,
                             i64 6, i64 7, i64 8)
  ret void
}

; The value put back after the call is the one the bump displaced, not the
; function's entry-time pointer. Here the two differ: this caller has an unsafe
; frame of its own, so the home holds its static top across the call.
;
; CHECK-LABEL: framed_restore:
; Entry: base in [[BASE]], static top 64 bytes below it published to the home.
; CHECK: movq %fs:([[HOME:%[a-z0-9]+]]), [[BASE:%r[a-z0-9]+]]
; CHECK-NEXT: leaq -64([[BASE]]), [[TOP:%r[a-z0-9]+]]
; CHECK-NEXT: movq [[TOP]], %fs:([[HOME]])
; The bump displaces the static top and restores exactly it.
; CHECK: movq %fs:([[HOME]]), [[SAVED:%r[a-z0-9]+]]
; CHECK-NEXT: leaq -16([[SAVED]]), [[BUMPED:%r[a-z0-9]+]]
; CHECK-NEXT: movq [[BUMPED]], %fs:([[HOME]])
; CHECK: callq sink@PLT
; CHECK-NEXT: movq [[SAVED]], %fs:([[HOME]])
; Only at the return does the entry-time base go back.
; CHECK: movq [[BASE]], %fs:([[HOME]])
define void @framed_restore(i32 %a) safestack {
  %buf = alloca [64 x i8], align 1
  call void @escape(ptr %buf)
  call void (i32, ...) @sink(i32 %a, i64 1, i64 2, i64 3, i64 4, i64 5,
                             i64 6, i64 7)
  call void @escape(ptr %buf)
  ret void
}

; Named stack arguments are unchanged: the seventh int still goes in the native
; outgoing area, and only the variadic block moves.
;
; Those four bytes would leave the variadic block at offset 8, but the block is
; padded to 16 first. Without the padding the caller would put the long double
; at base+8 while the callee's va_arg -- which rounds overflow_arg_area up to
; 16 for over-aligned types -- looked for it at base+0.
;
; CHECK-LABEL: named_stack_arg:
; CHECK: leaq -32([[T0:%r[a-z0-9]+]]), [[BUMPED:%r[a-z0-9]+]]
; CHECK-NEXT: movq [[BUMPED]], %fs:({{%[a-z0-9]+}})
; CHECK: fstpt -32([[T0]])
; CHECK-NEXT: movq $99, -16([[T0]])
; CHECK-NEXT: movl $7, (%rsp)
define void @named_stack_arg() safestack {
  call void (i32, i32, i32, i32, i32, i32, i32, ...) @sink7(
      i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7,
      x86_fp80 0xK3FFF8000000000000000, i64 99)
  ret void
}

; More than six integer and more than eight floating-point variadic arguments,
; so both classes spill past their registers into the staged area: three i64s
; at the bottom, then the two leftover doubles.
;
; CHECK-LABEL: reg_stack_split:
; CHECK: leaq -48([[T0:%r[a-z0-9]+]]), [[BUMPED:%r[a-z0-9]+]]
; CHECK-NEXT: movq [[BUMPED]], %fs:({{%[a-z0-9]+}})
; CHECK-DAG: movq {{%r[a-z0-9]+}}, -16([[T0]])
; CHECK-DAG: movq {{%r[a-z0-9]+}}, -24([[T0]])
; CHECK-DAG: movq $8, -32([[T0]])
; CHECK-DAG: movq $7, -40([[T0]])
; CHECK-DAG: movq $6, -48([[T0]])
; CHECK: callq sink@PLT
define void @reg_stack_split() safestack {
  call void (i32, ...) @sink(i32 0,
      i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8,
      double 1.0, double 2.0, double 3.0, double 4.0,
      double 5.0, double 6.0, double 7.0, double 8.0,
      double 9.0, double 1.0e+1)
  ret void
}

; The callee takes overflow_arg_area from its entry-time unsafe stack pointer.
; The va_list layout is untouched -- gp_offset at 0, fp_offset at 4,
; overflow_arg_area at 8, reg_save_area at 16 -- and so is the va_arg sequence,
; so an uninstrumented consumer handed this va_list still walks it correctly.
;
; CHECK-LABEL: callee:
; CHECK: movq %fs:({{%[a-z0-9]+}}), [[BASE:%r[a-z0-9]+]]
; CHECK: leaq -176([[BASE]]), [[SLOT:%r[a-z0-9]+]]
; gp_offset and fp_offset, packed into one store at the va_list's base.
; CHECK: movq {{%r[a-z0-9]+}}, -200([[BASE]])
; reg_save_area is the save-area slot; overflow_arg_area is the entry-time
; unsafe stack pointer itself, which is what the caller bumped to.
; CHECK-DAG: movq [[SLOT]], -184([[BASE]])
; CHECK-DAG: movq [[BASE]], -192([[BASE]])
define void @callee(i32 %n, ...) safestack {
  %ap = alloca [24 x i8], align 8
  call void @llvm.va_start.p0(ptr %ap)
  ret void
}

; A sibling call could neither keep the staged arguments alive across the
; callee's own frame nor restore the unsafe stack pointer afterwards, so an
; in-mode variadic call is never tail called. (LowerCall drops it explicitly;
; the restore point the pass adds for the same reason also blocks it.)
;
; CHECK-LABEL: no_sibcall:
; CHECK: callq sink@PLT
; CHECK-NOT: jmp sink
; OFF-LABEL: no_sibcall:
; OFF-NOT: callq sink
; OFF: jmp sink@PLT
define void @no_sibcall(i32 %a) safestack {
  tail call void (i32, ...) @sink(i32 %a, i64 1)
  ret void
}

; A frameless variadic musttail thunk works by construction: nothing moves the
; unsafe stack pointer between the thunk's entry and the forwarded call, so the
; callee sees the area the thunk's own caller staged. It is neither bumped nor
; un-tail-called, and the pass leaves it alone entirely.
;
; CHECK-LABEL: musttail_thunk:
; CHECK-NOT: __safestack_unsafe_stack_ptr
; CHECK: jmp sink@PLT
define void @musttail_thunk(i32 %a, ...) safestack {
  musttail call void (i32, ...) @sink(i32 %a, ...)
  ret void
}

; A function without the attribute is not instrumented, so it keeps the psABI
; placement whether or not the convention is on. Its variadic callees are
; broken by that, which is why the convention needs a whole-world build.
;
; CHECK-LABEL: uninstrumented:
; CHECK-NOT: __safestack_unsafe_stack_ptr
; CHECK: movq $6, (%rsp)
define void @uninstrumented(i32 %a) {
  call void (i32, ...) @sink(i32 %a, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6)
  ret void
}
