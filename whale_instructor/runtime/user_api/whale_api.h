/* whale_api.h -- public C API of the Whale Instructor runtime.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This header is part of the Whale Instructor runtime that user programs
 * are compiled against. It is deliberately LGPL (not GPL): programs you
 * write with Whale Instructor stay YOUR property under any terms you
 * choose. See LICENSE_EXCEPTION.md and THIRD_PARTY_NOTICES.md.
 */
#ifndef WHALE_API_H
#define WHALE_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"

#include "../open_core/wb_core.h"

/* Compat with handwritten C from the old header chain. */
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

/* ---------------- type names ---------------- */

typedef enum { MotorAll = 0, A = 1, B = 2, C = 3, D = 4 } Motor_ID;
typedef enum { P1 = 1, P2 = 2, P3 = 3, P4 = 4, P5 = 5 } Port_ID;
typedef enum { key_enter = 1, key_left = 2, key_right = 3 } Key_ID;
typedef enum { switch_off = 0, switch_on = 1 } Switch_State;
typedef enum { black_line = 1, white_line = 2 } Line_Type;
typedef enum { move_forward = 1, move_backward = 2,
               move_turnleft = 3, move_turnright = 4 } Move_Type;
typedef enum { omni_turnright = 0, omni_turnleft = 1 } Omni_Turn_Type;
typedef enum { intersection_left = 0, intersection_T = 1,
               intersection_right = 2 } Intersection_Type;
typedef enum { turn_left = 0, turn_center = 1, turn_right = 2 } Turn_Type;
typedef enum { compare_less_than = 1, compare_greater_than = 2,
               compare_equal = 3, compare_not_equal = 4 } Compare_Opt;
typedef enum { color_white = 1, color_yellow = 2, color_purple = 3,
               color_cyan = 4, color_red = 5, color_green = 6,
               color_blue = 7, color_black = 8 } Color_ID;
typedef enum { LED_emoji_eye = 1, LED_emoji_smile = 2, LED_emoji_sad = 3,
               LED_emoji_naughty = 4, LED_emoji_surprised = 5,
               LED_emoji_flare = 6, LED_emoji_tears = 7,
               LED_emoji_avarice = 8, LED_emoji_beckoning = 9,
               LED_emoji_anger = 10, LED_emoji_dizzy = 11,
               LED_emoji_grim = 12 } LED_Emoji_ID;
typedef enum { S1 = 1, S2 = 2, S3 = 3, S4 = 4, S5 = 5, S6 = 6, S7 = 7,
               S8 = 8, S9 = 9, S10 = 10, S11 = 11, S12 = 12, S13 = 13,
               S14 = 14, S15 = 15, S16 = 16, S17 = 17, S18 = 18 } Servo_ID;

/* Value = speech file index on the controller's data flash. */
typedef enum {
    sound_hi = 1, sound_welcome = 2, sound_thanks = 3,
    sound_concerned = 4, sound_bye = 5,
    sound_duck = 6, sound_bird = 7, sound_horse = 8, sound_sheep = 9,
    sound_cat = 10, sound_dog = 11, sound_cattle = 12,
    sound_dinosaur = 13, sound_cock = 14,
    sound_airplane = 15, sound_helicopter = 16, sound_horn = 17,
    sound_automobile = 18, sound_cannon = 19, sound_tank = 20,
    sound_brake = 21,
    sound_heartbeat = 22, sound_laugh = 23, sound_wow = 24,
    sound_whistling = 25,
    sound_piano_do = 26, sound_piano_re = 27, sound_piano_mi = 28,
    sound_piano_fa = 29, sound_piano_so = 30, sound_piano_la = 31,
    sound_piano_si = 32, sound_piano_DO = 33,
    sound_press_key = 46,
    sound_hi_en = 54, sound_welcome_en = 55, sound_concerned_en = 56,
    sound_thanks_en = 57, sound_bye_en = 58
} Sound_ID;

