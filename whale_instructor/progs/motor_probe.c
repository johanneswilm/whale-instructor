/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Motor bench probe (device validation of the control layer), matrix
 * edition. For each motor A..D drive +30 percent and -30 percent open
 * loop for 1 s (set_motor_new bypasses the PI, the goal ramp and the
 * stall logic) and render the encoder delta of each leg as a bar on
 * the dot matrix in P1. The bars accumulate; one row per leg:
 *
 *   rows 7/6: A+  A-      rows 5/4: B+  B-
 *   rows 3/2: C+  C-      rows 1/0: D+  D-
 *
 * (row 0 renders at the physical bottom, so A is the top pair). A bar
 * anchored at one edge is a positive count, anchored at the other
 * edge a negative count; one pixel = 50 counts, clamped at 8. An
 * empty row means that leg produced no encoder counts. */
#include "whale_instructor.h"

static void probe_bar(int row, int delta)
{
    static unsigned char rows[8];
    int n = delta < 0 ? -delta : delta;
    n = n / 50;
    if (n > 8) {
        n = 8;
    }
    unsigned char bits = 0;
    if (delta >= 0) {
        for (int k = 0; k < n; k++) {
            bits |= (unsigned char)(1u << k);
        }
    } else {
        for (int k = 0; k < n; k++) {
            bits |= (unsigned char)(0x80u >> k);
        }
    }
    rows[row] = bits;
    LedMaritx frame = {{rows[0], rows[1], rows[2], rows[3],
                        rows[4], rows[5], rows[6], rows[7]}};
    display_custom(1, frame);
}

static void probe_leg(int m, int duty, int row)
{
    reset_motor_encoder(m);
    set_motor_new(m, duty);
    vTaskDelay(1000);
    set_motor_new(m, 0);
    probe_bar(row, get_encoder_value(m));
    vTaskDelay(2200);
}

void user_main()
{
    vTaskDelay(300);
    for (int m = 1; m <= 4; m++)
    {
        int up = 8 - 2 * m;
        probe_leg(m, 30, up + 1);
        probe_leg(m, -30, up);
    }
}
