/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F407 独立轻量级 Bootloader 主程序 (Sector 0~4, 128KB)
  *          工业级双镜像 OTA 启动内核，提供防变砖安全跳转与 Flash 热搬运
  ******************************************************************************
  */

#include "stm32f4xx.h"
#include "boot_iap.h"
#include "boot_uart.h"
#include <stdio.h>

int main(void)
{
    /* 1. NVIC 优先级分组配置为 4 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 2. 初始化调试串口 (115200 8N1) */
    Boot_Uart_Init(115200);

    /* 3. 打印开机横幅 */
    printf("\r\n");
    printf("=======================================================\r\n");
    printf("   STM32F407 Dual-MCU SmartWatch Minimal Bootloader   \r\n");
    printf("   Build Version: V1.0.0  (IROM1: 0x08000000, 128KB)  \r\n");
    printf("=======================================================\r\n");

    /* 4. 执行开机自检与 OTA 状态机 */
    Boot_CheckAndUpdate();

    /* 5. 守护死循环 */
    while (1)
    {
    }
}
