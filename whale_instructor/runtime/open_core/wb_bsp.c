/* wb_bsp.c -- Whale Instructor board support for the MC101s controller.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Whale Instructor open core. Replaces the proprietary bsp.o member of
 * the vendor archive (mercury_core.a): board bring-up, bit-banged I2C
 * master, smart-sensor port access, status LED, debug/WDT/misc plumbing.
 * Written from the board's own hardware facts (pin map, peripheral
 * wiring, on-wire protocol shapes documented in README.md); the vendor
 * Control/GUI/USB members keep working on top of it unchanged.
 */
#include "wb_core.h"
#include <stm32f10x_gpio.h>
#include <stm32f10x_rcc.h>
#include <stm32f10x_usart.h>
#include <stm32f10x_adc.h>
#include <stm32f10x_dac.h>
#include <stm32f10x_dma.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_iwdg.h>
#include <misc.h>

/* adc/pwm/encoder init live in wb_control.c; it brings them up in the
 * right order relative to the I2C groups (shared pins). */
extern void adc_init(void);
extern void pwm_init(void);
extern void encoder_init(void);
extern void init_control_tab(void);
extern void init_program_space(void);
extern void _TIM6_Init(void);
extern void bus_init(void);
extern void sFLASH_Init(void);
extern void Key_Scan_Initial(void);

/* Measured LSI frequency used to derive the IWDG reload value. */
uint32_t LsiFreq = 102976;

/* ---------------------------------------------------------------------
 * Board wiring (extracted hardware facts -- see README.md):
 *   - smart-sensor I2C groups (bit-banged):
 *       0: SDA PB8 / SCL PB9   1: SDA PD6 / SCL PD7
 *       2: SDA PD5 / SCL PD12  3: SDA PD0 / SCL PD1
 *   - status LED sinks (open drain, lit = pin low):
 *       PE13 red, PE14 green, PE15 blue
 *   - face digit column/segment pins (open drain):
 *       PE7 PE11 PA7 PB1 PB2 PE8 PA6 PB0 PE9 PE10 PB11 PB10
 *   - port digital outputs: PE0..PE4 (ports 1..5)
 *   - keys: PE5, PC10 (pull-up inputs)
 *   - battery-empty detect: PE12 (pull-up input)
 *   - USB cable detect: PA10 (pull-down input), D+ pull control: PD3
 *   - bus power enable: PA4, controller power hold: PE6
 *   - Bluetooth module: USART2 PA2/PA3, power/reset: PC13
 *   - smart-sensor port line: UART5 PC12/PD2, 115200 8N1
 *   - audio amp shutdown: PC15, mic inputs: PC0..PC4 -> ADC2_IN10..14,
 *     speaker: DAC2_OUT2 (PA5) fed by DMA2 channel 4, timed by TIM7.
 * ------------------------------------------------------------------- */
const wb_i2c_t tI2cResource[4] = {
    {GPIOB, RCC_APB2Periph_GPIOB, GPIO_Pin_8,
     GPIOB, RCC_APB2Periph_GPIOB, GPIO_Pin_9},
    {GPIOD, RCC_APB2Periph_GPIOD, GPIO_Pin_6,
     GPIOD, RCC_APB2Periph_GPIOD, GPIO_Pin_7},
    {GPIOD, RCC_APB2Periph_GPIOD, GPIO_Pin_5,
     GPIOD, RCC_APB2Periph_GPIOD, GPIO_Pin_12},
    {GPIOD, RCC_APB2Periph_GPIOD, GPIO_Pin_0,
     GPIOD, RCC_APB2Periph_GPIOD, GPIO_Pin_1},
};

/* Status LED color table rows: red, green, blue, white, black, yellow,
 * magenta, cyan (values mirror the bsp.h color enum order). */
static const uint8_t ColorTab[8][3] = {
    {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1},
    {0, 0, 0}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1},
};

/* Face digit pins, scan order used by the GUI code. */
static const struct { GPIO_TypeDef *port; uint16_t pin; } DigitPins[12] = {
    {GPIOE, GPIO_Pin_7}, {GPIOE, GPIO_Pin_11}, {GPIOA, GPIO_Pin_7},
    {GPIOB, GPIO_Pin_1}, {GPIOB, GPIO_Pin_2},  {GPIOE, GPIO_Pin_8},
    {GPIOA, GPIO_Pin_6}, {GPIOB, GPIO_Pin_0},  {GPIOE, GPIO_Pin_9},
    {GPIOE, GPIO_Pin_10}, {GPIOB, GPIO_Pin_11}, {GPIOB, GPIO_Pin_10},
};

