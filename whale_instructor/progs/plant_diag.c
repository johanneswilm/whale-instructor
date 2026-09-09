/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Open-loop plant measurement (our build). Drive motor B with raw CCR
 * duty via set_motor_new (no PI, no ramp, no stall logic) and report
 * encoder counts per 500 ms window on the face. Markers: 5555 duty 25,
 * 6666 duty 50, 7777 duty 100, then closed loop 40 (8888) and 60 (444)
 * for comparison. Expect roughly 25/50/100 percent of the max-speed
 * count if the plant is linear. */
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
