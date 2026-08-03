// RUN: %clang_safestack %s -o %t
// RUN: %run %t

// RUN: %clang_safestack -O2 %s -o %t
// RUN: %run %t

// Variadic arguments must keep working once the register save area lives on
// the unsafe stack: register-passed and overflow-passed arguments, integer and
// floating point, va_copy, and handing the va_list to an uninstrumented
// consumer such as vsnprintf.

// REQUIRES: stable-runtime

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

__attribute__((noinline)) static long sum_ints(int n, ...) {
  va_list ap, aq;
  va_start(ap, n);
  va_copy(aq, ap);

  long total = 0;
  for (int i = 0; i < n; i++)
    total += va_arg(ap, int);

  // The copy must walk the same arguments, including the ones that spilled to
  // the caller's overflow area.
  long copied = 0;
  for (int i = 0; i < n; i++)
    copied += va_arg(aq, int);

  va_end(aq);
  va_end(ap);
  assert(total == copied);
  return total;
}

__attribute__((noinline)) static double sum_doubles(int n, ...) {
  va_list ap;
  va_start(ap, n);
  double total = 0;
  for (int i = 0; i < n; i++)
    total += va_arg(ap, double);
  va_end(ap);
  return total;
}

// Alternating int and double exhausts the 6 GPRs and the 8 XMMs at different
// points, so this walks the register save area and the overflow area for both
// classes.
__attribute__((noinline)) static double mixed(int n, ...) {
  va_list ap;
  va_start(ap, n);
  double total = 0;
  for (int i = 0; i < n; i++) {
    total += va_arg(ap, int);
    total += va_arg(ap, double);
  }
  va_end(ap);
  return total;
}

__attribute__((noinline)) static void format(char *buf, size_t len,
                                             const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  // vsnprintf is not instrumented, so this only works if the va_list layout is
  // unchanged and its pointers are dereferenceable from uninstrumented code.
  vsnprintf(buf, len, fmt, ap);
  va_end(ap);
}

int main(void) {
  assert(sum_ints(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 55);
  assert(sum_doubles(10, 1., 2., 3., 4., 5., 6., 7., 8., 9., 10.) == 55.);
  assert(mixed(10, 1, 1., 2, 2., 3, 3., 4, 4., 5, 5., 6, 6., 7, 7., 8, 8., 9,
               9., 10, 10.) == 110.);

  char buf[128];
  format(buf, sizeof buf, "%d %s %.1f %d %d %d %d %d %d", 1, "two", 3.0, 4, 5,
         6, 7, 8, 9);
  assert(strcmp(buf, "1 two 3.0 4 5 6 7 8 9") == 0);
  return 0;
}
