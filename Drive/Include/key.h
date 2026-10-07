#ifndef __KEY_H
#define __KEY_H

#include "stm32f4xx.h"

/* 
 * 按键硬件与接口定义（与原 F1 手表工程逻辑完全一致）
 * KEY1: PB1 (排针外接)
 * KEY2: PA6 (排针外接)
 * KEY3: PA4 (排针外接，支持短按和长按)
 * KEY_BOARD: PA15 (核心板板载用户按键 SW1，兼作备用)
 */

#define KEY1_PORT           GPIOB
#define KEY1_PIN            GPIO_Pin_1
#define KEY1_CLK            RCC_AHB1Periph_GPIOB

#define KEY2_PORT           GPIOA
#define KEY2_PIN            GPIO_Pin_6
#define KEY2_CLK            RCC_AHB1Periph_GPIOA

#define KEY3_PORT           GPIOA
#define KEY3_PIN            GPIO_Pin_4
#define KEY3_CLK            RCC_AHB1Periph_GPIOA

#define KEY_BOARD_PORT      GPIOA
#define KEY_BOARD_PIN       GPIO_Pin_15
#define KEY_BOARD_CLK       RCC_AHB1Periph_GPIOA

#define KEY3_LONG_PRESS_MS  1000

/* 函数声明 */
void    KEY_Init(void);
uint8_t Key_GetNum(void);
void    Key_Tick(void);
void    Key3_Tick(void);

#endif //__KEY_H