/* Microphone ADC channel per port (ports 1..5 -> PC0..PC4). */
static const uint8_t MicChannels[5] = {10, 11, 12, 13, 14};

uint8_t RGBCOLOR;
uint8_t RGBMODE;
uint16_t Gray[5];
uint8_t Uart5RxBuf[64];
uint32_t Uart5RxCnt;
uint32_t Uart5RxFrameState;
static uint8_t chIicMutex;
static uint16_t RGBCnt;

/* ---------------- private prototypes ---------------- */
static void digit_init(void);
static void InitUSBCheck(void);
void InitUSBPULL(void);
static void bus_pwr_interface_init(void);
static void power_ctrl_interface_init(void);
static void bt_power_init(void);
static void InitDryBatDetect(void);
static void key_interface_init(void);
static void DO_interface_init(void);
static void RGB_interface_init(void);
static void audio_interface_init(void);
static void i2c_inteface_init(wb_i2c_t *pI2c);
static bool setIicMutex(wb_i2c_t *pI2c);
static void clrIicMutex(wb_i2c_t *pI2c);
static void i2c_delay(void);
static bool i2c_sda_direction_config(wb_i2c_t *pI2c, uint8_t chDirection);
static bool i2c_sda_output(wb_i2c_t *pI2c, uint8_t chBit);
static bool i2c_sda_read_pin(wb_i2c_t *pI2c, uint8_t *pchBit);
static bool i2c_scl_output(wb_i2c_t *pI2c, uint8_t chBit);
static bool i2c_write_byte(wb_i2c_t *pI2c, uint8_t chByte);
static bool i2c_read_byte(wb_i2c_t *pI2c, uint8_t *pchByte);
static bool i2c_check_ack(wb_i2c_t *pI2c);
static bool i2c_send_ack(wb_i2c_t *pI2c, uint8_t chBit);
static bool i2c_trans_start(wb_i2c_t *pI2c);
static bool i2c_trans_stop(wb_i2c_t *pI2c);
static void test_delay(uint32_t nMilliSeconds);
static void InitWDT(void);
static void bt_interface_init(void);
static void usart5_init(void);

/* ---------------- small GPIO helpers ---------------- */

static void wb_gpio_out(GPIO_TypeDef *port, uint16_t pin, GPIOSpeed_TypeDef speed)
{
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin = pin;
    cfg.GPIO_Speed = speed;
    cfg.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(port, &cfg);
}

static void wb_gpio_od(GPIO_TypeDef *port, uint16_t pin, GPIOSpeed_TypeDef speed)
{
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin = pin;
    cfg.GPIO_Speed = speed;
    cfg.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(port, &cfg);
}

static void wb_gpio_in(GPIO_TypeDef *port, uint16_t pin, uint8_t chMode)
{
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin = pin;
    cfg.GPIO_Speed = GPIO_Speed_2MHz;
    cfg.GPIO_Mode = (GPIOMode_TypeDef)chMode;
    GPIO_Init(port, &cfg);
}

/* ---------------- board bring-up ---------------- */

void bsp_init(void)
{
    InitWDT();
    power_ctrl_interface_init();
    InitDryBatDetect();
    /* USB stays down until a device is seen; the app slot has no
     * download duties, so the USB/DAC2 interrupts stay masked here. */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, DISABLE);
    NVIC_DisableIRQ((IRQn_Type)59);
    NVIC_DisableIRQ((IRQn_Type)20);
    NVIC_DisableIRQ((IRQn_Type)42);
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    bt_power_init();
    key_interface_init();
    sFLASH_Init();
    InitUSBCheck();
    init_program_space();
    digit_init();
    adc_init();
    DO_interface_init();
    RGB_interface_init();
    bus_pwr_interface_init();
    bus_init();
    i2c_inteface_init((wb_i2c_t *)tI2cResource);
    bt_interface_init();
    usart5_init();
    audio_interface_init();
    /* Motor PWM/encoder setup runs after every peripheral that shares
     * pins with it (the I2C groups borrow PD12 = TIM4_CH1 and friends;
     * the vendor boot order also brought Control.o up last). */
    pwm_init();
    encoder_init();
    init_control_tab();
    _TIM6_Init();
}

