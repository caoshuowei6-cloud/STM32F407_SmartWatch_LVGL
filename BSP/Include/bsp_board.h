/**
  ******************************************************************************
  * @file    bsp_board.h
  * @brief   STM32F407ZGT6 智能手表硬件板级引脚与总线映射总表 (对标工业级规范)
  * @note    所有外设与驱动均在此统一映射，驱动层严禁硬编码单片机物理引脚
  ******************************************************************************
  */

#ifndef __BSP_BOARD_H__
#define __BSP_BOARD_H__

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
   1. 调试串口 (USART1: PA9 TX / PA10 RX)
   ============================================================================ */
#define BOARD_DEBUG_USART               USART1
#define BOARD_DEBUG_USART_CLK           RCC_APB2Periph_USART1
#define BOARD_DEBUG_USART_GPIO_CLK      RCC_AHB1Periph_GPIOA
#define BOARD_DEBUG_USART_PORT          GPIOA
#define BOARD_DEBUG_USART_TX_PIN        GPIO_Pin_9
#define BOARD_DEBUG_USART_RX_PIN        GPIO_Pin_10
#define BOARD_DEBUG_USART_AF            GPIO_AF_USART1
#define BOARD_DEBUG_USART_TX_SRC        GPIO_PinSource9
#define BOARD_DEBUG_USART_RX_SRC        GPIO_PinSource10

/* ============================================================================
   2. ESP32-C3 通信串口 (USART2: PA2 TX / PA3 RX)
   ============================================================================ */
#define BOARD_WIFI_USART                USART2
#define BOARD_WIFI_USART_CLK            RCC_APB1Periph_USART2
#define BOARD_WIFI_USART_GPIO_CLK       RCC_AHB1Periph_GPIOA
#define BOARD_WIFI_USART_DMA_CLK        RCC_AHB1Periph_DMA1
#define BOARD_WIFI_USART_PORT           GPIOA
#define BOARD_WIFI_USART_TX_PIN         GPIO_Pin_2
#define BOARD_WIFI_USART_RX_PIN         GPIO_Pin_3
#define BOARD_WIFI_USART_AF             GPIO_AF_USART2
#define BOARD_WIFI_USART_TX_SRC         GPIO_PinSource2
#define BOARD_WIFI_USART_RX_SRC         GPIO_PinSource3
#define BOARD_WIFI_DMA_STREAM           DMA1_Stream5
#define BOARD_WIFI_DMA_CHANNEL          DMA_Channel_4

/* ============================================================================
   3. MPU6050 六轴传感器 (软件 I2C: PF1 SCL / PF0 SDA / EXTI 中断预留)
   ============================================================================ */
#define BOARD_MPU_I2C_CLK               RCC_AHB1Periph_GPIOF
#define BOARD_MPU_I2C_PORT              GPIOF
#define BOARD_MPU_I2C_SCL_PIN           GPIO_Pin_1
#define BOARD_MPU_I2C_SDA_PIN           GPIO_Pin_0

/* ============================================================================
   4. AHT20 温湿度传感器 (软件 I2C: PB6 SCL / PB7 SDA)
   ============================================================================ */
#define BOARD_AHT_I2C_CLK               RCC_AHB1Periph_GPIOB
#define BOARD_AHT_I2C_PORT              GPIOB
#define BOARD_AHT_I2C_SCL_PIN           GPIO_Pin_6
#define BOARD_AHT_I2C_SDA_PIN           GPIO_Pin_7

/* ============================================================================
   5. W25Q64 外部 SPI Flash (SPI1: PA4 CS / PA5 SCK / PA6 MISO / PA7 MOSI)
   ============================================================================ */
