/* wb_audio.c -- speech playback engine for the MC101s controller.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Whale Instructor open core: WAV
 * playback from the SPI data flash (PlaySpeech / PlaySensorNum), the
 * built-in sound table (InitSound), the DAC2/DMA2/TIM7 speaker path
 * and the DMA transfer-complete interrupt.
 *
 * Hardware map (facts also listed in wb_bsp.c):
 *   - speaker: DAC2 out 2 on PA5 (analog), one sample per TIM7 update
 *     event (TRGO), samples streamed by DMA2 channel 4 in normal mode,
 *     re-armed per block by the flash-playback path;
 *   - 16-bit samples take the DHR12R2 halfword register path
 *     (0x40007414), 8-bit samples the DHR8R2 byte path (0x4000741C);
 *   - amplifier shutdown line PC15: high = amp on, low = muted. The
 *     pin mode belongs to wb_bsp.c's audio_interface_init (push-pull
 *     output, reset low); this file only sets/resets it;
 *   - the PlaySpeech path arms one DMA block at a time and POLLS the
 *     DMA TC flag (the IRQ stays NVIC-masked); the InitSound path
 *     arms the whole file and finishes via the DMA2 channel 4/5 IRQ.
 *
 * Flash speech-file layout (written by the vendor firmware, read here):
 * u32 word 0 = file count N, u32 word 4*idx = absolute byte offset of
 * 1-based file idx, then 44-byte canonical mono WAV headers + PCM.
 * Data is consumed strictly sequentially through the running SPI
 * cursor: the start address is set once per file, never mid-file.
 *
 * Deliberate divergences from the vendor object (accepted):
 *   - DiVoice (built-in sound table) ships empty as {0}: the vendor's
 *     embedded WAV snippet is proprietary audio data and is not
 *     reproduced. Every InitSound(idx) therefore returns 1 (a silent
 *     no-op) -- the same observable behavior as a table whose magic
 *     checks fail; the 8-bit DHR8R2 code path stays exercised.
 *   - PlayInit / PlaySpeechTest / ReadDataToBuf / nBuffer are omitted:
 *     no vendor-header declaration and no caller in any current link.
 *   - Register-level writes are expressed through StdPeriph calls
 *     (same register outcomes); the DAC DHR register constants below
 *     are the normative check.
 */
#include "wb_core.h"
#include <stm32f10x_gpio.h>
#include <stm32f10x_rcc.h>
#include <stm32f10x_dac.h>
#include <stm32f10x_dma.h>
#include <stm32f10x_tim.h>
#include <misc.h>
#include <string.h>

/* ---------------- private prototypes ---------------- */
static void StopSound(void);
static void dac_sample_rate_config(TIM_TypeDef *tim, uint32_t rate);
static void pcm_data_get_ready(uint32_t nAddress, uint16_t hwLength);
static void audio_output_init(void);
static void audio_output_interface_init(uint32_t nAddress, uint16_t hwLength);
static uint8_t check_packet_convert_over(void);
static uint8_t get_Speech_data(uint8_t *pBuf);
static uint8_t InitSpeech(uint16_t idx);
static uint16_t u16_le(const uint8_t *p);
static uint32_t u32_le(const uint8_t *p);
static uint8_t wav_header_ok(void);

/* ---------------- state ---------------- */

/* 44-byte canonical WAV header scratch. Fields are composed byte-wise
 * (no alignment assumptions), see u16_le/u32_le. */
static uint8_t wHeader[44];
static uint32_t Length;        /* PCM bytes still unread from the file */
static int16_t PerSecByte;     /* WAV block align: 1 = 8-bit, 2 = 16-bit */
static uint16_t PlayLength;    /* samples in the buffer just filled */
static uint16_t *pEncode;      /* buffer the next state-4 pass will arm */

/* 1 = InitSound armed a transfer, 2 = the DMA TC IRQ fired. Only
 * InitSound writes 1, only the IRQ handler writes 2; the Mode-driven
 * busy-wait in InitSound polls it, so it must be volatile. */
static volatile uint16_t s_tState;

