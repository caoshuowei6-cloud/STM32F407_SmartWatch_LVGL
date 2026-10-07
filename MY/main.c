/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F407 + LVGL 9 手表主程序（FreeRTOS V10.4.3 多任务分层架构）
  ******************************************************************************
  */

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

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

/* ============================================================================
   FreeRTOS 故障与断言钩子函数
   ============================================================================ */
void vAssertCalled(const char *file, int line)
{
    printf("\r\n[ASSERT FAILED] %s : %d\r\n", file, line);
    taskDISABLE_INTERRUPTS();
    while (1)
    {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("\r\n[FATAL ERROR] Stack Overflow detected in task: %s!\r\n", pcTaskName);
    taskDISABLE_INTERRUPTS();
    while (1)
    {
    }
}

void vApplicationMallocFailedHook(void)
{
    printf("\r\n[FATAL ERROR] FreeRTOS Malloc Failed (Heap Exhausted)!\r\n");
    taskDISABLE_INTERRUPTS();
    while (1)
    {
    }
}

/* ============================================================================
   FreeRTOS 任务句柄定义 (用于系统监控与任务栈高水位线深度诊断)
   ============================================================================ */
static TaskHandle_t s_task_ui_handle = NULL;
static TaskHandle_t s_task_sensor_handle = NULL;
static TaskHandle_t s_task_heartbeat_handle = NULL;

/* ============================================================================
   任务 1: Task_Heartbeat (系统心跳与高水位线健康诊断监控任务)
   - 优先级: 1 (低优先级，监控空闲调度与内存安全边界)
   - 栈大小: 256 words (1024 字节)
   ============================================================================ */
static void Task_Heartbeat(void *pvParameters)
{
    uint32_t beat_count = 0;
    (void)pvParameters;

    printf("[FreeRTOS] Heartbeat & Health Monitor Task Initialized (Priority 1).\r\n");

    while (1)
    {
        /* 翻转 PC13 绿灯 (低电平亮，500ms 周期指示操作系统平稳运行) */
        LED1_Toggle;
        vTaskDelay(pdMS_TO_TICKS(500));

        beat_count++;
        /* 每 5 秒输出一次全系统健康状态与高水位线 (Stack High Water Mark) 诊断报告 */
        if ((beat_count % 10) == 0)
        {
            UBaseType_t ui_stack_free = (s_task_ui_handle != NULL) ? uxTaskGetStackHighWaterMark(s_task_ui_handle) : 0;
            UBaseType_t sensor_stack_free = (s_task_sensor_handle != NULL) ? uxTaskGetStackHighWaterMark(s_task_sensor_handle) : 0;
            UBaseType_t beat_stack_free = uxTaskGetStackHighWaterMark(NULL);
            size_t free_heap = xPortGetFreeHeapSize();
            size_t min_ever_heap = xPortGetMinimumEverFreeHeapSize();

            printf("\r\n+---------------- [OS Health & Stack Watermark (%u s)] ----------------+\r\n", (unsigned int)(beat_count / 2));
            printf("| Task_UI Stack Free        : %4u words (%4u bytes)                 |\r\n", (unsigned int)ui_stack_free, (unsigned int)(ui_stack_free * 4));
            printf("| Task_Sensor Stack Free    : %4u words (%4u bytes)                 |\r\n", (unsigned int)sensor_stack_free, (unsigned int)(sensor_stack_free * 4));
            printf("| Task_Heartbeat Stack Free : %4u words (%4u bytes)                 |\r\n", (unsigned int)beat_stack_free, (unsigned int)(beat_stack_free * 4));
            printf("| FreeRTOS Heap Current     : %5u bytes                                |\r\n", (unsigned int)free_heap);
            printf("| FreeRTOS Heap Min Ever    : %5u bytes                                |\r\n", (unsigned int)min_ever_heap);
            printf("+----------------------------------------------------------------------+\r\n");
        }
    }
}

/* ============================================================================
   任务 2: Task_Sensor (传感器后台高频解算与环境采样任务)
   - 优先级: 1 (低优先级后台运算，彻底解耦 UI 与模拟 I2C 硬件通信)
   - 栈大小: 384 words (1536 字节)
   - 调度周期: 20ms (50Hz) 姿态解算/计步 + 1000ms (1Hz) 环境/电池采样
   ============================================================================ */
static void Task_Sensor(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t sample_count = 0;
    (void)pvParameters;

    printf("[FreeRTOS] Sensor Task Initialized (Priority 1, 50Hz AHRS + 1Hz Env/Power).\r\n");

    while (1)
    {
        /* 1. 20ms 周期 (50Hz) 运行 MPU6050 姿态滤波与动态计步算法 (工业级无抖动等距采样) */
        MPU6050_Update();

        sample_count++;
        /* 2. 1000ms 周期 (1Hz) 执行 AHT20 温湿度采样与电池 ADC 刷新 */
        if ((sample_count % 50) == 0)
        {
            AHT20_Read(NULL, NULL);
            ADC_Battery_GetPercent();
        }

        /* 3. 使用精确周期延时消除采样时间抖动 (Jitter-free) */
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}

/* ============================================================================
   任务 3: Task_UI (手表图形界面与交互调度任务)
   - 优先级: 2 (中优先级，保障界面丝滑响应)
   - 栈大小: 1024 words (4096 字节)
   ============================================================================ */
static void Task_UI(void *pvParameters)
{
    uint8_t key_val = 0;
    TickType_t last_100ms_tick = xTaskGetTickCount();
    TickType_t last_1s_tick = xTaskGetTickCount();
    (void)pvParameters;

    printf("[FreeRTOS] UI Task Initialized (Priority 2, Event-Driven & Decoupled).\r\n");

    while (1)
    {
        /* 1. 事件驱动：优先从 FreeRTOS 消息队列中提取按键事件 (零漏键、零阻塞) */
        if (g_xKeyQueue != NULL && xQueueReceive(g_xKeyQueue, &key_val, 0) == pdTRUE)
        {
            printf("[Queue Event] Key Triggered: %d\r\n", key_val);
            UI_Watch_HandleKey(key_val);
        }
        else
        {
            /* 兼容裸机回退通道 */
            key_val = Key_GetNum();
            if (key_val != 0)
            {
                printf("[Direct] Key Triggered: %d\r\n", key_val);
                UI_Watch_HandleKey(key_val);
            }
        }

        /* 2. 100ms 周期 UI 动画刷新 (秒表、水平仪动态气泡、萌宠表情包) */
        if ((xTaskGetTickCount() - last_100ms_tick) >= pdMS_TO_TICKS(100))
        {
            last_100ms_tick = xTaskGetTickCount();
            UI_Watch_Update_100ms();
        }

        /* 3. 1000ms 周期 UI 数据卡片与日历走时刷新 */
        if ((xTaskGetTickCount() - last_1s_tick) >= pdMS_TO_TICKS(1000))
        {
            last_1s_tick = xTaskGetTickCount();
            UI_Watch_Update_1s();
        }

        /* 4. LVGL 核心渲染引擎 */
        lv_timer_handler();

        /* 5. 非阻塞释放 CPU 5ms，调度器切换至传感器采集或心跳自检任务 */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ============================================================================
   主函数入口
   ============================================================================ */
int main(void)
{
    /* 1. NVIC 中断优先级分组：必须配置为 4 (所有 4 位均为抢占优先级，FreeRTOS 铁律) */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 2. 板级外设驱动初始化 */
    Delay_Init();       /* SysTick 1ms 初始化 (同时提供裸机启动期时基) */
    LED_Init();         /* PC13 绿灯 (低电平亮) */
    KEY_Init();         /* 按键初始化 (PB1, PA6, PA4, PA15) */
    KEY_Queue_Init();   /* FreeRTOS 按键消息队列初始化 (容量 10) */
    Usart_Config();     /* USART1 115200 8N1 */

    RTC_F407_Init();    /* STM32F407 硬件日历 RTC 初始化 (LSE/LSI 自动容错) */
    ADC_Battery_Init(); /* PA0 电池 12-bit ADC 初始化 */
    AHT20_Init();       /* PB6/PB7 AHT20 温湿度传感器初始化 */
    MPU6050_Init();     /* PF0/PF1 MPU6050 六轴姿态传感器初始化 */

    printf("\r\n=======================================================\r\n");
    printf("   STM32F407 SmartWatch [FreeRTOS V10.4.3 CM4F Online]  \r\n");
    printf("   Core Clock: %u MHz | RTOS Heap: %u KB                \r\n",
           (unsigned int)(SystemCoreClock / 1000000),
           (unsigned int)(configTOTAL_HEAP_SIZE / 1024));
    printf("=======================================================\r\n");

    /* 3. LVGL 核心与显示驱动初始化 */
    lv_init();
    lv_port_disp_init();

    /* 4. 初始化手表多页面状态机（进入主表盘） */
    UI_Watch_Init();
    printf("UI Manager Initialized (Default: Clock Page)\r\n");

    /* 5. 创建 FreeRTOS 任务 (三任务分层架构: UI 渲染、传感器解算、系统诊断) */
    xTaskCreate(Task_UI,        "Task_UI",        1024, NULL, 2, &s_task_ui_handle);
    xTaskCreate(Task_Sensor,    "Task_Sensor",    384,  NULL, 1, &s_task_sensor_handle);
    xTaskCreate(Task_Heartbeat, "Task_Heartbeat", 256,  NULL, 1, &s_task_heartbeat_handle);

    printf("Starting FreeRTOS Scheduler (3 Tasks: UI + Sensor + Heartbeat)...\r\n");

    /* 6. 启动任务调度器 (从此交由 FreeRTOS 多任务内核掌管系统) */
    vTaskStartScheduler();

    /* 正常情况下调度器绝不返回，若返回则说明堆内存耗尽 */
    while (1)
    {
        LED1_Toggle;
        Delay_ms(100);
    }
}