static void digit_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOE, ENABLE);
    for (uint8_t i = 0; i < 12; i++) {
        wb_gpio_od(DigitPins[i].port, DigitPins[i].pin, GPIO_Speed_10MHz);
    }
}

static void InitUSBCheck(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    wb_gpio_in(GPIOA, GPIO_Pin_10, GPIO_Mode_IPD);
}

/* Pulsing the D+ pull control line forces the host to re-enumerate;
 * the vendor USB scan (Control.o) calls this on plug events. The burn
 * loop must stay (matches the vendor's settle delay). */
void InitUSBPULL(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    wb_gpio_out(GPIOD, GPIO_Pin_3, GPIO_Speed_2MHz);
    GPIO_SetBits(GPIOD, GPIO_Pin_3);
    for (volatile uint32_t i = 0; i <= 29999; i++) {
    }
    GPIO_ResetBits(GPIOD, GPIO_Pin_3);
}

static void bus_pwr_interface_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    wb_gpio_out(GPIOA, GPIO_Pin_4, GPIO_Speed_50MHz);
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
}

static void power_ctrl_interface_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
    wb_gpio_out(GPIOE, GPIO_Pin_6, GPIO_Speed_50MHz);
    GPIO_SetBits(GPIOE, GPIO_Pin_6);
}

static void bt_power_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    wb_gpio_out(GPIOC, GPIO_Pin_13, GPIO_Speed_50MHz);
    GPIO_ResetBits(GPIOC, GPIO_Pin_13);
}

static void InitDryBatDetect(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
    wb_gpio_in(GPIOE, GPIO_Pin_12, GPIO_Mode_IPU);
}

static void key_interface_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOE, ENABLE);
    wb_gpio_in(GPIOE, GPIO_Pin_5, GPIO_Mode_IPU);
    wb_gpio_in(GPIOC, GPIO_Pin_10, GPIO_Mode_IPU);
    Key_Scan_Initial();
}

static void DO_interface_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE, ENABLE);
    for (uint8_t i = 0; i < 5; i++) {
        wb_gpio_out(GPIOE, (uint16_t)(1u << i), GPIO_Speed_2MHz);
    }
}

/* ---------------- status LED ---------------- */

void SetRGB(uint8_t chColor, uint8_t chMode)
{
    for (uint8_t i = 0; i < 3; i++) {
        if (ColorTab[chColor][i]) {
            GPIO_ResetBits(GPIOE, (uint16_t)(0x2000u << i));
        } else {
            GPIO_SetBits(GPIOE, (uint16_t)(0x2000u << i));
        }
    }
    RGBCOLOR = chColor;
    RGBMODE = chMode;
}

void SetLight(uint8_t chColor)
{
    for (uint8_t i = 0; i < 3; i++) {
        if (ColorTab[chColor][i]) {
            GPIO_ResetBits(GPIOE, (uint16_t)(0x2000u << i));
        } else {
            GPIO_SetBits(GPIOE, (uint16_t)(0x2000u << i));
        }
    }
}

/* LED animation modes: 0 flash (1 s), 1 steady, 2 off, 3 fast flash.
 * Called from the vendor 1 ms system tick. */
void RGB_Serverloop_ISR(void)
{
    if (RGBMODE == 0) {
        RGBCnt++;
        if (RGBCnt > 1000) {
            SetLight(4);
            RGBCnt = 0;
        } else if (RGBCnt > 500) {
            SetLight(RGBCOLOR);
        } else {
            SetLight(4);
        }
    } else if (RGBMODE == 1) {
        SetLight(RGBCOLOR);
    } else if (RGBMODE == 2) {
        SetLight(4);
    } else if (RGBMODE == 3) {
        RGBCnt++;
        if (RGBCnt > 200) {
            SetLight(4);
            RGBCnt = 0;
        } else if (RGBCnt > 100) {
            SetLight(RGBCOLOR);
        } else {
            SetLight(4);
        }
    }
}

