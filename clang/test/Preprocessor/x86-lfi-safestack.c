// The LFI safe-stack ABI constants are predefined only when the feature and an
// LFI triple are both present. The feature alone is inert: asking for it on a
// plain x86-64 target must be a no-op rather than a silent ABI change, which is
// the same rule X86Subtarget::hasLFISafeStack() enforces on the LLVM side.
//
// musl and libunwind key off these macros being defined at all, which is how
// they tell a safe-stack-out-of-sandbox world from a plain LFI one.

// RUN: %clang_cc1 -triple x86_64_lfi-linux-musl -target-feature +lfi-safestack \
// RUN:   -E -dM %s -o - | FileCheck %s --check-prefix=ON

// RUN: %clang_cc1 -triple x86_64_lfi-linux-musl -E -dM %s -o - \
// RUN:   | FileCheck %s --check-prefix=OFF --implicit-check-not=__LFI_SAFESTACK

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +lfi-safestack \
// RUN:   -E -dM %s -o - | FileCheck %s --check-prefix=NOLFI \
// RUN:   --implicit-check-not=__LFI

// These are frozen ABI constants restated in clang, which cannot include
// headers from llvm/lib. They MUST MATCH SafeStackSize and SafeStackMaxDisp in
// llvm/lib/Target/X86/MCTargetDesc/X86MCLFIRewriter.h.
// ON-DAG: #define __LFI_SAFESTACK_MAX_DISP__ 1048576
// ON-DAG: #define __LFI_SAFESTACK_SIZE__ 8388608
// ON-DAG: #define __LFI__ 1

// An LFI target without the feature is a plain sandbox: still LFI, no safe
// stack outside it.
// OFF: #define __LFI__ 1

// Neither macro, nor __LFI__ itself, on a target that is not LFI at all.
// NOLFI: #define __x86_64__ 1
