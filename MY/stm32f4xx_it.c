/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   中断服务函数
  ******************************************************************************
  */

#include "stm32f4xx_it.h"

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

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/* 外部函数引用 */
extern void TimingDelay_Decrement(void);
extern void lv_tick_inc(uint32_t tick_period);
extern void Key_Tick(void);
extern void Key3_Tick(void);

/**
  * @brief  SysTick 1ms 中断服务函数
  */
void SysTick_Handler(void)
{
    TimingDelay_Decrement();
    lv_tick_inc(1);             /* LVGL 1ms 软件时基 */
    Key_Tick();                 /* 按键状态机扫描与消抖 (20ms采样) */
    Key3_Tick();                /* KEY3 长按 1ms 累加计时 */
}
