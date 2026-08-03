; Well-formed uses of the SafeStack varargs handshake intrinsics verify cleanly.
; RUN: llvm-as -disable-output %s

declare void @llvm.safestack.vararg.save.regs(ptr)
declare void @llvm.va_start.safestack.p0(ptr, ptr, ptr)

; save.regs in the entry block, one va_start.safestack alongside it.
define void @simple(ptr %ap, ptr %slot, ptr %base, ...) safestack {
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  call void @llvm.va_start.safestack.p0(ptr %ap, ptr %slot, ptr %base)
  ret void
}

; Only save.regs is pinned to the entry block; va_start.safestack may appear in
; any block, and more than once (e.g. a conditionally-initialized va_list).
define void @va_start_in_other_blocks(ptr %ap, ptr %slot, ptr %base, i1 %c, ...) safestack {
entry:
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  br i1 %c, label %then, label %else

then:
  call void @llvm.va_start.safestack.p0(ptr %ap, ptr %slot, ptr %base)
  ret void

else:
  call void @llvm.va_start.safestack.p0(ptr %ap, ptr %slot, ptr %base)
  ret void
}

; The va_list pointer is overloaded on its address space, as for llvm.va_start.
declare void @llvm.va_start.safestack.p5(ptr addrspace(5), ptr, ptr)

define void @va_list_addrspace(ptr addrspace(5) %ap, ptr %slot, ptr %base, ...) safestack {
  call void @llvm.safestack.vararg.save.regs(ptr %slot)
  call void @llvm.va_start.safestack.p5(ptr addrspace(5) %ap, ptr %slot, ptr %base)
  ret void
}
