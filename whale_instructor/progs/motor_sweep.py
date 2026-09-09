# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later

from whale import A, B, C, D, set_motor, off_motor, set_dual_motor_time
from whale import sleep, set_display_num
from whale import get_encoder_value, reset_motor_encoder


def sweep(motor, idx):
    set_display_num(idx)
    set_motor(motor, 50)
    sleep(1000)
    set_motor(motor, -50)
    sleep(1000)
    set_motor(motor, 0)
    sleep(500)


def main():
    sweep(A, 1)
    sweep(B, 2)
    sweep(C, 3)
    sweep(D, 4)
    set_dual_motor_time(A, 40, B, -40, 2.0)
    off_motor(A)
    off_motor(B)
    reset_motor_encoder(A)
    set_motor(A, 40)
    sleep(1000)
    set_motor(A, 0)
    set_display_num(abs(get_encoder_value(A)))
