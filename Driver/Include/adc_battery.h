/**
  ******************************************************************************
  * @file    adc_battery.h
  * @brief   STM32F407 电池/电位器 ADC 检测驱动头文件 (PA0)
  ******************************************************************************
  */

#ifndef __ADC_BATTERY_H
#define __ADC_BATTERY_H

#include "stm32f4xx.h"
#include <stdint.h>

void     ADC_Battery_Init(void);
uint16_t ADC_Battery_GetRaw(void);
uint8_t  ADC_Battery_GetPercent(void);
float    ADC_Battery_GetVoltage(void);

#endif /* __ADC_BATTERY_H */
