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
   全局消息队列句柄
   ============================================================================ */
static QueueHandle_t s_key_queue = NULL;

/* ============================================================================
   任务 1: Task_Key (独立按键高优先级扫描任务)
   - 优先级: 3 (高优先级，确保物理按键瞬时捕获、0 丢失、0 抖动)
   - 栈大小: 256 words (1024 字节)
   - 周期:   10ms 周期查询消抖
   ============================================================================ */
static void Task_Key(void *pvParameters)
{
    uint8_t key_val = 0;
    (void)pvParameters;

    printf("[FreeRTOS] Key Task Initialized (Priority 3).\r\n");

    while (1)
    {
        key_val = Key_GetNum();
        if (key_val != 0)
        {
            if (s_key_queue != NULL)
            {
                /* 采用非阻塞入队，若队列已满则丢弃，避免高优先级任务被永久挂起 */
                xQueueSend(s_key_queue, &key_val, pdMS_TO_TICKS(10));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ============================================================================
   任务 2: Task_UI (手表图形界面与交互调度任务)
   - 优先级: 2 (中优先级，保障界面丝滑响应)
   - 栈大小: 1024 words (4096 字节)
   ============================================================================ */
static void Task_UI(void *pvParameters)
{
    uint8_t key_val = 0;
    TickType_t last_100ms_tick = xTaskGetTickCount();
    TickType_t last_1s_tick = xTaskGetTickCount();
    (void)pvParameters;

    printf("[FreeRTOS] UI Task Initialized (Priority 2).\r\n");

    while (1)
    {
        /* 1. 从按键队列非阻塞拉取按键事件并分发 */
        if (s_key_queue != NULL)
        {
            while (xQueueReceive(s_key_queue, &key_val, 0) == pdPASS)
            {
                if (key_val != 0)
                {
                    printf("[KeyQueue] Recv: %d\r\n", key_val);
                    UI_Watch_HandleKey(key_val);
                }
            }
        }

        /* 2. 100ms 周期业务动效刷新 (秒表、小恐龙跳跃、水平仪气泡、电子萌宠) */
        if ((xTaskGetTickCount() - last_100ms_tick) >= pdMS_TO_TICKS(100))
        {
            last_100ms_tick = xTaskGetTickCount();
            UI_Watch_Update_100ms();
        }

        /* 3. 1000ms 周期业务刷新 (硬件日历时钟走时、表盘页面数据更新) */
        if ((xTaskGetTickCount() - last_1s_tick) >= pdMS_TO_TICKS(1000))
        {
            last_1s_tick = xTaskGetTickCount();
            UI_Watch_Update_1s();
        }

        /* 4. LVGL 核心渲染引擎 */
        lv_timer_handler();

        /* 5. 非阻塞释放 CPU 5ms，让位给后台任务与空闲睡眠 */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ============================================================================
   任务 3: Task_Sensor (后台传感器持续解算与状态滤波任务)
   - 优先级: 1 (低优先级，在后台平稳运算，绝不抢占/阻塞 UI)
   - 栈大小: 512 words (2048 字节，充裕防御硬件 FPU 浮点寄存器压栈)
   - 周期:   20ms 高频姿态解算 (50Hz) + 1000ms 温湿度电池采样
   ============================================================================ */
static void Task_Sensor(void *pvParameters)
{
    TickType_t last_1s_tick = xTaskGetTickCount();
    (void)pvParameters;

    printf("[FreeRTOS] Sensor Task Initialized (Priority 1).\r\n");

    while (1)
    {
        /* 1. MPU6050 50Hz 高频互补滤波与动态计步解算 (20ms 周期) */
        MPU6050_Update();

        /* 2. 1000ms 周期采样 AHT20 温湿度传感器与 PA0 电池电压 */
        if ((xTaskGetTickCount() - last_1s_tick) >= pdMS_TO_TICKS(1000))
        {
            last_1s_tick = xTaskGetTickCount();
            AHT20_Read(NULL, NULL);
            ADC_Battery_GetVoltage();
        }

        /* 释放 CPU 20ms */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ============================================================================
   任务 4: Task_WiFi (ESP32-C3 网络通信与云端数据同步任务)
   - 优先级: 1 (低优先级)
   - 栈大小: 512 words (2048 字节)
   ============================================================================ */
static void Task_WiFi(void *pvParameters)
{
    TickType_t last_sync_tick = xTaskGetTickCount();
    (void)pvParameters;

    printf("[FreeRTOS] WiFi Task Initialized (Priority 1).\r\n");

    /* 上电缓冲 2 秒，等待 ESP32-C3 模组完成射频自检 */
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1)
    {
        /* 定期探活与业务同步骨架 (后续挂载 DMA+IDLE 接收环形缓冲) */
        if ((xTaskGetTickCount() - last_sync_tick) >= pdMS_TO_TICKS(5000))
        {
            last_sync_tick = xTaskGetTickCount();
            /* 预留: 定时组装传感器 JSON 包推向云端或拉取天气/对时 */
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ============================================================================
   任务 5: Task_Heartbeat (系统心跳与健康自检任务)
   - 优先级: 1 (低优先级)
   - 栈大小: 256 words (1024 字节)
   ============================================================================ */
static void Task_Heartbeat(void *pvParameters)
{
    uint32_t beat_count = 0;
    (void)pvParameters;

    printf("[FreeRTOS] Heartbeat Task Initialized (Priority 1).\r\n");

    while (1)
    {
        /* 翻转 PC13 绿灯 (低电平亮，500ms 周期指示操作系统平稳运行) */
        LED1_Toggle;
        vTaskDelay(pdMS_TO_TICKS(500));

        beat_count++;
        /* 每 5 秒输出一次心跳与剩余堆内存大小 */
        if ((beat_count % 10) == 0)
        {
            printf("[OS Heartbeat] UpTime: %u s | Free Heap: %u bytes\r\n",
                   (unsigned int)(beat_count / 2),
                   (unsigned int)xPortGetFreeHeapSize());
        }
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

    /* 3. 创建按键事件消息队列 (容量 10 个按键事件) */
    s_key_queue = xQueueCreate(10, sizeof(uint8_t));
    if (s_key_queue == NULL)
    {
        printf("[FATAL] Failed to create Key Queue!\r\n");
    }

    /* 4. LVGL 核心与显示驱动初始化 */
    lv_init();
    lv_port_disp_init();

    /* 5. 初始化手表多页面状态机（进入主表盘） */
    UI_Watch_Init();
    printf("UI Manager Initialized (Default: Clock Page)\r\n");

    /* 6. 创建 FreeRTOS 分层任务 (对标原 F1 架构，空间充足) */
    xTaskCreate(Task_Key,       "Task_Key",       256,  NULL, 3, NULL); /* 优先级 3 */
    xTaskCreate(Task_UI,        "Task_UI",        1024, NULL, 2, NULL); /* 优先级 2 */
    xTaskCreate(Task_Sensor,    "Task_Sensor",    512,  NULL, 1, NULL); /* 优先级 1 */
    xTaskCreate(Task_WiFi,      "Task_WiFi",      512,  NULL, 1, NULL); /* 优先级 1 */
    xTaskCreate(Task_Heartbeat, "Task_Heartbeat", 256,  NULL, 1, NULL); /* 优先级 1 */

    printf("Starting FreeRTOS Scheduler (5 Tasks Active)...\r\n");

    /* 7. 启动任务调度器 (从此交由 FreeRTOS 多任务内核掌管系统) */
    vTaskStartScheduler();

    /* 正常情况下调度器绝不返回，若返回则说明堆内存耗尽 */
    while (1)
    {
        LED1_Toggle;
        Delay_ms(100);
    }
}