#define BOARD_FLASH_SPI                 SPI1
#define BOARD_FLASH_SPI_CLK             RCC_APB2Periph_SPI1
#define BOARD_FLASH_GPIO_CLK            RCC_AHB1Periph_GPIOA
#define BOARD_FLASH_PORT                GPIOA
#define BOARD_FLASH_CS_PIN              GPIO_Pin_4
#define BOARD_FLASH_SCK_PIN             GPIO_Pin_5
#define BOARD_FLASH_MISO_PIN            GPIO_Pin_6
#define BOARD_FLASH_MOSI_PIN            GPIO_Pin_7
#define BOARD_FLASH_SCK_SRC             GPIO_PinSource5
#define BOARD_FLASH_MISO_SRC            GPIO_PinSource6
#define BOARD_FLASH_MOSI_SRC            GPIO_PinSource7
#define BOARD_FLASH_SPI_AF              GPIO_AF_SPI1

/* ============================================================================
   6. 1.54寸 LCD 屏幕 (SPI3: PB3 SCK / PB5 MOSI / PD11 CS / PD12 DC / PD13 背光)
   ============================================================================ */
#define BOARD_LCD_SPI                   SPI3
#define BOARD_LCD_SPI_CLK               RCC_APB1Periph_SPI3
#define BOARD_LCD_SPI_GPIO_CLK          RCC_AHB1Periph_GPIOB
#define BOARD_LCD_SPI_PORT              GPIOB
#define BOARD_LCD_SCK_PIN               GPIO_Pin_3
#define BOARD_LCD_MOSI_PIN              GPIO_Pin_5
#define BOARD_LCD_SCK_SRC               GPIO_PinSource3
#define BOARD_LCD_MOSI_SRC              GPIO_PinSource5
#define BOARD_LCD_SPI_AF                GPIO_AF_SPI3

#define BOARD_LCD_CTRL_GPIO_CLK         RCC_AHB1Periph_GPIOD
#define BOARD_LCD_CTRL_PORT             GPIOD
#define BOARD_LCD_CS_PIN                GPIO_Pin_11
#define BOARD_LCD_DC_PIN                GPIO_Pin_12
#define BOARD_LCD_BL_PIN                GPIO_Pin_13

/* ============================================================================
   7. 电池电压采样 (ADC1: PA0 - Channel 0)
   ============================================================================ */
#define BOARD_BAT_ADC                   ADC1
#define BOARD_BAT_ADC_CLK               RCC_APB2Periph_ADC1
#define BOARD_BAT_GPIO_CLK              RCC_AHB1Periph_GPIOA
#define BOARD_BAT_PORT                  GPIOA
#define BOARD_BAT_PIN                   GPIO_Pin_0
#define BOARD_BAT_ADC_CHANNEL           ADC_Channel_0

/* ============================================================================
   8. 板载 LED (PC13 - 核心板绿色 LED)
   ============================================================================ */
#define BOARD_LED_CLK                   RCC_AHB1Periph_GPIOC
#define BOARD_LED_PORT                  GPIOC
#define BOARD_LED_PIN                   GPIO_Pin_13

#define BOARD_LED1_CLK                  BOARD_LED_CLK
#define BOARD_LED1_PORT                 BOARD_LED_PORT
#define BOARD_LED1_PIN                  BOARD_LED_PIN

/* ============================================================================
   9. 按键接口 (KEY1: PB1, KEY2: PC0, KEY3: PC1, KEY_BOARD: PA15)
   ============================================================================ */
#define BOARD_KEY1_CLK                  RCC_AHB1Periph_GPIOB
#define BOARD_KEY1_PORT                 GPIOB
#define BOARD_KEY1_PIN                  GPIO_Pin_1

#define BOARD_KEY2_CLK                  RCC_AHB1Periph_GPIOC
#define BOARD_KEY2_PORT                 GPIOC
#define BOARD_KEY2_PIN                  GPIO_Pin_0

#define BOARD_KEY3_CLK                  RCC_AHB1Periph_GPIOC
#define BOARD_KEY3_PORT                 GPIOC
#define BOARD_KEY3_PIN                  GPIO_Pin_1

#define BOARD_KEY_BOARD_CLK             RCC_AHB1Periph_GPIOA
#define BOARD_KEY_BOARD_PORT            GPIOA
#define BOARD_KEY_BOARD_PIN             GPIO_Pin_15

#endif /* __BSP_BOARD_H__ */
