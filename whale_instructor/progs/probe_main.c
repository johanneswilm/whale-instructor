/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "whale_instructor.h"

void user_main()
{
    int v;
    patrol_integrated_initialization(A, 0, B, 0);
    set_motor(A, 50);
    vTaskDelay(400);
    set_motor(A, 0);
    vTaskDelay(400);
    v = get_integrated_grayscale(1);
    set_motor(A, 50);
    vTaskDelay(400);
    set_motor(A, 0);
    vTaskDelay(400);
    if (v > 10)
    {
        set_motor(A, 50);
        vTaskDelay(1500);
    }
    set_motor(A, 0);
    while (1)
    {
        vTaskDelay(100);
    }
}
