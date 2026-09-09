/* patrol.c -- line-patrol engine of the Whale Instructor runtime.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Clean-room implementation of the line-patrol engine: differential
 * and omni-wheel line following with EEPROM calibration, junction
 * handling and time/angle/sensor drives. The eleven entry points are
 * called from api_wrappers.c; their behavior -- including the
 * preserved reference quirks noted inline -- follows the engine spec
 * in the private archive tree. Like the
 * rest of the runtime this file is linked into user program images and
 * is therefore LGPL: your programs stay yours, see
 * LICENSE_EXCEPTION.md.
 */
#include <stdint.h>

#include "../open_core/wb_core.h"

/* Peer owned elsewhere: PlaySpeech() lives in the audio engine. */
uint8_t PlaySpeech(uint16_t idx);

/* Fixed engine ABI (api_wrappers.c calls these by name). */
void patrol_init_engine(int ls, int rs, int lch, int rch, int p1, int p2,
                        int p3, int p4, int p5, int type);
void patrol_init_engine_omni(int a, int b, int c, int d, int p1, int p2,
                             int p3, int p4, int p5, int type);
void patrol_calibrate(void);
void patrol_follow(int s, int l);
void patrol_until_junction(int k, int s, float t);
void patrol_for_time(int s, float t);
void patrol_turn_wheel(int a, int l, int r);
void patrol_drive_time(int l, int r, float t);
void patrol_drive_angle(int l, int r, int angle);
void patrol_drive_sensor(int l, int r, int sensor_port, int op, int value);
void patrol_wait_button(void);

#define LANG_CHINESE 0x01

/* ---------------- engine state ---------------- */

static int pow_a, pow_b, pow_c, pow_d;      /* drive power scales */
static int dl, dr;                          /* differential channels */
static int gray_type;                       /* 0 single gray, 1 five gray */
static int sm_type;                         /* 0 differential, 1 omni */
static int a1, a2, a3, a4, a5;              /* sensor map, a1 = leftmost */
static int hb[11];                          /* 1..5 black, 6..10 white cal */
static int cz1, cz2, cz3, cz4, cz5, cz_2, cz_4;
static int qd = 30;                         /* junction approach speed */
static int flag, flag1, flag2, js;          /* line-state memory */

/* ---------------- internal helpers ---------------- */

/* Calibration prompt: (Chinese, English) speech index pair. */
static void speak(int ch_idx, int en_idx)
{
    PlaySpeech((uint16_t)(get_language_type() == LANG_CHINESE ? ch_idx
                                                              : en_idx));
}

/* Line reading of one mapped sensor: five-gray direct slot or analog
 * port value. */
static int read_sensor(int port)
{
    if (gray_type == 1) {
        return get_Gray_Line((uint8_t)port);
    }
    return JY_AI(port);
}

/* One mapped sensor as a 0..100 percent value. */
static int sensor_percent(int port)
{
    if (gray_type == 1) {
        return get_Gray((uint8_t)port) * 100 / 2500;
    }
    return JY_AI(port) * 100 / 4000;
}

/* EEPROM calibration into hb[], then the derived thresholds. cz2..cz4
 * are black-white DIFFERENCES while cz1/cz5 (and cz_2/cz_4) are
 * midpoints -- the reference mixes both; keep it. */
static void load_calibration(float pause)
{
    for (int i = 0; i < 10; i++) {
        hb[i + 1] = ReadEEPROM((uint8_t)i);
    }
    wait(pause);
    cz1 = (int)((hb[1] + hb[6]) * 0.5);
    cz2 = hb[2] - hb[7];
    cz3 = hb[3] - hb[8];
    cz4 = hb[4] - hb[9];
    cz5 = (int)((hb[5] + hb[10]) * 0.5);
    cz_2 = (int)((hb[2] + hb[7]) * 0.5);
    cz_4 = (int)((hb[4] + hb[9]) * 0.5);
    qd = 30;
    flag = 0;
    flag1 = 0;
}

static void set_port_map(int p1, int p2, int p3, int p4, int p5, int type)
{
    gray_type = type;
    if (gray_type == 1) {
        a1 = 5;
        a2 = 4;
        a3 = 3;
        a4 = 2;
        a5 = 1;
    } else {
        a1 = p1;
        a2 = p2;
        a3 = p3;
        a4 = p4;
        a5 = p5;
    }
}

/* Scaled drive: the int products truncate at /100, and the omni
 * negation applies after that division. */
