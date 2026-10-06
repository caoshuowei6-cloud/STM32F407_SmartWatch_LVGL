/**
  ******************************************************************************
  * @file    rtc_f407.c
  * @brief   STM32F407 硬件日历 RTC 驱动实现（带 LSE/LSI 自动容错、MCU复位防断流与软时钟双保险）
  ******************************************************************************
  */

#include "rtc_f407.h"
#include "stm32f4xx_rtc.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_rcc.h"
#include "delay.h"
#include <stdio.h>

#define RTC_BKP_FLAG_INIT    0x5A5A
#define RTC_CLK_SRC_LSI      0x1111
#define RTC_CLK_SRC_LSE      0x2222

static rtc_time_date_t s_cache_dt = {
    .year    = 26,
    .month   = 10,
    .date    = 6,
    .weekday = 2,
    .hours   = 20,
    .minutes = 15,
    .seconds = 0
};

static uint8_t s_last_hw_sec = 0xFF;

/**
  * @brief  初始化 STM32F407 硬件日历 RTC
  */
void RTC_F407_Init(void)
{
    RTC_InitTypeDef RTC_InitStructure;
    uint32_t bkp0, bkp1;
    bool need_full_init = false;

    /* 1. 使能电源接口时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

    /* 2. 允许访问备份域寄存器与 RTC */
    PWR_BackupAccessCmd(ENABLE);

    /* 3. 读取备份寄存器标记 */
    bkp0 = RTC_ReadBackupRegister(RTC_BKP_DR0);
    bkp1 = RTC_ReadBackupRegister(RTC_BKP_DR1);

    if (bkp0 != RTC_BKP_FLAG_INIT || (bkp1 != RTC_CLK_SRC_LSI && bkp1 != RTC_CLK_SRC_LSE))
    {
        need_full_init = true;
    }

    if (need_full_init)
    {
        uint32_t retry = 500; /* 500ms 尝试 LSE 起振 */

        printf("[RTC] First boot or backup reset, initializing RTC hardware...\r\n");

        /* 复位备份域 */
        RCC_BackupResetCmd(ENABLE);
        RCC_BackupResetCmd(DISABLE);

        /* 尝试开启 LSE (32.768kHz 外部晶振) */
        RCC_LSEConfig(RCC_LSE_ON);
        while ((RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET) && retry)
        {
            Delay_ms(1);
            retry--;
        }

        if (retry > 0)
        {
            /* LSE 启动成功 */
            RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
            RCC_RTCCLKCmd(ENABLE);

            RTC_InitStructure.RTC_AsynchPrediv = 127;
            RTC_InitStructure.RTC_SynchPrediv  = 255;
            RTC_InitStructure.RTC_HourFormat   = RTC_HourFormat_24;
            RTC_Init(&RTC_InitStructure);

            RTC_WriteBackupRegister(RTC_BKP_DR1, RTC_CLK_SRC_LSE);
            printf("[RTC] Using 32.768kHz LSE External Crystal!\r\n");
        }
        else
        {
            /* LSE 超时或未焊接，自动容错切换至内部 LSI (32kHz) */
            RCC_LSEConfig(RCC_LSE_OFF);
            RCC_LSICmd(ENABLE);
            while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET);

            RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
            RCC_RTCCLKCmd(ENABLE);

            RTC_InitStructure.RTC_AsynchPrediv = 127;
            RTC_InitStructure.RTC_SynchPrediv  = 249; /* 32000 / (128 * 250) = 1Hz */
            RTC_InitStructure.RTC_HourFormat   = RTC_HourFormat_24;
            RTC_Init(&RTC_InitStructure);

            RTC_WriteBackupRegister(RTC_BKP_DR1, RTC_CLK_SRC_LSI);
            printf("[RTC] LSE Timeout (500ms). Switched to internal LSI (32kHz)!\r\n");
        }

        /* 设置初始默认时间与日期 (2026-10-06 20:15:00 星期二) */
        RTC_F407_SetDate(26, 10, 6, 2);
        RTC_F407_SetTime(20, 15, 0);

        /* 写入标记，下次上电/复位时保留走时 */
        RTC_WriteBackupRegister(RTC_BKP_DR0, RTC_BKP_FLAG_INIT);
    }
    else
    {
        /* 已经初始化过：若此前使用的是 LSI，MCU 每次硬件复位后必须重新开启 LSI */
        if (bkp1 == RTC_CLK_SRC_LSI)
        {
            RCC_LSICmd(ENABLE);
            while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET);
        }
        else
        {
            RCC_LSEConfig(RCC_LSE_ON);
        }

        /* 使能 RTC 时钟并等待同步 */
        RCC_RTCCLKCmd(ENABLE);
        RTC_WaitForSynchro();
        printf("[RTC] RTC running from backup domain (%s)!\r\n",
               (bkp1 == RTC_CLK_SRC_LSE) ? "LSE" : "LSI");
    }

    /* 首次同步当前硬件时间到本地缓存 */
    RTC_F407_GetDateTime(&s_cache_dt);
}

