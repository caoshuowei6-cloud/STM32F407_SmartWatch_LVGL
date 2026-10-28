#ifndef __FREERTOS_CONFIG_H__
#define __FREERTOS_CONFIG_H__

#include <stdint.h>
#include "stm32f4xx.h"

/* --------------------------------------------------------------------
 * 基本内核调度器配� (针� STM32F407ZGT6 168MHz Cortex-M4F)
 * -------------------------------------------------------------------- */
#define configUSE_PREEMPTION                    1   /* ��抢占式调度器 */
#define configUSE_TIME_SLICING                  1   /* ��同优先级时间片轮� */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1   /* ���件前导零指令(CLZ)优化任务选择 */
#define configUSE_TICKLESS_IDLE                 0   /* 暂时关闭低功耗Tickless (后期���) */
#define configCPU_CLOCK_HZ                      ( ( unsigned long ) 168000000 ) /* 168 MHz 主� */
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )          /* 1000 Hz = 1ms 滴答周期 */
#define configMAX_PRIORITIES                    ( 7 )                           /* 优先� 0 ~ 6 */
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 128 )            /* 空闲任务栈大� (128 words = 512 bytes) */
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_16_BIT_TICKS                  0   /* 32位系统使�32位Tick计数� */
#define configIDLE_SHOULD_YIELD                 1   /* 空闲任务让� */
#define configUSE_TASK_NOTIFICATIONS            1   /* �用轻量级任务通知 */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   3

/* --------------------------------------------------------------------
 * 互斥量�信号量与队�
 * -------------------------------------------------------------------- */
#define configUSE_MUTEXES                       1   /* 必须��互斥� (保障 LVGL 与传感器线程安全) */
#define configUSE_RECURSIVE_MUTEXES             1   /* �用�归互斥� */
#define configUSE_COUNTING_SEMAPHORES           1   /* �用�数信号� */
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0

/* --------------------------------------------------------------------
 * 内存管理 (SRAM 128KB，分� 32KB 堆空间给 FreeRTOS)
 * -------------------------------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 32 * 1024 ) )   /* 32 KB 堆空间，保障多任务充� */
#define configAPPLICATION_ALLOCATED_HEAP        0
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 0

/* --------------------------------------------------------------------
 * 钩子函数 (调试与��)
 * -------------------------------------------------------------------- */
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configRECORD_STACK_HIGH_ADDRESS         1   /* For CmBacktrace */   /* 栈溢出深度�测模� 2 */
#define configUSE_MALLOC_FAILED_HOOK            1   /* 内存申�失败进入钩� */

/* --------------------------------------------------------------------
 * �件定时器
 * -------------------------------------------------------------------- */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )

/* --------------------------------------------------------------------
 * Cortex-M4 ��嵌�与优先级配� (STM32F407 采用 NVIC_PriorityGroup_4)
 * -------------------------------------------------------------------- */
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS                     __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS                     4   /* 4位抢占优先级�16� (0 ~ 15) */
#endif

/* 内核�管理的最低中�优先� (0xF = 15) */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15

/* 系统调用�高中�优先� (优先级高� 5 的中�不能调用 FreeRTOS API) */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* --------------------------------------------------------------------
 * ��机制
 * -------------------------------------------------------------------- */
void vAssertCalled(const char *file, int line);
#define configASSERT( x ) if( ( x ) == 0 ) vAssertCalled( __FILE__, __LINE__ )

/* --------------------------------------------------------------------
 * �� API 包含
 * -------------------------------------------------------------------- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1

/* --------------------------------------------------------------------
 * ��服务函数重映� (映射� STM32F4 ��向量�)
 * -------------------------------------------------------------------- */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
/* 注意: SysTick_Handler � stm32f4xx_it.c �统一管理调用，��不定义� */

#endif /* __FREERTOS_CONFIG_H__ */
