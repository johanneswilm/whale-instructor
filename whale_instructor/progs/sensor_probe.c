/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Analog sensor bench probe (device validation of the control layer).
 * Cycles ports 1..5 and shows each port's raw JY_AI analog reading on
 * the face display: first the port number, then the raw value
 * (0..4000, clamped to 9999 for the 4-digit display). Expected with a
 * sensor plugged in: the touch switch idles low and crosses 2000
 * while pressed (touch_switch_pressed uses raw > 2000); the infrared
 * distance sensor reads high with the beam clear and drops below 500
 * with an obstacle in front (obstacle_infrared_detected uses
 * raw < 500, get_infrared_distance scales raw/40). Unconnected ports
 * float near 0. Leave the 5-in-1 grayscale out of the ports for this
 * test - port 5's analog line shares the connector. */
#include "whale_instructor.h"

static void probe_show(int n)
{
    set_display_num(n);
    vTaskDelay(1200);
}

void user_main()
{
    vTaskDelay(300);
    for (;;)
    {
        for (int p = 1; p <= 5; p++)
        {
            int v = JY_AI(p);
            if (v < 0) {
                v = 0;
            }
            if (v > 9999) {
                v = 9999;
            }
            probe_show(p);
            probe_show(v);
        }
    }
}