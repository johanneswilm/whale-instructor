# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
# Open-core sensor-path validation: reads all 5 channels of the 5-in-1
# integrated grayscale through the smart-sensor chain (vendor patr_init
# -> open-core i2c/UART5 -> Gray[] -> get_Gray port map).
#
# Watch protocol: motor B wiggles N times to announce channel N, then a
# ~1.2 s window follows in which motor C runs iff that channel reads
# dark (value > 60, black line under the sensor window).
from whale import A, B, C, sleep, set_motor, set_motor_time
from whale import get_integrated_grayscale, patrol_integrated_initialization


def wiggle(times):
    for i in range(0, times):
        set_motor_time(B, 40, 0.2)
        sleep(200)


def main():
    patrol_integrated_initialization(A, 0, B, 0)
    sleep(800)
    while True:
        for ch in range(1, 6):
            wiggle(ch)
            v = get_integrated_grayscale(ch)
            if v > 60:
                set_motor(C, 50)
                sleep(1200)
            else:
                sleep(1200)
            set_motor(C, 0)
            sleep(200)
