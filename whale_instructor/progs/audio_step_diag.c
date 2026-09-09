/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* InitSpeech step locator: replays PlaySpeech(1)'s hardware sequence
 * step by step using the REAL open-core public functions, with a
 * face code after every step - wherever the face freezes, that step
 * is stuck. Then calls PlaySpeech(1) itself for comparison.
 *   20: idle speaker init done (clocks/PA5/DAC/DMA idle)
 *   21: pcm_sample_rate_config(8000) done (TIM7 running)
 *   22: 388 header+PCM bytes read into hwBuff1 (extern, as PlaySpeech
 *       uses it)
 *   23: 12-bit in-place conversion done, shows 60xx = PlayLength
 *       candidate (194 expected)
 *   24: dac_dma_init(hwBuff1, PlayLength) done (the real, fixed
 *       function)
 *   25: SP_ON done
 *   26: 1 s poll verdict: 1 = TC arrived, 0 = timed out, +2 if the
 *       underrun flag set
 *   27: poll done
 *   31: PlaySpeech(1) entered (freeze here = inside PlaySpeech)
 *   30: PlaySpeech(1) returned
 * Loops forever after 28. */
#include "whale_instructor.h"
#include <stm32f10x_dac.h>
#include <stm32f10x_dma.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_rcc.h>
#include <stm32f10x_gpio.h>
#include <misc.h>

extern uint16_t hwBuff1[512];

static uint8_t hdr[44];

#define WB_DAC_SR          (*(volatile uint32_t *)0x40007434)
#define WB_DAC_SR_DMAUDR2  ((uint32_t)0x10000000)

static void mark(int n)
{
    set_display_num((uint16_t)(n & 0xFFFF));
    vTaskDelay(2000);
}

static void idle_init(uint16_t align)
{
    GPIO_InitTypeDef gi;
    DAC_InitTypeDef di;
    DMA_InitTypeDef dma;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2 | RCC_AHBPeriph_CRC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA,
                           ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
    gi.GPIO_Pin = GPIO_Pin_5;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gi);
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

void user_main()
{
    uint32_t off, len, rate;
    uint16_t align, plen, verdict;
    uint32_t t;
    int tc;

    vTaskDelay(300);
    for (;;)
    {
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
        idle_init(align);
        mark(20);
        pcm_sample_rate_config((uint16_t)rate);
        mark(21);
        SPI_ReadnByteFromFlash((uint8_t *)hwBuff1, (uint16_t)len);
        mark(22);
        if (align > 1) {
            for (uint16_t i = 0; i <= 255; i++) {
                int16_t v = (int16_t)hwBuff1[i];
                hwBuff1[i] = (uint16_t)(((v >> 4) ^ 0x800) & 0xFFF);
            }
        }
        plen = (uint16_t)(len >> (align / 2));
        mark(6000 + plen);
        mark(23);
        dac_dma_init((uint32_t)hwBuff1, plen);
        mark(24);
        SP_ON();
        mark(25);
        tc = 0;
        for (t = 0; t < 2000000; t++) {
            if (DMA_GetITStatus(DMA2_FLAG_TC4)) {
                tc = 1;
                break;
            }
        }
        verdict = (uint16_t)(tc | (((WB_DAC_SR & WB_DAC_SR_DMAUDR2) ? 1u : 0u)
                                   << 1));
        mark(26);
        mark(verdict);
        mark(27);
        TIM_Cmd(TIM7, DISABLE);
        DMA_Cmd(DMA2_Channel4, DISABLE);
        SP_OFF();
        mark(31);
        PlaySpeech(1);
        mark(30);
        mark(28);
        mark(40);
        display_run_program_idx(3);
        vTaskDelay(5000);
        mark(41);
        clr_display();
        vTaskDelay(5000);
        mark(42);
        set_display_num(0);
        vTaskDelay(5000);
        vTaskDelay(4000);
    }
}
