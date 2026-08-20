// On LFI the safe stack lives outside the sandbox, so libc owns it and the
// compiler-rt runtime must not be linked.
//
// RUN: %clang -### --target=x86_64_lfi-linux-musl -fsanitize=safe-stack \
// RUN:   -Xclang -target-feature -Xclang +lfi-safestack %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LFI --implicit-check-not=clang_rt.safestack \
// RUN:   --implicit-check-not=__safestack_init
// LFI: "-fsanitize=safe-stack"

// Elsewhere it is linked as before.
//
// RUN: %clang -### --target=x86_64-unknown-linux-gnu -fsanitize=safe-stack %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// NATIVE: clang_rt.safestack

int main(void) { return 0; }