/* 1-based LED matrix symbol table (53 entries). */
typedef enum {
    LED_symbol_question_mark = 1, LED_symbol_exclamation = 2,
    LED_symbol_dollar = 3, LED_symbol_RMB = 4, LED_symbol_equal = 5,
    LED_symbol_plus = 6, LED_symbol_minus = 7, LED_symbol_multiplied = 8,
    LED_symbol_divided = 9,
    LED_symbol_0 = 10, LED_symbol_1, LED_symbol_2, LED_symbol_3,
    LED_symbol_4, LED_symbol_5, LED_symbol_6, LED_symbol_7,
    LED_symbol_8, LED_symbol_9,
    LED_symbol_A = 20, LED_symbol_B, LED_symbol_C, LED_symbol_D,
    LED_symbol_E, LED_symbol_F, LED_symbol_G, LED_symbol_H,
    LED_symbol_I, LED_symbol_J, LED_symbol_K, LED_symbol_L,
    LED_symbol_M, LED_symbol_N, LED_symbol_O, LED_symbol_P,
    LED_symbol_Q, LED_symbol_R, LED_symbol_S, LED_symbol_T,
    LED_symbol_U, LED_symbol_V, LED_symbol_W, LED_symbol_X,
    LED_symbol_Y, LED_symbol_Z,
    LED_symbol_big_heart = 46, LED_symbol_little_heart = 47,
    LED_symbol_forward = 48, LED_symbol_backward = 49,
    LED_symbol_turnleft = 50, LED_symbol_turnright = 51,
    LED_symbol_GO = 52, LED_symbol_stop = 53,
    /* alias the palette exposes for the left-arrow glyph */
    LED_symbol_left = 50
} LED_Symbol_ID;

/* AI camera module image classes (module unsupported on this
 * controller; kept for program compatibility). */
typedef enum {
    AI_image_0 = 32, AI_image_1 = 16, AI_image_2 = 28, AI_image_3 = 26,
    AI_image_4 = 9, AI_image_5 = 8, AI_image_6 = 24, AI_image_7 = 23,
    AI_image_8 = 7, AI_image_9 = 15,
    AI_image_up = 30, AI_image_down = 5, AI_image_turn_left = 12,
    AI_image_turn_right = 22, AI_image_turn_around_left = 29,
    AI_image_turn_around_right = 31, AI_image_peach = 18,
    AI_image_pear = 19, AI_image_cherry = 2, AI_image_apple = 0,
    AI_image_banana = 1, AI_image_mouse = 14, AI_image_cattle = 17,
    AI_image_tiger = 27, AI_image_rabbit = 21, AI_image_dragon = 6,
    AI_image_snake = 25, AI_image_horse = 11, AI_image_goat = 10,
    AI_image_monkey = 13, AI_image_chicken = 3, AI_image_dog = 4,
    AI_image_pig = 20
} AI_Image_ID;

/* AI voice module commands (module unsupported on this controller). */
typedef enum {
    AI_voice_stop = 9, AI_voice_forward = 1, AI_voice_turnright = 4,
    AI_voice_backward = 2, AI_voice_startup = 8, AI_voice_singsong = 5,
    AI_voice_turnleft = 3, AI_voice_start = 7, AI_voice_lightoff = 12,
    AI_voice_lighton = 11, AI_voice_dance = 6, AI_voice_turnaround = 1
} AI_Voice_ID;

/* 8x8 matrix pattern, passed by value (ABI: 8-byte struct). */
typedef wb_led_t LedMaritx;

/* Status-LED / framework constants. */
enum { LEDC_RED = 0, LEDC_GREEN = 1, LEDC_BLUE = 2, LEDC_WHITE = 3,
       LEDC_BLACK = 4, LEDC_YELLOW = 5, LEDC_MAGENTA = 6,
       LEDC_CYAN = 7 };
enum { LEDM_FLASH = 0, LEDM_ON = 1, LEDM_OFF = 2, LEDM_FASTFLASH = 3 };
#define LANG_CHINESE 0x01
#define LANG_ENGLISH 0x02
#define KEY_ENTER_CODE 1
#define KEY_GRAY_CODE  2
#define EEPROM_PROGRAM_IDX 164
#define EEPROM_SELECTED_PROGRAM_IDX 165

/* ---------------- wrapper API ---------------- */

void set_dual_motor_time(Motor_ID m1, int s1, Motor_ID m2, int s2,
                         float t);
void set_motor_time(Motor_ID m, int s, float t);
void set_dual_motor_angle(Motor_ID m1, int s1, Motor_ID m2, int s2,
                          int angle);
