/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* PlaySpeech(47) replica bisection. File 47 = 8000 Hz, 16-bit, 892
 * bytes = 2 chunks; the manual loop plays both fine, so this replays
 * the machine's exact order - the second fill happens BETWEEN arming
 * block 1 and waiting for its transfer-complete flag - with a marker
 * per step and an underrun-aware verdict.
 *   91: idle speaker init + rate config done
 *   92: flash cursor walk (count/offset/header) done, shows Length
 *       (892 expected)
 *   93: fill 1 done (512 bytes into hwBuff1, converted, 256 samples)
 *   94: block 1 armed (underrun clear + DAC DMA re-enable)
 *   95: fill 2 done (380 bytes into hwBuff2, converted) - the step
 *       the real InitSpeech performs while block 1 plays
 *   96: wait verdict: 1 = block 1 completed, 0 = timed out, +2 if the
 *       underrun flag set during the wait
 *   97: replica done
 *   71: PlaySpeech(47) entered; 70 = returned
 * Loops forever. */
#include "whale_instructor.h"
#include <stm32f10x_dac.h>
#include <stm32f10x_dma.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_rcc.h>
#include <stm32f10x_gpio.h>
#include <misc.h>

extern uint16_t hwBuff1[512];
extern uint16_t hwBuff2[512];

static uint8_t hdr[44];

#define WB_DAC_SR          (*(volatile uint32_t *)0x40007434)
#define WB_DAC_SR_DMAUDR2  ((uint32_t)0x10000000)

static void mark(int n)
{
    set_display_num((uint16_t)(n & 0xFFFF));
    vTaskDelay(2000);
}

static void convert(uint16_t *p)
{
    for (uint16_t i = 0; i <= 255; i++) {
        int16_t v = (int16_t)p[i];
        p[i] = (uint16_t)(((v >> 4) ^ 0x800) & 0xFFF);
    }
}

static void arm_chunk(uint32_t buf, uint16_t count)
{
    DMA_InitTypeDef dma;

    DMA_Cmd(DMA2_Channel4, DISABLE);
    WB_DAC_SR = WB_DAC_SR_DMAUDR2;
    DAC_DMACmd(DAC_Channel_2, ENABLE);
    dma.DMA_BufferSize = count;
    dma.DMA_PeripheralBaseAddr = 0x40007414;
    dma.DMA_MemoryBaseAddr = buf;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel4, &dma);
    DMA_Cmd(DMA2_Channel4, ENABLE);
}

void user_main()
{
    uint32_t off, len, rate;
    uint32_t t;
    int tc;
    uint16_t verdict;

    vTaskDelay(300);
    for (;;)
    {
        SPI_SetFlashAddress(47 * 4);
        off = SPI_ReadLongFromFlash();
        SPI_SetFlashAddress(off);
        SPI_ReadnByteFromFlash(hdr, 44);
        rate = (uint32_t)hdr[24] | ((uint32_t)hdr[25] << 8);
        len = (uint32_t)hdr[40] | ((uint32_t)hdr[41] << 8) |
              ((uint32_t)hdr[42] << 16) | ((uint32_t)hdr[43] << 24);
        {
            GPIO_InitTypeDef gi;
            DAC_InitTypeDef di;
            DMA_InitTypeDef dma;
            TIM_TimeBaseInitTypeDef tb;
            RCC_ClocksTypeDef clk;
            RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2 | RCC_AHBPeriph_CRC,
                                  ENABLE);
            RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO |
                                   RCC_APB2Periph_GPIOA, ENABLE);
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
            DAC_SetChannel2Data(DAC_Align_12b_R, 0);
            dma.DMA_BufferSize = 0;
            dma.DMA_PeripheralBaseAddr = 0x40007414;
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
            DMA_Cmd(DMA2_Channel4, DISABLE);
            DMA_ITConfig(DMA2_Channel4, DMA_IT_TC, DISABLE);
            DAC_Cmd(DAC_Channel_2, ENABLE);
            DAC_DMACmd(DAC_Channel_2, ENABLE);
            TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);
            RCC_GetClocksFreq(&clk);
            TIM_TimeBaseStructInit(&tb);
            tb.TIM_Period = (uint16_t)(clk.PCLK1_Frequency / rate);
            tb.TIM_Prescaler = 1;
            TIM_TimeBaseInit(TIM7, &tb);
            TIM_Cmd(TIM7, ENABLE);
        }
        mark(91);
        mark(9200 + (uint16_t)(len & 0xFF));
        SPI_ReadnByteFromFlash((uint8_t *)hwBuff1, 512);
        convert(hwBuff1);
        mark(93);
        arm_chunk((uint32_t)hwBuff1, 256);
        mark(94);
        SPI_ReadnByteFromFlash((uint8_t *)hwBuff2, (uint16_t)(len - 512));
        convert(hwBuff2);
        mark(95);
        SP_ON();
        tc = 0;
        for (t = 0; t < 2000000; t++) {
            if (DMA_GetITStatus(DMA2_FLAG_TC4)) {
                tc = 1;
                break;
            }
        }
        verdict = (uint16_t)(tc | (((WB_DAC_SR & WB_DAC_SR_DMAUDR2) ? 1u : 0u)
                                   << 1));
        mark(96);
        mark(verdict);
        mark(97);
        TIM_Cmd(TIM7, DISABLE);
        DMA_Cmd(DMA2_Channel4, DISABLE);
        SP_OFF();
        mark(71);
        PlaySpeech(47);
        mark(70);
        vTaskDelay(3000);
    }
}
