/**
  ******************************************************************************
  * @file    led.h
  * @brief   STM32F407 板载 LED 驱动配置头文件
  ******************************************************************************
  */

#ifndef __LED_H
#define __LED_H

#include "stm32f4xx.h"
#include "bsp_board.h"

/* 引脚映射至 bsp_board.h */
#define LED1_PIN             BOARD_LED_PIN
#define LED1_PORT            BOARD_LED_PORT
#define LED1_CLK             BOARD_LED_CLK

/* 控制宏定义 */
#define LED1_ON              GPIO_ResetBits(LED1_PORT, LED1_PIN)
#define LED1_OFF             GPIO_SetBits(LED1_PORT, LED1_PIN)
#define LED1_Toggle          GPIO_ToggleBits(LED1_PORT, LED1_PIN)
#define LED_Toggle           GPIO_ToggleBits(LED1_PORT, LED1_PIN)

void LED_Init(void);

#endif /* __LED_H */
