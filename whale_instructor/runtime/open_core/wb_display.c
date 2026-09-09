/* wb_display.c -- face digit scan + LED matrix output for the MC101s.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Whale Instructor open core: the multiplexed face digit scan, the
 * matrix protocol (I2C device 0x51 on the port's group, command byte 2
 * then 8 row bytes), the number/program-index buffers and the emotion
 * engine. Glyph shapes and eye expressions below are original Whale
 * Instructor
 * artwork; the 7-segment digit encodings are the universal
 * common-anode patterns dictated by the board wiring (see README.md).
 */
#include "wb_core.h"
#include <stm32f10x_gpio.h>
#include <stm32f10x_rcc.h>

/* Vendor core members the screen task pumps (Control.o). */
extern void camera_sensor_task(void);
extern void USB_Scan_Connect(void);
extern void hid_rx_buffer_handle(void);
extern void hid_download_task(void);
extern void wait(float time);
extern void MainPower(int on);

/* ---------------- face digit hardware map ---------------- */
/* 8 segment lines (incl. decimal point on PB0) + 4 digit commons,
 * all open drain: low = LED on. Segment bit n of a pattern drives: */
static const struct { GPIO_TypeDef *port; uint16_t pin; } SegPins[7] = {
    {GPIOE, GPIO_Pin_7},   /* bit 0 */
    {GPIOE, GPIO_Pin_11},  /* bit 1 */
    {GPIOA, GPIO_Pin_7},   /* bit 2 */
    {GPIOB, GPIO_Pin_1},   /* bit 3 */
    {GPIOB, GPIO_Pin_2},   /* bit 4 */
    {GPIOE, GPIO_Pin_8},   /* bit 5 */
    {GPIOA, GPIO_Pin_6},   /* bit 6 */
};
static const struct { GPIO_TypeDef *port; uint16_t pin; } CommonPins[4] = {
    {GPIOB, GPIO_Pin_10},
    {GPIOB, GPIO_Pin_11},
    {GPIOE, GPIO_Pin_10},
    {GPIOE, GPIO_Pin_9},
};
#define DP_PORT GPIOB
#define DP_PIN  GPIO_Pin_1

/* 7-segment patterns, active low (bit set = segment dark). Universal
 * digit shapes; 10 'P', 11 blank, 12-14 running markers, 18 minus. */
static const uint8_t wb_seg[19] = {
    0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x82, 0xF8, 0x80, 0x90,
    0x0C, 0xFF, 0x88, 0xC1, 0xC8, 0xAF, 0xE3, 0xAB, 0xBF,
};

uint8_t smgbuf[4] = {11, 11, 11, 11};
uint8_t dp;
uint8_t scan;

/* The eighth "segment" line (the vendor's dp byte) is the shared
 * bottom bar of the four digits: bit i selects it during digit i's
 * scan phase. Light it for every glyph whose pattern includes the
 * bottom (pattern bit 3 clear = bottom present). */
static void wb_update_bottom(void)
{
    dp = 0;
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t pat = (smgbuf[i] < 19) ? wb_seg[smgbuf[i]] : wb_seg[11];
        if (!(pat & 0x08)) {
            dp |= (uint8_t)(1u << i);
        }
    }
}

/* ---------------- matrix protocol ---------------- */
/* 8x8 glyph bitmaps, bit i of a row byte = column i, row 0 first.
 * Index = LED_Symbol_ID (1..53); entry 0 unused. Original artwork. */
static const uint8_t wb_picture[54][8] = {
    {0},
    {0x00, 0x1C, 0x22, 0x2A, 0x24, 0x10, 0x00, 0x08},            /* ? */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00},            /* ! */
    {0x0E, 0x11, 0x3E, 0x10, 0x1C, 0x12, 0x3D, 0x00},            /* $ */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00},            /* yen */
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00},            /* = */
    {0x08, 0x08, 0x1F, 0x08, 0x08, 0x00, 0x00, 0x00},            /* + */
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00},            /* - */
    {0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00},            /* x */
    {0x04, 0x04, 0x00, 0x1F, 0x00, 0x04, 0x04, 0x00},            /* div */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00},            /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00},            /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00},            /* 2 */
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E, 0x00},            /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00},            /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00},            /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00},            /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00},            /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00},            /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00},            /* 9 */
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00},            /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00},            /* B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00},            /* C */
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00},            /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00},            /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00},            /* F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F, 0x00},            /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00},            /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00},            /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00},            /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00},            /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00},            /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11, 0x00},            /* M */
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11, 0x00},            /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},            /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00},            /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00},            /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00},            /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00},            /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00},            /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},            /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00},            /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11, 0x00},            /* W */
    {0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11, 0x00},            /* X */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00},            /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00},            /* Z */
    {0x0E, 0x1F, 0x1F, 0x1F, 0x1F, 0x0E, 0x04, 0x00},            /* heart */
    {0x0A, 0x1F, 0x1F, 0x1F, 0x0E, 0x04, 0x00, 0x00},            /* heart s */
    {0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04, 0x00},            /* up */
    {0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04, 0x00},            /* down */
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00},            /* left */
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00},            /* right */
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x12, 0x0F, 0x00},            /* G+O */
    {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E, 0x00},            /* stop */
};

