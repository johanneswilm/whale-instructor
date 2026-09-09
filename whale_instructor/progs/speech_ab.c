/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Speech A/B probe: plays flash speech files 1, 46 and 47 in a loop,
 * showing each index on the face first. Build this SAME program
 * against the open core (slot 3) and the vendor library (slot 2) and
 * compare what each index sounds like. Reference PC copies of the
 * same files: /tmp/kilo/sound/fileN.wav. */
#include "whale_instructor.h"

void user_main()
{
    vTaskDelay(300);
    for (;;)
    {
        set_display_num(1);
        vTaskDelay(1500);
        PlaySpeech(1);
        vTaskDelay(1000);
        set_display_num(46);
        vTaskDelay(1500);
        PlaySpeech(46);
        vTaskDelay(1000);
        set_display_num(47);
        vTaskDelay(1500);
        PlaySpeech(47);
        vTaskDelay(1000);
    }
}
