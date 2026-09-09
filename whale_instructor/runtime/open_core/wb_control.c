/* wb_control.c -- Whale Instructor control layer for the MC101s.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Whale Instructor open core: system tick supervision, motor PWM
 * with encoder feedback, ADC/battery monitoring, keys, EEPROM page on
 * internal flash, SPI data flash reads, Bluetooth mode control, the
 * PO16 servo bus, RGB module and ultrasonic sensor transactions.
 * The closed motor loop is a functional PI reimplementation (same
 * interfaces and units as the vendor engine, not cycle-identical);
 * the in-app USB download and camera paths are parked no-ops -- see
 * README.md. Hardware facts in README.md.
 */
#include "wb_core.h"
#include <string.h>
#include <stdlib.h>
#include <stm32f10x_gpio.h>
#include <stm32f10x_rcc.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_adc.h>
#include <stm32f10x_dma.h>
#include <stm32f10x_usart.h>
#include <stm32f10x_iwdg.h>
#include <stm32f10x_flash.h>
#include <stm32f10x_spi.h>
#include <misc.h>

/* Vendor Wav_Decode.o (speech engine) consumes these. */
uint16_t hwBuff1[512];
uint16_t hwBuff2[512];

/* Vendor peers this file drives. */
extern void DigitScan(void);
extern void RGB_Serverloop_ISR(void);
extern void set_display_program_idx(uint8_t chIdx);
extern void clr_display(void);
extern void SetRGB(uint8_t chColor, uint8_t chMode);
extern void InitWDT(void);
extern void test_delay(uint32_t nMilliSeconds);
extern void InitUSBCheck(void);
extern void InitUSBPULL(void);
extern void key_detection(void);
extern void wait(float time);

/* FreeRTOS + CMSIS. */
extern void xPortSysTickHandler(void);
extern uint32_t xTaskGetSchedulerState(void);

/* ---------------- system state (vendor layout) ---------------- */
static uint32_t systimtickrng;
static uint32_t SystemTimeCnt;
static uint32_t SystemTimeCnt_10MS;
static uint8_t cntd;
static uint8_t s_chTickIsrFlag;
uint16_t Key;
static uint16_t KeyGoLineFlag;
static uint8_t usb_Plug_Flag;
static uint32_t nobuttontime;
static uint16_t EyeUpdateTimes;
static uint32_t TimingDelay;
static uint32_t MotorbreakTimeout;
static uint16_t BATT_EMPTY_VALUE;
static uint16_t BATT_LOW_VALUE;
static uint32_t batt;
static uint32_t battEmptytime;
static uint32_t battlowtime;
static uint32_t battFulltime;
static uint16_t DryBatInfo;
static uint16_t v_hwPlayPageInterval;
static uint16_t v_hwKeyTimer;
static uint8_t v_chServoAssistant;
static uint8_t v_chCameraSampleInterval;
static uint16_t v_hwWirelessTimeout;
static uint8_t s_chBtStartFlag;
/* Motor loop state, one slot per channel A..D. The closed loop keeps a
 * ramped goal and its backup goal in the speed*0.64 unit (1 unit =
 * 1.5625 percent speed), PI error history, and stall supervision:
 * judge accumulator (encoder counts since the last check, baseline
 * 32767), tick window until the next check (grace after every command,
 * doubled after a direction reversal), stalled/action/needTuning
 * flags, the 25-tick post-recovery hold, and the per-motor direction
 * reverse table (all clear by default). */
static int32_t Encode[4];
static int32_t Position[4];
static uint8_t s_chMotorRun[4];
static int s_iGoal[4];
static int16_t s_iBackupGoal[4];
static int s_iPrevErr[4];
static int s_iLastErr[4];
static int32_t s_iIntegral[4];
static uint8_t s_chNeedTuning[4];
static uint8_t s_chStalled[4];
static uint8_t s_chRecoverCnt[4];
static uint8_t s_chStallAction[4];
static uint16_t s_hwStallWindow[4];
static uint16_t s_hwCheckWindow[4] = {50, 50, 50, 50};
static int32_t s_wJudge[4] = {32767, 32767, 32767, 32767};
static uint8_t s_chMotorRevFlag[4];
static uint16_t hwAdcBuffer[12];
static const uint16_t AI_TableIndex[6] = {1, 2, 4, 8, 0x10, 0x20};
static GPIO_TypeDef *const AI_GPIOx[6] = {
    GPIOC, GPIOC, GPIOC, GPIOC, GPIOC, GPIOC};
static const uint16_t IO_TableIndex[5] = {0x10, 0x08, 0x04, 0x02, 0x01};
static GPIO_TypeDef *const IO_GPIOx[5] = {
    GPIOE, GPIOE, GPIOE, GPIOE, GPIOE};
static uint32_t g_SpiAddr;
static uint8_t mic_ch;
static uint16_t mic_ene;
static uint16_t UsbRegBuffer;
static uint16_t usbCnt;
static uint8_t s_chStartSample;
static uint8_t s_chSampleFlag;
static uint8_t schStopSampleFlag;
static uint16_t s_hwIndex;
static uint16_t s_hwPtr;
/* UART5 smart-sensor parser state (see open_core/PROTOCOL.md). */
static uint8_t s_chUart5LineOptionArmed;
static uint8_t s_chUart5MechanicArmed;
static uint8_t s_chUart5OptionModuleArmed;
static uint8_t s_chUart5PayloadLeft;
static uint8_t s_chUart5DanceMotion;
static uint8_t btTxFrameOK;
static uint8_t btRxFrameOK;
static uint8_t btUsartRxBuf[20];
static uint16_t btUsartRxCnt;
static uint8_t gBtConnectedFlag;
static uint16_t g_language = 1;

/* EEPROM: one internal-flash page, 200 x uint16 slots. */
#define WB_EEPROM_PAGE 0x08018C00u
#define WB_EEPROM_SLOTS 200u

/* Motor channel map (from the vendor set_motor_new / pwm facts):
 * motor A = TIM8 CH3/CH4 (PC8/PC9), B = TIM4 CH4/CH3 (PD15/PD14),
 * C = TIM8 CH1/CH2 (PC6/PC7), D = TIM4 CH1/CH2 (PD12/PD13). */
typedef struct {
    TIM_TypeDef *tim;
    uint8_t chFwd;
    uint8_t chRev;
} wb_motor_map_t;
static const wb_motor_map_t wb_motors[4] = {
    {TIM8, 3, 4},
    {TIM4, 3, 4},
    {TIM8, 2, 1},
    {TIM4, 2, 1},
};

/* ---------------- motor primitives ---------------- */

static void wb_ccr(TIM_TypeDef *tim, uint8_t ch, uint16_t hwValue)
{
    switch (ch) {
    case 1: TIM_SetCompare1(tim, hwValue); break;
    case 2: TIM_SetCompare2(tim, hwValue); break;
    case 3: TIM_SetCompare3(tim, hwValue); break;
    default: TIM_SetCompare4(tim, hwValue); break;
    }
}

static int wb_sign(int v)
{
    return (v > 0) - (v < 0);
}

/* Bridge driver: duty -100..100 percent, motor 1..4. Positive duty
 * drives chFwd (the channel for positive speed), negative duty the
 * partner channel, zero releases both. CCR = |duty| * 36 against
 * ARR = 3599. */
void set_motor_new(int nMotor, int nDuty)
{
    if (nMotor < 1 || nMotor > 4) {
        return;
    }
    if (nDuty > 100) {
        nDuty = 100;
    }
    if (nDuty < -100) {
        nDuty = -100;
    }
    uint16_t hwPwm = (uint16_t)((nDuty < 0 ? -nDuty : nDuty) * 36);
    const wb_motor_map_t *m = &wb_motors[nMotor - 1];
    if (nDuty > 0) {
        wb_ccr(m->tim, m->chRev, 0);
        wb_ccr(m->tim, m->chFwd, hwPwm);
    } else if (nDuty < 0) {
        wb_ccr(m->tim, m->chFwd, 0);
        wb_ccr(m->tim, m->chRev, hwPwm);
    } else {
        wb_ccr(m->tim, m->chFwd, 0);
        wb_ccr(m->tim, m->chRev, 0);
    }
}