static void RGB_interface_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
    for (uint8_t i = 0; i < 3; i++) {
        wb_gpio_od(GPIOE, (uint16_t)(0x2000u << i), GPIO_Speed_2MHz);
    }
    SetRGB(4, 2);
}

/* ---------------- bit-banged I2C master ---------------- */

static void i2c_delay(void)
{
    for (volatile uint16_t i = 0; i < 80; i++) {
    }
}

static bool i2c_sda_direction_config(wb_i2c_t *pI2c, uint8_t chDirection)
{
    if (pI2c == NULL) {
        return false;
    }
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin = pI2c->hwSdaPin;
    cfg.GPIO_Speed = GPIO_Speed_2MHz;
    cfg.GPIO_Mode = chDirection ? GPIO_Mode_IN_FLOATING : GPIO_Mode_Out_PP;
    GPIO_Init(pI2c->pSdaPort, &cfg);
    return true;
}

static bool i2c_sda_output(wb_i2c_t *pI2c, uint8_t chBit)
{
    if (pI2c == NULL) {
        return false;
    }
    if (chBit) {
        GPIO_SetBits(pI2c->pSdaPort, pI2c->hwSdaPin);
    } else {
        GPIO_ResetBits(pI2c->pSdaPort, pI2c->hwSdaPin);
    }
    return true;
}

static bool i2c_sda_read_pin(wb_i2c_t *pI2c, uint8_t *pchBit)
{
    if (pI2c == NULL) {
        return false;
    }
    *pchBit = (uint8_t)GPIO_ReadInputDataBit(pI2c->pSdaPort, pI2c->hwSdaPin);
    return true;
}

static bool i2c_scl_output(wb_i2c_t *pI2c, uint8_t chBit)
{
    if (pI2c == NULL) {
        return false;
    }
    if (chBit) {
        GPIO_SetBits(pI2c->pSclPort, pI2c->hwSclPin);
    } else {
        GPIO_ResetBits(pI2c->pSclPort, pI2c->hwSclPin);
    }
    return true;
}

static bool i2c_write_byte(wb_i2c_t *pI2c, uint8_t chByte)
{
    if (pI2c == NULL) {
        return false;
    }
    i2c_scl_output(pI2c, 0);
    for (uint8_t i = 0; i < 8; i++) {
        i2c_sda_output(pI2c, (uint8_t)((chByte & (0x80u >> i)) ? 1 : 0));
        i2c_delay();
        i2c_scl_output(pI2c, 1);
        i2c_delay();
        i2c_scl_output(pI2c, 0);
        i2c_delay();
    }
    return true;
}

static bool i2c_read_byte(wb_i2c_t *pI2c, uint8_t *pchByte)
{
    if (pI2c == NULL) {
        return false;
    }
    uint8_t chByte = 0;
    i2c_scl_output(pI2c, 0);
    i2c_delay();
    for (uint8_t i = 0; i < 8; i++) {
        i2c_scl_output(pI2c, 1);
        i2c_delay();
        uint8_t chBit;
        i2c_sda_read_pin(pI2c, &chBit);
        if (chBit) {
            chByte |= (0x80u >> i);
        }
        i2c_scl_output(pI2c, 0);
        i2c_delay();
    }
    *pchByte = chByte;
    return true;
}

static bool i2c_check_ack(wb_i2c_t *pI2c)
{
    if (pI2c == NULL) {
        return false;
    }
    for (uint16_t hwRetry = 2; hwRetry != 0; hwRetry--) {
        i2c_scl_output(pI2c, 1);
        i2c_delay();
        uint8_t chBit;
        i2c_sda_read_pin(pI2c, &chBit);
        i2c_scl_output(pI2c, 0);
        i2c_delay();
        if (chBit == 0) {
            return true;
        }
    }
    return false;
}

static bool i2c_send_ack(wb_i2c_t *pI2c, uint8_t chBit)
{
    if (pI2c == NULL) {
        return false;
    }
    i2c_scl_output(pI2c, 0);
    i2c_delay();
    i2c_sda_output(pI2c, chBit);
    i2c_delay();
    i2c_delay();
    i2c_scl_output(pI2c, 1);
    i2c_delay();
    i2c_delay();
    i2c_scl_output(pI2c, 0);
    i2c_delay();
    return true;
}