static void yd(int l, int r)
{
    if (sm_type == 0) {
        set_motor(dl, pow_a * l / 100);
        set_motor(dr, pow_b * r / 100);
    } else {
        set_motor(1, pow_a * l / 100);
        set_motor(2, -(pow_b * r / 100));
        set_motor(3, -(pow_c * r / 100));
        set_motor(4, pow_d * l / 100);
    }
}

static void stop_driven(void)
{
    if (sm_type == 0) {
        set_motor(dl, 0);
        set_motor(dr, 0);
    } else {
        set_motor(1, 0);
        set_motor(2, 0);
        set_motor(4, 0);
        set_motor(3, 0);
    }
}

/* Junction tick counter: increments only while an outer line is
 * latched, saturating at 5000. */
static void js_latch(void)
{
    if (flag2 > 0) {
        js++;
    }
    if (js > 5000) {
        js = 5000;
    }
}

static int sgn(int v)
{
    return (v > 0) - (v < 0);
}

/* Angle-drive core: every selected channel runs its own closed loop to
 * an encoder-count target of ds, with the direction taken from its
 * scaled drive value (a zero value stops that channel immediately). */
static void drive_ds(int l, int r, int ds)
{
    if (sm_type == 0) {
        int sl = pow_a * l / 100;
        int sr = pow_b * r / 100;
        int sp[5] = {0, 0, 0, 0, 0};
        int mask = 0;

        if (dl >= 1 && dl <= 4) {
            sp[dl] = sgn(sl) * ds;
            mask |= 1 << (dl - 1);
        }
        if (dr >= 1 && dr <= 4) {
            sp[dr] = sgn(sr) * ds;
            mask |= 1 << (dr - 1);
        }
        set_motor_ad(mask, sp[1], sp[2], sp[3], sp[4]);
    } else {
        set_motor_ad(15, sgn(pow_a * l / 100) * ds,
                     -sgn(pow_b * r / 100) * ds,
                     -sgn(pow_c * r / 100) * ds,
                     sgn(pow_d * l / 100) * ds);
    }
    flag = flag1 = flag2 = 0;
}

/* ---------------- engine entry points ---------------- */

void patrol_init_engine(int ls, int rs, int lch, int rch, int p1, int p2,
                        int p3, int p4, int p5, int type)
{
    pow_a = ls;
    pow_b = rs;
    dl = lch;
    dr = rch;
    sm_type = 0;
    set_port_map(p1, p2, p3, p4, p5, type);
    load_calibration(0.2);
}

void patrol_init_engine_omni(int a, int b, int c, int d, int p1, int p2,
                             int p3, int p4, int p5, int type)
{
    pow_a = a;
    pow_b = b;
    pow_c = c;
    pow_d = d;
    sm_type = 1;
    set_port_map(p1, p2, p3, p4, p5, type);
    load_calibration(0.1);
}

void patrol_calibrate(void)
{
    int s1, s2, s3, s4, s5;
    int t1, t2, t3, t4, t5;
    int ok;

    button_state(1);
    speak(48, 71);                          /* "place the gray line" */
    while (GetCh() != 2) {
    }
    s1 = read_sensor(a1);
    s2 = read_sensor(a2);
    s3 = read_sensor(a3);
    s4 = read_sensor(a4);
    s5 = read_sensor(a5);
    for (int i = 0; i < 25; i++) {
        s1 = (read_sensor(a1) + s1) / 2;
        s2 = (read_sensor(a2) + s2) / 2;
        s3 = (read_sensor(a3) + s3) / 2;
        s4 = (read_sensor(a4) + s4) / 2;
        s5 = (read_sensor(a5) + s5) / 2;
    }
    speak(49, 70);                          /* "place the white line" */
    while (GetCh() != 2) {
    }
    clr_Goline_KeyFlag();
    t1 = read_sensor(a1);
    t2 = read_sensor(a2);
    t3 = read_sensor(a3);
    t4 = read_sensor(a4);
    t5 = read_sensor(a5);
    for (int i = 0; i < 25; i++) {
        t1 = (read_sensor(a1) + t1) / 2;
        t2 = (read_sensor(a2) + t2) / 2;
        t3 = (read_sensor(a3) + t3) / 2;
        t4 = (read_sensor(a4) + t4) / 2;
        t5 = (read_sensor(a5) + t5) / 2;
    }
    wait(0.1);
    WriteEEPROM(0, (uint16_t)s1);
    WriteEEPROM(1, (uint16_t)s2);
    WriteEEPROM(2, (uint16_t)s3);
    WriteEEPROM(3, (uint16_t)s4);
    WriteEEPROM(4, (uint16_t)s5);
    WriteEEPROM(5, (uint16_t)t1);
    WriteEEPROM(6, (uint16_t)t2);
    WriteEEPROM(7, (uint16_t)t3);
    WriteEEPROM(8, (uint16_t)t4);
    WriteEEPROM(9, (uint16_t)t5);
    WriteEEPROM(10, 0);
    wait(0.1);
    for (int i = 0; i < 10; i++) {
        hb[i + 1] = ReadEEPROM((uint8_t)i);
    }
    ok = hb[1] - hb[6] > 700 && hb[2] - hb[7] > 700 && hb[3] - hb[8] > 700
         && hb[4] - hb[9] > 700 && hb[5] - hb[10] > 700;
    if (ok) {
        speak(50, 73);                      /* "check successful" */
    } else {
        speak(51, 74);                      /* "check failed" */
    }
}

