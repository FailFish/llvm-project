; Verifier rules for the SafeStack varargs handshake intrinsics.
; RUN: not llvm-as -disable-output %s 2>&1 | FileCheck %s

declare void @llvm.safestack.vararg.save.regs(ptr)
declare void @llvm.va_start.safestack.p0(ptr, ptr, ptr)

; CHECK: safestack.vararg.save.regs called in a non-varargs function
define void @save_regs_not_varargs(ptr %slot) safestack {
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  ret void
}

; CHECK: safestack.vararg.save.regs called in a function without the safestack attribute
define void @save_regs_no_attr(ptr %slot, ...) {
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  ret void
}

; The spills consume physical argument registers, which are only live in the
; entry block.
; CHECK: safestack.vararg.save.regs used outside of entry block
define void @save_regs_not_entry(ptr %slot, ...) safestack {
entry:
  br label %next

next:
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  ret void
}

; CHECK: multiple calls to safestack.vararg.save.regs in one function
define void @save_regs_twice(ptr %slot, ...) safestack {
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  ret void
}

; CHECK: va_start.safestack called in a non-varargs function
define void @va_start_not_varargs(ptr %ap, ptr %slot, ptr %base) safestack {
  call void @llvm.va_start.safestack.p0(ptr %ap, ptr %slot, ptr %base)
  ret void
}

; CHECK: va_start.safestack called in a function without the safestack attribute
define void @va_start_no_attr(ptr %ap, ptr %slot, ptr %base, ...) {
  call void @llvm.va_start.safestack.p0(ptr %ap, ptr %slot, ptr %base)
  ret void
}