/* Eye expressions, one 8x8 frame per matrix (LED_Emoji_ID 1..12).
 * Original artwork; index 0 unused. */
static const uint8_t wb_emoji[13][8] = {
    {0},
    {0x1C, 0x22, 0x41, 0x5D, 0x41, 0x22, 0x1C, 0x00},            /* big eye */
    {0x00, 0x00, 0x00, 0x1F, 0x11, 0x11, 0x00, 0x00},            /* smile */
    {0x00, 0x11, 0x11, 0x1F, 0x00, 0x00, 0x00, 0x00},            /* sad */
    {0x1F, 0x00, 0x00, 0x11, 0x11, 0x1F, 0x00, 0x00},            /* sly */
    {0x1C, 0x22, 0x41, 0x41, 0x41, 0x22, 0x1C, 0x00},            /* shocked */
    {0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00, 0x00},            /* angry > < */
    {0x1C, 0x22, 0x41, 0x41, 0x22, 0x1C, 0x08, 0x08},            /* tears */
    {0x1C, 0x22, 0x5D, 0x55, 0x5D, 0x22, 0x1C, 0x00},            /* greedy */
    {0x0A, 0x15, 0x15, 0x15, 0x15, 0x15, 0x0A, 0x00},            /* heart eye */
    {0x11, 0x0A, 0x04, 0x0A, 0x11, 0x04, 0x0A, 0x11},            /* dizzy */
    {0x00, 0x00, 0x1F, 0x00, 0x00, 0x1F, 0x00, 0x00},            /* flat */
};
static const uint8_t wb_eye_blink[8] = {
    0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00,
};

/* ---------------- state ---------------- */
static uint8_t wb_screen_mode;
static uint8_t wb_left_port;
static uint8_t wb_right_port;
static uint16_t wb_display_id;
static uint16_t wb_eye_tick;

static uint8_t wb_draw_buf[9];

/* Push 8 bytes to a matrix module: device 0x51, option-address 2.
 * Slots are the physical ports 1..4; port 5 has no I2C group. */
static void wb_matrix_send(uint8_t chSlot, const uint8_t *pchRows)
{
    if (chSlot > 3) {
        return;
    }
    wb_draw_buf[0] = 2;
    for (uint8_t i = 0; i < 8; i++) {
        wb_draw_buf[1 + i] = pchRows[i];
    }
    i2c_write((wb_i2c_t *)&tI2cResource[chSlot], 0x51, 0, 2, 8,
              &wb_draw_buf[1]);
}

/* ---------------- public API ---------------- */

void DisplayCustom(uint8_t chPort, wb_led_t rows)
{
    wb_screen_mode = 3;
    wb_matrix_send(chPort - 1, rows.row);
}

void DisplayScreen(uint8_t chPort, uint16_t hwSymbol)
{
    wb_screen_mode = 2;
    uint8_t rows[8];
    const uint8_t *glyph;
    if (hwSymbol < 54) {
        glyph = wb_picture[hwSymbol];
    } else {
        glyph = wb_picture[0];
    }
    for (uint8_t i = 0; i < 8; i++) {
        rows[i] = glyph[i];
    }
    wb_matrix_send(chPort - 1, rows);
}

void DisplayEye(uint8_t chLeft, uint8_t chRight, uint16_t hwEmoji)
{
    wb_left_port = chLeft - 1;
    wb_right_port = chRight - 1;
    wb_display_id = hwEmoji;
    wb_screen_mode = 1;
}

/* Periodic eye animation, called from the vendor ServerLoop. Pushes
 * the expression with a periodic blink overlay on both eye matrices. */
int Eyes_API(void)
{
    if (wb_screen_mode != 1) {
        return 0;
    }
    wb_eye_tick++;
    const uint8_t *rows;
    if (wb_display_id >= 1 && wb_display_id <= 12) {
        rows = wb_emoji[wb_display_id];
    } else {
        rows = wb_emoji[1];
    }
    if ((wb_eye_tick & 0x1F) >= 30) {
        rows = wb_eye_blink;
    }
    wb_matrix_send(wb_left_port, rows);
    wb_matrix_send(wb_right_port, rows);
    return 1;
}