static bool i2c_trans_start(wb_i2c_t *pI2c)
{
    if (pI2c == NULL) {
        return false;
    }
    i2c_scl_output(pI2c, 0);
    i2c_delay();
    i2c_sda_output(pI2c, 1);
    i2c_delay();
    i2c_scl_output(pI2c, 1);
    i2c_delay();
    i2c_sda_output(pI2c, 0);
    i2c_delay();
    i2c_scl_output(pI2c, 0);
    i2c_delay();
    i2c_sda_output(pI2c, 0);
    i2c_delay();
    return true;
}

static bool i2c_trans_stop(wb_i2c_t *pI2c)
{
    if (pI2c == NULL) {
        return false;
    }
    i2c_scl_output(pI2c, 0);
    i2c_delay();
    i2c_sda_output(pI2c, 0);
    i2c_delay();
    i2c_scl_output(pI2c, 1);
    i2c_delay();
    i2c_sda_output(pI2c, 1);
    i2c_delay();
    return true;
}

/* One busy flag per hardware group, keyed by the SCL pin: bit 0 PB9,
 * bit 1 PD7, bit 2 PD12, bit 3 PD1; unknown pins take no mutex. */
static uint8_t i2c_group_bit(wb_i2c_t *pI2c)
{
    if (pI2c->pSclPort == GPIOB && pI2c->hwSclPin == GPIO_Pin_9) {
        return 0x01;
    }
    if (pI2c->pSclPort == GPIOD) {
        if (pI2c->hwSclPin == GPIO_Pin_7) {
            return 0x02;
        }
        if (pI2c->hwSclPin == GPIO_Pin_12) {
            return 0x04;
        }
        if (pI2c->hwSclPin == GPIO_Pin_1) {
            return 0x08;
        }
    }
    return 0;
}

static bool setIicMutex(wb_i2c_t *pI2c)
{
    uint8_t chBit = i2c_group_bit(pI2c);
    if (chBit == 0) {
        return true;
    }
    if (chIicMutex & chBit) {
        return false;
    }
    chIicMutex |= chBit;
    return true;
}

static void clrIicMutex(wb_i2c_t *pI2c)
{
    uint8_t chBit = i2c_group_bit(pI2c);
    chIicMutex &= (uint8_t)~chBit;
}

static void i2c_inteface_init(wb_i2c_t *pI2c)
{
    if (pI2c == NULL) {
        return;
    }
    for (uint8_t i = 0; i < 4; i++) {
        RCC_APB2PeriphClockCmd(pI2c[i].nSdaRcc, ENABLE);
        wb_gpio_out(pI2c[i].pSdaPort, pI2c[i].hwSdaPin, GPIO_Speed_2MHz);
        RCC_APB2PeriphClockCmd(pI2c[i].nSclRcc, ENABLE);
        wb_gpio_out(pI2c[i].pSclPort, pI2c[i].hwSclPin, GPIO_Speed_2MHz);
        i2c_sda_output((wb_i2c_t *)&pI2c[i], 1);
        i2c_scl_output((wb_i2c_t *)&pI2c[i], 1);
    }
}