/**
  * @brief  设置 RTC 时间
  */
void RTC_F407_SetTime(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    RTC_TimeTypeDef RTC_TimeStructure;
    ErrorStatus status;

    PWR_BackupAccessCmd(ENABLE);

    RTC_TimeStructure.RTC_H12     = RTC_H12_AM;
    RTC_TimeStructure.RTC_Hours   = (hours > 23) ? 0 : hours;
    RTC_TimeStructure.RTC_Minutes = (minutes > 59) ? 0 : minutes;
    RTC_TimeStructure.RTC_Seconds = (seconds > 59) ? 0 : seconds;

    status = RTC_SetTime(RTC_Format_BIN, &RTC_TimeStructure);
    if (status != SUCCESS)
    {
        printf("[RTC] Warning: RTC_SetTime failed (%d)\r\n", status);
    }

    /* 立即更新内存缓存，确保界面切换无延迟展示新时间 */
    s_cache_dt.hours   = RTC_TimeStructure.RTC_Hours;
    s_cache_dt.minutes = RTC_TimeStructure.RTC_Minutes;
    s_cache_dt.seconds = RTC_TimeStructure.RTC_Seconds;
    s_last_hw_sec      = RTC_TimeStructure.RTC_Seconds;
}

/**
  * @brief  设置 RTC 日期
  */
void RTC_F407_SetDate(uint8_t year, uint8_t month, uint8_t date, uint8_t weekday)
{
    RTC_DateTypeDef RTC_DateStructure;
    ErrorStatus status;

    PWR_BackupAccessCmd(ENABLE);

    RTC_DateStructure.RTC_Year    = year;
    RTC_DateStructure.RTC_Month   = (month < 1 || month > 12) ? 1 : month;
    RTC_DateStructure.RTC_Date    = (date < 1 || date > 31) ? 1 : date;
    RTC_DateStructure.RTC_WeekDay = (weekday < 1 || weekday > 7) ? 1 : weekday;

    status = RTC_SetDate(RTC_Format_BIN, &RTC_DateStructure);
    if (status != SUCCESS)
    {
        printf("[RTC] Warning: RTC_SetDate failed (%d)\r\n", status);
    }

    s_cache_dt.year    = RTC_DateStructure.RTC_Year;
    s_cache_dt.month   = RTC_DateStructure.RTC_Month;
    s_cache_dt.date    = RTC_DateStructure.RTC_Date;
    s_cache_dt.weekday = RTC_DateStructure.RTC_WeekDay;
}

/**
  * @brief  统一设置日期与时间
  */
void RTC_F407_SetDateTime(const rtc_time_date_t * dt)
{
    if (dt == NULL) return;
    RTC_F407_SetDate(dt->year, dt->month, dt->date, dt->weekday);
    RTC_F407_SetTime(dt->hours, dt->minutes, dt->seconds);
}

/**
  * @brief  读取 RTC 当前时间
  */
