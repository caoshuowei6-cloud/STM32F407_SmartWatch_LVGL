/**
  ******************************************************************************
  * @file    power_manager.c
  * @brief   穿戴设备级电源管理器实现（分级能耗状态机：活跃/浅待机/STOP深度休眠）
  * @note    支持 15s 息屏降频、待机 45s 进入 STOP 深度休眠、按键 EXTI 唤醒与 168MHz PLL 时钟自愈重配
  ******************************************************************************
  */

#include "power_manager.h"
#include "bsp_board.h"
#include "lcd_spi_154.h"
#include "w25qxx.h"
#include "mpu6050.h"
#include "esp32_wifi.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_exti.h"
#include "stm32f4xx_syscfg.h"
#include "stm32f4xx_usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* 电源管理器内部状态变量 */
static pm_state_t s_pm_state            = PM_STATE_ACTIVE;
static uint32_t   s_last_active_tick    = 0;
static uint32_t   s_standby_enter_tick  = 0;
static uint16_t   s_timeout_sec         = 15;   /* 默认 15 秒无操作自动熄屏进入待机 */
static uint16_t   s_stop_timeout_sec    = 45;   /* 待机状态下满 45 秒无操作切入 STOP 深度休眠 (共计 60s) */
static bool       s_auto_sleep_enabled  = true;

/* 内部前向声明 */
static void SystemClock_RestoreAfterStop(void);
static void Power_ConfigureWakeupEXTI(bool enable);

void Power_Manager_Init(void)
{
    s_pm_state            = PM_STATE_ACTIVE;
    s_last_active_tick    = xTaskGetTickCount();
    s_standby_enter_tick  = 0;
    s_auto_sleep_enabled  = true;
    LCD_Backlight_ON;

    /* 提前开启 PWR 模块时钟，为后续功耗管理做准备 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

    printf("[PowerManager] Initialized. Level 1: Active, Level 2: Standby (%d s), Level 3: STOP (%d s).\r\n",
           s_timeout_sec, s_stop_timeout_sec);
}

void Power_Manager_Wakeup(void)
{
    if (s_pm_state != PM_STATE_ACTIVE)
    {
        printf("[PowerManager] Waking up from %s Mode...\r\n",
               (s_pm_state == PM_STATE_STOP) ? "STOP (Deep Sleep)" : "Standby");

        /* 1. 唤醒片外 W25Q64 SPI Flash (发送 0xAB 命令解除掉电模式) */
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
        printf("[PowerManager] Entering Standby Mode (Level 2 Low-Power)...\r\n");

        /* 1. ST7789 屏幕深度休眠 (关闭背光 + 发送 0x10 SLPIN 切断驱动 IC 电荷泵，降耗 ~8mA) */
        LCD_EnterSleep();

        /* 2. MPU6050 陀螺仪断电休眠 (仅保留加速度计，立省 ~3.6mA) */
        MPU6050_EnterLowPowerMode();

        /* 3. W25Q64 进入深度掉电模式 (发送 0xB9，静态电流 <1uA) */
        W25QXX_PowerDown();

        /* 4. ESP32-C3 进入低功耗 Modem-Sleep (保持连网，射频周期性休眠，功耗降至 3~10mA) */
        ESP32_WiFi_EnterSleep();

        s_pm_state           = PM_STATE_STANDBY;
        s_standby_enter_tick = xTaskGetTickCount();
        printf("[PowerManager] Standby Active: LCD Sleep, Gyro Off, Flash Sleep. Power saved >85%%!\r\n");
    }
}

/**
  * @brief  进入 Cortex-M4 STOP 深度休眠模式 (微安级能耗)
  * @note   该函数会关断 168MHz 主 PLL，关闭 SysTick 与外设中断，通过按键 EXTI 下降沿中断唤醒后第一时间自愈重配主时钟
  */
