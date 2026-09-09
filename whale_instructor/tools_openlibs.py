#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build runtime/open/libwhale_open.a from the free upstream sources.

Compiles the vendored FreeRTOS V9.0.0 kernel, ST StdPeriph drivers, CMSIS
core (see runtime/open/README.md) with the same core flags build_tc.py
uses, and archives them into libwhale_open.a, the free kernel/driver
layer every image links (together with the open core's libwhale_core.a).

Usage:  python3 -m whale_instructor.tools_openlibs [--gcc=/path/to/arm-none-eabi-gcc]
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from whale_instructor import build_tc, paths

OPEN = paths.PACKAGE_DIR / 'runtime' / 'open'
OUT = OPEN / 'libwhale_open.a'
CORE = paths.PACKAGE_DIR / 'runtime' / 'open_core'
CORE_OUT = CORE / 'libwhale_core.a'

# wb_control.c is implemented and enabled: its UART5 smart-sensor
# parser follows open_core/PROTOCOL.md (rules 1-7, gray update and the
# listed deviations); device A/B of the parser is still pending. See
# open_core/README.md and open_core/PROTOCOL.md.
CORE_SOURCES = ['wb_queue.c', 'wb_it.c', 'wb_usart.c', 'wb_bsp.c',
                'wb_display.c', 'wb_control.c', 'wb_audio.c']

SOURCES = [
    'FreeRTOS/kernel/croutine.c',
    'FreeRTOS/kernel/event_groups.c',
    'FreeRTOS/kernel/list.c',
    'FreeRTOS/kernel/queue.c',
    'FreeRTOS/kernel/tasks.c',
    'FreeRTOS/kernel/timers.c',
    'FreeRTOS/portable/GCC/ARM_CM3/port.c',
    'FreeRTOS/portable/MemMang/heap_4.c',
    'STM32F10x_StdPeriph_Driver/src/misc.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_adc.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_dac.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_dma.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_flash.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_fsmc.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_gpio.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_iwdg.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_rcc.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_spi.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_tim.c',
    'STM32F10x_StdPeriph_Driver/src/stm32f10x_usart.c',
    'CMSIS/core_cm3.c',
]

# FreeRTOSConfig.h (FreeRTOS/include) and stm32f10x_conf.h (StdPeriph
# inc) are part of this tree; no external headers are needed. There is
# no in-app USB stack (the boot firmware owns USB; nothing links one).


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--gcc', default=None)
    args = ap.parse_args(argv)
    gcc = args.gcc or build_tc.find_gcc()
    if gcc is None:
        sys.exit('no arm-none-eabi-gcc found: pass --gcc=<path> or put one '
                 'on PATH')
    ar = gcc.replace('-gcc', '-ar')
    major = build_tc.gcc_major(gcc)
    print(f'building {OUT} with {gcc} (gcc {major})')

    cflags = ['-c', '-mcpu=cortex-m3', '-mthumb', '-Os', '-mthumb-interwork',
              '-DUSE_STDPERIPH_DRIVER', '-DSTM32F10X_HD', '--specs=nano.specs',
              '-ffunction-sections', '-fdata-sections',
              # kernel + drivers are plain vendor-era C; accept legacy code
              '-Wno-error=implicit-function-declaration',
              '-Wno-error=int-conversion', '-std=gnu11']
    if major >= 14:
        cflags += build_tc.LEGACY_CFLAGS
    incs = ['-I' + str(d) for d in [
        OPEN / 'STM32F10x_StdPeriph_Driver' / 'inc',
        OPEN / 'CMSIS',
        OPEN / 'CMSIS' / 'DeviceSupport' / 'ST' / 'STM32F10x',
        OPEN / 'FreeRTOS' / 'include',
        OPEN / 'FreeRTOS' / 'portable' / 'GCC' / 'ARM_CM3',
    ]]

    work = Path(tempfile.mkdtemp(prefix='whale_open_'))
    objs = []
    for rel in SOURCES:
        src = OPEN / rel
        obj = work / (rel.replace('/', '_') + '.o')
        r = subprocess.run([gcc] + cflags + incs + [str(src), '-o', str(obj)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout[-2000:], r.stderr[-2000:])
            sys.exit(f'FAILED compiling {rel}')
        objs.append(obj)
    # Stock ST startup (V3.5.0 TrueSTUDIO variant, ST-licensed like the
    # drivers above): vector table + Reset_Handler (.data copy, .bss
    # zeroing, SystemInit, __libc_init_array, main). The stock ST file
    # is the startup object every image links.
    startup_src = (OPEN / 'CMSIS' / 'DeviceSupport' / 'ST' / 'STM32F10x' /
                   'startup_stm32f10x_hd.s')
    startup_obj = work / 'startup_stm32f10x_hd.o'
    r = subprocess.run([gcc, '-c', '-mcpu=cortex-m3', '-mthumb',
                        '-x', 'assembler-with-cpp', str(startup_src),
                        '-o', str(startup_obj)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        sys.exit('FAILED assembling the ST startup')
    objs.append(startup_obj)
    if OUT.exists():
        OUT.unlink()
    r = subprocess.run([ar, 'rcs', str(OUT)] + [str(o) for o in objs],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        sys.exit('FAILED archiving')
    print(f'wrote {OUT} ({OUT.stat().st_size} bytes, {len(objs)} objects)')

    # The Whale Instructor open core (replaces vendor user_queue.o,
    # stm32f10x_it.o, usart.o and bsp.o; see runtime/open_core/README.md).
    core_objs = []
    for rel in CORE_SOURCES:
        obj = work / ('core_' + rel.replace('/', '_') + '.o')
        r = subprocess.run([gcc] + cflags + incs
                           + ['-I' + str(CORE), str(CORE / rel), '-o', str(obj)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout[-2000:], r.stderr[-2000:])
            sys.exit(f'FAILED compiling open_core/{rel}')
        core_objs.append(obj)
    if CORE_OUT.exists():
        CORE_OUT.unlink()
    r = subprocess.run([ar, 'rcs', str(CORE_OUT)] + [str(o) for o in core_objs],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        sys.exit('FAILED archiving open core')
    print(f'wrote {CORE_OUT} ({CORE_OUT.stat().st_size} bytes, '
          f'{len(core_objs)} objects)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
