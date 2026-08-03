; A target that does not implement getVarArgsSaveAreaInfo keeps the classic
; lowering: llvm.va_start is left alone and no save area is reserved. The
; feature degrades per target rather than requiring every target to opt out.
;
; RUN: opt -safe-stack -S -mtriple=aarch64-linux-gnu < %s -o - | FileCheck %s

%struct.__va_list = type { ptr, ptr, ptr, i32, i32 }

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)
declare i32 @vprintf(ptr, ptr)

; CHECK-LABEL: define i32 @simple
; CHECK: call void @llvm.va_start.p0
; CHECK-NOT: @llvm.safestack.vararg.save.regs
; CHECK-NOT: @llvm.va_start.safestack
; Only the va_list alloca is on the unsafe stack, with no save area added.
; CHECK: !{!"unsafe-stack-size", i32 32}
define i32 @simple(ptr %fmt, ...) safestack {
entry:
  %ap = alloca %struct.__va_list, align 8
  call void @llvm.va_start.p0(ptr %ap)
  %r = call i32 @vprintf(ptr %fmt, ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %r
}
