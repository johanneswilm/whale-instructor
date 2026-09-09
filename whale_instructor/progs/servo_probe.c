/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Servo bus smoke probe (device validation of the PO16 path in the
 * open core). Re-engages torque after the boot release, then sweeps
 * servos 1..3 between 0 and 150 degrees at speed 50: down, up, back,
 * one second per leg. Plug servos into the bus and watch each one
 * swing; a dead bus shows as servo-less silence. */
#include "whale_instructor.h"

void user_main()
{
    restore_torque();
    wait(2.0f);
    for (int id = 1; id <= 3; id++)
    {
        set_servo_angle(id, 50, 0);
        wait(1.0f);
        set_servo_angle(id, 50, 150);
        wait(1.0f);
        set_servo_angle(id, 50, 0);
        wait(1.0f);
    }
}