void pwm_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD | RCC_APB2Periph_TIM8,
                           ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Speed = GPIO_Speed_2MHz;
    cfg.GPIO_Mode = GPIO_Mode_AF_PP;
    cfg.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOD, &cfg);
    cfg.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOC, &cfg);
    GPIO_PinRemapConfig(GPIO_Remap_TIM4, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    TIM_TimeBaseInitTypeDef tb;
    tb.TIM_Prescaler = 1;
    tb.TIM_Period = 3599;
    tb.TIM_ClockDivision = 0;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM8, &tb);
    TIM_TimeBaseInit(TIM4, &tb);

    TIM_OCInitTypeDef oc;
    oc.TIM_OCMode = TIM_OCMode_PWM2;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = 0;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity = TIM_OCNPolarity_High;
    oc.TIM_OCIdleState = TIM_OCIdleState_Reset;
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC1Init(TIM8, &oc);
    TIM_OC2Init(TIM8, &oc);
    TIM_OC3Init(TIM8, &oc);
    TIM_OC4Init(TIM8, &oc);
    TIM_OC1Init(TIM4, &oc);
    TIM_OC2Init(TIM4, &oc);
    TIM_OC3Init(TIM4, &oc);
    TIM_OC4Init(TIM4, &oc);
    TIM_SetCompare1(TIM4, 0);
    TIM_SetCompare2(TIM4, 0);
    TIM_SetCompare3(TIM4, 0);
    TIM_SetCompare4(TIM4, 0);
    TIM_SetCompare1(TIM8, 0);
    TIM_SetCompare2(TIM8, 0);
    TIM_SetCompare3(TIM8, 0);
    TIM_SetCompare4(TIM8, 0);
    TIM_Cmd(TIM4, ENABLE);
    TIM_CtrlPWMOutputs(TIM4, ENABLE);
    TIM_Cmd(TIM8, ENABLE);
    TIM_CtrlPWMOutputs(TIM8, ENABLE);
}

/* Closed-loop speed command: motor 1..4, speed -100..100 percent. The
 * command is stored as the backup goal in speed*0.64 units; arming it
 * (re)opens the stall grace window and lets the tick loop ramp the
 * working goal toward it. Speed 0 stops: the stall flag clears, the
 * loop parks (run flag set) and the bridge releases. Arming is skipped
 * entirely while a recovery hold is running; while stalled, a command
 * re-arms too and a direction reversal additionally flips the ramped
 * goal so the swing starts immediately. */
void set_motor(int nMotor, int nSpeed)
{
    if (nMotor < 1 || nMotor > 4) {
        return;
    }
    int idx = nMotor - 1;
    if (s_chMotorRevFlag[idx]) {
        nSpeed = -nSpeed;
    }
    s_iBackupGoal[idx] = (int16_t)((nSpeed << 6) / 100);
    if (nSpeed == 0) {
        if (s_chStalled[idx]) {
            s_chStalled[idx] = 0;
        }
        s_chMotorRun[idx] = 1;
        const wb_motor_map_t *m = &wb_motors[idx];
        wb_ccr(m->tim, m->chFwd, 0);
        wb_ccr(m->tim, m->chRev, 0);
        return;
    }
    if (!s_chStalled[idx] && s_chRecoverCnt[idx] == 0) {
        __disable_irq();
        int reversal = wb_sign(s_iGoal[idx]) * wb_sign(s_iBackupGoal[idx]) < 0;
        s_hwStallWindow[idx] = 0;
        s_wJudge[idx] = 32767;
        s_chNeedTuning[idx] = 1;
        s_hwCheckWindow[idx] = reversal ? 100 : 50;
        s_chMotorRun[idx] = 0;
        __enable_irq();
    } else if (s_chStalled[idx]) {
        __disable_irq();
        int reversal = wb_sign(s_iGoal[idx]) * wb_sign(s_iBackupGoal[idx]) < 0;
        s_hwStallWindow[idx] = 0;
        s_wJudge[idx] = 32767;
        s_chNeedTuning[idx] = 1;
        s_hwCheckWindow[idx] = reversal ? 100 : 50;
        if (reversal) {
            s_iGoal[idx] = -s_iGoal[idx];
        }
        s_chMotorRun[idx] = 0;
        __enable_irq();
    }
}

void set_motor_ad(int nMask, int nS1, int nS2, int nS3, int nS4)
{
    int speeds[4] = {nS1, nS2, nS3, nS4};
    for (int i = 0; i < 4; i++) {
        if ((nMask >> i) & 1) {
            Position[i] = 0;
            set_motor(i + 1, speeds[i]);
        }
    }
    /* Run until every selected motor reaches its goal distance
     * (the speed magnitude doubles as the encoder count target). */
    for (int i = 0; i < 4; i++) {
        if (!((nMask >> i) & 1)) {
            continue;
        }
        int32_t target = speeds[i];
        if (target < 0) {
            target = -target;
        }
        while (Position[i] < target) {
        }
        set_motor(i + 1, 0);
    }
}

void motor_angle(uint8_t chMotor, int nAngle, int nSpeed)
{
    int32_t counts = (int32_t)((int64_t)nAngle * 864 / 360);
    if (chMotor < 1 || chMotor > 4) {
        return;
    }
    Position[chMotor - 1] = 0;
    set_motor(chMotor, nSpeed);
    int32_t target = counts;
    if (target < 0) {
        target = -target;
    }
    while (Position[chMotor - 1] < target) {
    }
    set_motor(chMotor, 0);
}

void daulMotor_angle(uint8_t chLeft, uint8_t chRight, int nAngle,
                     int nLeftSpeed, int nRightSpeed)
{
    int32_t counts = (int32_t)((int64_t)nAngle * 864 / 360);
    if (chLeft >= 1 && chLeft <= 4) {
        Position[chLeft - 1] = 0;
        set_motor(chLeft, nLeftSpeed);
    }
    if (chRight >= 1 && chRight <= 4) {
        Position[chRight - 1] = 0;
        set_motor(chRight, nRightSpeed);
    }
    int32_t target = counts;
    if (target < 0) {
        target = -target;
    }
    if (chLeft >= 1 && chLeft <= 4) {
        while (Position[chLeft - 1] < target) {
        }
        set_motor(chLeft, 0);
    }
    if (chRight >= 1 && chRight <= 4) {
        while (Position[chRight - 1] < target) {
        }
        set_motor(chRight, 0);
    }
}

void setDO(int nMask, int nOnOff)
{
    for (uint8_t i = 0; i < 5; i++) {
        if ((nMask >> i) & 1) {
            if (nOnOff) {
                GPIO_SetBits(IO_GPIOx[i], IO_TableIndex[i]);
            } else {
                GPIO_ResetBits(IO_GPIOx[i], IO_TableIndex[i]);
            }
        }
    }
}

uint16_t get_encoder_value_raw(uint8_t chMotor)
{
    TIM_TypeDef *tim;
    switch (chMotor) {
    case 0: tim = TIM3; break;
    case 1: tim = TIM2; break;
    case 2: tim = TIM1; break;
    case 3: tim = TIM5; break;
    default: return 0;
    }
    uint16_t hwCnt = tim->CNT;
    tim->CNT = 0x7FFF;
    return hwCnt;
}

/* User-facing encoder counter (whale API get_encoder_value /
 * reset_motor_encoder): the accumulated position from the TIM6 sample
 * loop, 1 = motor A .. 4 = motor D. reset zeroes the accumulator. */
int get_encoder_value(int nMotor)
{
    if (nMotor < 1 || nMotor > 4) {
        return 0;
    }
    return Position[nMotor - 1];
}

void reset_motor_encoder(uint8_t chChannel)
{
    if (chChannel < 1 || chChannel > 4) {
        return;
    }
    __disable_irq();
    Position[chChannel - 1] = 0;
    __enable_irq();
}

/* ---------------- closed motor loop (TIM6 @ 100 Hz) ----------------
 *
 * Tick order: encoder sampling, stall detection, goal ramp, then per
 * channel the PI pass or the stop hold (run flag set). Goals live in
 * the speed*0.64 unit, feedback is the encoder delta of the last
 * 10 ms tick, so one goal unit = 1.5625 percent speed.
 */

