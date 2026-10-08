/**
  ******************************************************************************
  * @file    boot_uart.h
  * @brief   Bootloader 调试串口与控制台输出头文件 (USART1 PA9/PA10 115200)
  ******************************************************************************
  */

#ifndef __BOOT_UART_H__
#define __BOOT_UART_H__

#include "stm32f4xx.h"
#include <stdio.h>

void Boot_Uart_Init(uint32_t baudrate);

#endif /* __BOOT_UART_H__ */
