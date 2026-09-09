/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Audio sequencing bench probe + display characterization.
 *
 * Display part (once at boot, 3 s each): set_display_num 1, 11, 88,
 * 1234 - characterizes the doubled-digit rendering against the vendor
 * image (slot 2 shows the same program single-digit with bottom bars).
 *
 * Audio part (endless loop, three variants of the PlaySpeech hardware
 * sequence for flash file 1, whose header reads rate 8000, block align
 * 2, length 388 bytes -> 194 halfword samples):
 *   E1 (shows 11): exact wb_audio order - pcm_sample_rate_config
 *       (starts TIM7) BEFORE the SPI read and DMA arm, no underrun
 *       flag handling.
 *   E2 (shows 12): same order, but the DAC DMA underrun flag is
 *       cleared and DAC_DMACmd re-enabled right before arming.
 *   E3 (shows 13): TIM7 held off during init/read/arm (rate config
 *       inlined with TIM_Cmd left disabled), started only after the
 *       channel is armed, underrun flag cleared first.
 * Each experiment displays: variant code (11/12/13), the DMAUDR2 flag
 * state read right before arming (1 = underrun already happened during
 * the read window, 0 = not), then the poll verdict (1 = transfer
 * complete flag arrived, 0 = timed out) plus the DMAUDR2 state after
 * the wait (x2). Audio should be audible in variants that work. */
#include "whale_instructor.h"
#include <stm32f10x_dac.h>
#include <stm32f10x_dma.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_rcc.h>
#include <stm32f10x_gpio.h>
#include <misc.h>

static uint8_t hdr[44];
static uint16_t pcm[256];

static void seq_show(uint32_t v)
{
    set_display_num((uint16_t)(v & 0xFFFF));
    vTaskDelay(3000);
}

static void dac_dma_clocks(void)
{
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2 | RCC_AHBPeriph_CRC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA,
                           ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
}

static void dac_gpio_init(void)
{
    GPIO_InitTypeDef gi;
    gi.GPIO_Pin = GPIO_Pin_5;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gi);
}

static void speaker_idle_init(uint16_t align)
{
    DAC_InitTypeDef di;
    DMA_InitTypeDef dma;

    dac_dma_clocks();
    dac_gpio_init();
    TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);
    TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);
    di.DAC_Trigger = DAC_Trigger_T7_TRGO;
    di.DAC_WaveGeneration = DAC_WaveGeneration_None;
    di.DAC_LFSRUnmask_TriangleAmplitude = 0;
    di.DAC_OutputBuffer = DAC_OutputBuffer_Disable;
    DAC_Init(DAC_Channel_2, &di);
    if (align > 1) {
        DAC_SetChannel2Data(DAC_Align_12b_R, 0);
    } else {
        DAC_SetChannel2Data(DAC_Align_8b_R, 0);
    }
    dma.DMA_BufferSize = 0;
    dma.DMA_PeripheralBaseAddr = (align > 1) ? 0x40007414 : 0x4000741C;
    dma.DMA_MemoryBaseAddr = 0;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = (align > 1)
                                     ? DMA_PeripheralDataSize_HalfWord
                                     : DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = (align > 1)
                                 ? DMA_MemoryDataSize_HalfWord
                                 : DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel4, &dma);
    DMA_Cmd(DMA2_Channel4, DISABLE);
    DMA_ITConfig(DMA2_Channel4, DMA_IT_TC, DISABLE);
    DAC_Cmd(DAC_Channel_2, ENABLE);
    DAC_DMACmd(DAC_Channel_2, ENABLE);
}

static void rate_config_hold_start(uint32_t rate, int start)
{
    RCC_ClocksTypeDef clk;
    TIM_TimeBaseInitTypeDef tb;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
    TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);
    RCC_GetClocksFreq(&clk);
    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Period = (uint16_t)(clk.PCLK1_Frequency / rate);
    tb.TIM_Prescaler = 1;
    TIM_TimeBaseInit(TIM7, &tb);
    TIM_Cmd(TIM7, start ? ENABLE : DISABLE);
}

static void arm_block(uint32_t buf, uint16_t count, uint16_t align)
{
    DMA_InitTypeDef dma;

    DMA_Cmd(DMA2_Channel4, DISABLE);
    dma.DMA_BufferSize = count;
    dma.DMA_PeripheralBaseAddr = (align > 1) ? 0x40007414 : 0x4000741C;
    dma.DMA_MemoryBaseAddr = buf;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = (align > 1)
                                     ? DMA_PeripheralDataSize_HalfWord
                                     : DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = (align > 1)
                                 ? DMA_MemoryDataSize_HalfWord
                                 : DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel4, &dma);
    DMA_Cmd(DMA2_Channel4, ENABLE);
}

/* DAC SR: 0x40007400 + 0x34; channel 2 underrun = bit 28 (RM0008).
 * The vendor-era StdPeriph header does not model this register. */
#define DAC_SR_REG  (*(volatile uint32_t *)0x40007434)

static uint16_t dmaudr2(void)
{
    return (DAC_SR_REG & DAC_SR_DMAUDR2) ? 1 : 0;
}

static int audio_experiment(int variant)
{
    uint32_t off, len, rate;
    uint16_t align, count, udr_before, verdict;
    uint32_t t;
    int tc;

    SPI_SetFlashAddress(4);
    off = SPI_ReadLongFromFlash();
    SPI_SetFlashAddress(off);
    SPI_ReadnByteFromFlash(hdr, 44);
    rate = (uint32_t)hdr[24] | ((uint32_t)hdr[25] << 8);
    align = (uint16_t)(hdr[32] | (hdr[33] << 8));
    len = (uint32_t)hdr[40] | ((uint32_t)hdr[41] << 8) |
          ((uint32_t)hdr[42] << 16) | ((uint32_t)hdr[43] << 24);
    if (len > 512) {
        len = 512;
    }
    count = (align > 1) ? (uint16_t)(len / 2) : (uint16_t)len;

    speaker_idle_init(align);
    if (variant == 2) {
        rate_config_hold_start(rate, 0);
    } else {
        rate_config_hold_start(rate, 1);
    }
    SPI_ReadnByteFromFlash((uint8_t *)pcm, (uint16_t)len);
    udr_before = dmaudr2();
    if (variant >= 1) {
        DAC_SR_REG = DAC_SR_DMAUDR2;
        DAC_DMACmd(DAC_Channel_2, ENABLE);
    }
    arm_block((uint32_t)pcm, count, align);
    if (variant == 2) {
        TIM_Cmd(TIM7, ENABLE);
    }
    SP_ON();
    tc = 0;
    for (t = 0; t < 4000000; t++) {
        if (DMA_GetITStatus(DMA2_FLAG_TC4)) {
            tc = 1;
            break;
        }
    }
    verdict = (uint16_t)(tc | (dmaudr2() << 1));
    TIM_Cmd(TIM7, DISABLE);
    DMA_Cmd(DMA2_Channel4, DISABLE);
    SP_OFF();
    return (int)udr_before * 100 + verdict;
}

void user_main()
{
    vTaskDelay(300);
    for (;;)
    {
        seq_show(1);
        seq_show(11);
        seq_show(88);
        seq_show(1234);
        seq_show(11);
        seq_show((uint32_t)audio_experiment(0));
        seq_show(12);
        seq_show((uint32_t)audio_experiment(1));
        seq_show(13);
        seq_show((uint32_t)audio_experiment(2));
        seq_show(0);
        vTaskDelay(4000);
    }
}
