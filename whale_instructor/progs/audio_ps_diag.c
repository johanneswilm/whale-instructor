/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* PlaySpeech freeze locator. Shows a stage code, runs one real audio
 * call, and shows the next code only if the call RETURNED - wherever
 * the face freezes, that call is stuck.
 *   4700-series: flash file 47's header (the leading word of
 *     read_number; the probe freezes on it): 47+rate, 48+block align,
 *     49+data length (low 16 bits).
 *   31 -> PlaySpeech(1) -> 30 if it returns.
 *   41 -> PlaySpeech(47) -> 40 if it returns.
 *   51 -> PlaySensorNum(1) -> 50 if it returns.
 *   61 -> PlaySpeech(46) -> 60 if it returns (the key click).
 * Then loops. Sound should accompany the calls that return. */
#include "whale_instructor.h"

static uint8_t hdr[44];

static void mark(int n)
{
    set_display_num((uint16_t)(n & 0xFFFF));
    vTaskDelay(2500);
}

static void read_file_header(uint16_t idx)
{
    uint32_t off;
    SPI_SetFlashAddress((uint32_t)idx * 4);
    off = SPI_ReadLongFromFlash();
    SPI_SetFlashAddress(off);
    SPI_ReadnByteFromFlash(hdr, 44);
}

void user_main()
{
    vTaskDelay(300);
    for (;;)
    {
        read_file_header(47);
        mark(4700 + ((uint32_t)hdr[24] | ((uint32_t)hdr[25] << 8)) % 100);
        mark(4800 + (hdr[32] | (hdr[33] << 8)));
        mark(4900 + (((uint32_t)hdr[40] | ((uint32_t)hdr[41] << 8)) & 0xFF));
        mark(31);
        PlaySpeech(1);
        mark(30);
        mark(41);
        PlaySpeech(47);
        mark(40);
        mark(51);
        PlaySensorNum(1);
        mark(50);
        mark(61);
        PlaySpeech(46);
        mark(60);
        vTaskDelay(4000);
    }
}
