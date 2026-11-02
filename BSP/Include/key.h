/**
  ******************************************************************************
  * @file    key.h
  * @brief   STM32F407 板载与外接按键驱动配置头文件
  ******************************************************************************
  */

#ifndef __KEY_H
#define __KEY_H

#include "stm32f4xx.h"
#include "bsp_board.h"

/* 按键硬件与接口定义 (映射至 bsp_board.h) */
#define KEY1_PORT           BOARD_KEY1_PORT
#define KEY1_PIN            BOARD_KEY1_PIN
#define KEY1_CLK            BOARD_KEY1_CLK

#define KEY2_PORT           BOARD_KEY2_PORT
#define KEY2_PIN            BOARD_KEY2_PIN
#define KEY2_CLK            BOARD_KEY2_CLK

#define KEY3_PORT           BOARD_KEY3_PORT
#define KEY3_PIN            BOARD_KEY3_PIN
#define KEY3_CLK            BOARD_KEY3_CLK

#define KEY_BOARD_PORT      BOARD_KEY_BOARD_PORT
#define KEY_BOARD_PIN       BOARD_KEY_BOARD_PIN
#define KEY_BOARD_CLK       BOARD_KEY_BOARD_CLK

#define KEY3_LONG_PRESS_MS  1000

/* 函数声明 */
void    KEY_Init(void);
uint8_t Key_GetNum(void);
void    Key_Tick(void);
void    Key3_Tick(void);

#endif /* __KEY_H */