/* Stall supervision. Each command opens a grace window (50 ticks =
 * 0.5 s, doubled to 1.0 s after a direction reversal) with checks
 * suspended; during it the stalled/recovery hold bookkeeping re-arms
 * the goal ramp as soon as the stall state has fully cleared. After
 * the window, movement of 20 counts or less while the goal is above
 * the +-5 floor is a stall: the action and stalled flags rise and the
 * goal is cut back to +-5. Movement past 20 counts instead recovers:
 * the stall clears and 25 ticks pass before commands arm again. */
static void Stall_Check(int idx)
{
    if (s_chRecoverCnt[idx] > 0) {
        s_chRecoverCnt[idx]--;
    }
    s_hwStallWindow[idx]++;
    if (s_hwStallWindow[idx] <= s_hwCheckWindow[idx]) {
        if (!s_chStalled[idx] && s_chRecoverCnt[idx] == 0) {
            s_chStallAction[idx] = 0;
            s_chNeedTuning[idx] = 1;
        }
        return;
    }
    int32_t movement = s_wJudge[idx] - 32767;
    if (movement < 0) {
        movement = -movement;
    }
    if (movement <= 20 && (s_iGoal[idx] > 5 || s_iGoal[idx] < -5)) {
        s_chStallAction[idx] = 1;
        s_chStalled[idx] = 1;
        if (s_iGoal[idx] > 5) {
            s_iGoal[idx] = 5;
        } else if (s_iGoal[idx] < -5) {
            s_iGoal[idx] = -5;
        }
    } else if (movement > 20 && s_chStalled[idx]) {
        s_chStalled[idx] = 0;
        s_chRecoverCnt[idx] = 25;
    }
    s_wJudge[idx] = 32767;
    s_hwStallWindow[idx] = 0;
}

/* Goal ramp: while a ramp is pending and no stall is flagged, move the
 * working goal toward the backup goal by at most 20 units (32 speed
 * points) per 10 ms tick. */
static void Goal_Ramp(int idx)
{
    if (!s_chNeedTuning[idx] || s_chStalled[idx]) {
        return;
    }
    if (s_iBackupGoal[idx] > s_iGoal[idx]) {
        int32_t diff = s_iBackupGoal[idx] - s_iGoal[idx];
        s_iGoal[idx] += (diff > 20) ? 20 : diff;
    } else if (s_iBackupGoal[idx] < s_iGoal[idx]) {
        int32_t diff = s_iGoal[idx] - s_iBackupGoal[idx];
        s_iGoal[idx] -= (diff > 20) ? 20 : diff;
    } else {
        s_chNeedTuning[idx] = 0;
    }
}

/* PI speed loop in fixed point (P = 5, I = 0.1, D = 0): duty in
 * percent = err*5 + integral/10, with the integral clamped to +-250
 * and sign-kick unwinding against the error. While a stall is flagged
 * the P term is disabled, so the drive into an obstruction is capped
 * at integral/10 <= 25 percent -- never a full-power stall. */
static void Speed_PID(int idx)
{
    int32_t fb = Encode[idx];
    int32_t err = s_iGoal[idx] - fb;
    int32_t err2 = s_iPrevErr[idx] - s_iLastErr[idx];
    s_iLastErr[idx] = s_iPrevErr[idx];
    s_iPrevErr[idx] = err;
    if (err > 3 || err < -3) {
        s_iIntegral[idx] += err;
        if (s_iIntegral[idx] > 250) {
            s_iIntegral[idx] = 250;
        } else if (s_iIntegral[idx] < -250) {
            s_iIntegral[idx] = -250;
        }
    }
    if (err < -2 && s_iIntegral[idx] > 0) {
        s_iIntegral[idx] = 0;
    }
    if (err > 2 && s_iIntegral[idx] < 0) {
        s_iIntegral[idx] = 0;
    }
    /* err2 * 0: the derivative gain is fixed at 0.0. */
    int32_t duty = err * 5 + err2 * 0 + s_iIntegral[idx] / 10;
    if (s_chStalled[idx]) {
        duty = s_iIntegral[idx] / 10;
    }
    if (duty > 100) {
        duty = 100;
    } else if (duty < -100) {
        duty = -100;
    }
    set_motor_new(idx + 1, (int)duty);
}

/* Stop hold for a channel with the run flag set: zero the loop state
 * and keep the bridge off. init_control_tab() parks all four. */
static void ClrPID(int idx)
{
    s_chMotorRun[idx] = 1;
    s_iGoal[idx] = 0;
    Encode[idx] = 0;
    s_iPrevErr[idx] = 0;
    s_iLastErr[idx] = 0;
    s_iIntegral[idx] = 0;
}

void TIM6_IRQHandler(void)
{
    if (!TIM_GetITStatus(TIM6, TIM_IT_Update)) {
        return;
    }
    TIM_ClearITPendingBit(TIM6, TIM_IT_Update);
    for (uint8_t i = 0; i < 4; i++) {
        Encode[i] = (int32_t)get_encoder_value_raw(i) - 0x7FFF;
        Position[i] += Encode[i];
        s_wJudge[i] += Encode[i];
    }
    for (uint8_t i = 0; i < 4; i++) {
        Stall_Check(i);
    }
    for (uint8_t i = 0; i < 4; i++) {
        Goal_Ramp(i);
    }
    for (uint8_t i = 0; i < 4; i++) {
        if (s_chMotorRun[i]) {
            ClrPID(i);
        } else {
            Speed_PID(i);
        }
    }
}

void _TIM6_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
    TIM_TimeBaseInitTypeDef tb;
    tb.TIM_Prescaler = 719;
    tb.TIM_Period = 1000;
    tb.TIM_ClockDivision = 0;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM6, &tb);
    NVIC_InitTypeDef nvic = {TIM6_IRQn, 2, 2, ENABLE};
    NVIC_Init(&nvic);
    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM6, ENABLE);
}

void encoder_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3 |
                           RCC_APB1Periph_TIM5, ENABLE);
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Speed = GPIO_Speed_2MHz;
    cfg.GPIO_Mode = GPIO_Mode_IPD;
    cfg.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_Init(GPIOB, &cfg);
    cfg.GPIO_Pin = GPIO_Pin_15;
    GPIO_Init(GPIOA, &cfg);
    cfg.GPIO_Pin = GPIO_Pin_3;
    GPIO_Init(GPIOB, &cfg);
    cfg.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &cfg);
    cfg.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_Init(GPIOA, &cfg);
    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM3, ENABLE);
    GPIO_PinRemapConfig(GPIO_FullRemap_TIM2, ENABLE);
    TIM_DeInit(TIM1);
    TIM_DeInit(TIM3);
    TIM_DeInit(TIM2);
    TIM_DeInit(TIM5);
    TIM_TimeBaseInitTypeDef tb;
    tb.TIM_Prescaler = 0;
    tb.TIM_Period = 0xFFFF;
    tb.TIM_ClockDivision = 0;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tb);
    TIM_TimeBaseInit(TIM3, &tb);
    TIM_TimeBaseInit(TIM2, &tb);
    TIM_TimeBaseInit(TIM5, &tb);
    /* Encoder mode TI1 (both edges of TI1, direction from TI2): the
     * count scale the speed loop's goal units are calibrated against.
     * TI12 would count 2x per line and double the loop plant gain. */
    TIM_EncoderInterfaceConfig(TIM1, TIM_EncoderMode_TI1,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI1,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI1,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM_EncoderInterfaceConfig(TIM5, TIM_EncoderMode_TI1,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM_ICInitTypeDef ic;
    TIM_ICStructInit(&ic);
    ic.TIM_ICFilter = 6;
    ic.TIM_Channel = TIM_Channel_1;
    ic.TIM_ICPolarity = TIM_ICPolarity_Rising;
    ic.TIM_ICSelection = TIM_ICSelection_DirectTI;
    ic.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    TIM_ICInit(TIM1, &ic);
    TIM_ICInit(TIM3, &ic);
    TIM_ICInit(TIM2, &ic);
    TIM_ICInit(TIM5, &ic);
    TIM_ClearFlag(TIM1, TIM_FLAG_Update);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    TIM_ClearFlag(TIM5, TIM_FLAG_Update);
    TIM1->CNT = 0x7FFF;
    TIM3->CNT = 0x7FFF;
    TIM2->CNT = 0x7FFF;
    TIM5->CNT = 0x7FFF;
    TIM_Cmd(TIM1, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM5, ENABLE);
}

/* ---------------- ADC / battery / voice energy ---------------- */

void adc_init(void)
{
    RCC_APB2PeriphClockCmd(0x210 | 0xC000, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    cfg.GPIO_Mode = GPIO_Mode_AIN;
    cfg.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
                   GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_Init(GPIOC, &cfg);
    DMA_DeInit(DMA1_Channel1);
    DMA_InitTypeDef dma;
    dma.DMA_PeripheralBaseAddr = 0x4001244Cu;
    dma.DMA_MemoryBaseAddr = (uint32_t)hwAdcBuffer;
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = 6;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &dma);
    DMA_Cmd(DMA1_Channel1, ENABLE);
    ADC_InitTypeDef adc;
    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = ENABLE;
    adc.ADC_ContinuousConvMode = ENABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 6;
    ADC_Init(ADC1, &adc);
    ADC_RegularChannelConfig(ADC1, 14, 1, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, 13, 2, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, 12, 3, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, 11, 4, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, 10, 5, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, 15, 6, ADC_SampleTime_239Cycles5);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_TempSensorVrefintCmd(ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1)) {
    }
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1)) {
    }
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

