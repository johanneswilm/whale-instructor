/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Closed-loop motor diagnostic. For motors B and A at speeds 40 and 60:
 * drive 1.5 s (settle past the ramp), then report two 500 ms encoder
 * windows on the face display. Healthy loop at speed 40 -> ~1280 counts
 * per window (goal 25.6 counts/10 ms); at 60 -> ~1920. A stall clamp
 * shows ~250 or less; a dead-feedback rail shows both speeds at the
 * same high value. Face sequence: 1111, d, d (B@40), 2222, d, d (B@60),
 * 3333, d, d (A@40), 4444, d, d (A@60). */
#include "whale_instructor.h"

static void face(int n)
{
    set_display_num((unsigned short)n);
    vTaskDelay(900);
}

static int window(int m, int ms)
{
    reset_motor_encoder((unsigned char)m);
    vTaskDelay(ms);
    int d = get_encoder_value(m);
    return d < 0 ? -d : d;
}

static void leg(int m, int spd, int marker)
{
    face(marker);
    set_motor(m, spd);
    vTaskDelay(1500);
    face(window(m, 500));
    face(window(m, 500));
    set_motor(m, 0);
    vTaskDelay(300);
}

void user_main(void)
{
    vTaskDelay(500);
    leg(B, 40, 1111);
    leg(B, 60, 2222);
    leg(A, 40, 3333);
    leg(A, 60, 4444);
    face(9999);
}
