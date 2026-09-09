/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Open-loop plant measurement (vendor-library build). Same sequence as
 * plant_diag.c, but get_encoder_value on the vendor runtime returns the
 * raw timer counter (preloaded to 0x7FFF and reset every 10 ms tick),
 * so movement is recovered from per-tick maxima: sample the counter
 * every 1 ms; when the reading jumps by more than the per-sample
 * movement, a tick boundary passed and the previous reading minus
 * 0x7FFF was that tick's movement. Markers: 5555 duty 25, 6666 duty 50,
 * 7777 duty 100, closed loop 40 (8888) and 60 (444). */
#include "whale_instructor.h"

static void face(int n)
{
    set_display_num((unsigned short)n);
    vTaskDelay(900);
}

static int window(int m, int ms)
{
    int total = 0;
    int prev = get_encoder_value(m - 1);
    for (int k = 0; k < ms; k++) {
        vTaskDelay(1);
        int v = get_encoder_value(m - 1);
        int d = v - prev;
        if (d > 6 || d < -6) {
            total += prev - 0x7FFF;
        }
        prev = v;
    }
    total += prev - 0x7FFF;
    return total < 0 ? -total : total;
}

static void open_leg(int duty, int marker)
{
    face(marker);
    set_motor_new(B, duty);
    vTaskDelay(700);
    face(window(B, 500));
    set_motor_new(B, 0);
    vTaskDelay(400);
}

static void closed_leg(int spd, int marker)
{
    face(marker);
    set_motor(B, spd);
    vTaskDelay(1500);
    face(window(B, 500));
    set_motor(B, 0);
    vTaskDelay(400);
}

void user_main(void)
{
    vTaskDelay(500);
    open_leg(25, 5555);
    open_leg(50, 6666);
    open_leg(100, 7777);
    closed_leg(40, 8888);
    closed_leg(60, 4444);
    face(9999);
}
