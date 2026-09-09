/* api_wrappers.c -- user API wrappers over the open core (Whale
 * Instructor runtime).
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is linked into user programs, hence LGPL -- your programs
 * stay yours, see LICENSE_EXCEPTION.md. Implements the wrapper and
 * omni-drive layers of whale_api.h; the patrol entry points forward to
 * patrol.c.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "whale_api.h"

/* Audio layer entry points; the open core declares them in no header. */
uint8_t PlaySpeech(uint16_t idx);
void PlaySensorNum(uint16_t value);

/* ---------------- motors ---------------- */

void set_dual_motor_time(Motor_ID m1, int s1, Motor_ID m2, int s2,
                         float t)
{
    set_motor(m1, s1);
    set_motor(m2, s2);
    wait(t);
    set_motor(m1, 0);
    set_motor(m2, 0);
}

void set_motor_time(Motor_ID m, int s, float t)
{
    set_motor(m, s);
    wait(t);
    set_motor(m, 0);
}

/* Reference conversion, kept for its truncation semantics (2.4 counts
 * per degree, truncated toward zero); the core's angle API takes
 * degrees directly, so nothing calls it anymore. */
__attribute__((unused)) static int32_t to_encoder_value(int angle)
{
    return (int32_t)((24.0 * 36.0) / 360.0 * angle);
}

void set_dual_motor_angle(Motor_ID m1, int s1, Motor_ID m2, int s2,
                          int angle)
{
    daulMotor_angle(m1, m2, angle, s1, s2);
}

void set_motor_angle(Motor_ID m, int s, int angle)
{
    motor_angle(m, angle, s);
}

void off_motor(Motor_ID m)
{
    if (m == MotorAll) {
        set_motor(A, 0);
        set_motor(B, 0);
        set_motor(C, 0);
        set_motor(D, 0);
    } else {
        set_motor(m, 0);
    }
}

void get_motor_speed(Move_Type t, int speed, int *left, int *right)
{
    switch (t) {
    case move_forward:
        *left = speed;
        *right = -speed;
        break;
    case move_backward:
        *left = -speed;
        *right = speed;
        break;
    case move_turnleft:
        *left = -speed;
        *right = -speed;
        break;
    case move_turnright:
        *left = speed;
        *right = speed;
        break;
    default:
        *left = 0;
        *right = 0;
        break;
    }
}

void move(Move_Type t, int speed)
{
    int l, r;

    get_motor_speed(t, speed, &l, &r);
    set_motor(A, l);
    set_motor(B, r);
}

void move_time(Move_Type t, int speed, float time)
{
    int l, r;

    get_motor_speed(t, speed, &l, &r);
    set_dual_motor_time(A, l, B, r, time);
}

void stop_move(void)
{
    set_motor(A, 0);
    set_motor(B, 0);
}

/* ---------------- sound / lights / display ---------------- */

void play_sound(Sound_ID s)
{
    PlaySpeech((uint16_t)s);
}

void set_light(Port_ID p, Switch_State st)
{
    setDO(1 << (p - 1), st);
}

void set_magnet(Port_ID p, Switch_State st)
{
    setDO(1 << (p - 1), st);
}

void display_emotion(Port_ID l, Port_ID r, LED_Emoji_ID e)
{
    DisplayEye(l, r, e);
}

void off_emotion(Port_ID l, Port_ID r)
{
    LedMaritx z = {{0, 0, 0, 0, 0, 0, 0, 0}};

    DisplayCustom(l, z);
    DisplayCustom(r, z);
}

void display_symbol(Port_ID p, LED_Symbol_ID s)
{
    DisplayScreen(p, s);
}

void display_custom(Port_ID p, LedMaritx m)
{
    DisplayCustom(p, m);
}

void off_LED(Port_ID p)
{
    LedMaritx z = {{0, 0, 0, 0, 0, 0, 0, 0}};

    DisplayCustom(p, z);
}

void read_number(int n)
{
    PlaySensorNum((uint16_t)n);
}

/* Dead on this firmware (no display to print to), kept compilable and
 * harmless. */
void show(int line, char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int needed;
    char text[64] = {0};
    int i;

    va_start(ap, fmt);
    needed = vsnprintf(buf, 127, fmt, ap);
    va_end(ap);
    if (needed < 0 || needed >= 127) {
        buf[126] = '\0';
    } else {
        buf[needed] = '\0';
    }
    for (i = 1; i <= 8; i++) {
        strcat(text, i == line ? "%s" : "\n");
    }
}

/* No-ops on this firmware; kept so old programs link. */
void display_digital_tube(Port_ID p, int n)
{
    (void)p;
    (void)n;
}

void display_digital_tube_score(Port_ID p, int left, int right)
{
    (void)p;
    (void)left;
    (void)right;
}

void off_digital_tube(Port_ID p)
{
    (void)p;
}

