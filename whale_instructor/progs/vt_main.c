/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "whale_instructor.h"

void user_main()
{
    set_motor(A, 50);
    vTaskDelay(500);
    set_motor(A, 0);
    while (1)
    {
        vTaskDelay(100);
    }
}