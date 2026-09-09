# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
# Grayscale matrix readout v2 (diagnostic encoding).
# Row 0: alive blinker (toggles every refresh).
# Rows 1..5: channel 1..5 darkness bars, capped at 6 LEDs; the 8th LED
#   of a bar row lights when that channel reads > 100 (raw > ~2600:
#   saturated/floating/unparsed data).
# Row 6: five baseline dots (columns of the 5 channels).
# Row 7: dark.
from whale import A, B, P1, sleep, get_integrated_grayscale
from whale import patrol_integrated_initialization, display_custom


def bar(v):
    h = v // 13
    if h > 6:
        h = 6
    b = 0
    m = 1
    for i in range(0, 8):
        if i < h:
            b = b + m
        m = m * 2
    if v > 100:
        b = b + 128
    return b


def main():
    patrol_integrated_initialization(A, 0, B, 0)
    sleep(500)
    t = 0
    while True:
        r1 = bar(get_integrated_grayscale(1))
        r2 = bar(get_integrated_grayscale(2))
        r3 = bar(get_integrated_grayscale(3))
        r4 = bar(get_integrated_grayscale(4))
        r5 = bar(get_integrated_grayscale(5))
        t = t + 1
        if t // 2 * 2 == t:
            r0 = 255
        else:
            r0 = 0
        display_custom(P1, r0, r1, r2, r3, r4, r5, 31, 0)
        sleep(300)
