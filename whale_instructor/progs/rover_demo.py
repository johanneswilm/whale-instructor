from whale import (A, B, P1, P2, P3, set_motor, sleep, play_sound,
                   sound_hi, patrol_integrated_initialization,
                   touch_switch_pressed, get_infrared_distance,
                   get_integrated_grayscale)


def drive(left, right):
    set_motor(A, left)
    set_motor(B, -right)


def stop():
    set_motor(A, 0)
    set_motor(B, 0)


def backup_and_avoid(left_hit):
    stop()
    sleep(80)
    drive(-42, -42)
    sleep(550)
    if left_hit:
        drive(34, -34)
    else:
        drive(-34, 34)
    sleep(600)
    stop()
    sleep(100)


def follow_line():
    g1 = get_integrated_grayscale(1)
    g2 = get_integrated_grayscale(2)
    g3 = get_integrated_grayscale(3)
    g4 = get_integrated_grayscale(4)
    g5 = get_integrated_grayscale(5)
    if g3 > 55:
        drive(42, 42)
    elif g2 > 55:
        drive(0, 42)
    elif g4 > 55:
        drive(42, 0)
    elif g1 > 55:
        drive(-34, 34)
    elif g5 > 55:
        drive(34, -34)
    else:
        return False
    return True


def seek_creature():
    d = get_infrared_distance(P3)
    if d < 10:
        stop()
        sleep(250)
    elif d < 40:
        drive(30, 30)
    else:
        drive(20, -20)


def main():
    play_sound(sound_hi)
    patrol_integrated_initialization(A, 42, B, 42)
    sleep(300)
    while True:
        if touch_switch_pressed(P1) or touch_switch_pressed(P2):
            backup_and_avoid(touch_switch_pressed(P1))
        elif follow_line():
            sleep(30)
        else:
            seek_creature()
            sleep(60)
