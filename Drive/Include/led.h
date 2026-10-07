#ifndef __LED_H
#define __LED_H

#include "stm32f4xx.h"

/*---------------------- LED 引脚宏 ------------------------*/

#define LED1_PIN             GPIO_Pin_13       		 // LED1 引脚 (板载绿灯)
#define LED1_PORT            GPIOC                   // LED1 GPIO 端口     
#define LED1_CLK             RCC_AHB1Periph_GPIOC	 // LED1 GPIO 时钟

/*---------------------- LED 控制宏 ------------------------*/
					
#define LED1_ON 	    GPIO_ResetBits(LED1_PORT, LED1_PIN)			// 低电平点亮 LED1	
#define LED1_OFF 	    GPIO_SetBits(LED1_PORT, LED1_PIN)			// 高电平熄灭 LED1	
#define LED1_Toggle	    GPIO_ToggleBits(LED1_PORT, LED1_PIN)		// LED1 状态翻转

#define LED_Toggle()    GPIO_ToggleBits(LED1_PORT, LED1_PIN)        // 兼容带括号调用
#define LED_Toggle      GPIO_ToggleBits(LED1_PORT, LED1_PIN)        // 兼容无括号调用

/*---------------------- 函数声明 ----------------------------*/

void LED_Init(void);	// LED 初始化

#endif // __LED_H