static int _AI(int idx)
{
    int v = hwAdcBuffer[idx];
    if (idx <= 5) {
        if (v <= 19) {
            v = 0;
        }
        v = v * 33 / 25;
        if (v > 4000) {
            v = 4000;
        }
    }
    return v;
}

int AI_read(int ch)
{
    if (ch == 0 || ch > 6) {
        return 0;
    }
    return _AI(ch - 1);
}

int JY_AI(int nChannel)
{
    if (nChannel > 5) {
        return 0;
    }
    return AI_read(nChannel);
}

int get_battery(void)
{
    int v = AI_read(6);
    return ((((v * 3300) + ((v * 3300) < 0 ? 4095 : 0)) >> 12) + 100) * 4;
}

static void DetectBatMode(void)
{
    if (GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_12) != 0) {
        DryBatInfo = 2;
    } else {
        DryBatInfo = 1;
    }
    if (DryBatInfo == 2) {
        BATT_EMPTY_VALUE = 8870;
        BATT_LOW_VALUE = 8470;
    } else {
        BATT_EMPTY_VALUE = 8700;
        BATT_LOW_VALUE = 5500;
    }
}

static int getADCRANG(void)
{
    int sum = 0;
    for (int i = 0; i <= 5; i++) {
        sum += hwAdcBuffer[i];
    }
    return sum;
}

static void voice_detect_loop(void)
{
    static uint16_t lastvalue;
    static uint32_t ulEne;
    static uint16_t loopcnt;
    int v = AI_read(mic_ch);
    int diff = v - lastvalue;
    lastvalue = v;
    if (diff < 0) {
        diff = -diff;
    }
    ulEne += (uint32_t)(diff * diff);
    loopcnt++;
    if (loopcnt > 100) {
        mic_ene = (uint16_t)(ulEne >> 16);
        ulEne = 0;
        loopcnt = 0;
        if (mic_ene > 2000) {
            mic_ene = 2000;
        }
    }
}

int Detect_Ene(int nChannel)
{
    mic_ch = (uint8_t)nChannel;
    return mic_ene;
}

/* ---------------- SysTick supervision + timers ---------------- */

void SysTick_Handler(void)
{
    static uint32_t systimtickrng_local;
    s_chTickIsrFlag = 1;
    systimtickrng_local++;
    systimtickrng = systimtickrng_local;
    SystemTimeCnt++;
    if (xTaskGetSchedulerState() != 1) {
        xPortSysTickHandler();
    }
    uint16_t ch = TstCh();
    if (ch == 1) {
        /* Debug stop: kill every PWM output and reset the chip. */
        TIM_SetCompare1(TIM2, 0);
        TIM_SetCompare2(TIM2, 0);
        TIM_SetCompare3(TIM2, 0);
        TIM_SetCompare4(TIM2, 0);
        for (uint8_t m = 0; m < 4; m++) {
            const wb_motor_map_t *mm = &wb_motors[m];
            wb_ccr(mm->tim, mm->chFwd, 0);
            wb_ccr(mm->tim, mm->chRev, 0);
        }
        set_motor_new(1, 0);
        set_motor_new(2, 0);
        set_motor_new(3, 0);
        set_motor_new(4, 0);
        SCB->AIRCR = 0x05FA0004;
        while (1) {
        }
    }
    if (ch == 0x81) {
        for (uint8_t m = 0; m < 4; m++) {
            const wb_motor_map_t *mm = &wb_motors[m];
            wb_ccr(mm->tim, mm->chFwd, 0);
            wb_ccr(mm->tim, mm->chRev, 0);
        }
        set_motor_new(1, 0);
        set_motor_new(2, 0);
        set_motor_new(3, 0);
        set_motor_new(4, 0);
        SetRGB(4, 2);
        clr_display();
        DigitScan();
        MainPower(0);
        while (1) {
            IWDG_ReloadCounter();
        }
    }
    cntd++;
    if (cntd > 2) {
        DigitScan();
        cntd = 0;
    }
    DetectBatMode();
    voice_detect_loop();
    if (usb_Plug_Flag == 0) {
        nobuttontime++;
        if (nobuttontime > 300000) {
            MainPower(0);
            while (1) {
            }
        }
    }
    key_detection();
    RGB_Serverloop_ISR();
    if (EyeUpdateTimes != 0) {
        EyeUpdateTimes--;
    }
    if (MotorbreakTimeout != 0) {
        MotorbreakTimeout--;
    }
    if (TimingDelay != 0) {
        TimingDelay--;
    }
    SystemTimeCnt_10MS++;
    if (cntd == 0) {
        batt = get_battery();
        if (batt < BATT_EMPTY_VALUE) {
            if (batt < BATT_LOW_VALUE) {
                battlowtime++;
                battFulltime = cntd;
                if (battlowtime > 2000) {
                    MainPower(0);
                    while (1) {
                    }
                }
            } else {
                battFulltime++;
                if (battFulltime > 2000) {
                    SetRGB(3, 2);
                    battFulltime = 2001;
                }
            }
        } else {
            battEmptytime = 0;
            battlowtime = 0;
            battFulltime = 0;
        }
    }
    IWDG_ReloadCounter();
    if (v_hwPlayPageInterval != 0) {
        v_hwPlayPageInterval--;
    }
    if (v_hwKeyTimer != 0) {
        v_hwKeyTimer--;
    }
    if (v_chServoAssistant != 0) {
        v_chServoAssistant--;
    }
    if (v_chCameraSampleInterval != 0) {
        v_chCameraSampleInterval--;
    }
    if (v_hwWirelessTimeout != 0) {
        v_hwWirelessTimeout--;
    }
    if (s_chBtStartFlag) {
        /* Bluetooth frames are parsed on USART2 interrupts; the tick
         * only refreshes the connection bookkeeping. */
    }
    s_chTickIsrFlag = 0;
}

void TIM7_IRQHandler(void)
{
    if (!TIM_GetITStatus(TIM7, TIM_IT_Update)) {
        return;
    }
    TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
    if (s_chStartSample == 0) {
        ADC_SoftwareStartConvCmd(ADC2, ENABLE);
        s_chSampleFlag = 1;
        return;
    }
    uint16_t v = ADC_GetConversionValue(ADC2);
    uint16_t scaled = (uint16_t)(((uint64_t)v * 33u * 0x51EB851Fu) >> 35);
    uint16_t idx = ++s_hwIndex;
    hwBuff2[s_hwPtr] = scaled;
    if (idx > 255) {
        s_chStartSample = 0;
        schStopSampleFlag = 1;
        s_hwIndex = 0;
    }
}

/* ---------------- time / misc API ---------------- */

float seconds(void)
{
    return (float)((double)SystemTimeCnt / 1000.0);
}

void reset_time(void)
{
    __disable_irq();
    SystemTimeCnt = 0;
    __enable_irq();
}

int rng(int nMin, int nMax)
{
    srand(getADCRANG() + SystemTimeCnt);
    int r = rand();
    r %= (nMax - nMin + 1);
    return r + nMin;
}

int math_mod(int nA, int nB)
{
    return nA - (nA / nB) * nB;
}

