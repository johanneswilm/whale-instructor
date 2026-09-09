/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* PlaySpeech(47) internals watch: runs the call in a side task and
 * cycles its live state on the face while it works or hangs.
 *   5000+playState: 0 idle, 1 in InitSpeech, 3 waiting for the block's
 *     DMA transfer-complete, 4 arm next block, 5 cleanup
 *   6000+Length&0xFFF: PCM bytes still unread (892 -> 380 -> 0)
 *   4000+PlayLength: samples armed with the last fill (256 / 190)
 *   3000/3001: DMA2 channel 4 transfer-complete flag (raw ISR bit)
 *   3200/3201: DMA2 channel 4 underrun flag (raw ISR bit)
 *   7000+PerSecByte: bytes per sample (2 expected)
 *   8000+pageIndex&0xFF: chunk parity counter
 * Also plays no sound? note it per stage. Loops forever. */
#include "whale_instructor.h"
#include <stm32f10x_dma.h>

extern void wb_audio_dbg(uint16_t *dst);

static uint16_t dbg[6];

#define WB_DMA2_ISR (*(volatile uint32_t *)0x40020400)
#define WB_TCIF4 ((uint32_t)0x00002000)
#define WB_DMAUDR2 ((uint32_t)0x10000000)

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        PlaySpeech(47);
        vTaskDelay(3000);
    }
}

void user_main()
{
    vTaskDelay(300);
    xTaskCreate(audio_task, "at", 256, NULL, 1, NULL);
    for (;;) {
        wb_audio_dbg(dbg);
        set_display_num((uint16_t)(5000 + dbg[0]));
        vTaskDelay(1500);
        set_display_num((uint16_t)(6000 + (dbg[1] & 0xFFF)));
        vTaskDelay(1500);
        set_display_num((uint16_t)(4000 + dbg[2]));
        vTaskDelay(1500);
        set_display_num((uint16_t)(3000 +
                ((WB_DMA2_ISR & WB_TCIF4) ? 1 : 0)));
        vTaskDelay(1500);
        set_display_num((uint16_t)(3200 +
                ((WB_DMA2_ISR & WB_DMAUDR2) ? 1 : 0)));
        vTaskDelay(1500);
        set_display_num((uint16_t)(7000 + dbg[3]));
        vTaskDelay(1500);
        set_display_num((uint16_t)(8000 + dbg[5]));
        vTaskDelay(1500);
    }
}