/* PlaySpeech blocking machine: playState 0 idle, 1 init, 3 waiting
 * for the current DMA block, 4 arm next block, 5 cleanup. */
static uint8_t playState;
static uint16_t pageIndex;


/* Built-in sound table: ours is empty (count 0). Format: u32 count,
 * then u32 offsets into the table for idx 0..count, then WAV blobs. */
static const uint32_t DiVoice[] = {0};

/* DAC data registers the DMA writes to, selected by PerSecByte. */
static const uint32_t DAC_DHR12R2 = 0x40007414u;
static const uint32_t DAC_DHR8R2 = 0x4000741Cu;

/* ---------------- amplifier + timer control ---------------- */

void SP_ON(void)
{
    GPIO_SetBits(GPIOC, GPIO_Pin_15);
}

void SP_OFF(void)
{
    GPIO_ResetBits(GPIOC, GPIO_Pin_15);
}

static void StopSound(void)
{
    TIM_Cmd(TIM7, DISABLE);
}

/* Pacing timer for the DAC trigger: TIM7 update events at `rate` Hz.
 * The APB1 clock is 2x PCLK1 (APB1 div 2), the prescaler adds one
 * more divider, so the net update rate is PCLK1/Period = ~rate Hz. */
static void dac_sample_rate_config(TIM_TypeDef *tim, uint32_t rate)
{
    RCC_ClocksTypeDef clk;
    TIM_TimeBaseInitTypeDef tb;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
    TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);
    RCC_GetClocksFreq(&clk);
    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Period = (uint16_t)(clk.PCLK1_Frequency / rate);
    tb.TIM_Prescaler = 1;
    TIM_TimeBaseInit(tim, &tb);
    TIM_Cmd(tim, ENABLE);
}

void pcm_sample_rate_config(uint16_t hwSample)
{
    dac_sample_rate_config(TIM7, hwSample);
}

/* DAC SR lives at 0x40007400 + 0x34 (RM0008); channel 2's DMA underrun
 * flag is bit 28. The vendor-era StdPeriph header predates the register
 * and offers no accessor. A set underrun flag disables the channel's
 * DMA requests until software clears it, which silently stalls any
 * later block -- bench-confirmed 2026-09-09 (audio_seq_diag: E1 stalled
 * with the flag set, E2 completed after the clear). */
#define WB_DAC_SR          (*(volatile uint32_t *)0x40007434)
#define WB_DAC_SR_DMAUDR2  ((uint32_t)0x10000000)

/* Re-arm one DMA block on channel 4: the DAC DCR12R2/DHR8R2 register
 * as fixed peripheral target, the given buffer as source. Normal
 * mode: the channel disables itself at transfer end and is re-armed
 * here for the next block. TC interrupt config is NOT touched. Any
 * stale underrun state is cleared and the DAC's DMA interface
 * re-enabled first, or the block would arm into a dead request
 * line. */
void dac_dma_init(uint32_t nAddress, uint16_t hwLength)
{
    DMA_InitTypeDef dma;

    DMA_Cmd(DMA2_Channel4, DISABLE);
    WB_DAC_SR = WB_DAC_SR_DMAUDR2;
    DAC_DMACmd(DAC_Channel_2, ENABLE);
    dma.DMA_BufferSize = hwLength;
    dma.DMA_PeripheralBaseAddr = (PerSecByte > 1) ? DAC_DHR12R2 : DAC_DHR8R2;
    dma.DMA_MemoryBaseAddr = nAddress;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = (PerSecByte > 1)
                                     ? DMA_PeripheralDataSize_HalfWord
                                     : DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = (PerSecByte > 1)
                                 ? DMA_MemoryDataSize_HalfWord
                                 : DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel4, &dma);
    DMA_Cmd(DMA2_Channel4, ENABLE);
}

static void pcm_data_get_ready(uint32_t nAddress, uint16_t hwLength)
{
    dac_dma_init(nAddress, hwLength);
}

/* ---------------- DAC/DMA bring-up ---------------- */

