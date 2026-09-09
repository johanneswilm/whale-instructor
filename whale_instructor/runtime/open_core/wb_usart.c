/* wb_usart.c -- debug printf sink for application images.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Whale Instructor open core. Replaces the proprietary usart.o of the
 * vendor archive. printf()/vsnprintf land here: blocking character
 * write to USART1 (the vendor runtime leaves USART1 pin/clock setup to
 * the boot environment; when unconfigured, output is simply lost).
 * _sys_exit replaces the newlib hook so nothing falls into semihosting
 * (see also runtime/assert_override.c for the sibling problem).
 */
#include "wb_core.h"

int fputc(int ch, FILE *f)
{
    (void)f;
    /* Wait for TC (SR bit 6), then write the data register. */
    while (!(USART1->SR & USART_SR_TC)) {
    }
    USART1->DR = (uint8_t)ch;
    return ch;
}

void _sys_exit(int rc)
{
    (void)rc;
    while (1) {
    }
}
