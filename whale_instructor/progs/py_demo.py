# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later

from whale import A, B, P1, set_motor, set_dual_motor_time, off_motor
from whale import sleep, display_digital_tube


def wiggle(motor, speed):
    set_motor(motor, speed)
    sleep(500)
    set_motor(motor, 0)


def main():
    set_motor(A, 50)
    sleep(3000)
    set_motor(A, 0)
    sleep(500)
    for i in range(0, 3):
        wiggle(B, 40 + i * 10)
    set_dual_motor_time(A, 40, B, -40, 2.0)
    off_motor(A)
    off_motor(B)
    display_digital_tube(P1, 42)
