/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   中断服务函数 (FreeRTOS + LVGL + 裸机外设时基桥接)
  ******************************************************************************
  */

#include "stm32f4xx_it.h"
#include "FreeRTOS.h"
#include "task.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  /* 进入死循环方便调试 */
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

/* 
 * 注意: SVC_Handler 与 PendSV_Handler 由 FreeRTOS port.c 接管
 * 在 FreeRTOSConfig.h 中已通过宏重定向:
 * #define vPortSVCHandler    SVC_Handler
 * #define xPortPendSVHandler PendSV_Handler
 * 此处注销，避免符号多重定义冲突。
 */
#if 0
void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}
#endif

void DebugMon_Handler(void)
{
}

/* 外部函数引用 */
extern void xPortSysTickHandler(void);
extern void TimingDelay_Decrement(void);
extern void lv_tick_inc(uint32_t tick_period);
extern void Key_Tick(void);
extern void Key3_Tick(void);

/**
  * @brief  SysTick 1ms 中断服务函数 (统一驱动 FreeRTOS 调度、LVGL 与按键扫描)
  */
void SysTick_Handler(void)
{
    /* 1. FreeRTOS 内核 1ms 滴答时基 */
    #if (INCLUDE_xTaskGetSchedulerState == 1)
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
    #else
    xPortSysTickHandler();
    #endif

    /* 2. 传统延时递减 */
    TimingDelay_Decrement();

    /* 3. LVGL 图形库 1ms 心跳 */
    lv_tick_inc(1);

    /* 4. 板级按键消抖状态机 (20ms 采样) 与长按计时 */
    Key_Tick();
    Key3_Tick();
}