void MainPower(int nOn)
{
    if (nOn == 1) {
        GPIO_SetBits(GPIOE, GPIO_Pin_6);
    } else {
        GPIO_ResetBits(GPIOE, GPIO_Pin_6);
    }
}

int get_language_type(void)
{
    uint16_t v = ReadEEPROM(150);
    if (v != 1 && v != 2) {
        v = 1;
    }
    return v;
}

/* ---------------- keys ---------------- */

void Key_Scan_Initial(void)
{
    /* The vendor zeroes eight key state machines; the open core keys
     * are level-polled, so only the published state is cleared. */
    Key = 0;
    KeyGoLineFlag = 0;
}

void key_detection(void)
{
    /* Minimal level scan: PE5 press publishes to the Key register the
     * same way the vendor state machine did (press = key code 1). */
    static uint8_t s_prev;
    uint8_t down =
        (GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_5) == Bit_RESET) ? 1 : 0;
    if (down && !s_prev) {
        Key = 1;
    }
    s_prev = down;
    if (KeyGoLineFlag != 0) {
        KeyGoLineFlag = 0;
    }
}

int button_state(int nKey)
{
    int pressed = 0;
    if (nKey == 1) {
        if ((GPIOE->IDR & 0x20) == 0) {
            pressed = 1;
        }
    }
    KeyGoLineFlag = 0;
    v_hwKeyTimer = 1;
    return pressed;
}

uint16_t TstCh(void)
{
    return Key;
}

uint16_t GetCh(void)
{
    uint16_t v = Key;
    Key = 0;
    if (v != 0) {
        PlaySpeech(46);
    }
    return v;
}

void clr_Goline_KeyFlag(void)
{
    KeyGoLineFlag = 0;
}

/* ---------------- Bluetooth ---------------- */

static void BT_Send(const char *pchData, uint8_t chLen)
{
    for (uint8_t i = 0; i < chLen; i++) {
        while (!(USART2->SR & USART_SR_TXE)) {
        }
        USART2->DR = (uint8_t)pchData[i];
    }
    while (!(USART2->SR & USART_SR_TC)) {
    }
}

void BT_mode(uint8_t chMode)
{
    if (chMode == 0) {
        BT_Send("update", 6);
    } else {
        BT_Send("exitud", 6);
    }
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t ch = (uint8_t)USART_ReceiveData(USART2);
        if (btUsartRxCnt <= 19) {
            if (btUsartRxCnt == 0) {
                if (ch == 'w') {
                    btUsartRxBuf[0] = ch;
                    btUsartRxCnt = 1;
                }
            } else {
                btUsartRxBuf[btUsartRxCnt++] = ch;
            }
        }
    }
    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET) {
        btRxFrameOK = 1;
    }
}

/* ---------------- UART5 smart-sensor frame parser ----------------
 *
 * Receive-side parser for the smart-sensor port line (see
 * open_core/PROTOCOL.md). Four frame types share the line:
 *   - gray data (prefix 0xAA/0xFE, tag 0x55): header = payload count,
 *     no checksum; a sealed frame refreshes Gray[] with little-endian
 *     16-bit slot values and raises Uart5RxFrameState,
 *   - line option ('w' 'h' 0x17 + 17 payload bytes + one-complement
 *     sum): on success the first payload byte goes into the
 *     dance-motion register,
 *   - mechanic (tag 0xAA): header = payload count - 1, payload ends
 *     with the negated byte sum of the preceding payload,
 *   - option module (tag <= 0x3F): the tag doubles as the payload
 *     count; 0xFE bytes are fillers that are stored but not counted,
 *     no checksum.
 * Every frame ends with a seal byte received while the payload
 * counter is zero; it finalizes the frame and is never stored.
 * Broken frames, overlong declared lengths and interrupts without
 * RXNE abandon the partial frame and return to idle.
 */

/* Return the parser to idle: clear the frame position, the payload
 * counter and all three mode flags. The status/data register read
 * pair that accompanies a reset is performed by the interrupt entry
 * and exit paths. */
static void wb_uart5_frame_reset(void)
{
    Uart5RxCnt = 0;
    s_chUart5PayloadLeft = 0;
    s_chUart5LineOptionArmed = 0;
    s_chUart5MechanicArmed = 0;
    s_chUart5OptionModuleArmed = 0;
}

void UART5_IRQHandler(void)
{
    uint8_t chByte;
    if (UART5->SR & USART_SR_RXNE) {
        chByte = (uint8_t)UART5->DR;
    } else {
        /* Interrupt without receive flag (overrun/noise): abandon the
         * partial frame, then clear the flags by read. */
        wb_uart5_frame_reset();
        (void)UART5->SR;
        (void)UART5->DR;
        return;
    }
    switch (Uart5RxCnt) {
    case 0:
        /* Idle: 0xAA/0xFE start every frame type except line option;
         * 'w' arms a line-option frame. Anything else stays idle and
         * only drops the line-option arming. */
        if (chByte == 0xAAu || chByte == 0xFEu) {
            Uart5RxBuf[0] = chByte;
            Uart5RxCnt = 1;
        } else if (chByte == 'w') {
            Uart5RxBuf[0] = chByte;
            Uart5RxCnt = 1;
            s_chUart5LineOptionArmed = 1;
        } else {
            s_chUart5LineOptionArmed = 0;
        }
        break;
    case 1:
        /* Tag: an armed line-option frame must see 'h'; otherwise the
         * tag selects gray (0x55), mechanic (0xAA, arms the mechanic
         * mode) or option module (<= 0x3F, arms that mode and doubles
         * as the payload count). Anything else is a full reset. */
        if (s_chUart5LineOptionArmed != 0) {
            if (chByte == 'h') {
                Uart5RxBuf[1] = chByte;
                Uart5RxCnt = 2;
            } else {
                wb_uart5_frame_reset();
            }
        } else if (chByte == 0x55u) {
            Uart5RxBuf[1] = chByte;
            Uart5RxCnt = 2;
        } else if (chByte == 0xAAu) {
            Uart5RxBuf[1] = chByte;
            Uart5RxCnt = 2;
            s_chUart5MechanicArmed = 1;
        } else if (chByte <= 0x3Fu) {
            Uart5RxBuf[1] = chByte;
            Uart5RxCnt = 2;
            s_chUart5PayloadLeft = chByte;
            s_chUart5OptionModuleArmed = 1;
        } else {
            wb_uart5_frame_reset();
        }
        break;
    case 2:
        /* Header: 0x17 completes a line-option header; for mechanic
         * frames it declares payload + checksum byte, for gray frames
         * the payload count; for option-module frames 0xFE is a filler
         * that extends the expected payload by one. Every advancing
         * case stores the byte at Uart5RxBuf[2]. */
        if (s_chUart5LineOptionArmed != 0 && chByte != 0x17u) {
            wb_uart5_frame_reset();
            break;
        }
        Uart5RxBuf[2] = chByte;
        Uart5RxCnt = 3;
        if (Uart5RxBuf[1] == 'h') {
            s_chUart5PayloadLeft = 17;
        } else if (Uart5RxBuf[1] == 0xAAu) {
            s_chUart5PayloadLeft = (uint8_t)(chByte + 1u);
        } else if (Uart5RxBuf[1] == 0x55u) {
            s_chUart5PayloadLeft = chByte;
        } else if (chByte == 0xFEu) {
            s_chUart5PayloadLeft++;
        }
        break;
    default:
        if (s_chUart5PayloadLeft == 0) {
            /* Seal: the byte finalizes the frame and is dropped. */
            if (Uart5RxBuf[1] == 0x55u) {
                /* Gray data: refresh the slot values from little-endian
                 * payload pairs and raise the frame flag. */
                uint32_t nValues = ((uint32_t)Uart5RxBuf[2] + 1) / 2;
                if (nValues > 5) {
                    nValues = 5;
                }
                for (uint32_t k = 0; k < nValues; k++) {
                    Gray[k] = (uint16_t)(Uart5RxBuf[3 + 2 * k] |
                                         (Uart5RxBuf[4 + 2 * k] << 8));
                }
                Uart5RxFrameState = 1;
            } else if (Uart5RxBuf[1] == 'h') {
                /* Line option: payload[16] holds the one-complement
                 * sum of the header and payload[0..15]. */
                uint8_t chSum = 0;
                for (uint8_t i = 2; i <= 18; i++) {
                    chSum += Uart5RxBuf[i];
                }
                if ((uint8_t)~chSum == Uart5RxBuf[19]) {
                    s_chUart5DanceMotion = Uart5RxBuf[3];
                    s_chUart5LineOptionArmed = 0;
                } else {
                    wb_uart5_frame_reset();
                }
            } else if (Uart5RxBuf[1] == 0xAAu) {
                /* Mechanic: payload ends with the negated sum of the
                 * preceding payload bytes. */
                uint8_t chSum = 0;
                uint8_t chLen = Uart5RxBuf[2];
                for (uint8_t i = 0; i < chLen; i++) {
                    chSum += Uart5RxBuf[3 + i];
                }
                if ((uint8_t)(0u - chSum) != Uart5RxBuf[3 + chLen]) {
                    wb_uart5_frame_reset();
                }
            }
            /* Option-module frames (tag <= 0x3F) carry no checksum:
             * the seal consumes the frame and the arming stays set, as
             * does the mechanic arming on a verified frame. */
            Uart5RxCnt = 0;
        } else {
            /* Payload: store and count down -- except that 0xFE bytes
             * of option-module frames (tag <= 0x3F) are stored without
             * counting. When the counter reaches zero the next byte is
             * the seal. */
            if (Uart5RxCnt >= sizeof(Uart5RxBuf)) {
                wb_uart5_frame_reset();
                break;
            }
            Uart5RxBuf[Uart5RxCnt] = chByte;
            Uart5RxCnt++;
            if (!(Uart5RxBuf[1] <= 0x3Fu && chByte == 0xFEu)) {
                s_chUart5PayloadLeft--;
            }
        }
        break;
    }
}

