// RUN: %clang_safestack %s -o %t
// RUN: %run %t

// RUN: %clang_safestack -O2 %s -o %t
// RUN: %run %t

// The point of the feature: assert directly that the va_list's reg_save_area
// lands inside the unsafe stack region rather than on the safe stack. Without
// it, a raw safe-stack address sits in attacker-reachable memory, since the
// va_list itself is address-taken and therefore unsafe.

// REQUIRES: stable-runtime

#include <assert.h>
#include <stdarg.h>

#if defined(__x86_64__)

extern void *__get_unsafe_stack_bottom(void);
extern void *__get_unsafe_stack_top(void);

// The SysV x86-64 va_list, as laid out by the psABI.
struct va_list_sysv {
  unsigned gp_offset;
  unsigned fp_offset;
  void *overflow_arg_area;
  void *reg_save_area;
};

__attribute__((noinline)) static void check(int n, ...) {
  va_list ap;
  va_start(ap, n);

  struct va_list_sysv *v = (struct va_list_sysv *)(void *)ap;
  char *rsa = (char *)v->reg_save_area;
  char *bottom = (char *)__get_unsafe_stack_bottom();
  char *top = (char *)__get_unsafe_stack_top();

  assert(bottom < top);
  assert(rsa >= bottom && rsa < top);

  // The arguments must still read back correctly through it.
  long total = 0;
  for (int i = 0; i < n; i++)
    total += va_arg(ap, int);
  assert(total == 21);

  va_end(ap);
}

int main(void) {
  check(6, 1, 2, 3, 4, 5, 6);
  return 0;
}

#else

int main(void) { return 0; }

#endif