bool i2c_write(wb_i2c_t *pI2c, uint8_t chDeviceAdd, uint8_t chOptAddWidth,
               uint16_t hwOptAdd, uint16_t hwOptLength, uint8_t *pchBuff)
{
    if (pI2c == NULL || hwOptLength == 0 || pchBuff == NULL) {
        return false;
    }
    if (!setIicMutex(pI2c)) {
        return false;
    }
    uint16_t hwRemaining = hwOptLength;
    uint16_t hwWritten = 0;
    bool ok = true;
    do {
        i2c_sda_direction_config(pI2c, 0);
        i2c_trans_start(pI2c);
        if (!i2c_write_byte(pI2c, (uint8_t)(chDeviceAdd << 1)) ||
            !i2c_sda_direction_config(pI2c, 1) || !i2c_check_ack(pI2c)) {
            ok = false;
            break;
        }
        if (chOptAddWidth != 0) {
            i2c_sda_direction_config(pI2c, 0);
            if (!i2c_write_byte(pI2c, (uint8_t)(hwOptAdd >> 8)) ||
                !i2c_sda_direction_config(pI2c, 1) || !i2c_check_ack(pI2c)) {
                ok = false;
                break;
            }
        }
        i2c_sda_direction_config(pI2c, 0);
        if (!i2c_write_byte(pI2c, (uint8_t)hwOptAdd) ||
            !i2c_sda_direction_config(pI2c, 1) || !i2c_check_ack(pI2c)) {
            ok = false;
            break;
        }
        /* EEPROM-style paging: at most 64 data bytes per addressed
         * transfer, then STOP and a fresh address phase for the next
         * page (option address is repeated unchanged). */
        uint16_t hwChunk = (uint16_t)((hwRemaining > 64) ? 64 : hwRemaining);
        for (uint16_t i = 0; i < hwChunk; i++) {
            i2c_sda_direction_config(pI2c, 0);
            if (!i2c_write_byte(pI2c, *pchBuff++) ||
                !i2c_sda_direction_config(pI2c, 1) || !i2c_check_ack(pI2c)) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            break;
        }
        i2c_sda_direction_config(pI2c, 0);
        i2c_trans_stop(pI2c);
        hwWritten = (uint16_t)(hwWritten + hwChunk);
        hwRemaining = (uint16_t)(hwRemaining - hwChunk);
    } while (hwRemaining != 0);
    if (!ok) {
        i2c_sda_direction_config(pI2c, 0);
        i2c_trans_stop(pI2c);
    }
    clrIicMutex(pI2c);
    return ok;
}

bool i2c_read(wb_i2c_t *pI2c, uint8_t chDeviceAdd, uint8_t chOptAddWidth,
              uint16_t hwOptAdd, uint16_t hwOptLength, uint8_t *pchBuff)
{
    if (pI2c == NULL || hwOptLength == 0) {
        return false;
    }
    if (!setIicMutex(pI2c)) {
        return false;
    }
    i2c_sda_direction_config(pI2c, 0);
    i2c_trans_start(pI2c);
    if (!i2c_write_byte(pI2c, (uint8_t)(chDeviceAdd << 1)) ||
        !i2c_sda_direction_config(pI2c, 1) || !i2c_check_ack(pI2c)) {
        clrIicMutex(pI2c);
        return false;
    }
    if (chOptAddWidth != 0) {
        i2c_sda_direction_config(pI2c, 0);
        if (!i2c_write_byte(pI2c, (uint8_t)(hwOptAdd >> 8)) ||
            !i2c_sda_direction_config(pI2c, 1) || !i2c_check_ack(pI2c)) {
            clrIicMutex(pI2c);
            return false;
        }
    }
    i2c_sda_direction_config(pI2c, 0);
    if (!i2c_write_byte(pI2c, (uint8_t)hwOptAdd) ||
        !i2c_sda_direction_config(pI2c, 1) || !i2c_check_ack(pI2c)) {
        clrIicMutex(pI2c);
        return false;
    }
    /* Repeated START in read direction. */
    i2c_sda_direction_config(pI2c, 0);
    i2c_trans_start(pI2c);
    if (!i2c_write_byte(pI2c, (uint8_t)((chDeviceAdd << 1) | 1)) ||
        !i2c_sda_direction_config(pI2c, 1) || !i2c_check_ack(pI2c)) {
        clrIicMutex(pI2c);
        return false;
    }
    for (uint16_t i = 0; i < hwOptLength; i++) {
        i2c_sda_direction_config(pI2c, 1);
        if (!i2c_read_byte(pI2c, pchBuff++)) {
            clrIicMutex(pI2c);
            return false;
        }
        if (i == hwOptLength - 1) {
            i2c_sda_direction_config(pI2c, 1);
            i2c_send_ack(pI2c, 1);
        } else {
            i2c_sda_direction_config(pI2c, 0);
            i2c_send_ack(pI2c, 0);
        }
    }
    i2c_sda_direction_config(pI2c, 0);
    i2c_trans_stop(pI2c);
    clrIicMutex(pI2c);
    return true;
}

/* ---------------- grayscale + smart-sensor ports ---------------- */

/* Port 1..5 map onto slots 4..0 (sensor slot order is reversed
 * relative to the physical port numbering). */
uint16_t get_Gray(uint8_t chChannel)
{
    static const uint8_t chPortMap[5] = {4, 3, 2, 1, 0};
    if (chChannel >= 1 && chChannel <= 5) {
        return Gray[chPortMap[chChannel - 1]];
    }
    return 0;
}