/* Screen task pump: picture mode refreshes the emotion; otherwise the
 * camera + USB/HID download machinery runs (vendor behavior). */
void ScreenDisplay_API(void)
{
    if (wb_screen_mode == 1) {
        const uint8_t *rows;
        if (wb_display_id >= 1 && wb_display_id <= 12) {
            rows = wb_emoji[wb_display_id];
        } else {
            rows = wb_emoji[1];
        }
        wb_matrix_send(wb_left_port, rows);
        wb_matrix_send(wb_right_port, rows);
        return;
    }
    camera_sensor_task();
    USB_Scan_Connect();
    hid_rx_buffer_handle();
    hid_download_task();
}

void clr_display(void)
{
    for (uint8_t i = 0; i < 4; i++) {
        smgbuf[i] = 11;
    }
    wb_update_bottom();
}

void set_display_program_idx(uint8_t chIdx)
{
    smgbuf[0] = 11;
    smgbuf[1] = chIdx;
    smgbuf[2] = 10;
    smgbuf[3] = 11;
    wb_update_bottom();
}

void display_run_program_idx(uint8_t chIdx)
{
    smgbuf[0] = chIdx;
    smgbuf[1] = 14;
    smgbuf[2] = 13;
    smgbuf[3] = 12;
    wb_update_bottom();
}

void set_display_num(uint16_t hwNum)
{
    int16_t hwValue = (int16_t)hwNum;
    uint8_t chMinus = 0;
    uint8_t chShown = 0;
    smgbuf[0] = 11;
    smgbuf[1] = 11;
    smgbuf[2] = 11;
    smgbuf[3] = 11;
    if (hwValue < 0) {
        hwValue = -hwValue;
        if (hwValue > 999) {
            hwValue = 999;
        }
        chMinus = 1;
    }
    if (hwValue >= 1000) {
        smgbuf[3] = hwValue / 1000;
        hwValue %= 1000;
        chShown = 1;
    }
    if (hwValue > 99) {
        smgbuf[2] = hwValue / 100;
        hwValue %= 100;
        chShown = 1;
    } else if (chShown) {
        smgbuf[2] = 0;
    }
    if (hwValue > 9) {
        smgbuf[1] = hwValue / 10;
        hwValue %= 10;
        chShown = 1;
    } else if (chShown) {
        smgbuf[1] = 0;
    }
    smgbuf[0] = hwValue;
    if (chMinus) {
        smgbuf[3] = 18;
    }
    wb_update_bottom();
}

/* Multiplexed face digit scan, called from the vendor 1 ms tick: one
 * of the four digits per call (12.5 Hz refresh per digit at 2 kHz... at
 * 1 kHz per digit 250 Hz, plenty). Pattern bits are active low. */
void DigitScan(void)
{
    scan++;
    if (scan > 3) {
        scan = 0;
    }
    /* All 12 pins high (LEDs off) before selecting the next digit. */
    for (uint8_t i = 0; i < 4; i++) {
        GPIO_SetBits(CommonPins[i].port, CommonPins[i].pin);
    }
    for (uint8_t i = 0; i < 7; i++) {
        GPIO_SetBits(SegPins[i].port, SegPins[i].pin);
    }
    GPIO_SetBits(DP_PORT, DP_PIN);

    uint8_t chPattern = (smgbuf[scan] < 19) ? wb_seg[smgbuf[scan]] : wb_seg[11];
    GPIO_ResetBits(CommonPins[scan].port, CommonPins[scan].pin);
    for (uint8_t i = 0; i < 7; i++) {
        if (chPattern & (1u << i)) {
            GPIO_SetBits(SegPins[i].port, SegPins[i].pin);
        } else {
            GPIO_ResetBits(SegPins[i].port, SegPins[i].pin);
        }
    }
    /* The "dp" line is segment d's drive repeated for the current
     * digit (PB1), driven consistently with the segment loop; PB0
     * (the strip's decimal-point line) stays low from digit_init,
     * which keeps the dots dark like the vendor app. */
    uint8_t chDpBit = (uint8_t)(1u << scan);
    if (dp & chDpBit) {
        GPIO_ResetBits(DP_PORT, DP_PIN);
    } else {
        GPIO_SetBits(DP_PORT, DP_PIN);
    }
}

/* Thermal guard (vendor semantics): >60 degrees for 1000 calls cuts
 * main power. Called by the vendor control loop if it needs it. */
void CheckTemp(int nTemp)
{
    static uint16_t hwHotTicks;
    if (nTemp > 60) {
        if (hwHotTicks < 32767) {
            hwHotTicks++;
        }
    } else {
        hwHotTicks = 0;
    }
    if (hwHotTicks > 1000) {
        wait(1.0f);
        MainPower(0);
        while (1) {
        }
    }
}
