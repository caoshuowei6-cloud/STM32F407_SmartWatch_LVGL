/**
  ******************************************************************************
  * @file    usart.h
  * @brief   STM32F407 调试串口 (USART1) 配置头文件
  ******************************************************************************
  */

#ifndef __USART_H
#define __USART_H

#include <stdio.h>
#include "stm32f4xx.h"
#include "bsp_board.h"

/* 调试串口波特率 */
#define USART1_BaudRate             921600

/* 引脚与外设映射至 bsp_board.h */
#define USART1_TX_PIN               BOARD_DEBUG_USART_TX_PIN
#define USART1_TX_PORT              BOARD_DEBUG_USART_PORT
#define USART1_TX_CLK               BOARD_DEBUG_USART_GPIO_CLK
#define USART1_TX_PinSource         BOARD_DEBUG_USART_TX_SRC

#define USART1_RX_PIN               BOARD_DEBUG_USART_RX_PIN
#define USART1_RX_PORT              BOARD_DEBUG_USART_PORT
#define USART1_RX_CLK               BOARD_DEBUG_USART_GPIO_CLK
#define USART1_RX_PinSource         BOARD_DEBUG_USART_RX_SRC

void Usart_Config(void);

#endif /* __USART_H */
