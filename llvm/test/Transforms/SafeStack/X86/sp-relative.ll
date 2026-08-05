; Mode 1: the SP-relative classification. An object stays on the native
; stack only if every access to it folds into a [SP + constant] operand.
;
; Each clause below has its own demonstrating case: one that classic
; classification keeps native and this predicate must evict.
;
; RUN: opt -safe-stack -S -mtriple=x86_64-pc-linux-gnu < %s -o - \
; RUN:   | FileCheck %s --check-prefix=CLASSIC
; RUN: opt -safe-stack -safe-stack-force-sp-relative -S \
; RUN:   -mtriple=x86_64-pc-linux-gnu < %s -o - | FileCheck %s --check-prefix=MODE1
; RUN: opt -safe-stack -safe-stack-force-all-unsafe -S \
; RUN:   -mtriple=x86_64-pc-linux-gnu < %s -o - | FileCheck %s --check-prefix=ALLUNSAFE

declare void @escape(ptr)
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

; Field-wise access through constant GEPs: foldable, so it stays native under
; every policy except the all-unsafe stress knob.
;
; CLASSIC-LABEL: define i32 @sp_relative
; CLASSIC-NOT: __safestack_unsafe_stack_ptr
; MODE1-LABEL: define i32 @sp_relative
; MODE1-NOT: __safestack_unsafe_stack_ptr
; ALLUNSAFE-LABEL: define i32 @sp_relative
; ALLUNSAFE: __safestack_unsafe_stack_ptr
define i32 @sp_relative() safestack {
entry:
  %s = alloca { i32, i32 }, align 4
  %f1 = getelementptr inbounds { i32, i32 }, ptr %s, i32 0, i32 1
  store i32 7, ptr %f1
  %v = load i32, ptr %f1
  ret i32 %v
}

; Core clause. The index is masked into range, so SCEV proves the access in
; bounds and classic classification keeps this native -- but it lowers to
; base-plus-index addressing, which needs the base in a register.
;
; CLASSIC-LABEL: define i32 @variable_index
; CLASSIC-NOT: __safestack_unsafe_stack_ptr
; MODE1-LABEL: define i32 @variable_index
; MODE1: __safestack_unsafe_stack_ptr
define i32 @variable_index(i32 %i) safestack {
entry:
  %a = alloca [4 x i32], align 4
  %c = and i32 %i, 3
  %p = getelementptr inbounds [4 x i32], ptr %a, i32 0, i32 %c
  store i32 7, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

; Alignment clause. SP-relative, but an over-aligned object forces stack
; realignment, after which the backend addresses fixed objects off the frame
; pointer rather than the stack pointer.
;
; CLASSIC-LABEL: define i32 @over_aligned
; CLASSIC-NOT: __safestack_unsafe_stack_ptr
; MODE1-LABEL: define i32 @over_aligned
; MODE1: __safestack_unsafe_stack_ptr
define i32 @over_aligned() safestack {
entry:
  %s = alloca i32, align 64
  store i32 7, ptr %s
  %v = load i32, ptr %s
  ret i32 %v
}

; Static-alloca clause, stated explicitly rather than inherited from the use
; walk: with no uses at all the walk succeeds vacuously, yet the VLA still
; emits a runtime-sized stack adjustment.
;
; CLASSIC-LABEL: define void @unused_vla
; CLASSIC-NOT: __safestack_unsafe_stack_ptr
; MODE1-LABEL: define void @unused_vla
; MODE1: __safestack_unsafe_stack_ptr
define void @unused_vla(i64 %n) safestack {
entry:
  %vla = alloca i8, i64 %n, align 1
  ret void
}

; Block-locality clause. The GEP is computed in the entry block and used in a
; successor, so instruction selection -- which works one block at a time --
; exports it through a virtual register, materializing the address.
;
; CLASSIC-LABEL: define i32 @cross_block_gep
; CLASSIC-NOT: __safestack_unsafe_stack_ptr
; MODE1-LABEL: define i32 @cross_block_gep
; MODE1: __safestack_unsafe_stack_ptr
define i32 @cross_block_gep(i1 %c) safestack {
entry:
  %s = alloca { i32, i32 }, align 4
  %f1 = getelementptr inbounds { i32, i32 }, ptr %s, i32 0, i32 1
  br i1 %c, label %then, label %else

then:
  store i32 7, ptr %f1
  br label %else

else:
  %v = load i32, ptr %f1
  ret i32 %v
}

; The same shape with the GEP repeated in each block stays native: this is
; what CodeGenPrepare's address sinking produces, and it is the reason the
; clause is exact rather than conservative.
;
; CLASSIC-LABEL: define i32 @same_block_gep
; MODE1-LABEL: define i32 @same_block_gep
; MODE1-NOT: __safestack_unsafe_stack_ptr
define i32 @same_block_gep(i1 %c) safestack {
entry:
  %s = alloca { i32, i32 }, align 4
  br i1 %c, label %then, label %else

then:
  %f1 = getelementptr inbounds { i32, i32 }, ptr %s, i32 0, i32 1
  store i32 7, ptr %f1
  br label %else

else:
  %f1b = getelementptr inbounds { i32, i32 }, ptr %s, i32 0, i32 1
  %v = load i32, ptr %f1b
  ret i32 %v
}

; A mem intrinsic takes the pointer in a register for its libcall form, so it
; is not foldable even with constant indices.
;
; MODE1-LABEL: define void @mem_intrinsic
; MODE1: __safestack_unsafe_stack_ptr
define void @mem_intrinsic(ptr %src) safestack {
entry:
  %s = alloca [16 x i8], align 1
  call void @llvm.memcpy.p0.p0.i64(ptr %s, ptr %src, i64 16, i1 false)
  ret void
}

; A byval that passes the predicate stays exactly where the caller put it,
; with no unsafe slot and no copy.
;
; MODE1-LABEL: define void @byval_kept
; MODE1-NOT: __safestack_unsafe_stack_ptr
; MODE1-NOT: llvm.memcpy
define void @byval_kept(ptr byval({ i32, i32 }) align 8 %p) safestack {
entry:
  %f1 = getelementptr inbounds { i32, i32 }, ptr %p, i32 0, i32 1
  store i32 7, ptr %f1
  ret void
}

; A byval that fails it gets today's treatment: an unsafe slot filled by a
; memcpy from the incoming argument area.
;
; MODE1-LABEL: define void @byval_moved
; MODE1: __safestack_unsafe_stack_ptr
; MODE1: call void @llvm.memcpy
define void @byval_moved(ptr byval({ i32, i32 }) align 8 %p) safestack {
entry:
  call void @escape(ptr %p)
  ret void
}
