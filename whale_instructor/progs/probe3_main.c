/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "whale_instructor.h"

void user_main()
{
    patrol_integrated_initialization(A, 0, B, 0);
    vTaskDelay(200);
}

void user_task1()
{
    int k = 1;
    int v;
    while (1)
    {
        v = get_integrated_grayscale(k);
        if (v > 10)
        {
            set_motor(A, 50);
            vTaskDelay(400);
            set_motor(A, 0);
            vTaskDelay(400);
        }
        else
        {
            vTaskDelay(800);
        }
        k = k + 1;
        if (k > 5)
        {
            k = 1;
        }
    }
}
