/* app_main.c -- FreeRTOS application framework for user programs (Whale
 * Instructor runtime).
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is linked into user programs, hence LGPL -- your programs
 * stay yours, see LICENSE_EXCEPTION.md. It boots the board, restores the
 * program slot last selected on the face and starts the FreeRTOS tasks:
 * "screen" refreshes the LED matrix, "main" runs user_main() after the
 * boot flourish, "taskN" run user_taskN() when the build defines
 * USER_TASKN. wait(), music() and PlaySound() are support entry points
 * of the public API.
 */
#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "../open_core/wb_core.h"

void user_main(void);
void user_task1(void);
void user_task2(void);
void user_task3(void);
void user_task4(void);
void user_task5(void);
void user_task6(void);
void user_task7(void);
void user_task8(void);
void user_task9(void);
void user_task10(void);
void user_task11(void);
void user_task12(void);
void user_task13(void);
void user_task14(void);
void user_task15(void);

enum { LEDC_WHITE = 3 };
enum { LEDM_FLASH = 0 };
#define EEPROM_SELECTED_PROGRAM_IDX 165

static TaskHandle_t screen_task_handle;
static TaskHandle_t host_task_handle;

static void screen_task_entry(void *pv)
{
    (void)pv;
    while (1) {
        ScreenDisplay_API();
    }
}

static void host_task0(void *pv)
{
    (void)pv;
    SetRGB(LEDC_WHITE, LEDM_FLASH);
    vTaskDelay(1000);
    BT_mode(0);
    vTaskDelay(200);
    user_main();
    while (1) {
        vTaskDelay(100);
    }
}

#if defined(USER_TASK1)
static TaskHandle_t host_task1_handle;
static void host_task1(void *pv)
{
    (void)pv;
    user_task1();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK2)
static TaskHandle_t host_task2_handle;
static void host_task2(void *pv)
{
    (void)pv;
    user_task2();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK3)
static TaskHandle_t host_task3_handle;
static void host_task3(void *pv)
{
    (void)pv;
    user_task3();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK4)
static TaskHandle_t host_task4_handle;
static void host_task4(void *pv)
{
    (void)pv;
    user_task4();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK5)
static TaskHandle_t host_task5_handle;
static void host_task5(void *pv)
{
    (void)pv;
    user_task5();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK6)
static TaskHandle_t host_task6_handle;
static void host_task6(void *pv)
{
    (void)pv;
    user_task6();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK7)
static TaskHandle_t host_task7_handle;
static void host_task7(void *pv)
{
    (void)pv;
    user_task7();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK8)
static TaskHandle_t host_task8_handle;
static void host_task8(void *pv)
{
    (void)pv;
    user_task8();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK9)
static TaskHandle_t host_task9_handle;
static void host_task9(void *pv)
{
    (void)pv;
    user_task9();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK10)
static TaskHandle_t host_task10_handle;
static void host_task10(void *pv)
{
    (void)pv;
    user_task10();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK11)
static TaskHandle_t host_task11_handle;
static void host_task11(void *pv)
{
    (void)pv;
    user_task11();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK12)
static TaskHandle_t host_task12_handle;
static void host_task12(void *pv)
{
    (void)pv;
    user_task12();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK13)
static TaskHandle_t host_task13_handle;
static void host_task13(void *pv)
{
    (void)pv;
    user_task13();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK14)
static TaskHandle_t host_task14_handle;
static void host_task14(void *pv)
{
    (void)pv;
    user_task14();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

#if defined(USER_TASK15)
static TaskHandle_t host_task15_handle;
static void host_task15(void *pv)
{
    (void)pv;
    user_task15();
    while (1) {
        vTaskDelay(10);
    }
}
#endif

int main(void)
{
    bsp_init();
    __set_FAULTMASK(0);
    __set_PRIMASK(0);
    if (SysTick_Config(SystemCoreClock / 1000)) {
        while (1) {
        }
    }
    display_run_program_idx((uint8_t)ReadEEPROM(EEPROM_SELECTED_PROGRAM_IDX));
    xTaskCreate(screen_task_entry, "screen", 256, NULL, 2, &screen_task_handle);
    xTaskCreate(host_task0, "main", 256, NULL, 2, &host_task_handle);
#if defined(USER_TASK1)
    xTaskCreate(host_task1, "task1", 256, NULL, 2, &host_task1_handle);
#endif
#if defined(USER_TASK2)
    xTaskCreate(host_task2, "task2", 256, NULL, 2, &host_task2_handle);
#endif
#if defined(USER_TASK3)
    xTaskCreate(host_task3, "task3", 256, NULL, 2, &host_task3_handle);
#endif
#if defined(USER_TASK4)
    xTaskCreate(host_task4, "task4", 256, NULL, 2, &host_task4_handle);
#endif
#if defined(USER_TASK5)
    xTaskCreate(host_task5, "task5", 256, NULL, 2, &host_task5_handle);
#endif
#if defined(USER_TASK6)
    xTaskCreate(host_task6, "task6", 256, NULL, 2, &host_task6_handle);
#endif
#if defined(USER_TASK7)
    xTaskCreate(host_task7, "task7", 256, NULL, 2, &host_task7_handle);
#endif
#if defined(USER_TASK8)
    xTaskCreate(host_task8, "task8", 256, NULL, 2, &host_task8_handle);
#endif
#if defined(USER_TASK9)
    xTaskCreate(host_task9, "task9", 256, NULL, 2, &host_task9_handle);
#endif
#if defined(USER_TASK10)
    xTaskCreate(host_task10, "task10", 256, NULL, 2, &host_task10_handle);
#endif
#if defined(USER_TASK11)
    xTaskCreate(host_task11, "task11", 256, NULL, 2, &host_task11_handle);
#endif
#if defined(USER_TASK12)
    xTaskCreate(host_task12, "task12", 256, NULL, 2, &host_task12_handle);
#endif
#if defined(USER_TASK13)
    xTaskCreate(host_task13, "task13", 256, NULL, 2, &host_task13_handle);
#endif
#if defined(USER_TASK14)
    xTaskCreate(host_task14, "task14", 256, NULL, 2, &host_task14_handle);
#endif
#if defined(USER_TASK15)
    xTaskCreate(host_task15, "task15", 256, NULL, 2, &host_task15_handle);
#endif
    vTaskStartScheduler();
    return 0;
}

void wait(float sec)
{
    vTaskDelay((uint32_t)(sec * 1000.0));
}

void music(int idx)
{
    (void)InitSound(idx, 1);
}

void PlaySound(int idx)
{
    (void)InitSound(idx, 0);
}

__attribute__((weak)) void user_main(void)
{
}

__attribute__((weak)) void user_task1(void)
{
}

__attribute__((weak)) void user_task2(void)
{
}

__attribute__((weak)) void user_task3(void)
{
}

__attribute__((weak)) void user_task4(void)
{
}

__attribute__((weak)) void user_task5(void)
{
}

__attribute__((weak)) void user_task6(void)
{
}

__attribute__((weak)) void user_task7(void)
{
}

__attribute__((weak)) void user_task8(void)
{
}

__attribute__((weak)) void user_task9(void)
{
}

__attribute__((weak)) void user_task10(void)
{
}

__attribute__((weak)) void user_task11(void)
{
}

__attribute__((weak)) void user_task12(void)
{
}

__attribute__((weak)) void user_task13(void)
{
}

__attribute__((weak)) void user_task14(void)
{
}

__attribute__((weak)) void user_task15(void)
{
}
