/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Audio trace diag: the SysTick surfaces wb_audio's live trace code
 * (9000+N) while PlaySpeech(47) runs or hangs. Legend: 1 entered,
 * 2 InitSpeech entered, 3 past idx check, 13 count too small,
 * 4 header phase, 14 bad magic, 5 header ok, 6 hw init done,
 * 7 armed+filled, 20 InitSpeech ok, 8 waiting for DMA completion,
 * 9 refill pass, 10 clean finish, 12 fail return. */
#include "whale_instructor.h"

void user_main()
{
    vTaskDelay(300);
    for (;;)
    {
        set_display_num(71);
        PlaySpeech(47);
        set_display_num(70);
        vTaskDelay(3000);
    }
}
