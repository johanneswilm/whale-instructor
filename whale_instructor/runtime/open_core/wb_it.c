/* wb_it.c -- Cortex-M3 exception handlers for application images.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Whale Instructor open core. Replaces the proprietary stm32f10x_it.o of
 * the vendor archive. The fault handlers spin: on a debugger-less
 * controller a fault is unrecoverable and the watchdog reboots the
 * board, while spinning keeps the state inspectable with a debugger.
 * NMI/DebugMon return (non-fatal), matching the vendor handlers.
 */
#include "wb_core.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1) {
    }
}

void MemManage_Handler(void)
{
    while (1) {
    }
}

void BusFault_Handler(void)
{
    while (1) {
    }
}

void UsageFault_Handler(void)
{
    while (1) {
    }
}

void DebugMon_Handler(void)
{
}