/* One follower tick: s = nominal speed, l picks which outer line is
 * checked first (0 = a1/left, nonzero = a5/right). */
void patrol_follow(int s, int l)
{
    int raw2, raw3, raw4;
    int pct2, pct3, pct4;

    if (gray_type == 1) {
        raw2 = get_Gray_Line((uint8_t)a2);
        raw3 = get_Gray_Line((uint8_t)a3);
        raw4 = get_Gray_Line((uint8_t)a4);
    } else {
        raw2 = JY_AI(a2);
        raw3 = JY_AI(a3);
        raw4 = JY_AI(a4);
    }
    pct2 = (raw2 - hb[7]) * 100 / cz2;
    pct3 = (raw3 - hb[8]) * 100 / cz3;
    pct4 = (raw4 - hb[9]) * 100 / cz4;

    if (l == 0) {
        if (read_sensor(a1) > cz1) {
            flag2 = 1;
            js = 0;
        }
        if (read_sensor(a5) > cz5) {
            flag2 = 2;
            js = 0;
        }
    } else {
        if (read_sensor(a5) > cz5) {
            flag2 = 2;
            js = 0;
        }
        if (read_sensor(a1) > cz1) {
            flag2 = 1;
            js = 0;
        }
    }
    if (js >= 350) {
        flag2 = 0;
        js = 0;
    }

    if (pct3 > 80 || (pct3 >= pct2 && pct3 >= pct4 && pct3 > 20)) {
        js_latch();
        if (pct2 > pct4 && pct2 > 5) {
            if (pct3 > 80) {
                if (pct2 - pct4 < 20) yd(s * 0.95, s);
                else if (pct2 - pct4 < 40) yd(s * 0.90, s);
                else if (pct2 - pct4 < 60) yd(s * 0.85, s);
                else yd(s * 0.80, s);
            } else if (pct3 > 75) yd(s * 0.85, s);
            else if (pct3 > 70) yd(s * 0.80, s);
            else if (pct3 > 65) yd(s * 0.75, s);
            else if (pct3 > 60) yd(s * 0.70, s);
            else if (pct3 > 55) yd(s * 0.65, s);
            else if (pct3 > 50) yd(s * 0.60, s);
            else if (pct3 > 45) yd(s * 0.55, s);
            else if (pct3 > 40) yd(s * 0.50, s);
            else if (pct3 > 35) yd(s * 0.45, s);
            else yd(s * 0.40, s);
            flag = 3;
        } else if (pct4 > pct2 && pct4 > 5) {
            if (pct3 > 80) {
                if (pct4 - pct2 < 20) yd(s, s * 0.95);
                else if (pct4 - pct2 < 40) yd(s, s * 0.90);
                else if (pct4 - pct2 < 60) yd(s, s * 0.85);
                else yd(s, s * 0.80);
            } else if (pct3 > 75) yd(s, s * 0.85);
            else if (pct3 > 70) yd(s, s * 0.80);
            else if (pct3 > 65) yd(s, s * 0.75);
            else if (pct3 > 60) yd(s, s * 0.70);
            else if (pct3 > 55) yd(s, s * 0.65);
            else if (pct3 > 50) yd(s, s * 0.60);
            else if (pct3 > 45) yd(s, s * 0.55);
            else if (pct3 > 40) yd(s, s * 0.50);
            else yd(s, s * 0.40);
            flag = 3;
        } else {
            yd(s, s);
            flag = 0;
        }
    } else if (pct2 > pct4 && pct2 > 25) {
        js_latch();
        if (pct3 > 60) { yd(s * 0.70, s); flag = 3; }
        else if (pct3 > 55) { yd(s * 0.65, s); flag = 3; }
        else if (pct3 > 50) { yd(s * 0.60, s); flag = 3; }
        else if (pct3 > 45) { yd(s * 0.55, s); flag = 3; }
        else if (pct3 > 40) { yd(s * 0.50, s); flag = 3; }
        else if (pct3 > 35) { yd(s * 0.45, s); flag = 3; }
        else if (pct3 > 30) { yd(s * 0.40, s); flag = 3; }
        else if (pct3 > 25) { yd(s * 0.35, s); flag = 1; }
        else if (pct3 > 20) { yd(s * 0.30, s); flag = 1; }
        else if (pct3 > 15) { yd(s * 0.25, s); flag = 1; }
        else if (pct3 > 10) { yd(s * 0.20, s); flag = 1; }
        else if (pct2 > 70) { yd(s * 0.10, s); flag = 1; }
        else if (pct2 > 60) { yd(s * 0.05, s); flag = 1; }
        else if (pct2 > 50) { yd(s * 0.00, s); flag = 1; }  /* 0 kept */
        else if (pct2 > 40) { yd(-s * 0.05, s); flag = 1; }
        else if (pct2 > 20) { yd(-s * 0.10, s); flag = 1; }
    } else if (pct4 > pct2 && pct4 > 25) {
        js_latch();
        if (pct3 > 60) { yd(s, s * 0.70); flag = 3; }
        else if (pct3 > 55) { yd(s, s * 0.65); flag = 3; }
        else if (pct3 > 50) { yd(s, s * 0.60); flag = 3; }
        else if (pct3 > 45) { yd(s, s * 0.55); flag = 3; }
        else if (pct3 > 40) { yd(s, s * 0.50); flag = 3; }
        else if (pct3 > 35) { yd(s, s * 0.45); flag = 3; }
        else if (pct3 > 30) { yd(s, s * 0.40); flag = 3; }
        else if (pct3 > 25) { yd(s, s * 0.35); flag = 2; }
        else if (pct3 > 20) { yd(s, s * 0.30); flag = 2; }
        else if (pct3 > 15) { yd(s, s * 0.25); flag = 2; }
        else if (pct3 > 10) { yd(s, s * 0.20); flag = 2; }
        else if (pct3 > 5) { yd(s, s * 0.15); flag = 2; }
        else if (pct4 > 70) { yd(s, s * 0.10); flag = 2; }
        else if (pct4 > 60) { yd(s, s * 0.05); flag = 2; }
        else if (pct4 > 50) { yd(s, s * 0.00); flag = 2; }  /* 0 kept */
        else if (pct4 > 40) { yd(s, -s * 0.05); flag = 2; }
        else if (pct4 > 20) { yd(s, -s * 0.10); flag = 2; }
    } else {
        if (flag2 == 1 && pct2 < 5) yd(-s * 0.3, s);
        else if (flag2 == 2 && pct4 < 5) yd(s, -s * 0.3);
        else if (flag == 1) {
            if (pct2 > 10) yd(-s * 0.15, s);
            else if (pct2 > 5) yd(-s * 0.20, s);
        } else if (flag == 2) {
            if (pct4 > 10) yd(s, -s * 0.15);
            else if (pct4 > 5) yd(s, -s * 0.20);
        } else {
            if (read_sensor(a1) > cz1) yd(-s * 0.3, s);
            else if (read_sensor(a5) > cz5) yd(s, -s * 0.3);
        }
    }
}

