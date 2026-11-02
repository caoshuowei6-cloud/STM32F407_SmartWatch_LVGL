/**
  ******************************************************************************
  * @file    power_manager.c
  * @brief   穿戴设备级电源管理器实现（15s 无操作自动息屏、按键秒级唤醒与防误触拦截）
  ******************************************************************************
  */

#include "power_manager.h"
#include "lcd_spi_154.h"
#include "w25qxx.h"
#include "mpu6050.h"
#include "esp32_wifi.h"
#include "key.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
        printf("[PowerManager] Waking up from Standby Mode...\r\n");

        /* 1. 唤醒片外 W25Q64 SPI Flash (发送 0xAB) */
        W25QXX_Wakeup();

        /* 2. 唤醒 MPU6050 恢复六轴全功能 (恢复陀螺仪工作) */
        MPU6050_ExitLowPowerMode();

        /* 3. 唤醒 ST7789 LCD (发送 0x11 SLPOUT + 开启显示与背光) */
        LCD_ExitSleep();

        /* 4. 唤醒 ESP32-C3 退出 Modem-Sleep */
        ESP32_WiFi_ExitSleep();

        s_pm_state = PM_STATE_ACTIVE;
        printf("[PowerManager] Fully Awake! Screen, Gyro, Flash Online.\r\n");
    }
    s_last_active_tick = xTaskGetTickCount();
}

void Power_Manager_Sleep(void)
{
    if (s_pm_state == PM_STATE_ACTIVE)
    {
        printf("[PowerManager] Entering Deep Standby Mode...\r\n");

        /* 1. ST7789 屏幕深度休眠 (关闭背光 + 发送 0x10 SLPIN 切断驱动 IC 电荷泵，降耗 ~8mA) */
        LCD_EnterSleep();

        /* 2. MPU6050 陀螺仪断电休眠 (仅保留加速度计，立省 ~3.6mA) */
        MPU6050_EnterLowPowerMode();

        /* 3. W25Q64 进入深度掉电模式 (发送 0xB9，静态电流 <1uA) */
        W25QXX_PowerDown();

        /* 4. ESP32-C3 进入低功耗 Modem-Sleep (保持连网，射频周期性休眠，功耗降至 3~10mA) */
        ESP32_WiFi_EnterSleep();

        KEY_DrainWakeupEvent();
        s_pm_state = PM_STATE_STANDBY;
        printf("[PowerManager] Standby Active: LCD Sleep, Gyro Off, Flash Sleep. Power saved >85%%!\r\n");
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

/* ============================================================================
   抬腕亮屏 (Raise-to-Wake) 动作特征识别算法
   ============================================================================ */
static bool     s_wrist_wake_enabled = true;
static uint32_t s_last_wake_tick = 0;
static uint8_t  s_wrist_steady_count = 0;
static bool     s_wrist_motion_detected = false;
static uint32_t s_motion_start_tick = 0;

void Power_Manager_SetWristWake(bool enable)
{
    s_wrist_wake_enabled = enable;
}

bool Power_Manager_GetWristWake(void)
{
    return s_wrist_wake_enabled;
}

void Power_Manager_CheckWristAction(float pitch, float roll, int16_t gx, int16_t gy, int16_t gz)
{
    uint32_t now;
    int32_t gyro_mag;
    (void)gz;

    if (!s_wrist_wake_enabled || s_pm_state != PM_STATE_STANDBY)
    {
        s_wrist_motion_detected = false;
        s_wrist_steady_count = 0;
        return;
    }

    static float s_prev_pitch = -90.0f;
    now = xTaskGetTickCount();

    /* 唤醒后 2 秒防抖冷却期 */
    if ((now - s_last_wake_tick) < pdMS_TO_TICKS(2000))
    {
        return;
    }

    gyro_mag = (int32_t)abs(gx) + (int32_t)abs(gy);

    /* 1. 检测启动特征：支持陀螺仪角速度或重力俯仰角剧烈上抬特征 (零陀螺仪亦可识别) */
    if (!s_wrist_motion_detected)
    {
        bool motion_trigger = false;
        if (gyro_mag > 2200)
        {
            motion_trigger = true;
        }
        else if (s_prev_pitch < -35.0f && pitch >= -30.0f)
        {
            /* 从下垂姿态快速抬起进入看表水平线 */
            motion_trigger = true;
        }

        if (motion_trigger)
        {
            s_wrist_motion_detected = true;
            s_motion_start_tick = now;
            s_wrist_steady_count = 0;
        }
    }
    else
    {
        /* 2. 运动时间窗口 (150ms ~ 900ms) */
        uint32_t elapsed = now - s_motion_start_tick;
        if (elapsed > pdMS_TO_TICKS(900))
        {
            s_wrist_motion_detected = false;
            s_wrist_steady_count = 0;
            return;
        }

        /* 3. 看表视线区间与稳定度检查 */
        bool is_viewing_angle = (pitch >= -35.0f && pitch <= 40.0f) &&
                                (roll  >= -40.0f && roll  <= 40.0f);
        bool is_steady = (gyro_mag < 2500);

        if (is_viewing_angle && is_steady)
        {
            s_wrist_steady_count++;
            if (s_wrist_steady_count >= 3)
            {
                printf("\r\n[PowerManager] >>> Wrist-Raise Gesture Detected! (Pitch:%.1f, Roll:%.1f) Screen On! <<<\r\n", pitch, roll);
                Power_Manager_Wakeup();
                s_last_wake_tick = now;
                s_wrist_motion_detected = false;
                s_wrist_steady_count = 0;
            }
        }
        else
        {
            if (s_wrist_steady_count > 0)
            {
                s_wrist_steady_count--;
            }
        }
    }
    s_prev_pitch = pitch;
}
