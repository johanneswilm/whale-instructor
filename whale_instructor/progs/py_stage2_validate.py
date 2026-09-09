# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
# Stage-2 device validation: exercises the codegen paths fixed/tested
# today (elif chains, float helper params, timer(), negative-step range,
# and/or/not, while with compound condition).
from whale import A, B, P1, set_motor, set_dual_motor_time
from whale import off_motor, sleep, display_digital_tube, timer


def blink(motor, speed, seconds):
    set_motor(motor, speed)
    sleep(seconds * 1000)
    set_motor(motor, 0)


def react(dist):
    if dist < 500:
        set_motor(A, 60)
        sleep(300)
        set_motor(A, 0)
    elif dist < 2000 and dist >= 500:
        blink(B, 40, 0.5)
    elif dist > 3500 or dist == 4095:
        blink(A, -50, 0.5)
    else:
        blink(B, -40, 0.5)


def main():
    t = timer()
    display_digital_tube(P1, 7)
    sleep(500)
    blink(A, 50, 1.0)
    for i in range(3, 0, -1):
        display_digital_tube(P1, i)
        sleep(400)
    if t < 10.0:
        blink(A, 30, 0.3)
    react(300)
    react(1000)
    react(4000)
    n = 0
    while n < 3 and not n == 2:
        n += 1
    display_digital_tube(P1, n * 10)
    set_dual_motor_time(A, 40, B, -40, 2.0)
    off_motor(A)
    off_motor(B)
    display_digital_tube(P1, 99)
