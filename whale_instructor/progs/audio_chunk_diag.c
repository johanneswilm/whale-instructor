/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Chunked-playback locator + display checks (display fix should make
 * every value readable now).
 *   40: boot marker RUN3 (5 s) - extra bars gone?
 *   41: set_display_num(20) (5 s) - must show 2 then 0, not 88.
 *   470: file 47 header, shown one value at a time: 471 = sample rate,
 *   472 = block align, 473 = data length low 16 bits.
 *   60: manual chunk loop over file 47's PCM using the REAL
 *       dac_dma_init (with the underrun clear) and a 1 s TC timeout
 *       per chunk; shows 61, then 0 = every chunk completed, or N =
 *       the chunk (1-based) whose transfer-complete flag never
 *       arrived (left on the face, playback stopped).
 *   71: PlaySpeech(47) entered; 70 = returned.
 *   81: PlaySensorNum(1) entered; 80 = returned. Listen here.
 * Loops forever. */
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
    vTaskDelay(2500);
}

static void chunk_show(uint16_t v)
{
    set_display_num(v);
    vTaskDelay(2500);
}

static void convert_buf(void)
{
    for (uint16_t i = 0; i <= 255; i++) {
        int16_t v = (int16_t)hwBuff1[i];
        hwBuff1[i] = (uint16_t)(((v >> 4) ^ 0x800) & 0xFFF);
    }
}

/* Self-contained per-chunk arm (align-aware) with the underrun clear
 * and DAC DMA re-enable, matching the E2 recipe that completed on the
 * bench. Does not touch wb_audio's private state. */
static void arm_chunk(uint32_t buf, uint16_t count, uint16_t align)
{
    DMA_InitTypeDef dma;

    DMA_Cmd(DMA2_Channel4, DISABLE);
    WB_DAC_SR = WB_DAC_SR_DMAUDR2;
    DAC_DMACmd(DAC_Channel_2, ENABLE);
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

void user_main()
{
    uint32_t off, len, rate, remaining;
    uint16_t align, chunkno;

    vTaskDelay(300);
    for (;;)
    {
        mark(40);
        display_run_program_idx(3);
        vTaskDelay(3000);
        mark(41);
        set_display_num(20);
        vTaskDelay(3000);
        SPI_SetFlashAddress(47 * 4);
        off = SPI_ReadLongFromFlash();
        SPI_SetFlashAddress(off);
        SPI_ReadnByteFromFlash(hdr, 44);
        rate = (uint32_t)hdr[24] | ((uint32_t)hdr[25] << 8);
        align = (uint16_t)(hdr[32] | (hdr[33] << 8));
        len = (uint32_t)hdr[40] | ((uint32_t)hdr[41] << 8) |
              ((uint32_t)hdr[42] << 16) | ((uint32_t)hdr[43] << 24);
        mark(470);
        chunk_show((uint16_t)(rate & 0xFFFF));
        mark(472);
        chunk_show(align);
        mark(473);
        chunk_show((uint16_t)(len & 0xFFFF));
        /* idle speaker path for the manual loop */
        {
            GPIO_InitTypeDef gi;
            DAC_InitTypeDef di;
            DMA_InitTypeDef dma;
            RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2 | RCC_AHBPeriph_CRC,
                                  ENABLE);
            RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO |
                                   RCC_APB2Periph_GPIOA, ENABLE);
            RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC |
                                   RCC_APB1Periph_TIM7, ENABLE);
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
            {
                TIM_TimeBaseInitTypeDef tb;
                RCC_ClocksTypeDef clk;
                TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);
                RCC_GetClocksFreq(&clk);
                TIM_TimeBaseStructInit(&tb);
                tb.TIM_Period = (uint16_t)(clk.PCLK1_Frequency / rate);
                tb.TIM_Prescaler = 1;
                TIM_TimeBaseInit(TIM7, &tb);
                TIM_Cmd(TIM7, ENABLE);
            }
        }
        SP_ON();
        remaining = len;
        chunkno = 0;
        for (;;)
        {
            uint32_t n = (remaining > 512) ? 512 : remaining;
            uint16_t plen = (uint16_t)(n >> (align / 2));
            uint32_t t;
            int tc = 0;
            SPI_ReadnByteFromFlash((uint8_t *)hwBuff1, (uint16_t)n);
            if (align > 1) {
                convert_buf();
            }
            arm_chunk((uint32_t)hwBuff1, plen, align);
            for (t = 0; t < 1000000; t++) {
                if (DMA_GetITStatus(DMA2_FLAG_TC4)) {
                    tc = 1;
                    break;
                }
            }
            chunkno++;
            if (tc == 0) {
                break;
            }
            remaining -= n;
            if (remaining == 0) {
                break;
            }
        }
        TIM_Cmd(TIM7, DISABLE);
        DMA_Cmd(DMA2_Channel4, DISABLE);
        SP_OFF();
        mark(60);
        mark(61);
        chunk_show(chunkno);
        mark(71);
        PlaySpeech(47);
        mark(70);
        mark(81);
        PlaySensorNum(1);
        mark(80);
        vTaskDelay(3000);
    }
}
