// RUN: %clang_safestack %s -o %t
// RUN: %run %t

// RUN: %clang_safestack -O2 %s -o %t
// RUN: %run %t

// The va_list need not be a local. A heap-allocated va_list means the function
// may have no unsafe allocas at all, so reserving the register save area is
// what causes the unsafe frame to exist in the first place. Also covers a
// conditionally executed va_start.

// REQUIRES: stable-runtime

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

__attribute__((noinline)) static long heap_valist(int n, ...) {
  va_list *ap = malloc(sizeof(va_list));
  assert(ap);

  va_start(*ap, n);
  long total = 0;
  for (int i = 0; i < n; i++)
    total += va_arg(*ap, int);
  va_end(*ap);

  free(ap);
  return total;
}

// va_start runs only on one path, so the function must still spill its
// argument registers unconditionally at entry.
__attribute__((noinline)) static long conditional(int use, int n, ...) {
  if (!use)
    return -1;

  va_list ap;
  va_start(ap, n);
  long total = 0;
  for (int i = 0; i < n; i++)
    total += va_arg(ap, int);
  va_end(ap);
  return total;
}

int main(void) {
  assert(heap_valist(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 55);
  assert(conditional(0, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == -1);
  assert(conditional(1, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 55);
  return 0;
}
