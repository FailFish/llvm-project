// The driver's config file supplies +lfi-safestack alongside the sanitizer, so
// only a direct -cc1 can ask for one without the other. Without the feature the
// instrumentation would place the safe stack inside the sandbox, where
// sandboxed code can overwrite it.
//
// RUN: not %clang_cc1 -triple x86_64_lfi-linux-musl -fsanitize=safe-stack \
// RUN:   -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s
// CHECK: error: '-fsanitize=safe-stack' on an LFI target requires the 'lfi-safestack' target feature

// RUN: %clang_cc1 -triple x86_64_lfi-linux-musl -target-feature +lfi-safestack \
// RUN:   -fsanitize=safe-stack -emit-llvm -o /dev/null %s

// The error is about the pair: neither a non-LFI target nor an LFI target
// without the sanitizer is affected.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=safe-stack \
// RUN:   -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple x86_64_lfi-linux-musl -emit-llvm -o /dev/null %s

int main(void) { return 0; }