uint16_t get_Gray_Line(uint8_t chChannel)
{
    if (chChannel >= 1 && chChannel <= 5) {
        return Gray[chChannel - 1];
    }
    return 0;
}

/* Smart-sensor command dispatcher: the sensor family selects the 7-bit
 * device address and transfer shape:
 *   1 grayscale read       -> I2C read  0x55, 1 byte
 *   2 write setup block    -> I2C write 0x51, 8 bytes
 *   3 write event counter  -> I2C write 0x52, 2 bytes (counter++)
 *   4 read status byte     -> I2C read  0x54, 1 byte
 *   5 write 3-byte command -> I2C write 0x30, 3 bytes
 *   6 read value byte      -> I2C read  0x43, 1 byte
 *   7 read config byte     -> I2C read  0x51, 1 byte */
bool sensor_operation(uint8_t chChannel, uint8_t chType, uint8_t *pchData)
{
    uint8_t chResult = 0;
    if (pchData == NULL) {
        return false;
    }
    switch (chType) {
    case 1:
        chResult = (uint8_t)i2c_read((wb_i2c_t *)&tI2cResource[chChannel], 0x55,
                                     0, 0, 1, pchData);
        break;
    case 2:
        chResult = (uint8_t)i2c_write((wb_i2c_t *)&tI2cResource[chChannel], 0x51,
                                      0, 0, 8, pchData);
        break;
    case 3: {
        uint16_t *phwCnt = (uint16_t *)pchData;
        *phwCnt = (uint16_t)(*phwCnt + 1);
        chResult = (uint8_t)i2c_write((wb_i2c_t *)&tI2cResource[chChannel], 0x52,
                                      0, 0, 2, pchData);
        break;
    }
    case 4:
        chResult = (uint8_t)i2c_read((wb_i2c_t *)&tI2cResource[chChannel], 0x54,
                                     0, 0, 1, pchData);
        break;
    case 5:
        chResult = (uint8_t)i2c_write((wb_i2c_t *)&tI2cResource[chChannel], 0x30,
                                      0, 0, 3, pchData);
        break;
    case 6:
        chResult = (uint8_t)i2c_read((wb_i2c_t *)&tI2cResource[chChannel], 0x43,
                                     0, 0, 1, pchData);
        break;
    case 7:
        chResult = (uint8_t)i2c_read((wb_i2c_t *)&tI2cResource[chChannel], 0x51,
                                     0, 0, 1, pchData);
        break;
    default:
        break;
    }
    return chResult != 0;
}

/* ---------------- UART wiring ---------------- */

static void bt_interface_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    GPIO_InitTypeDef tx = {GPIO_Pin_2, GPIO_Speed_10MHz, GPIO_Mode_AF_PP};
    GPIO_Init(GPIOA, &tx);
    wb_gpio_in(GPIOA, GPIO_Pin_3, GPIO_Mode_IPD);
    USART_DeInit(USART2);
    USART_InitTypeDef uart = {115200, USART_WordLength_8b, USART_StopBits_1,
                              USART_Parity_No, USART_Mode_Tx | USART_Mode_Rx,
                              USART_HardwareFlowControl_None};
    USART_Init(USART2, &uart);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);
    USART_Cmd(USART2, ENABLE);
    NVIC_InitTypeDef nvic = {USART2_IRQn, 1, 1, ENABLE};
    NVIC_Init(&nvic);
}

static void usart5_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART5, ENABLE);
    GPIO_InitTypeDef tx = {GPIO_Pin_12, GPIO_Speed_50MHz, GPIO_Mode_AF_PP};
    GPIO_Init(GPIOC, &tx);
    wb_gpio_in(GPIOD, GPIO_Pin_2, GPIO_Mode_IPD);
    USART_DeInit(UART5);
    USART_InitTypeDef uart = {115200, USART_WordLength_8b, USART_StopBits_1,
                              USART_Parity_No, USART_Mode_Tx | USART_Mode_Rx,
                              USART_HardwareFlowControl_None};
    USART_Init(UART5, &uart);
    USART_ITConfig(UART5, USART_IT_RXNE, ENABLE);
    USART_ITConfig(UART5, USART_IT_TXE, DISABLE);
    USART_Cmd(UART5, ENABLE);
    NVIC_InitTypeDef nvic = {UART5_IRQn, 0, 0, ENABLE};
    NVIC_Init(&nvic);
    USART_ClearFlag(UART5, USART_FLAG_TC);
}

