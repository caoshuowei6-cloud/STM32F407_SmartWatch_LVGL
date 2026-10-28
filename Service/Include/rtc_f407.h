/**
  ******************************************************************************
  * @file    rtc_f407.h
  * @brief   STM32F407 硬件日历 RTC 驱动头文件（带 LSE/LSI 自动容错与时钟防冻结双保险）
  ******************************************************************************
  */

#ifndef __RTC_F407_H
#define __RTC_F407_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t year;   /* 0~99 (对应 2000~2099) */
    uint8_t month;  /* 1~12 */
    uint8_t date;   /* 1~31 */
    uint8_t weekday;/* 1~7 (1:周一 ~ 7:周日) */
    uint8_t hours;  /* 0~23 */
    uint8_t minutes;/* 0~59 */
    uint8_t seconds;/* 0~59 */
} rtc_time_date_t;

/* 初始化硬件 RTC（自动检测 LSE 晶振，若未起振则自动容错切换至 LSI，MCU 复位保持 LSI） */
void RTC_F407_Init(void);

/* 设置时间与日期 */
void RTC_F407_SetTime(uint8_t hours, uint8_t minutes, uint8_t seconds);
void RTC_F407_SetDate(uint8_t year, uint8_t month, uint8_t date, uint8_t weekday);
void RTC_F407_SetDateTime(const rtc_time_date_t * dt);

/* 获取时间与日期 */
void RTC_F407_GetTime(uint8_t * hours, uint8_t * minutes, uint8_t * seconds);
void RTC_F407_GetDate(uint8_t * year, uint8_t * month, uint8_t * date, uint8_t * weekday);
void RTC_F407_GetDateTime(rtc_time_date_t * dt);

/* 1s 周期走时维持（硬件自走时同步，异常停摆时软时钟双保险平滑推进） */
void RTC_F407_Tick1s(void);

#endif /* __RTC_F407_H */
