/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Audio path bench probe (isolates the phase-4 open-core audio bug).
 * Runs the stages in order, showing the stage number on the face
 * display before each, with intermediate values where useful:
 *   1: raw DAC beep - configures DAC2/DMA2 ch4/TIM7/amp directly and
 *      plays a soft 500 Hz square. No SPI involved: if this is silent
 *      the DAC/DMA/TIM7/amp path is broken, not the flash reads.
 *   2: get_speech_count() - shows the u32 word at flash address 0.
 *      A sane table gives a small count (tens); garbage means the SPI
 *      flash read layer returns nonsense.
 *   3: file-1 offset (low 16 bits) read from flash word 1.
 *   4: file-1 WAV header verdict: 1 = RIFF/WAVE magic ok, 0 = bad.
 *   5: file-1 sample rate (should be 8000 for the vendor speech).
 *   6: file-1 data length (low 16 bits).
 *   7: PlaySpeech(1) - expects a spoken "one".
 *   8: PlaySpeech(10) - expects a spoken "ten".
 * Then shows 0 and repeats the whole tour every ~6 s so it can be
 * re-checked after a missed stage. */
#include "whale_instructor.h"
#include <stm32f10x_dac.h>
#include <stm32f10x_dma.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_rcc.h>
#include <stm32f10x_gpio.h>
#include <misc.h>

static void stage(int n)
{
    set_display_num(n);
    vTaskDelay(1500);
}

static void stage_value(uint32_t v)
{
    set_display_num((uint16_t)(v & 0xFFFF));
    vTaskDelay(2500);
}

static void raw_beep(void)
{
    static uint16_t buf[256];
    GPIO_InitTypeDef gi;
    TIM_TimeBaseInitTypeDef tb;
    DAC_InitTypeDef di;
    DMA_InitTypeDef dma;

    for (int i = 0; i < 256; i++) {
        buf[i] = ((i / 4) & 1) ? 3072 : 1024;
    }
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2 | RCC_AHBPeriph_CRC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA,
                           ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC | RCC_APB1Periph_TIM7,
                           ENABLE);
    gi.GPIO_Pin = GPIO_Pin_5;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gi);
    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler = 1;
    tb.TIM_Period = 4500;
    TIM_TimeBaseInit(TIM7, &tb);
    TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);
    di.DAC_Trigger = DAC_Trigger_T7_TRGO;
    di.DAC_WaveGeneration = DAC_WaveGeneration_None;
    di.DAC_LFSRUnmask_TriangleAmplitude = 0;
    di.DAC_OutputBuffer = DAC_OutputBuffer_Disable;
    DAC_Init(DAC_Channel_2, &di);
    DAC_SetChannel2Data(DAC_Align_12b_R, 2048);
    dma.DMA_BufferSize = 256;
    dma.DMA_PeripheralBaseAddr = 0x40007414;
    dma.DMA_MemoryBaseAddr = (uint32_t)buf;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel4, &dma);
    DAC_Cmd(DAC_Channel_2, ENABLE);
    DAC_DMACmd(DAC_Channel_2, ENABLE);
    SP_ON();
    DMA_Cmd(DMA2_Channel4, ENABLE);
    TIM_Cmd(TIM7, ENABLE);
    vTaskDelay(600);
    TIM_Cmd(TIM7, DISABLE);
    DMA_Cmd(DMA2_Channel4, DISABLE);
    SP_OFF();
}

void user_main()
{
    uint8_t hdr[44];
    uint32_t off;
    uint8_t magic_ok;

    vTaskDelay(300);
    for (;;)
    {
        stage(1);
        raw_beep();
        stage(2);
        stage_value(get_speech_count());
        stage(3);
        SPI_SetFlashAddress(4);
        off = SPI_ReadLongFromFlash();
        stage_value(off);
        stage(4);
        SPI_SetFlashAddress(off);
        SPI_ReadnByteFromFlash(hdr, 44);
        magic_ok = (hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' &&
                    hdr[3] == 'F' && hdr[8] == 'W' && hdr[9] == 'A' &&
                    hdr[10] == 'V' && hdr[11] == 'E');
        stage_value(magic_ok);
        if (magic_ok)
        {
            stage(5);
            stage_value((uint32_t)hdr[24] | ((uint32_t)hdr[25] << 8));
            stage(6);
            stage_value((uint32_t)hdr[40] | ((uint32_t)hdr[41] << 8));
        }
        stage(7);
        PlaySpeech(1);
        vTaskDelay(2000);
        stage(8);
        PlaySpeech(10);
        vTaskDelay(2000);
        stage(0);
        vTaskDelay(4000);
    }
}