/* Speaker path setup without starting playback (flash path): clocks,
 * PA5 analog, TIM7 update trigger selected, DAC channel 2 on TIM7
 * TRGO, idle DMA configuration with TC interrupt DISABLED (polled
 * mode). NVIC is left untouched -- IRQ 59 stays masked from bsp_init
 * unless the one-shot path enables it. */
static void audio_output_init(void)
{
    GPIO_InitTypeDef dac;
    DAC_InitTypeDef dacInit;
    DMA_InitTypeDef dma;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2 | RCC_AHBPeriph_CRC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA,
                           ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);

    dac.GPIO_Pin = GPIO_Pin_5;
    dac.GPIO_Speed = GPIO_Speed_50MHz;
    dac.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &dac);

    TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);
    TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);

    dacInit.DAC_Trigger = DAC_Trigger_T7_TRGO;
    dacInit.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dacInit.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    dacInit.DAC_OutputBuffer = DAC_OutputBuffer_Disable;
    DAC_Init(DAC_Channel_2, &dacInit);
    if (PerSecByte > 1) {
        DAC_SetChannel2Data(DAC_Align_12b_R, 0);
    } else {
        DAC_SetChannel2Data(DAC_Align_8b_R, 0);
    }

    dma.DMA_BufferSize = 0;
    dma.DMA_PeripheralBaseAddr = (PerSecByte > 1) ? DAC_DHR12R2 : DAC_DHR8R2;
    dma.DMA_MemoryBaseAddr = 0;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = (PerSecByte > 1)
                                     ? DMA_PeripheralDataSize_HalfWord
                                     : DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = (PerSecByte > 1)
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

/* Speaker path setup AND start for the one-shot (built-in sound)
 * path: same peripherals, but the DMA is armed with the whole file,
 * the TC interrupt enabled and the NVIC line unmasked. TIM7 is
 * commanded on here but its clock/rate setup happens in the caller's
 * pcm_sample_rate_config afterwards, so the timer starts last. */
static void audio_output_interface_init(uint32_t nAddress, uint16_t hwLength)
{
    GPIO_InitTypeDef dac;
    DAC_InitTypeDef dacInit;
    DMA_InitTypeDef dma;
    NVIC_InitTypeDef nvic;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2 | RCC_AHBPeriph_CRC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA,
                           ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

    dac.GPIO_Pin = GPIO_Pin_5;
    dac.GPIO_Speed = GPIO_Speed_50MHz;
    dac.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &dac);

    dacInit.DAC_Trigger = DAC_Trigger_T7_TRGO;
    dacInit.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dacInit.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    dacInit.DAC_OutputBuffer = DAC_OutputBuffer_Disable;
    DAC_Init(DAC_Channel_2, &dacInit);
    if (PerSecByte > 1) {
        DAC_SetChannel2Data(DAC_Align_12b_R, 0);
    } else {
        DAC_SetChannel2Data(DAC_Align_8b_R, 0);
    }

    dma.DMA_BufferSize = hwLength;
    dma.DMA_PeripheralBaseAddr = (PerSecByte > 1) ? DAC_DHR12R2 : DAC_DHR8R2;
    dma.DMA_MemoryBaseAddr = nAddress;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = (PerSecByte > 1)
                                     ? DMA_PeripheralDataSize_HalfWord
                                     : DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = (PerSecByte > 1)
                                 ? DMA_MemoryDataSize_HalfWord
                                 : DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel4, &dma);

    nvic.NVIC_IRQChannel = DMA2_Channel4_5_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    DAC_Cmd(DAC_Channel_2, ENABLE);
    DAC_DMACmd(DAC_Channel_2, ENABLE);
    TIM_Cmd(TIM7, ENABLE);
    DMA_ITConfig(DMA2_Channel4, DMA_IT_TC, ENABLE);
    DMA_Cmd(DMA2_Channel4, ENABLE);
}

/* Polled transfer-complete check for the flash path: the TC4 bit
 * stays set until this clears it (the IRQ is masked here). */
static uint8_t check_packet_convert_over(void)
{
    if (DMA_GetITStatus(DMA2_FLAG_TC4)) {
        DMA_ClearITPendingBit(DMA2_FLAG_TC4);
        return 1;
    }
    return 0;
}