/* ---------------- audio path (speaker DAC + microphone ADC) --------- */

static void audio_interface_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    wb_gpio_out(GPIOC, GPIO_Pin_15, GPIO_Speed_10MHz);
    GPIO_ResetBits(GPIOC, GPIO_Pin_15);
}

void voice_record_interface_init(uint16_t *phwBuffer, uint8_t chChannel)
{
    (void)phwBuffer;
    if (chChannel < 1 || chChannel > 5) {
        return;
    }
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC2, ENABLE);
    ADC_InitTypeDef adc;
    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    ADC_Init(ADC2, &adc);
    ADC_RegularChannelConfig(ADC2, MicChannels[chChannel - 1], 1,
                             ADC_SampleTime_239Cycles5);
    ADC_Cmd(ADC2, ENABLE);
    ADC_ResetCalibration(ADC2);
    while (ADC_GetResetCalibrationStatus(ADC2)) {
    }
}

void voice_recoder_timer_init(TIM_TypeDef *TIMx, uint32_t nSampleRate)
{
    RCC_ClocksTypeDef clocks;
    RCC_GetClocksFreq(&clocks);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
    TIM_TimeBaseInitTypeDef tb;
    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler = 1;
    tb.TIM_Period = (uint16_t)(clocks.PCLK1_Frequency / nSampleRate);
    TIM_TimeBaseInit(TIMx, &tb);
    NVIC_InitTypeDef nvic = {TIM7_IRQn, 1, 1, ENABLE};
    NVIC_Init(&nvic);
    TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM7, ENABLE);
}

void voice_recoder_timer_deinit(void)
{
    NVIC_InitTypeDef nvic = {TIM7_IRQn, 0, 1, DISABLE};
    NVIC_Init(&nvic);
    TIM_DeInit(TIM7);
}

uint8_t voice_recoder_packet_ready(void)
{
    return 0;
}

/* Speaker: DAC2 (PA5), sample pacing from TIM7 TRGO, samples streamed
 * to DHR12R2 by DMA2 channel 4; the speech engine re-arms per block. */
static const uint32_t DAC_DHR12R2 = 0x40007414u;

void voice_echo_interface_init(void)
{
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_CRC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    GPIO_InitTypeDef dac = {GPIO_Pin_5, GPIO_Speed_50MHz, GPIO_Mode_AIN};
    GPIO_Init(GPIOA, &dac);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
    TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);
    TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);
    DAC_InitTypeDef dacInit = {DAC_Trigger_T7_TRGO, DAC_WaveGeneration_None,
                               DAC_OutputBuffer_Disable};
    DAC_Init(DAC_Channel_2, &dacInit);
    DAC_SetChannel2Data(DAC_Align_12b_R, 0);
    DMA_InitTypeDef dma;
    dma.DMA_BufferSize = 0;
    dma.DMA_PeripheralBaseAddr = DAC_DHR12R2;
    dma.DMA_MemoryBaseAddr = 0;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel4, &dma);
}

void voice_dac_dma_init(uint32_t nAddress, uint16_t hwLength)
{
    DMA_Cmd(DMA2_Channel4, DISABLE);
    DMA_InitTypeDef dma;
    dma.DMA_BufferSize = hwLength;
    dma.DMA_PeripheralBaseAddr = DAC_DHR12R2;
    dma.DMA_MemoryBaseAddr = nAddress;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel4, &dma);
    DMA_ITConfig(DMA2_Channel4, DMA_IT_TC, ENABLE);
    DMA_Cmd(DMA2_Channel4, ENABLE);
}

/* ---------------- watchdog + delay ---------------- */

static void InitWDT(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload((uint8_t)(LsiFreq >> 7));
    IWDG_ReloadCounter();
    IWDG_Enable();
}

static void test_delay(uint32_t nMilliSeconds)
{
    for (; nMilliSeconds; nMilliSeconds--) {
        for (volatile uint16_t i = 1000; i; i--) {
            IWDG_ReloadCounter();
        }
    }
}