void Power_Manager_EnterStopMode(void)
{
    printf("\r\n=======================================================\r\n");
    printf("   [PowerManager] >>> ENTERING STM32F4 STOP MODE <<<   \r\n");
    printf("   168MHz PLL Stopped. Current drops to microamps!     \r\n");
    printf("   Press KEY2 (PC0) or KEY3 (PC1) to Wake Up!          \r\n");
    printf("=======================================================\r\n");

    /* 1. 确保外设先处于休眠状态 */
    if (s_pm_state == PM_STATE_ACTIVE)
    {
        Power_Manager_Sleep();
    }
    s_pm_state = PM_STATE_STOP;

    /* 2. 等待按键完全释放 (防止长按进入时手指未松开引起抖动误唤醒) */
    while (GPIO_ReadInputDataBit(BOARD_KEY2_PORT, BOARD_KEY2_PIN) == 0 ||
           GPIO_ReadInputDataBit(BOARD_KEY3_PORT, BOARD_KEY3_PIN) == 0 ||
           GPIO_ReadInputDataBit(BOARD_KEY1_PORT, BOARD_KEY1_PIN) == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(50)); /* 彻底消除按键机械回弹 */

    /* 3. 等待串口完全发送完毕，确保日志完整打印不被截断 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);

    /* 4. 挂起 FreeRTOS 任务调度器 */
    vTaskSuspendAll();

    /* 5. 关断 SysTick 1ms 滴答中断！
          (核心关键：SysTick 若未关闭，每 1ms 就会触发一次中断将 MCU 瞬间踢出 STOP 模式！) */
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;

    /* 6. 临时屏蔽非唤醒外设中断 (如 ESP32 WiFi 串口等) */
    NVIC_DisableIRQ(USART2_IRQn);
    NVIC_ClearPendingIRQ(USART2_IRQn);

    /* 7. 配置按键 EXTI 外部中断唤醒通道并清除挂起 */
    Power_ConfigureWakeupEXTI(true);

    /* 8. 确保电源控制器时钟开启 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

    /* 9. 执行 Cortex-M4 STOP 深度休眠指令 (低功耗调压器模式 + WFI 等待中断) */
    PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);

    /* ========================================================================
       【核心唤醒执行点】CPU 被有效按键 EXTI 中断瞬间唤醒！
       注意：退出 STOP 模式后，STM32F4 系统时钟默认降级回内部 16MHz HSI！
       ======================================================================== */

    /* 10. 第一时间自愈重配主时钟树，闭环恢复 HSE + PLL = 168MHz */
    SystemClock_RestoreAfterStop();

    /* 11. 重新恢复 SysTick 滴答时基 */
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

    /* 12. 恢复外设中断与 FreeRTOS 调度器 */
    NVIC_ClearPendingIRQ(USART2_IRQn);
    NVIC_EnableIRQ(USART2_IRQn);
    xTaskResumeAll();

    /* 13. 关闭 EXTI 唤醒中断，恢复常规按键消抖轮询 */
    Power_ConfigureWakeupEXTI(false);

    /* 14. 打印唤醒状态与验证当前系统核心时钟 */
    printf("\r\n[PowerManager] >>> Woken up from STOP Mode! <<<\r\n");
    printf("[PowerManager] Core Clock restored to %u MHz.\r\n", (unsigned int)(SystemCoreClock / 1000000));

    /* 15. 唤醒所有外设并点亮屏幕 */
    Power_Manager_Wakeup();
}

/**
  * @brief  从 STOP 模式唤醒后，自愈重配主时钟树 (恢复 HSE + PLL = 168MHz)
  */
static void SystemClock_RestoreAfterStop(void)
{
    __IO uint32_t StartUpCounter = 0, HSEStatus = 0;

    /* 1. 使能外部高速晶振 HSE (板载 8MHz) */
    RCC->CR |= ((uint32_t)RCC_CR_HSEON);

    /* 2. 等待 HSE 稳定就绪 */
    do
    {
        HSEStatus = RCC->CR & RCC_CR_HSERDY;
        StartUpCounter++;
    } while ((HSEStatus == 0) && (StartUpCounter != HSE_STARTUP_TIMEOUT));

    if ((RCC->CR & RCC_CR_HSERDY) != RESET)
    {
        /* 3. 重新使能主 PLL */
        RCC->CR |= RCC_CR_PLLON;

        /* 4. 等待主 PLL 锁定就绪 (PLLRDY) */
        while ((RCC->CR & RCC_CR_PLLRDY) == 0)
        {
        }

        /* 5. 切换主系统时钟 SYSCLK 来源为 PLL 输出 */
        RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_SW));
        RCC->CFGR |= (uint32_t)RCC_CFGR_SW_PLL;

        /* 6. 等待 SYSCLK 切换生效 (0x08 表示 PLL 作为系统时钟) */
        while ((RCC->CFGR & (uint32_t)RCC_CFGR_SWS) != (uint32_t)0x08)
        {
        }
    }

    /* 7. 更新 SystemCoreClock 全局系统时钟变量 */
    SystemCoreClockUpdate();
}

