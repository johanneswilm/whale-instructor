/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Assert override for newlib >= 3.2 on the controller (Whale Instructor
 * runtime; this file is linked into user programs, hence LGPL -- your
 * programs stay yours, see LICENSE_EXCEPTION.md).
 *
 * newlib 3.2+ adds assert() calls inside libc internals (mprec/_dtoa_r used
 * by nano printf's float support, rand, ...). The vendored vendor .a calls
 * sprintf/vsnprintf with float formatting early at boot; on newlib >= 3.2 an
 * assert fires there. The stock __assert_func prints to stderr, which in the
 * rdimon syscall layer executes semihosting BKPT 0xAB -> HardFault on the
 * debugger-less controller -> vendor fault handler -> power off.
 *
 * This strong definition wins the link before libc's assert.o (our objects
 * are linked first), so the fatal stderr/BKPT path is never pulled in.
 * An assert that would have fired is silently ignored: the controller stays
 * up. Do not treat this as a correctness guarantee of the libc internals;
 * it exists to keep assert-instrumented newlib builds bootable.
 */
#include <stddef.h>

void __assert_func(const char *file, int line, const char *func,
                   const char *expr)
{
    (void)file;
    (void)line;
    (void)func;
    (void)expr;
    /* Return instead of the stock print-and-abort: if the asserted condition
     * was benign, the app keeps running (expected case); if libc state is
     * truly corrupt, we will see a distinguishable partial failure instead of
     * an instant power-off. */
}
