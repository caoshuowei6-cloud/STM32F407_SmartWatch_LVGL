/**
  ******************************************************************************
  * @file    adc_battery.c
  * @brief   STM32F407 电池/电位器 ADC 采样与百分比计算实现 (PA0)
  ******************************************************************************
  */

#include "adc_battery.h"
#include "stm32f4xx_adc.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"

/**
  * @brief  初始化 PA0 为 ADC1_IN0 模拟输入
  */
void ADC_Battery_Init(void)
{
    GPIO_InitTypeDef       GPIO_InitStructure;
    ADC_InitTypeDef        ADC_InitStructure;
    ADC_CommonInitTypeDef  ADC_CommonInitStructure;

    /* 1. 使能 GPIOA 与 ADC1 时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    /* 2. 配置 PA0 为模拟输入 (AIN) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 3. 配置 ADC 通用参数 (独立模式, 4分频 = 84MHz/4 = 21MHz) */
    ADC_CommonInitStructure.ADC_Mode             = ADC_Mode_Independent;
    ADC_CommonInitStructure.ADC_Prescaler        = ADC_Prescaler_Div4;
    ADC_CommonInitStructure.ADC_DMAAccessMode    = ADC_DMAAccessMode_Disabled;
    ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
    ADC_CommonInit(&ADC_CommonInitStructure);

    /* 4. 配置 ADC1 参数 (12位分辨率, 单次转换) */
    ADC_InitStructure.ADC_Resolution           = ADC_Resolution_12b;
    ADC_InitStructure.ADC_ScanConvMode         = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode   = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    ADC_InitStructure.ADC_ExternalTrigConv     = ADC_ExternalTrigConv_T1_CC1;
    ADC_InitStructure.ADC_DataAlign            = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfConversion      = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    /* 5. 使能 ADC1 */
    ADC_Cmd(ADC1, ENABLE);
}

/**
  * @brief  单次获取 PA0 原始采样值 (0~4095)
  */
uint16_t ADC_Battery_GetRaw(void)
{
    uint32_t timeout = 10000;

    /* 配置通道 0 采样周期 56周期 */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_56Cycles);

    /* 软件触发转换 */
    ADC_SoftwareStartConv(ADC1);

    /* 等待转换完成 */
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET && timeout > 0)
    {
        timeout--;
    }

    return ADC_GetConversionValue(ADC1);
}

/**
  * @brief  多次滤波并计算电量/电位器百分比 (0~100%)
  */
uint8_t ADC_Battery_GetPercent(void)
{
    uint32_t sum = 0;
    uint16_t avg;
    uint8_t  i;

    /* 均值滤波采样 8 次 */
    for (i = 0; i < 8; i++)
    {
        sum += ADC_Battery_GetRaw();
    }
    avg = sum / 8;

    /* 映射为 0~100% */
    if (avg >= 4050) return 100;
    if (avg <= 50)   return 0;

    return (uint8_t)((avg * 100UL) / 4095UL);
}

/**
  * @brief  获取当前电压值 (0.0V ~ 3.3V)
  */
float ADC_Battery_GetVoltage(void)
{
    uint16_t raw = ADC_Battery_GetRaw();
    return ((float)raw / 4095.0f) * 3.3f;
}
