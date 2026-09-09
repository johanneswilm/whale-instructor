/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "whale_instructor.h"

void user_main()
{
    int i;
    int k = 1;
    int v;
    for (i = 0; i < 4; i++)
    {
        set_motor(A, 40);
        vTaskDelay(500);
        set_motor(A, 0);
        vTaskDelay(500);
    }
    patrol_integrated_initialization(A, 0, B, 0);
    vTaskDelay(500);
    for (i = 0; i < 4; i++)
    {
        set_motor(A, 40);
        vTaskDelay(500);
        set_motor(A, 0);
        vTaskDelay(500);
    }
    while (1)
    {
        v = get_integrated_grayscale(k);
        if (v > 50)
        {
            set_motor(A, 30);
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
