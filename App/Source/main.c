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
#include "esp32_wifi.h"
#include "power_manager.h"
#include "param_storage.h"
#include "ota_manager.h"
#include "version.h"
#include "cm_backtrace.h"
#include "w25qxx.h"
#include "history_storage.h"
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

void vApplicationIdleHook(void)
{
    /* Cortex-M4 内核休眠：在各任务阻塞等待时进入硬件低功�?Sleep 模式，任意中�?SysTick/按键/DMA)瞬时唤醒 */
    __WFI();
}

/* ============================================================================
   全局消息队列句柄
   ============================================================================ */
static QueueHandle_t s_key_queue = NULL;

/* ============================================================================
   任务 1: Task_Key (独立按键高优先级扫描任务)
   - 优先�? 3 (最高优先级，确保物理按键瞬时捕获，0 丢失�? 抖动)
   - 栈大�? 256 words (1024 字节)
   - 周期:   10ms 周期查询消抖
   ============================================================================ */
static void Task_Key(void *pvParameters)
{
    uint8_t key_val = 0;
    (void)pvParameters;

    printf("[FreeRTOS] Key Task Initialized with EXTI Wakeup (Priority 3).\r\n");

    while (1)
    {
        if (Power_Manager_GetState() == PM_STATE_STANDBY)
        {
            /* 待机状态：阻塞在 EXTI 硬件中断信号量上，超时 150ms 兼顾周期降频轮询与极致省电 */
            if (KEY_WaitForWakeup(pdMS_TO_TICKS(150)) == pdPASS)
            {
                /* EXTI 外部硬件中断瞬间捕获按键按下！立即唤醒屏幕与外设 */
                Power_Manager_Wakeup();
            }
            else
            {
                /* 150ms 超时备份检测 (检测共用引脚 KEY3) */
                key_val = Key_GetNum();
                if (key_val != 0)
                {
                    Power_Manager_Wakeup();
                    if (s_key_queue != NULL)
                    {
                        xQueueSend(s_key_queue, &key_val, pdMS_TO_TICKS(10));
                    }
                }
            }
        }
        else
        {
            /* 交互活跃状态：10ms 高频消抖轮询，保证 UI 界面与长按操作极致流畅 */
            key_val = Key_GetNum();
            if (key_val != 0)
            {
                if (s_key_queue != NULL)
                {
                    /* 采用非阻塞入队，若队列已满则丢弃，避免高优先级任务永久挂起 */
                    xQueueSend(s_key_queue, &key_val, pdMS_TO_TICKS(10));
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

/* ============================================================================
   任务 2: Task_UI (手表图形界面与交互调度任�?
   - 优先�? 2 (中高优先级，保障界面丝滑响应)
   - 栈大�? 1024 words (4096 字节)
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
        /* 1. 从按键队列非阻塞拉取按键事件并分�?*/
        if (s_key_queue != NULL)
        {
            while (xQueueReceive(s_key_queue, &key_val, 0) == pdPASS)
            {
                if (key_val != 0)
                {
                    /* 企业�?OTA 互斥保护：空中升级固件接收期间全面锁定按键，防止抢占 CPU 与总线冲突 */
                                        /* 1. 低功耗电源管理：优先允许按键点亮屏幕，保证升级期间随时能看表盘与状态 */

                    if (Power_Manager_OnKey(key_val))

                    {

                        continue;

                    }



                    /* 2. 外部 Flash 暂存期间：放行按键操作与 UI 响应，并提示后台下载中 */

                    if (OTA_GetState() == OTA_STATE_RECEIVING)

                    {

                        printf("[OTA Active] Key %d processed. Firmware streaming in background to W25Q64.\r\n", key_val);

                    }
                    printf("[KeyQueue] Recv: %d\r\n", key_val);
                    UI_Watch_HandleKey(key_val);
                }
            }
        }

        /* 2. 待机低功耗动态调度：息屏状态下挂起 100ms 动效与高频渲染，让出 CPU 给内核休�?*/
        if (Power_Manager_GetState() == PM_STATE_STANDBY)
        {
            /* 待机态：�?50ms 慢轮询维持日历时钟和电源更新，CPU 占用率立降至 <0.5% */
            if ((xTaskGetTickCount() - last_1s_tick) >= pdMS_TO_TICKS(1000))
            {
                last_1s_tick = xTaskGetTickCount();
                UI_Watch_Update_1s();
                Power_Manager_Update();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        else
        {
            /* 运行态：100ms 动效刷新�?LVGL 1ms 满血高频渲染流水�?(保障 30~60 FPS 极速跟�? */
            if ((xTaskGetTickCount() - last_100ms_tick) >= pdMS_TO_TICKS(100))
            {
                last_100ms_tick = xTaskGetTickCount();
                UI_Watch_Update_100ms();
            }

            if ((xTaskGetTickCount() - last_1s_tick) >= pdMS_TO_TICKS(1000))
            {
                last_1s_tick = xTaskGetTickCount();
                UI_Watch_Update_1s();
                Power_Manager_Update();
            }

            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* ============================================================================
   任务 3: Task_Sensor (后台传感器持续解算与状态滤波任�?
   - 优先�? 1 (低优先级，在后台平稳运算，绝不抢�?阻塞 UI)
   - 栈大�? 512 words (2048 字节，充裕防御硬�?FPU 浮点寄存器压�?
   - 周期:   20ms 高频姿态解�?(50Hz) + 1000ms 温湿度电池采�?
   ============================================================================ */
static void Task_Sensor(void *pvParameters)
{
    TickType_t last_env_tick = xTaskGetTickCount();
    int16_t s_last_ax = 0, s_last_ay = 0, s_last_az = 0;
    uint16_t s_still_counter = 0;
    float pitch, roll, yaw;
    int16_t gx, gy, gz;
    int16_t ax, ay, az;
    int32_t accel_diff;
    uint32_t env_interval_ms;
    (void)pvParameters;

    printf("[FreeRTOS] Sensor Task Initialized (Priority 1).\r\n");

    while (1)
    {
        /* 1. MPU6050 计步与姿态解算 */
        MPU6050_Update();

        /* 抬腕亮屏手势识别：仅在息屏待机状态下进行特征匹配，亮屏后自动停止计算 */
        if (Power_Manager_GetState() == PM_STATE_STANDBY)
        {
            MPU6050_GetAttitude(&pitch, &roll, &yaw);
            MPU6050_GetRawData(&ax, &ay, &az, &gx, &gy, &gz);
            Power_Manager_CheckWristAction(pitch, roll, gx, gy, gz);

            /* 静置自适应降频检测：若连续多次三轴加速度差值低于阈值，判定手表平放桌上静止 */
            accel_diff = abs((int)ax - (int)s_last_ax) + abs((int)ay - (int)s_last_ay) + abs((int)az - (int)s_last_az);
            s_last_ax = ax;
            s_last_ay = ay;
            s_last_az = az;

            if (accel_diff < 500)
            {
                if (s_still_counter < 300) s_still_counter++;
            }
            else
            {
                s_still_counter = 0;
            }
        }
        else
        {
            s_still_counter = 0;
        }

        /* 2. 环境温湿度 (AHT20) 与电池电压采样：
           活跃态 1s 采样一次；息屏待机态降频至 30s 采样一次，极大减少无谓的 I2C 总线事务与 ADC 转换耗电 */
        env_interval_ms = (Power_Manager_GetState() == PM_STATE_STANDBY) ? 30000 : 1000;
        if ((xTaskGetTickCount() - last_env_tick) >= pdMS_TO_TICKS(env_interval_ms))
        {
            last_env_tick = xTaskGetTickCount();
            AHT20_Read(NULL, NULL);
            ADC_Battery_GetVoltage();
            /* OTA 升级期间禁止 Flash 参数同步，彻底杜绝 Flash 写入冲突 */
            if (OTA_GetState() != OTA_STATE_RECEIVING)
            {
                Param_Storage_Sync();
                History_Storage_PeriodicCheck();
            }
        }

        /* 释放 CPU：
           - 活跃态：20ms (50Hz) 采样与动态计步解算；
           - 待机动态态 (戴在手上)：100ms (10Hz) 采样捕获抬腕动作；
           - 待机静置态 (放桌上静止 > 3秒)：进一步降频至 300ms (3.3Hz)，功耗降至冰点 */
        if (Power_Manager_GetState() == PM_STATE_STANDBY)
        {
            if (s_still_counter >= 30)
            {
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

/* ============================================================================
   任务 4: Task_WiFi (ESP32-C3 网络通信与云端数据同步任�?
   - 优先�? 1 (低优先级)
   - 栈大�? 512 words (2048 字节)
   - 架构:   USART2 DMA + 空闲中断 + 环形缓冲�?(RingBuffer) 异步事件驱动
   ============================================================================ */
static void Task_WiFi(void *pvParameters)
{
    (void)pvParameters;

    printf("[FreeRTOS] WiFi Task Initialized (Priority 1).\r\n");

    /* 上电缓冲 2 秒，等待 ESP32-C3 模组完成射频硬件自检 */
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1)
    {
        /* 执行 WiFi 状态机轮询 (自适应探活与数据同�? */
        ESP32_WiFi_TaskLoop();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ============================================================================
   任务 5: Task_Heartbeat (系统心跳与健康自检任务)
   - 优先�? 1 (低优先级)
   - 栈大�? 256 words (1024 字节)
   ============================================================================ */
static void Task_Heartbeat(void *pvParameters)
{
    uint32_t beat_count = 0;
    (void)pvParameters;

    printf("[FreeRTOS] Heartbeat Task Initialized (Priority 1).\r\n");

    while (1)
    {
        /* 翻转 PC13 绿灯 (低电平亮�?00ms 周期指示操作系统平稳运行) */
        LED1_Toggle;
        vTaskDelay(pdMS_TO_TICKS(500));

        beat_count++;
        /* �?5 秒输出一次心跳与剩余堆内存大�?*/
        if ((beat_count % 10) == 0)
        {
            uint32_t uptime_s = beat_count / 2;
            printf("[OS Heartbeat] UpTime: %u s | Free Heap: %u bytes\r\n",
                   (unsigned int)uptime_s,
                   (unsigned int)xPortGetFreeHeapSize());
            if (uptime_s == 30)
            {
                OTA_Confirm_Success();
            }
        }
    }
}

/* ============================================================================
   主函数入�?
   ============================================================================ */
int main(void)
{
    /* 0. 向量表重映射双重保险：确保中断向量表严格对齐�?APP 所在的 Sector 5 (0x08020000) */
    SCB->VTOR = FLASH_BASE | 0x20000;
    __enable_irq();

    /* 1. NVIC 中断优先级分组：必须配置�?4 (全部 4 位均为抢占优先级，FreeRTOS 铁律) */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 2. 板级外设驱动初始�?*/
    Delay_Init();       /* SysTick 1ms 初始�?(同时提供裸机启动期时�? */
    LED_Init();         /* PC13 绿灯 (低电平亮) */
    KEY_Init();         /* 按键初始�?(PB1, PA6, PA4, PA15) */
    Usart_Config();
    cm_backtrace_init("STM32F407_SmartWatch", "V1.0", FIRMWARE_VERSION_STR);     /* USART1 115200 8N1 */

    RTC_F407_Init();    /* STM32F407 硬件日历 RTC 初始�?(LSE/LSI 自动容错) */
    ADC_Battery_Init(); /* PA0 电池 12-bit ADC 初始�?*/
    AHT20_Init();       /* PB6/PB7 AHT20 温湿度传感器初始�?*/
    MPU6050_Init();     /* PF0/PF1 MPU6050 六轴姿态传感器初始�?*/
    ESP32_WiFi_Init();  /* PA2/PA3 USART2 DMA + IDLE + RingBuffer 初始�?*/
    Power_Manager_Init();/* 低功耗电源管理器初始�?(PD13 背光�?5s 超时) */
    Param_Storage_Init();/* 两级非易失性参数存储初始化 (恢复步数/配置，跨天自动归�? */
    OTA_Manager_Init();  /* 无线 OTA 暂存区管理器初始�?(双镜像防变砖就绪) */
    W25QXX_Init();       /* 硬件 SPI1 W25Q64 初始化 (PA4~PA7) */
    W25QXX_SelfTest();
    History_Storage_Init();   /* 读取 ID 与测试扇区自检 */

    printf("\r\n=======================================================\r\n");
    printf("   STM32F407 SmartWatch [Firmware " FIRMWARE_VERSION_STR " Online!]     \r\n");
    printf("   Build Timestamp: %s %s                       \r\n", FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);
    printf("   Core Clock: %u MHz | RTOS Heap: %u KB                \r\n",
           (unsigned int)(SystemCoreClock / 1000000),
           (unsigned int)(configTOTAL_HEAP_SIZE / 1024));
    printf("=======================================================\r\n");

    /* 3. 创建按键事件消息队列 (容量 10 个按键事�? */
    s_key_queue = xQueueCreate(10, sizeof(uint8_t));
    if (s_key_queue == NULL)
    {
        printf("[FATAL] Failed to create Key Queue!\r\n");
    }

    /* 4. LVGL 核心与显示驱动初始化 */
    lv_init();
    lv_port_disp_init();

    /* 5. 初始化手表多页面状态机（进入主表盘�?*/
    UI_Watch_Init();
    printf("UI Manager Initialized (Default: Clock Page)\r\n");

    /* 6. 创建 FreeRTOS 分层任务 (对标�?F1 架构，空间充�? */
    xTaskCreate(Task_Key,       "Task_Key",       256,  NULL, 3, NULL); /* 优先�?3 */
    xTaskCreate(Task_UI,        "Task_UI",        1024, NULL, 2, NULL); /* 优先�?2 */
    xTaskCreate(Task_Sensor,    "Task_Sensor",    512,  NULL, 1, NULL); /* 优先�?1 */
    xTaskCreate(Task_WiFi,      "Task_WiFi",      512,  NULL, 1, NULL); /* 优先�?1 */
    xTaskCreate(Task_Heartbeat, "Task_Heartbeat", 256,  NULL, 1, NULL); /* 优先�?1 */

    printf("Starting FreeRTOS Scheduler (5 Tasks Active)...\r\n");

    /* 7. 启动任务调度�?(从此交由 FreeRTOS 多任务内核掌管系�? */
    vTaskStartScheduler();

    /* 正常情况下调度器绝不返回，若返回则说明堆内存耗尽 */
    while (1)
    {
        LED1_Toggle;
        Delay_ms(100);
    }
}
