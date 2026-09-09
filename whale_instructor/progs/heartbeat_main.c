/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "whale_instructor.h"

void user_main()
{
    while (1)
    {
        set_motor_time(A, 40, 0.4f);
        vTaskDelay(500);
        off_motor(A);
        vTaskDelay(500);
    }
}
