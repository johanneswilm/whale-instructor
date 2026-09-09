/* syscalls.c -- newlib syscall stubs for the Whale Instructor runtime.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is linked into user programs, hence LGPL -- your programs
 * stay yours, see LICENSE_EXCEPTION.md. The link uses --specs=nano.specs
 * --specs=rdimon.specs and these reentrant syscalls must exist or
 * printf/malloc crash. Output goes to the debug USART through the open
 * core's fputc; there is no console input and the heap grows from the
 * end-of-BSS linker symbol.
 */
#include <stdlib.h>
#include <stdbool.h>
#include <reent.h>
#include <sys/stat.h>

#include "../open_core/wb_core.h"

int _read_r(struct _reent *r, int file, void *ptr, size_t len)
{
    (void)r;
    (void)file;
    (void)ptr;
    return (int)len;
}

int _write_r(struct _reent *r, int file, const void *ptr, size_t len)
{
    const char *p = (const char *)ptr;
    size_t i;

    (void)r;
    (void)file;
    for (i = 0; i < len; i++) {
        if (p[i] == '\n') {
            fputc('\r', NULL);
        }
        fputc(p[i], NULL);
    }
    return (int)len;
}

int _close_r(struct _reent *r, int file)
{
    (void)r;
    (void)file;
    return 0;
}

_off_t _lseek_r(struct _reent *r, int file, _off_t ptr, int dir)
{
    (void)r;
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _fstat_r(struct _reent *r, int file, struct stat *st)
{
    (void)r;
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

void *_sbrk_r(struct _reent *r, ptrdiff_t nbytes)
{
    extern char end[];
    static char *heap_ptr;
    char *base;

    (void)r;
    if (heap_ptr == 0) {
        heap_ptr = end;
    }
    base = heap_ptr;
    heap_ptr += nbytes;
    return base;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}
