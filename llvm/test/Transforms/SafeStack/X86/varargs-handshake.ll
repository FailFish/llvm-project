; The SafeStack pass reserves the varargs register save area in the unsafe
; frame and hands its address to ISel through the handshake intrinsics.
;
; RUN: opt -safe-stack -S -mtriple=x86_64-pc-linux-gnu < %s -o - | FileCheck %s

%struct.__va_list_tag = type { i32, i32, ptr, ptr }

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)
declare i32 @vprintf(ptr, ptr)
declare i32 @setjmp(ptr) returns_twice

; The frame grows by the 176-byte save area, the slot address is derived from
; the base pointer, and va_start becomes the SafeStack variant carrying both
; the slot and the entry-time base pointer.
;
; CHECK-LABEL: define i32 @simple
; CHECK: %[[BASE:.*]] = load ptr, ptr @__safestack_unsafe_stack_ptr
; CHECK: %[[SLOT:varargs_reg_save_area]] = getelementptr i8, ptr %[[BASE]], i32 -176
; CHECK-NEXT: call void @llvm.safestack.vararg.save.regs(ptr %[[SLOT]])
; CHECK: call void @llvm.va_start.safestack.p0(ptr %{{.*}}, ptr %[[SLOT]], ptr %[[BASE]])
; CHECK-NOT: call void @llvm.va_start.p0
define i32 @simple(ptr %fmt, ...) safestack {
entry:
  %ap = alloca [1 x %struct.__va_list_tag], align 16
  call void @llvm.va_start.p0(ptr %ap)
  %r = call i32 @vprintf(ptr %fmt, ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %r
}

; save.regs stays in the entry block even when va_start does not: only the
; register spills need the entry block, the va_list initialisation does not.
;
; CHECK-LABEL: define void @va_start_in_successor
; CHECK: entry:
; CHECK: call void @llvm.safestack.vararg.save.regs
; CHECK: init:
; CHECK: call void @llvm.va_start.safestack.p0
define void @va_start_in_successor(i1 %c, ...) safestack {
entry:
  %ap = alloca [1 x %struct.__va_list_tag], align 16
  br i1 %c, label %init, label %done

init:
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  br label %done

done:
  ret void
}

; Several va_starts over two va_lists: one save.regs, one rewrite each, all
; sharing the same slot.
;
; CHECK-LABEL: define void @two_va_lists
; CHECK: call void @llvm.safestack.vararg.save.regs(ptr %[[SLOT2:.*]])
; CHECK: call void @llvm.va_start.safestack.p0(ptr %{{.*}}, ptr %[[SLOT2]], ptr %{{.*}})
; CHECK: call void @llvm.va_start.safestack.p0(ptr %{{.*}}, ptr %[[SLOT2]], ptr %{{.*}})
; CHECK-NOT: @llvm.safestack.vararg.save.regs
define void @two_va_lists(...) safestack {
entry:
  %ap = alloca [1 x %struct.__va_list_tag], align 16
  %aq = alloca [1 x %struct.__va_list_tag], align 16
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_start.p0(ptr %aq)
  call void @llvm.va_end.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %aq)
  ret void
}

; The frameless corner: a heap va_list means there are no unsafe allocas at
; all, so without this feature the pass would bail out entirely. Reserving the
; slot is what brings the frame -- and its prologue, epilogue and restore
; points -- into existence, through the ordinary code path.
;
; CHECK-LABEL: define void @heap_va_list
; CHECK: %[[HBASE:.*]] = load ptr, ptr @__safestack_unsafe_stack_ptr
; CHECK: store ptr %{{.*}}, ptr @__safestack_unsafe_stack_ptr
; CHECK: %[[HSLOT:varargs_reg_save_area]] = getelementptr i8, ptr %[[HBASE]], i32 -176
; CHECK: call void @llvm.safestack.vararg.save.regs(ptr %[[HSLOT]])
; CHECK: call void @llvm.va_start.safestack.p0(ptr %{{.*}}, ptr %[[HSLOT]], ptr %[[HBASE]])
define void @heap_va_list(ptr %heap, ...) safestack {
entry:
  call void @llvm.va_start.p0(ptr %heap)
  call void @llvm.va_end.p0(ptr %heap)
  ret void
}

; A variadic function that never calls va_start needs no save area, so nothing
; changes -- the pass still bails out when there is nothing else to do.
;
; CHECK-LABEL: define void @variadic_no_va_start
; CHECK-NOT: @llvm.safestack.vararg.save.regs
; CHECK-NOT: __safestack_unsafe_stack_ptr
define void @variadic_no_va_start(i32 %x, ...) safestack {
entry:
  ret void
}

; A va_list consumer -- vprintf-style -- takes a va_list but never starts one,
; so it gets no save area either.
;
; CHECK-LABEL: define i32 @va_list_consumer
; CHECK-NOT: @llvm.safestack.vararg.save.regs
define i32 @va_list_consumer(ptr %fmt, ptr %ap) safestack {
entry:
  %r = call i32 @vprintf(ptr %fmt, ptr %ap)
  ret i32 %r
}

; setjmp and va_start coexist: the slot lives above StaticTop, so the restore
; point resets the unsafe stack pointer to StaticTop without freeing the save
; area. This is the scenario that ruled out reserving the area in the backend.
;
; CHECK-LABEL: define void @setjmp_and_va_start
; CHECK: %[[SJBASE:.*]] = load ptr, ptr @__safestack_unsafe_stack_ptr
; CHECK: %[[SJTOP:unsafe_stack_static_top]] = getelementptr i8, ptr %[[SJBASE]], i32 -208
; CHECK: %[[SJSLOT:varargs_reg_save_area]] = getelementptr i8, ptr %[[SJBASE]], i32 -176
; CHECK: call void @llvm.safestack.vararg.save.regs(ptr %[[SJSLOT]])
; CHECK: call i32 @setjmp
; The restore point stores StaticTop, which is below the save area.
; CHECK: store ptr %[[SJTOP]], ptr @__safestack_unsafe_stack_ptr
define void @setjmp_and_va_start(ptr %jb, ...) safestack {
entry:
  %ap = alloca [1 x %struct.__va_list_tag], align 16
  call void @llvm.va_start.p0(ptr %ap)
  %r = call i32 @setjmp(ptr %jb)
  call void @llvm.va_end.p0(ptr %ap)
  ret void
}

; Frame sizes, checked together because the annotations land in the module's
; metadata rather than inline. Each includes the 176-byte save area: @simple
; and @setjmp_and_va_start add 32 for a va_list alloca, @heap_va_list has no
; other unsafe object at all.
; CHECK-DAG: !{!"unsafe-stack-size", i32 208}
; CHECK-DAG: !{!"unsafe-stack-size", i32 176}
