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
    while (1)
    {
        int v = get_integrated_grayscale(k);
        if (v > 50)
        {
            set_motor(A, 50);
        }
        else
        {
            set_motor(A, 0);
        }
        vTaskDelay(300);
        k = k + 1;
        if (k > 5)
        {
            k = 1;
        }
    }
}