void set_servo_angle(Servo_ID s, int speed, int angle)
{
    (void)s;
    (void)speed;
    (void)angle;
}

void set_servo_rotation(Servo_ID s, int speed)
{
    (void)s;
    (void)speed;
}

void recorder(Port_ID p)
{
    (void)p;
}

void play_record(void)
{
}

/* ---------------- servo (bus servos, m2 protocol) ---------------- */

static int angle_to_servo(int angle)
{
    return (angle * 888) / 300.0 + 512;
}

__attribute__((unused)) static int speed_to_servo(int speed)
{
    return (speed * 1023) / 200.0 + 512;
}

static int speed_to_servo_m2(int speed)
{
    int v = (speed * 1023) / 100;

    return speed < 0 ? -v : 1024 + v;
}

void set_servo_angle_m2(Servo_ID s, int32_t speed, int angle)
{
    servo_mode_set(s, 0);
    servo_ctrl(s, speed_to_servo_m2(speed), angle_to_servo(angle), 0);
}

void set_servo_rotation_m2(Servo_ID s, int32_t speed)
{
    servo_mode_set(s, 1);
    servo_ctrl(s, speed_to_servo_m2(speed), angle_to_servo(0), 1);
}

void restore_torque(void)
{
    wait(1.5);
    PO16_WriteRegister(0xfe, 16, 0);
}

/* ---------------- RGB ---------------- */

void toRGB(Color_ID c, int *r, int *g, int *b)
{
    switch (c) {
    case color_yellow:
        *r = 255;
        *g = 255;
        *b = 0;
        break;
    case color_purple:
        *r = 255;
        *g = 0;
        *b = 255;
        break;
    case color_cyan:
        *r = 0;
        *g = 255;
        *b = 255;
        break;
    case color_red:
        *r = 255;
        *g = 0;
        *b = 0;
        break;
    case color_green:
        *r = 0;
        *g = 255;
        *b = 0;
        break;
    case color_blue:
        *r = 0;
        *g = 0;
        *b = 255;
        break;
    case color_black:
        *r = 0;
        *g = 0;
        *b = 0;
        break;
    case color_white:
    default:
        *r = 255;
        *g = 255;
        *b = 255;
        break;
    }
}

void set_RGB(Port_ID p, int r, int g, int b)
{
    Set_Rgb_Color(p, r, g, b);
}

void set_RGB_color(Port_ID p, Color_ID c)
{
    int r, g, b;

    toRGB(c, &r, &g, &b);
    Set_Rgb_Color(p, r, g, b);
}

void off_RGB_color(Port_ID p)
{
    Set_Rgb_Color(p, 0, 0, 0);
}

/* ---------------- sensors ---------------- */

int get_integrated_grayscale(int port)
{
    return (int)(get_Gray(port) / 2500.0 * 100.0);
}

int integrated_grayscale_detected(int port, Line_Type lt)
{
    int v = get_Gray(port);

    return lt == black_line ? v > 1500 : v < 500;
}

int get_infrared_distance(int port)
{
    return (int)(JY_AI(port) / 4000.0 * 100.0);
}

int obstacle_infrared_detected(int port)
{
    return JY_AI(port) < 500;
}

int touch_switch_pressed(int port)
{
    return JY_DO(port);
}

int get_ambient_light(int port)
{
    return (int)(JY_AI(port) / 4000.0 * 100.0);
}

int get_single_grayscale(int port)
{
    return (int)(JY_AI(port) / 4000.0 * 100.0);
}

int single_grayscale_detected(int port, Line_Type lt)
{
    int v = JY_AI(port);

    return lt == black_line ? v > 2000 : v < 800;
}

int get_flame(int port)
{
    return (int)(JY_AI(port) / 4000.0 * 100.0);
}

int magnetic_detected(int port)
{
    return JY_DO(port);
}

int get_ultrasonic_distance(int port)
{
    return get_ultra_distance(port);
}

float timer(void)
{
    return seconds();
}

void reset_timer(void)
{
    reset_time();
}

int get_sound_volume(int port)
{
    Detect_Ene(port);
    wait(0.2);
    return Detect_Ene(port);
}

int button_pressed(Key_ID k)
{
    return button_state(k);
}

int touch_event(int port)
{
    int hit = 0;

    while (touch_switch_pressed(port)) {
        hit = 1;
    }
    return hit;
}

int random_number(int lo, int hi)
{
    return rng(lo, hi);
}

int math_modulus(int a, int b)
{
    return math_mod(a, b);
}

int get_digital_input(int port)
{
    return JY_AI(port) > 2000;
}

void set_digital_output(Port_ID p, Switch_State st)
{
    setDO(1 << (p - 1), st);
}

int get_analog_input(int port)
{
    return JY_AI(port);
}

int read_EEPROM(int addr)
{
    return ReadEEPROM(addr);
}

void write_EEPROM(int addr, int val)
{
    WriteEEPROM(addr, val);
}