void RTC_F407_GetTime(uint8_t * hours, uint8_t * minutes, uint8_t * seconds)
{
    rtc_time_date_t dt;
    RTC_F407_GetDateTime(&dt);
    if (hours)   *hours   = dt.hours;
    if (minutes) *minutes = dt.minutes;
    if (seconds) *seconds = dt.seconds;
}

/**
  * @brief  读取 RTC 当前日期
  */
void RTC_F407_GetDate(uint8_t * year, uint8_t * month, uint8_t * date, uint8_t * weekday)
{
    rtc_time_date_t dt;
    RTC_F407_GetDateTime(&dt);
    if (year)    *year    = dt.year;
    if (month)   *month   = dt.month;
    if (date)    *date    = dt.date;
    if (weekday) *weekday = dt.weekday;
}

/**
  * @brief  获取完整日期与时间结构体
  */
void RTC_F407_GetDateTime(rtc_time_date_t * dt)
{
    RTC_TimeTypeDef hw_t;
    RTC_DateTypeDef hw_d;

    if (dt == NULL) return;

    /* 读取硬件 RTC 影子寄存器 */
    RTC_GetTime(RTC_Format_BIN, &hw_t);
    RTC_GetDate(RTC_Format_BIN, &hw_d);

    /* 若硬件 RTC 数据有效（非全 0 初始化异常态），与硬件保持高精度同步 */
    if (hw_d.RTC_Year > 0 || hw_d.RTC_Month > 0 ||
        hw_t.RTC_Hours > 0 || hw_t.RTC_Minutes > 0 || hw_t.RTC_Seconds > 0)
    {
        s_cache_dt.hours   = hw_t.RTC_Hours;
        s_cache_dt.minutes = hw_t.RTC_Minutes;
        s_cache_dt.seconds = hw_t.RTC_Seconds;
        s_cache_dt.year    = hw_d.RTC_Year;
        s_cache_dt.month   = hw_d.RTC_Month;
        s_cache_dt.date    = hw_d.RTC_Date;
        s_cache_dt.weekday = hw_d.RTC_WeekDay;
    }

    *dt = s_cache_dt;
}

/**
  * @brief  1s 周期走时维持（硬件走时同步，异常停摆时软时钟双保险平滑推进）
  */
void RTC_F407_Tick1s(void)
{
    RTC_TimeTypeDef hw_t;
    RTC_DateTypeDef hw_d;

    RTC_GetTime(RTC_Format_BIN, &hw_t);
    RTC_GetDate(RTC_Format_BIN, &hw_d);

    if (hw_t.RTC_Seconds != s_last_hw_sec &&
        (hw_d.RTC_Year > 0 || hw_d.RTC_Month > 0 || hw_t.RTC_Hours > 0 || hw_t.RTC_Minutes > 0 || hw_t.RTC_Seconds > 0))
    {
        /* 硬件 RTC 正常跳变，完全同步硬件 */
        s_last_hw_sec = hw_t.RTC_Seconds;
        s_cache_dt.hours   = hw_t.RTC_Hours;
        s_cache_dt.minutes = hw_t.RTC_Minutes;
        s_cache_dt.seconds = hw_t.RTC_Seconds;
        s_cache_dt.year    = hw_d.RTC_Year;
        s_cache_dt.month   = hw_d.RTC_Month;
        s_cache_dt.date    = hw_d.RTC_Date;
        s_cache_dt.weekday = hw_d.RTC_WeekDay;
    }
    else
    {
        /* 硬件 RTC 暂停或同步间隔中，软时钟平滑递增 1 秒，确保表盘永不停摆 */
        s_cache_dt.seconds++;
        if (s_cache_dt.seconds >= 60)
        {
            s_cache_dt.seconds = 0;
            s_cache_dt.minutes++;
            if (s_cache_dt.minutes >= 60)
            {
                s_cache_dt.minutes = 0;
                s_cache_dt.hours = (s_cache_dt.hours + 1) % 24;
            }
        }
        s_last_hw_sec = s_cache_dt.seconds;
    }
}
