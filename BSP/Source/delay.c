/**
  ******************************************************************************
  * @file    delay.c
  * @brief   精确微秒与毫秒延时实现（基于 SysTick 硬件计数器）
  ******************************************************************************
  */

#include "delay.h"

static __IO uint32_t TimingDelay;

void Delay_Init(void)
{
    SysTick_Config(SystemCoreClock / 1000);  /* 1ms SysTick 中断 */
}

void TimingDelay_Decrement(void)
{
    if (TimingDelay != 0)
    { 
        TimingDelay--;
    }
}

void Delay_ms(uint32_t nTime)
{ 
    TimingDelay = nTime;
    while (TimingDelay != 0);
}

void Delay_us(uint32_t nus)
{
    uint32_t ticks;
    uint32_t told, tnow, tcnt = 0;
    uint32_t reload = SysTick->LOAD;

    if (nus == 0) return;
    ticks = nus * (SystemCoreClock / 1000000);
    told = SysTick->VAL;
    while (1)
    {
        tnow = SysTick->VAL;
        if (tnow != told)
        {
            if (tnow < told)
            {
                tcnt += told - tnow;
            }
            else
            {
                tcnt += reload - tnow + told;
            }
            told = tnow;
            if (tcnt >= ticks)
            {
                break;
            }
        }
    }
}
