/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F407 独立轻量级 Bootloader 主程序 (Sector 0~4, 128KB)
  *          工业级双镜像 OTA 启动内核，提供防变砖安全跳转与 Flash 热搬运
  ******************************************************************************
  */

#include "stm32f4xx.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "boot_iap.h"
#include "boot_uart.h"
#include <stdio.h>

int main(void)
{
    /* 1. 立即初始化板载 PC13 LED 并点亮，直观硬件指示 Bootloader 已正常上电 */
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_13;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_ResetBits(GPIOC, GPIO_Pin_13); /* 点亮 PC13 绿灯 */

    /* 2. NVIC 优先级分组配置为 4 (必须与 APP 保持一致) */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 3. 初始化调试串口 (USART1 115200 8N1 PA9/PA10) */
    Boot_Uart_Init(115200);

    /* 4. 打印开机横幅 */
    printf("\r\n=======================================================\r\n");
    printf("   STM32F407 SmartWatch Bootloader Online [0x08000000]  \r\n");
    printf("   Build Version: V1.0.0 (Sector 0~4, 128KB Partition)  \r\n");
    printf("=======================================================\r\n");

    /* 5. 执行开机自检与 OTA 状态机 (正常启动则 1ms 内跳转进入 APP) */
    Boot_CheckAndUpdate();

    /* 6. 守护死循环 */
    while (1)
    {
    }
}