void set_motor_angle(Motor_ID m, int s, int angle);
void off_motor(Motor_ID m);
void get_motor_speed(Move_Type t, int speed, int *left, int *right);
void move(Move_Type t, int speed);
void move_time(Move_Type t, int speed, float time);
void stop_move(void);

void play_sound(Sound_ID s);
void set_light(Port_ID p, Switch_State st);
void set_magnet(Port_ID p, Switch_State st);
void display_emotion(Port_ID l, Port_ID r, LED_Emoji_ID e);
void off_emotion(Port_ID l, Port_ID r);
void display_symbol(Port_ID p, LED_Symbol_ID s);
void display_custom(Port_ID p, LedMaritx m);
void off_LED(Port_ID p);
void read_number(int n);
void show(int line, char *fmt, ...);

void set_servo_angle(Servo_ID s, int speed, int angle);
void set_servo_rotation(Servo_ID s, int speed);
void set_servo_angle_m2(Servo_ID s, int32_t speed, int angle);
void set_servo_rotation_m2(Servo_ID s, int32_t speed);
void restore_torque(void);
void display_digital_tube(Port_ID p, int n);
void display_digital_tube_score(Port_ID p, int left, int right);
void off_digital_tube(Port_ID p);

void set_RGB(Port_ID p, int r, int g, int b);
void set_RGB_color(Port_ID p, Color_ID c);
void off_RGB_color(Port_ID p);
void toRGB(Color_ID c, int *r, int *g, int *b);

void recorder(Port_ID p);
void play_record(void);

int get_integrated_grayscale(int port);
int integrated_grayscale_detected(int port, Line_Type lt);
int get_infrared_distance(int port);
int obstacle_infrared_detected(int port);
int touch_switch_pressed(int port);
int get_ambient_light(int port);
int get_single_grayscale(int port);
int single_grayscale_detected(int port, Line_Type lt);
int get_flame(int port);
int magnetic_detected(int port);
int get_ultrasonic_distance(int port);
float timer(void);
void reset_timer(void);
int get_sound_volume(int port);
int button_pressed(Key_ID k);
int touch_event(int port);
int random_number(int lo, int hi);
int math_modulus(int a, int b);
int get_digital_input(int port);
void set_digital_output(Port_ID p, Switch_State st);
int get_analog_input(int port);
int read_EEPROM(int addr);
void write_EEPROM(int addr, int val);
int get_AI_image(int port);
int get_AI_voice(void);
uint8_t color_value(uint8_t port);
uint8_t color_detected(uint8_t port, int color_id);

/* ---------------- line patrol ---------------- */

void patrol_integrated_initialization(Motor_ID l, int ls, Motor_ID r,
                                      int rs);
void patrol_single_initialization(Motor_ID l, int ls, Motor_ID r, int rs,
                                  Port_ID p1, Port_ID p2, Port_ID p3,
                                  Port_ID p4, Port_ID p5);
void patrol_omni_wheel_integrated_init(int a, int b, int c, int d);
void patrol_omni_wheel_single_init(int a, int b, int c, int d,
                                   Port_ID p1, Port_ID p2, Port_ID p3,
                                   Port_ID p4, Port_ID p5);
void patrol_ambient_detection(void);
void patrol_speed(int speed);
void patrol_road(Intersection_Type t, int speed, float time);
void patrol_time(int speed, float t);
void patrol_turn(Turn_Type tt, int ls, int rs);
void start_motor_time(int ls, int rs, float t);
void start_motor_angle(int ls, int rs, int ang);
void start_motor_sensor(int ls, int rs, Port_ID sp, Compare_Opt op,
                        int v);
void patrol_button(void);

/* Patrol engine (internal; implemented in patrol.c). */
void patrol_init_engine(int ls, int rs, int lch, int rch,
                        int p1, int p2, int p3, int p4, int p5, int type);
void patrol_init_engine_omni(int a, int b, int c, int d,
                             int p1, int p2, int p3, int p4, int p5,
                             int type);
void patrol_calibrate(void);
void patrol_follow(int s, int l);
void patrol_until_junction(int k, int s, float t);
void patrol_for_time(int s, float t);
void patrol_turn_wheel(int a, int l, int r);
void patrol_drive_time(int l, int r, float t);
void patrol_drive_angle(int l, int r, int angle);
void patrol_drive_sensor(int l, int r, int sensor_port, int op,
                         int value);