/**
  * @brief  配置/关闭按键 EXTI 外部中断唤醒源 (使用 KEY2:PC0 和 KEY3:PC1)
  */
static void Power_ConfigureWakeupEXTI(bool enable)
{
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

    if (enable)
    {
        /* 连接 EXTI Line0 到 PC0 (KEY2) */
        SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource0);

        /* 连接 EXTI Line1 到 PC1 (KEY3) */
        SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource1);

        EXTI_InitStructure.EXTI_Line    = EXTI_Line0 | EXTI_Line1;
        EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
        EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling; /* 按键按下为低电平下降沿 */
        EXTI_InitStructure.EXTI_LineCmd = ENABLE;
        EXTI_Init(&EXTI_InitStructure);

        /* 关键：必须在 EXTI_Init 之后清除挂起标志，防止配置瞬间误触发 */
        EXTI_ClearITPendingBit(EXTI_Line0 | EXTI_Line1);

        /* NVIC 中断优先级配置 (优先级设为 5，符合 FreeRTOS 托管范围) */
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;

        NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
        NVIC_Init(&NVIC_InitStructure);
        NVIC_ClearPendingIRQ(EXTI0_IRQn);

        NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
        NVIC_Init(&NVIC_InitStructure);
        NVIC_ClearPendingIRQ(EXTI1_IRQn);
    }
    else
    {
        /* 唤醒后禁用 EXTI 中断，切回正常按键消抖轮询模式 */
        EXTI_InitStructure.EXTI_Line    = EXTI_Line0 | EXTI_Line1;
        EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
        EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
        EXTI_InitStructure.EXTI_LineCmd = DISABLE;
        EXTI_Init(&EXTI_InitStructure);

        NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;

        NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
        NVIC_Init(&NVIC_InitStructure);

        NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
        NVIC_Init(&NVIC_InitStructure);

        EXTI_ClearITPendingBit(EXTI_Line0 | EXTI_Line1);
        NVIC_ClearPendingIRQ(EXTI0_IRQn);
        NVIC_ClearPendingIRQ(EXTI1_IRQn);
    }
}

void Power_Manager_Update(void)
{
    if (!s_auto_sleep_enabled) return;

    uint32_t now = xTaskGetTickCount();

    if (s_pm_state == PM_STATE_ACTIVE)
    {
        if (s_timeout_sec > 0 && (now - s_last_active_tick) >= pdMS_TO_TICKS((uint32_t)s_timeout_sec * 1000))
        {
            Power_Manager_Sleep();
        }
    }
    else if (s_pm_state == PM_STATE_STANDBY)
    {
        /* 待机状态下，若持续无操作超过 s_stop_timeout_sec，切入深度休眠 STOP 模式 */
        if (s_stop_timeout_sec > 0 && (now - s_standby_enter_tick) >= pdMS_TO_TICKS((uint32_t)s_stop_timeout_sec * 1000))
        {
            Power_Manager_EnterStopMode();
        }
    }
}

bool Power_Manager_OnKey(uint8_t key_val)
{
    if (s_pm_state != PM_STATE_ACTIVE)
    {
        /* 息屏待机或休眠状态下唤醒屏幕，并拦截本次按键（防误触） */
        Power_Manager_Wakeup();
        return true; /* 已拦截唤醒 */
    }

    /* 亮屏状态下长按 KEY3 (key_val == 4) 直接手动触发切入 STOP 深度休眠 (方便快速测试) */
    if (key_val == 4)
    {
        printf("[PowerManager] Manual Sleep Triggered (KEY3 Long Press)!\r\n");
        Power_Manager_EnterStopMode();
        return true;
    }

    /* 亮屏状态下普通按键放行，并刷新活跃时间戳 */
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

void Power_Manager_SetStopTimeout(uint16_t seconds)
{
    s_stop_timeout_sec = seconds;
}

uint16_t Power_Manager_GetStopTimeout(void)
{
    return s_stop_timeout_sec;
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