/* ---------------- EEPROM (internal flash page) ---------------- */

uint16_t ReadEEPROM(uint8_t chIdx)
{
    uint16_t v = *(uint16_t *)(WB_EEPROM_PAGE + (uint32_t)chIdx * 2u);
    if (v >= 0x1000) {
        v = 0xFFF;
    }
    return v;
}

void WriteEEPROM(uint8_t chIdx, uint16_t hwValue)
{
    uint16_t buf[WB_EEPROM_SLOTS];
    for (uint32_t i = 0; i < WB_EEPROM_SLOTS; i++) {
        buf[i] = *(uint16_t *)(WB_EEPROM_PAGE + i * 2u);
    }
    buf[chIdx] = hwValue;
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR |
                    FLASH_FLAG_WRPRTERR);
    FLASH_ErasePage(WB_EEPROM_PAGE);
    for (uint32_t i = 0; i < WB_EEPROM_SLOTS; i++) {
        FLASH_ProgramHalfWord(WB_EEPROM_PAGE + i * 2u, buf[i]);
    }
    FLASH_Lock();
}

/* Program slot table (from the vendor init_program_space). */
static uint8_t ProgramIdx = 1;
static uint32_t USER_PROGRAM_ADDR = 0x08019000u;

void init_program_space(void)
{
    ProgramIdx = (uint8_t)ReadEEPROM(165);
    if (ProgramIdx == 4) {
        USER_PROGRAM_ADDR = 0x08064000u;
    } else if (ProgramIdx == 3) {
        USER_PROGRAM_ADDR = 0x08059000u;
    } else if (ProgramIdx == 2) {
        USER_PROGRAM_ADDR = 0x08039000u;
    } else {
        ProgramIdx = 1;
        USER_PROGRAM_ADDR = 0x08019000u;
    }
    set_display_program_idx(ProgramIdx);
}

/* ---------------- SPI data flash (SPI2, CS = PB12) ---------------- */

static void wb_flash_cs(int nLow)
{
    if (nLow) {
        GPIO_ResetBits(GPIOB, GPIO_Pin_12);
    } else {
        GPIO_SetBits(GPIOB, GPIO_Pin_12);
    }
}

static uint8_t wb_spi_byte(uint8_t chOut)
{
    while (!(SPI2->SR & SPI_I2S_FLAG_TXE)) {
    }
    SPI2->DR = chOut;
    while (!(SPI2->SR & SPI_I2S_FLAG_RXNE)) {
    }
    return (uint8_t)SPI2->DR;
}

void sFLASH_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB,
                           ENABLE);
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    cfg.GPIO_Mode = GPIO_Mode_AF_PP;
    cfg.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOB, &cfg);
    cfg.GPIO_Mode = GPIO_Mode_Out_PP;
    cfg.GPIO_Pin = GPIO_Pin_12;
    GPIO_Init(GPIOB, &cfg);
    GPIO_ResetBits(GPIOB, GPIO_Pin_12);
    SPI_I2S_DeInit(SPI2);
    SPI_InitTypeDef spi;
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_High;
    spi.SPI_CPHA = SPI_CPHA_2Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7;
    SPI_Init(SPI2, &spi);
    SPI_Cmd(SPI2, ENABLE);
    wb_flash_cs(0);
}

void SPI_SetFlashAddress(uint32_t nAddress)
{
    g_SpiAddr = nAddress;
}

static void IncAddress(void)
{
    g_SpiAddr++;
}

uint32_t SPI_ReadLongFromFlash(void)
{
    uint32_t v;
    wb_flash_cs(1);
    wb_spi_byte(0x03);
    wb_spi_byte((uint8_t)(g_SpiAddr >> 16));
    wb_spi_byte((uint8_t)(g_SpiAddr >> 8));
    wb_spi_byte((uint8_t)g_SpiAddr);
    v = wb_spi_byte(0xA5);
    v |= (uint32_t)wb_spi_byte(0xA5) << 8;
    v |= (uint32_t)wb_spi_byte(0xA5) << 16;
    v |= (uint32_t)wb_spi_byte(0xA5) << 24;
    IncAddress();
    IncAddress();
    IncAddress();
    IncAddress();
    wb_flash_cs(0);
    return v;
}

void SPI_ReadnByteFromFlash(uint8_t *pchBuf, uint16_t hwLen)
{
    wb_flash_cs(1);
    wb_spi_byte(0x03);
    wb_spi_byte((uint8_t)(g_SpiAddr >> 16));
    wb_spi_byte((uint8_t)(g_SpiAddr >> 8));
    wb_spi_byte((uint8_t)g_SpiAddr);
    for (uint16_t i = 0; i < hwLen; i++) {
        pchBuf[i] = wb_spi_byte(0xA5);
        IncAddress();
    }
    wb_flash_cs(0);
}

/* ---------------- sensors / modules / screen pump ---------------- */

int get_ultra_distance(uint8_t chPort)
{
    static uint16_t temp11225;
    uint8_t buf[6];
    uint16_t dist = 0;
    wait(1.0f);
    memset(buf, 0, sizeof(buf));
    if (chPort >= 1 && chPort <= 4) {
        i2c_read((wb_i2c_t *)&tI2cResource[chPort - 1], 0x55, 0, 0, 2, buf);
    }
    dist = (uint16_t)(buf[3] << 8 | buf[2]);
    if (dist <= 350) {
        uint16_t filtered = buf[3];
        temp11225 = (uint16_t)(filtered << 8);
        filtered = buf[2];
        temp11225 = (uint16_t)(temp11225 + filtered);
    }
    return dist;
}

void Set_Rgb_Color(uint8_t chPort, uint8_t chR, uint8_t chG, uint8_t chB)
{
    uint8_t buf[5] = {8, 1, chR, chG, chB};
    if (chPort >= 1 && chPort <= 4) {
        i2c_write((wb_i2c_t *)&tI2cResource[chPort - 1], 0x30, 0, 0, 5, buf);
    }
}

int get_image(uint8_t chPort)
{
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x12;
    buf[1] = 0x34;
    if (chPort == 0 || chPort > 4) {
        return 0xFF;
    }
    i2c_read((wb_i2c_t *)&tI2cResource[chPort - 1], 0x54, 0, 0, 1, buf);
    return buf[0];
}

/* ---------------- parked vendor paths (see README.md) ---------------
 * The in-app USB download receiver and the camera/vision task are
 * vendor protocols not yet reimplemented. The hooks below keep the
 * link and runtime behavior sane: uploads go through the boot
 * firmware (whale_cli path) and the MC101s has no camera module. */