/* Follow to a junction, k: 0 = a1/left, 1 = cross (a1+a5), 2 = a5/right,
 * anything else = inner pair (a2+a4). Then hold s for t seconds. */
void patrol_until_junction(int k, int s, float t)
{
    float t1 = -6000, t5 = -5000;   /* sentinels differ so |t1-t5| starts big */
    float t2 = 0.0, t3 = 0.07;

    yd(s, s);
    t2 = seconds();
    if (flag1 == 0 && s > qd) {
        patrol_follow(qd, k);
        while ((seconds() - t2) < 0.1 && read_sensor(a1) < cz1
               && read_sensor(a5) < cz5) {
            patrol_follow(qd, k);
        }
    }
    if (k == 1) {
        while ((t1 - t5) > t3 || (t1 - t5) < -t3) {
            if (read_sensor(a1) > cz1) {
                t1 = seconds();
            }
            if (read_sensor(a5) > cz5) {
                t5 = seconds();
            }
            patrol_follow(s, k);
        }
        while (read_sensor(a1) > cz1 * 0.8 || read_sensor(a5) > cz5 * 0.8) {
            patrol_follow(s, k);
        }
    } else if (k == 0) {
        while (read_sensor(a1) < cz1) {
            patrol_follow(s, k);
        }
        while (read_sensor(a1) > cz1 * 0.8) {
            patrol_follow(s, k);
        }
    } else if (k == 2) {
        while (read_sensor(a5) < cz5) {
            patrol_follow(s, k);
        }
        while (read_sensor(a5) > cz5 * 0.8) {
            patrol_follow(s, k);
        }
    } else {
        while ((t1 - t5) > t3 || (t1 - t5) < -t3) {
            if (read_sensor(a2) > cz_2) {
                t1 = seconds();
            }
            if (read_sensor(a4) > cz_4) {
                t5 = seconds();
            }
            patrol_follow(s, k);
        }
        /* Plain drive below, not follow -- as in the reference. */
        while (read_sensor(a2) > cz_2 * 0.8 || read_sensor(a4) > cz_4 * 0.8) {
            yd(s, s);
        }
        while (read_sensor(a1) > cz1 * 0.8 || read_sensor(a5) > cz5 * 0.8) {
            yd(s, s);
        }
    }
    yd(s, s);
    wait(t);
    yd(0, 0);
    flag1 = 1;
}

