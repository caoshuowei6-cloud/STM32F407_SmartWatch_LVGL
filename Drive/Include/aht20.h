/**
  ******************************************************************************
  * @file    aht20.h
  * @brief   STM32F407 AHT20 温湿度传感器驱动头文件 (SCL=PB6, SDA=PB7)
  ******************************************************************************
  */

#ifndef __AHT20_H
#define __AHT20_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* 初始化 AHT20 (配置 PB6/PB7 模拟 I2C，校准传感器) */
bool AHT20_Init(void);

/* 读取实时温湿度数据 (返回 true 表示成功) */
bool AHT20_Read(float * temperature, float * humidity);

/* 获取最近一次缓存的温湿度数据 */
float AHT20_GetLastTemp(void);
float AHT20_GetLastHumi(void);
bool  AHT20_IsOnline(void);

#endif /* __AHT20_H */