void hid_rx_buffer_handle(void)
{
}

void hid_download_task(void)
{
}

void camera_sensor_task(void)
{
}

void USB_Scan_Connect(void)
{
    /* Track the cable-detect line and pulse the pull control when a
     * host appears, then keep the device stack clocked down: the app
     * slot performs no USB traffic (downloads belong to the boot
     * firmware). */
    static uint16_t prev;
    static uint16_t usbCnt;
    uint32_t state = 0;
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_10) != 0) {
        state |= 4;
    }
    if (state == 0) {
        if (prev != state && usbCnt == 31) {
            RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, DISABLE);
            NVIC_DisableIRQ((IRQn_Type)59);
            NVIC_DisableIRQ((IRQn_Type)20);
            NVIC_DisableIRQ((IRQn_Type)42);
            usbCnt = 0;
        }
        prev = 0;
        usbCnt = 0;
    } else {
        if (prev != state) {
            InitUSBPULL();
            prev = state;
        }
    }
}

/* ---------------- PO16 servo bus ----------------
 *
 * The servo chain hangs on a dedicated half-duplex bus speaking a
 * Feetech-SCS-style protocol over USART3 at 1 Mbaud: every frame is
 * ff ff id len instr params... sum, with sum = the one-complement of
 * the byte sum of id, len, instr and params (header and sum byte
 * excluded). The direction gate on PB10/PB11 selects TX or RX phase;
 * transmit streams through DMA1 channel 2 from Uart3sendbuf and
 * replies land in Uart3receivebuf through DMA1 channel 3, which stays
 * armed with NDTR = 128 between transactions. Replies are checksum
 * verified in servo_state_update and folded into s_chServoReportState
 * (one state byte per servo id 1..16).
 */

static uint8_t Uart3sendbuf[128];
static uint8_t Uart3receivebuf[128];
uint8_t s_chServoReportState[16];

/* Write-frame shape per register: 1 = 16-bit register (len 05, value
 * low + high byte), 2 = short form (len 04, value low byte), 0 =
 * rejected (read-only/reserved, no frame sent). */
static const uint8_t s_chServoRegClass[49] = {
    1, 0, 0, 2, 2, 2, 1, 0, 1, 0,
    2, 2, 2, 2, 1, 0, 2, 2, 2, 0,
    1, 0, 1, 0, 2, 2, 2, 2, 2, 2,
    1, 0, 2, 0, 1, 0, 1, 0, 1, 0,
    1, 0, 2, 2, 2, 0, 2, 2, 2,
};

/* Re-arm the RX DMA for the next transaction: stop the channel, reset
 * the transfer count to the full buffer, run again. */
void servo_bus_rev_prep(void)
{
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA_SetCurrDataCounter(DMA1_Channel3, 128);
    DMA_Cmd(DMA1_Channel3, ENABLE);
}

/* Send a frame in chunks of at most 128 bytes. Waits for any previous
 * transfer to drain (the TX channel idles at NDTR 128, counting down
 * while the bus sits in RX phase, capped at 30000 spins), flips the
 * direction gate to TX, streams every chunk through the DMA, and
 * returns the gate to RX phase for the reply. */
void servo_bus_send(uint8_t *pchBuf, uint16_t hwLen)
{
    uint32_t nSpin = 30000;
    uint16_t hwOff = 0;
    while (DMA_GetCurrDataCounter(DMA1_Channel2) != 0 && nSpin != 0) {
        nSpin--;
    }
    GPIO_SetBits(GPIOD, GPIO_Pin_10);
    GPIO_ResetBits(GPIOD, GPIO_Pin_11);
    while (hwOff < hwLen) {
        uint16_t hwChunk = hwLen - hwOff;
        if (hwChunk > 128) {
            hwChunk = 128;
        }
        memcpy(Uart3sendbuf, pchBuf + hwOff, hwChunk);
        DMA_Cmd(DMA1_Channel2, DISABLE);
        DMA_SetCurrDataCounter(DMA1_Channel2, hwChunk);
        DMA_Cmd(DMA1_Channel2, ENABLE);
        while (DMA_GetFlagStatus(DMA1_FLAG_TC2) == RESET) {
        }
        DMA_ClearFlag(DMA1_FLAG_TC2);
        DMA_Cmd(DMA1_Channel2, DISABLE);
        DMA_SetCurrDataCounter(DMA1_Channel2, 128);
        DMA_Cmd(DMA1_Channel2, ENABLE);
        hwOff += hwChunk;
    }
    GPIO_SetBits(GPIOD, GPIO_Pin_11);
    GPIO_ResetBits(GPIOD, GPIO_Pin_10);
}

/* Wait for a reply (double timeout: 29999 spins or a 19 ms window on
 * the 1 ms tick, whichever expires first). The first RXNE byte is
 * discarded; the DMA buffer then holds the reply. Returns the reply
 * length, 0 on timeout. Only called after a send, so the gate is
 * already in RX phase. */
int servo_bus_rev(uint8_t *pchBuf)
{
    uint32_t nT0 = SystemTimeCnt;
    for (uint32_t nSpin = 0; nSpin < 29999; nSpin++) {
        if (SystemTimeCnt - nT0 > 19) {
            break;
        }
        if (USART3->SR & USART_SR_RXNE) {
            int nLen;
            (void)USART3->DR;
            DMA_Cmd(DMA1_Channel3, DISABLE);
            nLen = 128 - (int)DMA_GetCurrDataCounter(DMA1_Channel3);
            if (nLen < 0) {
                nLen = 0;
            }
            memcpy(pchBuf, Uart3receivebuf, (size_t)nLen);
            servo_bus_rev_prep();
            return nLen;
        }
    }
    servo_bus_rev_prep();
    return 0;
}

/* Checksum-verify a reply frame and record the sender's state byte:
 * the sum of id, length and payload must complement to the trailing
 * sum byte. */
void servo_state_update(uint8_t *pchBuf)
{
    uint8_t chSum = 0;
    if (pchBuf == NULL) {
        return;
    }
    for (int i = 2; i <= (int)pchBuf[3] + 2; i++) {
        chSum += pchBuf[i];
    }
    if ((uint8_t)(~chSum) == pchBuf[pchBuf[3] + 3] &&
        pchBuf[2] >= 1 && pchBuf[2] <= 16) {
        s_chServoReportState[pchBuf[2] - 1] = pchBuf[4];
    }
}

/* Register write, dispatched over the register class table. Registers
 * above 48 and rejected classes return 0 without a frame. A broadcast
 * (id 0xFE) succeeds without waiting for a reply. */
uint8_t PO16_WriteRegister(int nId, int nReg, int nVal)
{
    uint8_t pchBuf[16];
    uint8_t chLen;
    if (nReg < 0 || nReg > 48) {
        return 0;
    }
    if (s_chServoRegClass[nReg] == 1) {
        pchBuf[3] = 5;
        pchBuf[5] = (uint8_t)nReg;
        pchBuf[6] = (uint8_t)nVal;
        pchBuf[7] = (uint8_t)(nVal >> 8);
        chLen = 9;
    } else if (s_chServoRegClass[nReg] == 2) {
        pchBuf[3] = 4;
        pchBuf[5] = (uint8_t)nReg;
        pchBuf[6] = (uint8_t)nVal;
        chLen = 8;
    } else {
        return 0;
    }
    pchBuf[0] = 0xFF;
    pchBuf[1] = 0xFF;
    pchBuf[2] = (uint8_t)nId;
    pchBuf[4] = 3;
    uint8_t chSum = 0;
    for (int i = 2; i <= (int)chLen - 2; i++) {
        chSum += pchBuf[i];
    }
    pchBuf[chLen - 1] = (uint8_t)~chSum;
    servo_bus_send(pchBuf, chLen);
    if (nId == 0xFE) {
        return 1;
    }
    if (servo_bus_rev(pchBuf) == 0) {
        return 0;
    }
    servo_state_update(pchBuf);
    return 1;
}

/* Register read: both writable classes use the same read frame and
 * return the reply's parameter bytes as a 16-bit little-endian value,
 * 0 on timeout or a rejected register. */