void patrol_for_time(int s, float t)
{
    float t1;

    yd(s, s);
    t1 = seconds();
    if (flag1 == 0 && s > qd) {
        patrol_follow(qd, 1);
        while ((seconds() - t1) < 0.1 && (seconds() - t1) < t) {
            patrol_follow(qd, 1);
        }
    }
    while ((seconds() - t1) < t) {
        patrol_follow(s, 1);
    }
    flag1 = 1;
    yd(0, 0);
}

/* Sweep off/onto the line: a 0 = left, 1 = center, 2 = right. */
void patrol_turn_wheel(int a, int l, int r)
{
    int mid1 = (int)((hb[1] + hb[6]) * 0.5);
    int mid2 = (int)((hb[2] + hb[7]) * 0.5);
    int mid3 = (int)((hb[3] + hb[8]) * 0.5);
    int mid4 = (int)((hb[4] + hb[9]) * 0.5);
    int mid5 = (int)((hb[5] + hb[10]) * 0.5);

    yd(l, r);
    if (l < r) {
        while (read_sensor(a1) < mid1) {
        }
        while (read_sensor(a2) < mid2) {
        }
        if (a == 1) {
            while (read_sensor(a3) < mid3) {
            }
        }
        if (a == 2) {
            while (read_sensor(a3) < mid3) {
            }
            while (read_sensor(a4) < mid4) {
            }
        }
    } else {
        while (read_sensor(a5) < mid5) {
        }
        while (read_sensor(a4) < mid4) {
        }
        if (a == 1) {
            while (read_sensor(a3) < mid3) {
            }
        }
        if (a == 0) {
            while (read_sensor(a3) < mid3) {
            }
            while (read_sensor(a2) < mid2) {
            }
        }
    }
    yd(-l, -r);
    wait(0.05);
    yd(0, 0);
    flag = flag1 = flag2 = 0;
}

void patrol_drive_time(int l, int r, float t)
{
    yd(l, r);
    wait(t);
    stop_driven();
    flag = flag1 = flag2 = 0;
}

void patrol_drive_angle(int l, int r, int angle)
{
    int ds = angle * 34 * 24 / 360;

    drive_ds(l, r, ds);
}

/* Drive until the mapped sensor passes the comparison: op 1 less_than,
 * 2 greater_than, 3 equal, 4 not_equal. */
void patrol_drive_sensor(int l, int r, int sensor_port, int op, int value)
{
    yd(l, r);
    for (;;) {
        int rd = sensor_percent(sensor_port);
        int keep;

        if (op == 1) {
            keep = rd > value;
        } else if (op == 2) {
            keep = rd < value;
        } else if (op == 3) {
            keep = rd == value;
        } else if (op == 4) {
            /* Reference quirk kept: single-gray not_equal spins on
             * equality. */
            keep = (gray_type == 1) ? rd != value : rd == value;
        } else {
            keep = 0;
        }
        if (!keep) {
            break;
        }
    }
    stop_driven();
    flag = flag1 = flag2 = 0;
}

void patrol_wait_button(void)
{
    button_state(1);
    speak(52, 72);                          /* "push the one key" */
    while (GetCh() != 2) {
    }
    clr_Goline_KeyFlag();
}
