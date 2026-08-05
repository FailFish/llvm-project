// RUN: %clang_safestack -mllvm -safestack-vararg-position-convention %s -o %t
// RUN: %run %t

// RUN: %clang_safestack -O2 -mllvm -safestack-vararg-position-convention %s -o %t
// RUN: %run %t

// The varargs position convention: the caller stages a variadic call's stack
// arguments on its own unsafe stack, so no safe-stack address reaches the
// va_list at all -- not even overflow_arg_area.
//
// This is a calling-convention change, sound only when caller and callee are
// built the same way. Everything variadic here therefore stays inside this
// translation unit; in particular nothing hands a va_list to libc or calls a
// libc variadic function with enough arguments to overflow into memory, which
// is why this test reports through assert() rather than printf().

// REQUIRES: stable-runtime

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>

// Seven fixed integer arguments: six go in registers and one on the native
// stack, so the variadic block would start at an odd multiple of eight. A
// long double needs sixteen-byte alignment, which caller and callee only
// agree on because the block is padded to sixteen first.
__attribute__((noinline)) static void
overaligned(int a, int b, int c, int d, int e, int f, int g, ...) {
  va_list ap;
  va_start(ap, g);
  assert(a == 1 && g == 7);
  assert(va_arg(ap, long double) == 2.5L);
  assert(va_arg(ap, long long) == 99);
  assert(va_arg(ap, double) == 1.25);
  va_end(ap);
}

// More than six integer and more than eight floating-point variadic
// arguments, so both classes spill past their registers into the staged area.
__attribute__((noinline)) static void split(int n, ...) {
  va_list ap, aq;
  va_start(ap, n);
  va_copy(aq, ap);

  long long isum = 0;
  for (int i = 0; i < 10; i++)
    isum += va_arg(ap, long long);
  double fsum = 0;
  for (int i = 0; i < 10; i++)
    fsum += va_arg(ap, double);
  va_end(ap);
  assert(isum == 55 && fsum == 55.0);

  // The copy walks the same area independently.
  long long isum2 = 0;
  for (int i = 0; i < 10; i++)
    isum2 += va_arg(aq, long long);
  va_end(aq);
  assert(isum2 == 55);
}

static jmp_buf jb;

__attribute__((noinline)) static void jumps_out(int n, ...) {
  va_list ap;
  va_start(ap, n);
  assert(va_arg(ap, long long) == 1);
  va_end(ap);
  longjmp(jb, 1);
}

int main(void) {
  overaligned(1, 2, 3, 4, 5, 6, 7, 2.5L, 99LL, 1.25);

  split(0, 1LL, 2LL, 3LL, 4LL, 5LL, 6LL, 7LL, 8LL, 9LL, 10LL, 1.0, 2.0, 3.0,
        4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0);

  // Leaving a variadic call by longjmp skips the caller-side restore, so the
  // bump survives it. The setjmp restore point is what undoes it; without one
  // this loop would walk the unsafe stack down without bound.
  volatile char *first = 0;
  for (int i = 0; i < 1000; i++) {
    char local;
    volatile char *here = &local;
    if (first == 0)
      first = here;
    assert(here == first);
    if (setjmp(jb) == 0)
      jumps_out(0, 1LL, 2LL, 3LL, 4LL, 5LL, 6LL, 7LL, 8LL, 9LL, 10LL);
  }

  return 0;
}