/* ---------------- PCM streaming ---------------- */

/* Read the next chunk of the open speech file into a sample buffer
 * and convert 16-bit signed PCM in place to unsigned 12-bit DAC
 * codes: (s >> 4) ^ 0x800. Blocks are 512 bytes (16-bit) or 256
 * bytes (8-bit); the last chunk may be short. The conversion loop
 * always touches all 256 halfwords even on a short final block --
 * the stale tail is harmless because DMA only plays PlayLength
 * samples. Always returns 1. */
static uint8_t get_Speech_data(uint8_t *pBuf)
{
    uint32_t n;

    if (Length > 512) {
        n = (uint32_t)PerSecByte << 8;
        SPI_ReadnByteFromFlash(pBuf, (uint16_t)n);
        PlayLength = 256;
        Length -= n;
    } else {
        SPI_ReadnByteFromFlash(pBuf, (uint16_t)Length);
        PlayLength = (uint16_t)(Length >> (PerSecByte / 2));
        Length = 0;
    }
    if (PerSecByte > 1) {
        uint16_t *p = (uint16_t *)pBuf;
        for (uint16_t i = 0; i <= 255; i++) {
            int16_t v = (int16_t)p[i];
            p[i] = (uint16_t)(((v >> 4) ^ 0x800) & 0xFFF);
        }
    }
    return 1;
}

/* Open flash speech file idx (1-based; 0 is invalid and would collide
 * with the count word). Reads the WAV header, configures the speaker
 * path, then prefills both double buffers: hwBuff1 gets armed right
 * away, hwBuff2 waits for the first state-4 pass of PlaySpeech.
 * Returns 0 = started, 1 = fail. */
static uint8_t InitSpeech(uint16_t idx)
{
    if (idx == 0) {
        return 1;
    }
    SPI_SetFlashAddress(0);
    uint32_t count = SPI_ReadLongFromFlash();
    if (count < (uint32_t)idx) {
        return 1;
    }
    SPI_SetFlashAddress((uint32_t)idx * 4);
    uint32_t offset = SPI_ReadLongFromFlash();
    SPI_SetFlashAddress(offset);
    SPI_ReadnByteFromFlash(wHeader, 44);
    if (!wav_header_ok()) {
        return 1;
    }
    Length = u32_le(wHeader + 40);
    PerSecByte = (int16_t)u16_le(wHeader + 32);
    audio_output_init();
    pcm_sample_rate_config((uint16_t)u32_le(wHeader + 24));
    pEncode = hwBuff1;
    get_Speech_data((uint8_t *)hwBuff1);
    pcm_data_get_ready((uint32_t)pEncode, PlayLength);
    pEncode = hwBuff2;
    get_Speech_data((uint8_t *)hwBuff2);
    SP_ON();
    return 0;
}

/* Blocking playback machine. State 3 busy-waits for each DMA block
 * (SysTick/IWDG keep running); state 4 arms the prefilled buffer and
 * refills the other one. Returns 1 (finished or failed), never 0. */
uint8_t PlaySpeech(uint16_t idx)
{
    for (;;) {
        switch (playState) {
        case 0:
            pageIndex = 0;
            playState = 1;
            /* fall through */
        case 1:
            if (InitSpeech(idx) != 0) {
                playState = 0;
                    return 1;
            }
            playState = 3;
            /* fall through */
        case 3:
            if (!check_packet_convert_over()) {
                continue;
            }
            playState = 4;
            /* fall through */
        case 4:
            if (Length == 0) {
                playState = 5;
                continue;
            }
            pageIndex++;
            /* Arm the buffer pEncode points at with the PlayLength of
             * its LAST fill -- read before the refill below. */
            pcm_data_get_ready((uint32_t)pEncode, PlayLength);
            pEncode = (pageIndex & 1) ? hwBuff1 : hwBuff2;
            get_Speech_data((uint8_t *)pEncode);
            playState = 3;
            continue;
        case 5:
            playState = 0;
            StopSound();
            SP_OFF();
            return 1;
        default:
            continue;
        }
    }
}

