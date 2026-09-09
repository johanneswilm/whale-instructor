Open runtime libraries
======================

SPDX-FileCopyrightText: Johannes Wilm
SPDX-License-Identifier: GPL-3.0-or-later

This directory holds FREE, redistributable upstream sources. `tools_openlibs.py`
builds them (plus the Whale Instructor open core in ../open_core) into the
archives every image links. `FreeRTOSConfig.h` and `stm32f10x_conf.h` are
part of this tree.

Contents and licenses
---------------------

FreeRTOS/kernel/, FreeRTOS/include/, FreeRTOS/portable/
    FreeRTOS V9.0.0 kernel sources downloaded from the official FreeRTOS
    GitHub repository (tag V9.0.0), plus the ARM_CM3 port and heap_4 memory
    scheme exactly as shipped in the vendor's own build tree
    (installer/AI_Module_GCC/AI_Module2/source/FreeRTOS/portable).
    License: GPLv2 with the FreeRTOS linking exception (the exception text
    is at the top of every file: you may link FreeRTOS with external code
    closed-source). Kernel: Copyright (C) 2016 Real Time Engineers Ltd.

FreeRTOS/include/FreeRTOSConfig.h
    OUR kernel configuration (project code, GPL-3.0-or-later): preemptive
    kernel, 1 ms tick, 32 priorities, 20 KiB heap_4 pool, timers enabled,
    IRQ priority split at 5/15. The values reproduce the behavior of the
    firmware the controller shipped with.

STM32F10x_StdPeriph_Driver/
    ST's STM32F10x Standard Peripheral Library drivers as shipped in the
    vendor's build tree (the 12 drivers the controller firmware uses).
    License: ST's permissive BSD-3-Clause-style license, stated in the
    header of every file. Copyright (c) 2011 STMicroelectronics.

STM32F10x_StdPeriph_Driver/inc/stm32f10x_conf.h
    OUR peripheral-header subset (project code, GPL-3.0-or-later): flash,
    gpio, rcc, spi, tim, usart + misc; assert_param compiled out.

CMSIS/
    ARM's CMSIS v3 core support (core_cm3.c/.h) as shipped in the vendor's
    build tree, with ARM's `License.doc`. Copyright (c) 2009 ARM Ltd.

CMSIS/DeviceSupport/ST/STM32F10x/
    ST's stock V3.5.0 device support, copied from the ST-licensed sources
    in the vendor's build tree. Copyright (c) 2011 STMicroelectronics.
    - startup_stm32f10x_hd.s: vector table, Reset_Handler (.data copy,
      .bss zeroing, SystemInit, __libc_init_array, main), weak default
      handlers. tools_openlibs.py assembles it into libwhale_open.a.
    - stm32f10x.h / system_stm32f10x.h: stock ST headers.
    - system_stm32f10x.c: stock ST clock init (HSE 8 MHz x PLL9 =
      72 MHz SYSCLK, APB1 = 36 MHz) with ONE guarded change: VECT_TAB_OFFSET
      may be predefined by the build; build_tc.py compiles it per upload
      slot with 0x19000 / 0x39000 / 0x59000 so SCB->VTOR points at the
      slot's vector table. The default remains 0.

There is no in-app USB device stack: no image ever linked one (the boot
firmware owns USB) and the open core parks the USB paths as no-ops.