int get_AI_image(int port)
{
    return get_image(port);
}

int get_AI_voice(void)
{
    return 0;
}

/* Color sensor: I2C device 0x44, read 13 bytes of register 0 through
 * the port's I2C group. */
uint8_t color_value(uint8_t port)
{
    uint8_t d[16] = {0};

    if (i2c_read((wb_i2c_t *)&tI2cResource[port - 1], 0x44, 0, 0x00, 13,
                 d)) {
        return d[0];
    }
    return 255;
}

uint8_t color_detected(uint8_t port, int color_id)
{
    return color_value(port) == (uint8_t)color_id;
}

/* ---------------- patrol pass-throughs ---------------- */

void patrol_integrated_initialization(Motor_ID l, int ls, Motor_ID r,
                                      int rs)
{
    patrol_init_engine(ls, rs, l, r, 1, 2, 3, 4, 5, 1);
}

void patrol_single_initialization(Motor_ID l, int ls, Motor_ID r, int rs,
                                  Port_ID p1, Port_ID p2, Port_ID p3,
                                  Port_ID p4, Port_ID p5)
{
    patrol_init_engine(ls, rs, l, r, p1, p2, p3, p4, p5, 0);
}

void patrol_omni_wheel_integrated_init(int a, int b, int c, int d)
{
    patrol_init_engine_omni(a, b, c, d, 1, 2, 3, 4, 5, 1);
}

void patrol_omni_wheel_single_init(int a, int b, int c, int d,
                                   Port_ID p1, Port_ID p2, Port_ID p3,
                                   Port_ID p4, Port_ID p5)
{
    patrol_init_engine_omni(a, b, c, d, p1, p2, p3, p4, p5, 0);
}

void patrol_ambient_detection(void)
{
    patrol_calibrate();
}

void patrol_speed(int speed)
{
    patrol_follow(speed, 1);
}

void patrol_road(Intersection_Type t, int speed, float time)
{
    patrol_until_junction(t, speed, time);
}

void patrol_time(int speed, float t)
{
    patrol_for_time(speed, t);
}

void patrol_turn(Turn_Type tt, int ls, int rs)
{
    patrol_turn_wheel(tt, ls, rs);
}

void start_motor_time(int ls, int rs, float t)
{
    patrol_drive_time(ls, rs, t);
}

void start_motor_angle(int ls, int rs, int ang)
{
    patrol_drive_angle(ls, rs, ang);
}

void start_motor_sensor(int ls, int rs, Port_ID sp, Compare_Opt op,
                        int v)
{
    patrol_drive_sensor(ls, rs, sp, op, v);
}

void patrol_button(void)
{
    patrol_wait_button();
}

/* ---------------- digital threshold read ---------------- */

int JY_DO(int port)
{
    return port > 5 ? 0 : (JY_AI(port) > 2000);
}

/* ---------------- omni wheel ---------------- */

/* Reference float pipeline: binary32 pi and scale constants, two
 * distinct sqrt(0.5) neighbors, binary64 libm for the heading, float
 * per-motor sums truncated to int; m2/m4 negate the sum before the
 * truncation cast. */
void omni_wheel_ctrl(float speed, float angle_deg)
{
    const float PI_F = 3.14159274f;
    const float K = 3.6015625f;
    const float A = 0.70710683f;
    const float B = 0.70710677f;
    float p = angle_deg * PI_F;
    double x = (double)p / (double)K;
    double c = cos(x);
    double s = sin(x);
    int t_sin = (int)((double)speed * s);
    int t_cos = (int)((double)speed * c);
    int m1 = (int)(t_sin * A + t_cos * B);
    int m2 = (int)-(t_sin * (-A) + t_cos * B);
    int m3 = (int)(t_sin * (-A) + t_cos * (-B));
    int m4 = (int)-(t_sin * A + t_cos * (-B));

    if (m1 > 100) {
        m1 = 100;
    }
    if (m1 < -100) {
        m1 = -100;
    }
    if (m2 > 100) {
        m2 = 100;
    }
    if (m2 < -100) {
        m2 = -100;
    }
    if (m3 > 100) {
        m3 = 100;
    }
    if (m3 < -100) {
        m3 = -100;
    }
    if (m4 > 100) {
        m4 = 100;
    }
    if (m4 < -100) {
        m4 = -100;
    }
    set_motor(1, m1);
    set_motor(2, m2);
    set_motor(3, m3);
    set_motor(4, m4);
}

void omni_wheel_turn(int dir, int speed)
{
    if (dir == omni_turnleft) {
        speed = -speed;
    }
    set_motor(1, speed);
    set_motor(2, speed);
    set_motor(3, speed);
    set_motor(4, speed);
}

void omni_wheel_stop(void)
{
    set_motor(1, 0);
    set_motor(2, 0);
    set_motor(3, 0);
    set_motor(4, 0);
}
