# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
# Object style: device objects over the flat whale API, keyword arguments
# and the abs/min/max/wait helpers on top of the whale dialect.  The same
# program builds to C and runs live over Bluetooth.

from whale import (Motor, TouchSensor, InfraredSensor,
                   A, B, P3, P5, wait)


def main():
    """Drive forward until the touch sensor is pressed."""
    left = Motor(A)
    right = Motor(B)
    bumper = TouchSensor(P5)
    eyes = InfraredSensor(P3)

    base = 40
    left.set(base)
    right.set(speed=base)
    closest = 4000
    while not bumper.pressed():
        dist = eyes.value()
        closest = min(closest, dist)
        if dist < 300:
            left.set(base / 2)
            right.set(-base)
        else:
            left.set(base)
            right.set(base)
        wait(50)

    left.off()
    right.off()
    gap = abs(closest - 4000)
    left.set_angle(speed=60, degrees=90)
    for i in range(0, gap // 1000):
        right.set_angle(60, -45)
