// RUN: %clang_safestack %s -o %t
// RUN: %run %t

// RUN: %clang_safestack -O2 %s -o %t
// RUN: %run %t

// va_start before setjmp, va_arg after longjmp.
//
// This is the scenario that decides where the varargs register save area may
// be reserved. The SafeStack pass emits a restore point after every setjmp
// call site, which resets the unsafe stack pointer to the frame's StaticTop.
// The save area is an ordinary static frame object, so it sits above StaticTop
// and survives; anything reserved *below* StaticTop would be handed back to
// the allocator by that restore point, and the next call would overwrite the
// saved argument registers. The clobber() call below is what makes such a bug
// observable rather than latent.

// REQUIRES: stable-runtime

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>

#include "utils.h"

static jmp_buf jb;

__attribute__((noinline)) static void do_longjmp(void) { longjmp(jb, 1); }

// Consumes a fresh unsafe frame. If the save area had been freed by the
// restore point, this would land on top of it.
__attribute__((noinline)) static void clobber(void) {
  volatile char buf[512];
  for (unsigned i = 0; i < sizeof(buf); i++)
    buf[i] = (char)0xAB;
  break_optimization((void *)buf);
}

__attribute__((noinline)) static int walk(int n, ...) {
  va_list ap;
  va_start(ap, n);

  // Read one argument from the register save area before jumping.
  int first = va_arg(ap, int);

  if (setjmp(jb) == 0)
    do_longjmp();

  // Back here via longjmp, with the restore point having reset the unsafe
  // stack pointer. Burn a frame, then keep walking the same va_list.
  clobber();

  int rest = 0;
  for (int i = 1; i < n; i++)
    rest += va_arg(ap, int);

  va_end(ap);
  return first + rest;
}

int main(void) {
  // Ten arguments: enough that the walk continues past the six GPRs into the
  // caller's overflow area after the longjmp.
  assert(walk(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 55);
  return 0;
}
