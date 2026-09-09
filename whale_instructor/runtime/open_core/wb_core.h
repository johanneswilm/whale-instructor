/* wb_core.h -- shared declarations for the Whale Instructor open core.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * The open core is linked into user program images (like the rest of the
 * Whale Instructor runtime), hence LGPL -- your programs stay yours, see
 * LICENSE_EXCEPTION.md. See README.md in this directory for the module
 * map, the hardware facts and the device validation record.
 */
#ifndef WB_CORE_H
#define WB_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "stm32f10x.h"

/* ---------------- byte queue (replaces vendor user_queue.o) ------------
 * Struct layout must match the one the vendor code instantiates:
 * buffer pointer, size, head, tail, fill level, peek state, peek count. */
typedef struct {
    uint8_t *pchBuffer;
    uint16_t hwSize;
    uint16_t hwHead;
    uint16_t hwTail;
    uint16_t hwLength;
    uint16_t hwPeek;
    uint16_t hwPeekCnt;
} byte_queue_t;

bool init_byte_queue(byte_queue_t *ptQueue, uint8_t *pchByte, uint16_t hwSize);
bool enqueue(byte_queue_t *ptQueue, uint8_t chByte);
bool dequeue(byte_queue_t *ptQueue, uint8_t *pchByte);
bool peek_byte_queue(byte_queue_t *ptQueue, uint8_t *pchByte);
bool reset_peek_byte(byte_queue_t *ptQueue);
bool get_all_peeked_byte(byte_queue_t *ptQueue);

/* ---------------- board support (replaces vendor bsp.o) ---------------- */

/* Bit-banged I2C interface descriptor. One per hardware group; the
 * concrete pin assignment of each group is the board's port wiring (see
 * README.md). Layout matches the vendor I2cInit_t: SDA port/RCC/pin,
 * then SCL port/RCC/pin. */
typedef struct {
    GPIO_TypeDef *pSdaPort;
    uint32_t nSdaRcc;
    uint16_t hwSdaPin;
    GPIO_TypeDef *pSclPort;
    uint32_t nSclRcc;
    uint16_t hwSclPin;
} wb_i2c_t;

/* One entry per smart-sensor I2C group, indexed by the group numbers the
 * vendor code passes (0..3). Populated from the board wiring; the GPIO
 * RCC enable bits live in the vendor struct as APB2 flags. */
extern const wb_i2c_t tI2cResource[4];

/* On-board status RGB: color/state selector the ISR animation reads. */
extern uint8_t RGBCOLOR;
extern uint8_t RGBMODE;

/* 5-in-1 grayscale slots. Slot order is fixed by the sensor wiring:
 * Gray[0] = port 5 ... Gray[4] = port 1. Filled by the vendor UART5
 * frame parser, which is why this must be a 5 x uint16_t array. */
extern uint16_t Gray[5];

/* UART5 receive ring for the smart-sensor port line, filled by the
 * vendor UART5 interrupt handler. 64-byte frame buffer + counters. */
extern uint8_t Uart5RxBuf[64];
extern uint32_t Uart5RxCnt;
extern uint32_t Uart5RxFrameState;

void bsp_init(void);

/* Master transactions: chOptAddWidth 0 sends one option-address byte,
 * 1 sends a second high byte first. Returns false on missing ACK. */
bool i2c_write(wb_i2c_t *pI2c, uint8_t chDeviceAdd, uint8_t chOptAddWidth,
               uint16_t hwOptAdd, uint16_t hwOptLength, uint8_t *pchBuff);
bool i2c_read(wb_i2c_t *pI2c, uint8_t chDeviceAdd, uint8_t chOptAddWidth,
              uint16_t hwOptAdd, uint16_t hwOptLength, uint8_t *pchBuff);

/* Smart-sensor command dispatcher on a port's I2C group: the sensor
 * family decides the 7-bit device address and the transfer shape
 * (1-byte config read, 8-byte setup write, 2-byte counter write,
 * 3-byte command write or single-byte value read). */
bool sensor_operation(uint8_t chChannel, uint8_t chType, uint8_t *pchData);

/* Integrated grayscale: port 1..5 (normalized raw slot value). */
uint16_t get_Gray(uint8_t chChannel);
/* Direct slot access: slot 1..5 -> Gray[0..4] (sensor slot order). */
uint16_t get_Gray_Line(uint8_t chChannel);

/* Status LED: index into the color table (0 red, 1 green, 2 blue,
 * 3 white, 4 black, 5 yellow, 6 magenta, 7 cyan); mode 0 flash, 1 on,
 * 2 off, 3 fast flash (see RGB_Serverloop_ISR). */