void patrol_wait_button(void);

/* ---------------- omni drive / digital read ---------------- */

void omni_wheel_ctrl(float speed, float angle_deg);
void omni_wheel_turn(int dir, int speed);
void omni_wheel_stop(void);
int JY_DO(int port);

/* ---------------- app framework ----------------
 * wait() is declared in wb_core.h and defined in app_main.c, which also
 * defines music() and PlaySound(). */

void user_main(void);
void user_task1(void);
void user_task2(void);
void user_task3(void);
void user_task4(void);
void user_task5(void);
void user_task6(void);
void user_task7(void);
void user_task8(void);
void user_task9(void);
void user_task10(void);
void user_task11(void);
void user_task12(void);
void user_task13(void);
void user_task14(void);
void user_task15(void);

/* ---------------- declarations provided elsewhere ----------------
 * Implemented by the open core or left intentionally undefined (the
 * linker is the truth); repeated so handwritten C gets prototypes.
 * int32_t == int on this target. */

typedef struct { uint8_t chValue; uint8_t chEvent; } user_key_t;

int32_t AI(int32_t nChannel);
int32_t digital_input(int32_t nChannel);
int Get_Button(void);
int Get_KeyVal(void);
int Get_KeyVSC(void);
void Printf(char *fmt, ...);
int encoder(int nMotor);
int CntClear(int nMotor);
int CntStart(int nMotor);
void CntStop(int nMotor);
void Cntinital(void);
int32_t get_battery(void);
void Sound_Play(int a, int b, uint32_t c, uint32_t d);
void BEEP(int a, int b, uint32_t c, int d);
void sFLASH_WriteBuffer(uint8_t *buf, uint32_t addr, uint16_t len);
void sFLASH_ReadBuffer(uint8_t *buf, uint32_t addr, uint16_t len);
float get_temperature(uint8_t ch);
float get_humidity(uint8_t ch);
void reverse_motor(uint32_t m);
void user_printf(char *s);
void wait_button(uint8_t ch);
uint32_t switch_state(uint32_t n);
uint8_t eeprom_read(uint16_t addr);
void eeprom_write(uint16_t addr, uint16_t val);
void set_motor_app(int32_t a, int32_t b);
void pid_setpi(float p, float i);
float pid_getp(void);
float pid_geti(void);
uint8_t GetRecValue(void);
uint8_t voice_record_echo(uint8_t ch);
uint32_t get_bt_remote_control(void);
void set_bt_remote_control(uint32_t mask);
uint16_t SPI_ReadWordFromFlash(void);
void JY_Motor_Distance(uint8_t m, int32_t a, int32_t b);
void JY_Motor_Time(uint8_t m, int32_t a, int32_t b);
void JY_DaulMotor_Distance(uint8_t m1, uint8_t m2, int32_t a, int32_t b,
                           int32_t c);
void JY_DaulMotor_Time(uint8_t m1, uint8_t m2, int32_t a, int32_t b,
                       int32_t c);
uint32_t motor_encoder(uint8_t m);
uint8_t get_key(user_key_t *key);
void switch_user_code(void);
int Get_Temperate(void);
void display_screen(int n);
void clear_screen(void);
bool get_gSensor_Acc(uint8_t ch, uint16_t *buf);
uint16_t get_gSensor_Temp(uint8_t ch);
bool get_gSensor_Groy(uint8_t ch, uint16_t *buf);
uint16_t get_gSensor_data(uint8_t ch, uint16_t *buf);
int math_round(double x);
double math_abs(double x);
double math_floor(double x);
double math_ceiling(double x);
double math_sqrt(double x);
double math_sin(double x);
double math_cos(double x);
double math_tan(double x);
double math_asin(double x);
double math_acos(double x);
double math_atan(double x);
double math_ln(double x);
double math_log(double x);
double math_exp(double x);
double math_pow10(double x);

/* Motor encoders (defined in the open core, declared nowhere else). */
int get_encoder_value(int nMotor);
void reset_motor_encoder(uint8_t chMotor);

#endif /* WHALE_API_H */