int PO16_ReadRegister(int nId, int nReg)
{
    uint8_t pchBuf[16];
    if (nReg < 0 || nReg > 48 || s_chServoRegClass[nReg] == 0) {
        return 0;
    }
    pchBuf[0] = 0xFF;
    pchBuf[1] = 0xFF;
    pchBuf[2] = (uint8_t)nId;
    pchBuf[3] = 4;
    pchBuf[4] = 2;
    pchBuf[5] = (uint8_t)nReg;
    pchBuf[6] = 2;
    pchBuf[7] = (uint8_t)~(nId + 4 + 2 + nReg + 2);
    servo_bus_send(pchBuf, 8);
    if (servo_bus_rev(pchBuf) == 0) {
        return 0;
    }
    servo_state_update(pchBuf);
    return pchBuf[5] | ((int)pchBuf[6] << 8);
}

/* Goal move: position/speed as 16-bit little-endian words behind the
 * goal register 0x1E (instruction 3). Out-of-range values are clamped,
 * negatives become 0 in the payload. The mode argument selects nothing
 * on the wire (rotation/angle selection happens via servo_mode_set). */
uint8_t set_servo(int nId, int nSpeed, int nPos, int nMode)
{
    uint8_t pchBuf[16];
    (void)nMode;
    if (nPos > 1023) {
        nPos = 1023;
    }
    if (nSpeed > 2047) {
        nSpeed = 2047;
    }
    if (nPos < 0) {
        nPos = 0;
    }
    if (nSpeed < 0) {
        nSpeed = 0;
    }
    pchBuf[0] = 0xFF;
    pchBuf[1] = 0xFF;
    pchBuf[2] = (uint8_t)nId;
    pchBuf[3] = 7;
    pchBuf[4] = 3;
    pchBuf[5] = 0x1E;
    pchBuf[6] = (uint8_t)nPos;
    pchBuf[7] = (uint8_t)(nPos >> 8);
    pchBuf[8] = (uint8_t)nSpeed;
    pchBuf[9] = (uint8_t)(nSpeed >> 8);
    pchBuf[10] = (uint8_t)~(pchBuf[2] + 7 + 3 + 0x1E + pchBuf[6] +
                            pchBuf[7] + pchBuf[8] + pchBuf[9]);
    servo_bus_send(pchBuf, 11);
    if (servo_bus_rev(pchBuf) == 0) {
        return 0;
    }
    servo_state_update(pchBuf);
    return 1;
}

/* Range-checked goal move wrapper: speed 0..2047, position 0..1023. */
uint8_t servo_ctrl(int nId, int nSpeed, int nPos, int nMode)
{
    if (nSpeed < 0 || nSpeed >= 2048 || nPos < 0 || nPos >= 1024) {
        return 0xFF;
    }
    return set_servo(nId, nSpeed, nPos, nMode);
}

/* Position (servo) mode: write the mode register, then restore the
 * speed limit; broadcasts need a pause between the two writes. */
int PO16_ServoMode(int nId)
{
    PO16_WriteRegister(nId, 6, 0);
    if (nId == 0xFE) {
        wait(0.01);
    }
    PO16_WriteRegister(nId, 8, 1023);
    return 0;
}

/* Endless (rotation) mode: mode register, pause for broadcasts, then
 * zero the speed limit. */
int PO16_EndlessMode(int nId)
{
    PO16_WriteRegister(nId, 6, 0);
    if (nId == 0xFE) {
        wait(0.01);
    }
    PO16_WriteRegister(nId, 8, 0);
    return 0;
}

void servo_mode_set(int nId, int nMode)
{
    if (nMode != 0) {
        PO16_EndlessMode(nId);
    } else {
        PO16_ServoMode(nId);
    }
}

/* Probe for a servo: two attempts of the ping frame, each verified
 * against the expected id before the reply is recorded. */
int Servo_Ping(int nId)
{
    uint8_t pchBuf[16];
    for (int nTry = 0; nTry < 2; nTry++) {
        memset(pchBuf, 0, sizeof(pchBuf));
        pchBuf[0] = 0xFF;
        pchBuf[1] = 0xFF;
        pchBuf[2] = (uint8_t)nId;
        pchBuf[3] = 2;
        pchBuf[4] = 1;
        pchBuf[5] = (uint8_t)~(nId + 2 + 1);
        servo_bus_send(pchBuf, 6);
        if (servo_bus_rev(pchBuf) != 0 && pchBuf[2] == (uint8_t)nId) {
            servo_state_update(pchBuf);
            wait(0.002);
            return 1;
        }
    }
    return 0;
}

/* Refresh the cached current position before torque engagement (no
 * effect on the register bus; register 80 is not writable). */
void servo_refresh_current_position(int nId)
{
    if (nId == 0xFE || (nId > 0 && nId <= 20)) {
        if (PO16_WriteRegister(nId, 80, 1) != 0) {
            wait(0.0011);
        }
    }
}

void servo_enable_torque(int nId)
{
    servo_refresh_current_position(nId);
    PO16_WriteRegister(nId, 24, 1);
    wait(0.002);
}

void servo_disable_torque(int nId)
{
    PO16_WriteRegister(nId, 24, 0);
    wait(0.002);
}

void servo_id_change(int nId, int nNewId)
{
    PO16_WriteRegister(nId, 3, nNewId);
    wait(0.002);
}

int ReadServoaAngle(int nId)
{
    return PO16_ReadRegister(nId, 36);
}

void servo_init_flag_refresh(void)
{
}

/* Angle <-> position for the user-side command paths: position =
 * angle * 888 / 300 + 512 (float form), and the integer inverse. */
float servo_position_convert_float(float nAngle)
{
    return (nAngle * 888.0f) / 300.0f + 512.0f;
}

int servo_position_convert_int(int nPosition)
{
    return ((nPosition - 512) * 300) / 888;
}

/* ---------------- init tail ---------------- */

void init_control_tab(void)
{
    for (uint8_t i = 0; i <= 3; i++) {
        ClrPID(i);
    }
}

void bus_init(void)
{
    /* Expansion/servo bus: USART3 at 1 Mbaud 8N1 on the full remap,
     * half-duplex direction gate on PB10/PB11, TX DMA on channel 2
     * (armed per transfer), RX DMA on channel 3 (armed for the full
     * 128-byte buffer continuously). */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOD |
                           RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    /* USART3 full remap: TX = PD8, RX = PD9; the half-duplex
     * direction gate is PD10/PD11 (TX selects PD10 low, RX selects
     * PD11 low). The vendor configures exactly these GPIOD pins --
     * hardware-found 2026-09-09: an earlier port put the gate on
     * PB10/PB11, which are the face-digit scan commons, and their
     * cells then rendered the union of their patterns. */
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    cfg.GPIO_Mode = GPIO_Mode_Out_PP;
    cfg.GPIO_Pin = GPIO_Pin_11;
    GPIO_Init(GPIOD, &cfg);
    cfg.GPIO_Pin = GPIO_Pin_10;
    GPIO_Init(GPIOD, &cfg);
    cfg.GPIO_Mode = GPIO_Mode_AF_PP;
    cfg.GPIO_Pin = GPIO_Pin_8;
    GPIO_Init(GPIOD, &cfg);
    cfg.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    cfg.GPIO_Pin = GPIO_Pin_9;
    GPIO_Init(GPIOD, &cfg);
    GPIO_SetBits(GPIOD, GPIO_Pin_11);
    GPIO_ResetBits(GPIOD, GPIO_Pin_10);
    GPIO_PinRemapConfig(GPIO_FullRemap_USART3, ENABLE);
    USART_InitTypeDef uart;
    uart.USART_BaudRate = 1000000;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &uart);
    USART_DMACmd(USART3, USART_DMAReq_Tx | USART_DMAReq_Rx, ENABLE);
    USART_Cmd(USART3, ENABLE);
    DMA_DeInit(DMA1_Channel2);
    DMA_InitTypeDef dma;
    dma.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    dma.DMA_MemoryBaseAddr = (uint32_t)Uart3sendbuf;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = 0;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_VeryHigh;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel2, &dma);
    DMA_DeInit(DMA1_Channel3);
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_MemoryBaseAddr = (uint32_t)Uart3receivebuf;
    dma.DMA_BufferSize = 128;
    DMA_Init(DMA1_Channel3, &dma);
    DMA_Cmd(DMA1_Channel3, ENABLE);
}



