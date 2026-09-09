/* stm32f10x_conf.h -- Standard Peripheral Library driver selection for
 * the Whale Instructor runtime.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The ST template shape with the driver subset the Whale Instructor
 * open libraries are built from.
 */
#ifndef __STM32F10x_CONF_H
#define __STM32F10x_CONF_H

#include "stm32f10x_flash.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_spi.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_usart.h"
#include "misc.h"

#ifdef  USE_FULL_ASSERT
    #define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))
    void assert_failed(uint8_t *file, uint32_t line);
#else
    #define assert_param(expr) ((void)0)
#endif

#endif /* __STM32F10x_CONF_H */