void SetRGB(uint8_t chColor, uint8_t chMode);
/* Drive the three LED sink pins directly from a color-table row. */
void SetLight(uint8_t chColor);
/* Periodic status-LED animation; called from a vendor timer tick. */
void RGB_Serverloop_ISR(void);

/* Audio plumbing shared with the vendor speech engine: microphone
 * capture setup per port channel, sample timer (TIM7) and the DAC2
 * playback DMA on channel 4 (PA5 = DAC_OUT2). */
void voice_record_interface_init(uint16_t *phwBuffer, uint8_t chChannel);
void voice_recoder_timer_init(TIM_TypeDef *TIMx, uint32_t nSampleRate);
void voice_recoder_timer_deinit(void);
uint8_t voice_recoder_packet_ready(void);
void voice_echo_interface_init(void);
void voice_dac_dma_init(uint32_t nAddress, uint16_t hwLength);

/* ---------------- display (replaces vendor GUI.o) ---------------- */

/* 8x8 matrix pattern: row[0] is the first row, bit i of a row byte =
 * column i. Matches the vendor LedMaritx struct layout/ABI exactly. */
typedef struct {
    uint8_t row[8];
} wb_led_t;

void DigitScan(void);
void DisplayCustom(uint8_t chPort, wb_led_t rows);
void DisplayScreen(uint8_t chPort, uint16_t hwSymbol);
void DisplayEye(uint8_t chLeft, uint8_t chRight, uint16_t hwEmoji);
int Eyes_API(void);
void ScreenDisplay_API(void);
void clr_display(void);
void set_display_num(uint16_t hwNum);
void set_display_program_idx(uint8_t chIdx);
void display_run_program_idx(uint8_t chIdx);
void CheckTemp(int nTemp);

/* ---------------- control (replaces vendor Control.o) -------------- */

void adc_init(void);
void pwm_init(void);
void encoder_init(void);
void init_control_tab(void);
void init_program_space(void);
void _TIM6_Init(void);
void bus_init(void);
void sFLASH_Init(void);
void Key_Scan_Initial(void);

void set_motor(int nMotor, int nSpeed);
void set_motor_ad(int nMask, int nS1, int nS2, int nS3, int nS4);
void motor_angle(uint8_t chMotor, int nAngle, int nSpeed);
void daulMotor_angle(uint8_t chLeft, uint8_t chRight, int nAngle,
                     int nLeftSpeed, int nRightSpeed);
void setDO(int nMask, int nOnOff);
int JY_AI(int nChannel);
float seconds(void);
void reset_time(void);
int rng(int nMin, int nMax);
int math_mod(int nA, int nB);
void MainPower(int nOn);
int get_language_type(void);
uint16_t ReadEEPROM(uint8_t chIdx);
void WriteEEPROM(uint8_t chIdx, uint16_t hwValue);
void BT_mode(uint8_t chMode);
int button_state(int nKey);
uint16_t TstCh(void);
uint16_t GetCh(void);
void clr_Goline_KeyFlag(void);
int Detect_Ene(int nChannel);
int get_image(uint8_t chPort);
int get_ultra_distance(uint8_t chPort);
void Set_Rgb_Color(uint8_t chPort, uint8_t chR, uint8_t chG, uint8_t chB);
uint8_t servo_ctrl(int nId, int nSpeed, int nPos, int nMode);
void servo_mode_set(int nId, int nMode);
uint8_t PO16_WriteRegister(int nId, int nReg, int nVal);
void USB_Scan_Connect(void);
void camera_sensor_task(void);
void hid_download_task(void);
void hid_rx_buffer_handle(void);
void SPI_SetFlashAddress(uint32_t nAddress);
uint32_t SPI_ReadLongFromFlash(void);
void SPI_ReadnByteFromFlash(uint8_t *pchBuf, uint16_t hwLen);

extern uint16_t hwBuff1[512];
extern uint16_t hwBuff2[512];

/* ---------------- debug usart (replaces vendor usart.o) ---------------- */

/* printf character sink: blocking write to USART1. */
int fputc(int ch, FILE *f);
/* libsys exit hook: the controller has nowhere to go, stall quietly. */
void _sys_exit(int rc);

/* ---------------- seconds-scale helpers shared with the app layer --- */

/* Blocking seconds delay (FreeRTOS vTaskDelay behind the scenes). */
void wait(float sec);
/* One-shot built-in speaker clip (idx into the built-in table; mode 1
 * = jingle flavor, 0 = plain). Returns 1 when armed. */
uint8_t InitSound(int idx, uint8_t chMode);
/* Speech-file playback from the SPI data flash (index = file number,
 * count word lives at flash offset 0). */
uint8_t PlaySpeech(uint16_t idx);
void PlaySensorNum(uint16_t value);

#endif /* WB_CORE_H */