/* Speak a value digit-by-digit from the flash speech table: digits
 * 0..9 are speech 35+d (Chinese) / 60+d (English), the leading word
 * is speech 47 / 59. Leading zeros are suppressed, but interior zeros
 * after a spoken thousand/ten-thousand digit are spoken (e.g. 105 ->
 * "1", "5"; 1005 -> "1", "0", "0", "5"). The hundreds/tens branches
 * deliberately do NOT set `spoken` (vendor quirk, reproduced as-is).
 * The language type is re-read per branch (vendor shape). */
void PlaySensorNum(uint16_t value)
{
    uint8_t spoken = 0;
    uint16_t base;
    uint16_t d;

    PlaySpeech((get_language_type() == 1) ? 47 : 59);
    d = value / 10000;
    if (d != 0) {
        base = (get_language_type() == 1) ? 35 : 60;
        PlaySpeech((uint16_t)(d + base));
        value = (uint16_t)(value - d * 10000);
        spoken = 1;
    }
    d = value / 1000;
    base = (get_language_type() == 1) ? 35 : 60;
    if (d != 0) {
        PlaySpeech((uint16_t)(d + base));
        value = (uint16_t)(value - d * 1000);
        spoken = 1;
    } else if (spoken != 0) {
        PlaySpeech(base);
    }
    d = value / 100;
    base = (get_language_type() == 1) ? 35 : 60;
    if (d != 0) {
        PlaySpeech((uint16_t)(d + base));
        value = (uint16_t)(value - d * 100);
    } else if (spoken != 0) {
        PlaySpeech(base);
    }
    d = value / 10;
    base = (get_language_type() == 1) ? 35 : 60;
    if (d != 0) {
        PlaySpeech((uint16_t)(d + base));
        value = (uint16_t)(value - d * 10);
    } else if (spoken != 0) {
        PlaySpeech(base);
    }
    base = (get_language_type() == 1) ? 35 : 60;
    PlaySpeech((uint16_t)(value + base));
}

/* Stream one WAV from the built-in DiVoice table (NOT the flash):
 * one-shot DMA of the whole file (CNDTR truncates the length to 16
 * bits), finished by the TC interrupt. Mode != 0 busy-waits for the
 * IRQ. Returns 0 = started, 1 = fail. With the empty table every idx
 * fails the unsigned count check (or the RIFF/WAVE magic for idx 0),
 * so this is a silent no-op in our build. */
uint8_t InitSound(int idx, uint8_t Mode)
{
    if ((uint32_t)idx > DiVoice[0]) {
        return 1;
    }
    uint32_t offset = u32_le((const uint8_t *)DiVoice + (uint32_t)idx * 4);
    const uint8_t *p = (const uint8_t *)DiVoice + offset;
    memcpy(wHeader, p, 44);
    if (!wav_header_ok()) {
        return 1;
    }
    Length = u32_le(wHeader + 40);
    PerSecByte = (int16_t)u16_le(wHeader + 32);
    p += 44;
    audio_output_interface_init((uint32_t)p, (uint16_t)Length);
    pcm_sample_rate_config((uint16_t)u32_le(wHeader + 24));
    s_tState = 1;
    SP_ON();
    if (Mode != 0) {
        while (s_tState != 2) {
        }
    }
    return 0;
}

/* One-shot path finisher: playback self-terminates by stopping the
 * pacing timer when the whole file's DMA transfer completes. */
void DMA2_Channel4_5_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA2_FLAG_TC4)) {
        DMA_ClearITPendingBit(DMA2_FLAG_TC4);
        s_tState = 2;
        StopSound();
    }
}

uint32_t get_speech_count(void)
{
    SPI_SetFlashAddress(0);
    return SPI_ReadLongFromFlash();
}

/* ---------------- little-endian readers ---------------- */

static uint16_t u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* RIFF/WAVE magic check. Only 4 bytes of each tag are compared (the
 * vendor kept 5-byte arrays with the trailing NUL). */
static uint8_t wav_header_ok(void)
{
    return (uint8_t)(memcmp(wHeader, "RIFF", 4) == 0 &&
                     memcmp(wHeader + 8, "WAVE", 4) == 0);
}
