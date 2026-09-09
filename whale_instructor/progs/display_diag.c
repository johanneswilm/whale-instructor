/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Display-path diagnostic for the open core (device validation).
 * Phase A: face digits: 0/1111/.../9999 counter (checks all glyph
 *          bottoms), "1234", then every glyph table entry 0..18
 *          announced in slot 2 position (" P k ").
 * Phase B: matrix custom patterns (box / blank, 6x).
 * Phase C: all 12 emotions on the matrix.
 * Phase D: all 53 matrix symbols, looping forever.
 * Plug the dot matrix into P1. */
#include "whale_instructor.h"

extern unsigned char smgbuf[4];
extern unsigned char dp;

void user_main()
{
    vTaskDelay(300);
    for (int k = 0; k <= 9999; k += 1111)
    {
        set_display_num(k);
        vTaskDelay(1200);
    }
    set_display_num(1234);
    vTaskDelay(1500);
    for (int k = 0; k < 19; k++)
    {
        set_display_program_idx(k);
        vTaskDelay(800);
    }
    for (int i = 0; i < 6; i++)
    {
        LedMaritx box = {{255, 129, 189, 189, 189, 189, 129, 255}};
        display_custom(1, box);
        vTaskDelay(700);
        LedMaritx blank = {{0, 0, 0, 0, 0, 0, 0, 0}};
        display_custom(1, blank);
        vTaskDelay(400);
    }
    for (int e = 1; e <= 12; e++)
    {
        display_emotion(1, 1, e);
        vTaskDelay(1200);
    }
    while (1)
    {
        for (int s = 1; s <= 53; s++)
        {
            display_symbol(1, s);
            vTaskDelay(700);
        }
    }
}
