# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later

from whale import sleep, read_number, set_display_num


def main():
    for i in range(1, 11):
        set_display_num(i)
        read_number(i)
        sleep(1500)
    set_display_num(0)
