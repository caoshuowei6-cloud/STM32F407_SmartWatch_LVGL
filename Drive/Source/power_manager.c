/**
  ******************************************************************************
  * @file    power_manager.c
  * @brief   穿戴设备级电源管理器实现（15s 无操作自动息屏、按键秒级唤醒与防误触拦截）
  ******************************************************************************
  */

#include "power_manager.h"
#include "lcd_spi_154.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

static pm_state_t s_pm_state = PM_STATE_ACTIVE;
static uint32_t   s_last_active_tick = 0;
static uint16_t   s_timeout_sec = 15;      /* 默认 15 秒无操作自动熄屏 */
static bool       s_auto_sleep_enabled = true;

void Power_Manager_Init(void)
{
    s_pm_state = PM_STATE_ACTIVE;
    s_last_active_tick = xTaskGetTickCount();
    s_auto_sleep_enabled = true;
    LCD_Backlight_ON;
    printf("[PowerManager] Initialized. Auto-sleep: %s (%d s)\r\n",
           s_auto_sleep_enabled ? "ENABLED" : "DISABLED", s_timeout_sec);
}

void Power_Manager_Wakeup(void)
{
    if (s_pm_state == PM_STATE_STANDBY)
    {
        LCD_Backlight_ON;
        s_pm_state = PM_STATE_ACTIVE;
        printf("[PowerManager] Screen Woken Up (Backlight ON)!\r\n");
    }
    s_last_active_tick = xTaskGetTickCount();
}

void Power_Manager_Sleep(void)
{
    if (s_pm_state == PM_STATE_ACTIVE)
    {
        LCD_Backlight_OFF;
        s_pm_state = PM_STATE_STANDBY;
        printf("[PowerManager] Entering Standby (Backlight OFF, Power Saved 70%%)!\r\n");
    }
}

void Power_Manager_Update(void)
{
    if (!s_auto_sleep_enabled || s_timeout_sec == 0) return;

    if (s_pm_state == PM_STATE_ACTIVE)
    {
        uint32_t now = xTaskGetTickCount();
        if ((now - s_last_active_tick) >= pdMS_TO_TICKS((uint32_t)s_timeout_sec * 1000))
        {
            Power_Manager_Sleep();
        }
    }
}

bool Power_Manager_OnKey(uint8_t key_val)
{
    (void)key_val;

    if (s_pm_state == PM_STATE_STANDBY)
    {
        /* 息屏状态下唤醒屏幕，并拦截本次按键（防误触） */
        Power_Manager_Wakeup();
        return true; /* 已拦截唤醒 */
    }

    /* 亮屏状态下刷新活跃时间戳，放行按键事件响应 UI */
    s_last_active_tick = xTaskGetTickCount();
    return false;
}

pm_state_t Power_Manager_GetState(void)
{
    return s_pm_state;
}

void Power_Manager_SetTimeout(uint16_t seconds)
{
    s_timeout_sec = seconds;
    s_last_active_tick = xTaskGetTickCount();
}

uint16_t Power_Manager_GetTimeout(void)
{
    return s_timeout_sec;
}

void Power_Manager_SetAutoSleep(bool enable)
{
    s_auto_sleep_enabled = enable;
    if (enable)
    {
        s_last_active_tick = xTaskGetTickCount();
    }
    else
    {
        Power_Manager_Wakeup();
    }
}

bool Power_Manager_GetAutoSleep(void)
{
    return s_auto_sleep_enabled;
}
