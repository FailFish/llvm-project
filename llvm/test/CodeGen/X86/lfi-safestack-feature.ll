; The lfi-safestack feature must be accepted wherever it is asked for. Nothing
; consults it yet -- each consumer lands with the pass that needs it -- and on a
; non-LFI target it stays inert forever, because every consumer goes through
; X86Subtarget::hasLFISafeStack(), which folds the triple check in.
;
; RUN: llc -mtriple=x86_64_lfi-linux-musl -mattr=+lfi-safestack < %s | FileCheck %s
; RUN: llc -mtriple=x86_64_lfi-linux-musl < %s | FileCheck %s
; RUN: llc -mtriple=x86_64-unknown-linux-gnu -mattr=+lfi-safestack < %s | FileCheck %s
; RUN: llc -mtriple=x86_64-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: f:
; CHECK: retq
define void @f() {
  ret void
}
