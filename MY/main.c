/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F407 + LVGL 9 手表主程序（阶段 2：多页面状态机与业务导航）
  ******************************************************************************
  */

#include "stm32f4xx.h"
#include "led.h"
#include "delay.h"
#include "key.h"
#include "usart.h"
#include "rtc_f407.h"
#include "adc_battery.h"
#include "aht20.h"
#include "mpu6050.h"
#include "lvgl.h"
#include "lv_port_lcd_stm32.h"
#include "ui_watch.h"
#include <stdio.h>

/**
  * @brief  主函数入口
  */
int main(void)
{
    uint8_t key_val = 0;
    static uint32_t last_100ms_tick = 0;
    static uint32_t last_1s_tick = 0;
    uint32_t current_tick;

    /* 1. 板级外设初始化 */
    Delay_Init();       /* SysTick 1ms 初始化 (同时提供 lv_tick_inc 与按键时基) */
    LED_Init();         /* PC13 绿灯 (低电平亮) */
    KEY_Init();         /* 按键初始化 (PB1, PA6, PA4, PA15，上拉) */
    Usart_Config();     /* USART1 115200 8N1 */

    RTC_F407_Init();    /* STM32F407 硬件日历 RTC 初始化 (LSE/LSI 自动容错) */
    ADC_Battery_Init(); /* PA0 电池/电位器 12-bit ADC 初始化 */
    AHT20_Init();       /* PB6/PB7 AHT20 温湿度传感器初始化 */
    MPU6050_Init();     /* PF0/PF1 MPU6050 六轴传感器初始化 */

    printf("\r\n=========================================\r\n");
    printf(" STM32F407 Watch LVGL 9 - Stage 4 Ready   \r\n");
    printf("=========================================\r\n");

    /* 2. LVGL 核心与显示驱动初始化 */
    lv_init();
    lv_port_disp_init();

    /* 3. 初始化手表页面架构（进入主表盘） */
    UI_Watch_Init();

    printf("UI Manager Initialized (Default: Clock Page)\r\n");

    /* 4. 主循环 */
    while (1)
    {
        /* 获取按键输入 (非阻塞) */
        key_val = Key_GetNum();
        if (key_val != 0)
        {
            printf("Key Triggered: %d\r\n", key_val);
            UI_Watch_HandleKey(key_val);
        }

        /* 周期性任务调度 */
        current_tick = lv_tick_get();

        /* 100ms 周期（秒表计时） */
        if (current_tick - last_100ms_tick >= 100)
        {
            last_100ms_tick = current_tick;
            UI_Watch_Update_100ms();
        }

        /* 1000ms 周期（时钟走时、传感器监测） */
        if (current_tick - last_1s_tick >= 1000)
        {
            last_1s_tick = current_tick;
            UI_Watch_Update_1s();
        }

        /* LVGL 核心任务处理器 */
        lv_timer_handler();

        /* 短暂延时，释放 CPU */
        Delay_ms(5);
    }
}
