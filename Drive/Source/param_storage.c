/**
  ******************************************************************************
  * @file    param_storage.c
  * @brief   智能手表两级非易失性参数持久化存储实现（RTC备份域 + 内部Flash双保险）
  *          支持掉电步数保持、次日自动清零、用户配置持久化存储
  ******************************************************************************
  */

#include "param_storage.h"
#include "power_manager.h"
#include "mpu6050.h"
#include "rtc_f407.h"
#include "stm32f4xx_flash.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_rtc.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

static sys_params_t s_params;
static uint32_t s_last_commit_tick = 0;
static uint32_t s_last_saved_steps = 0;

static uint32_t Calc_Checksum(const sys_params_t * p)
{
    const uint32_t * words = (const uint32_t *)p;
    uint32_t sum = 0;
    uint32_t count = (sizeof(sys_params_t) - sizeof(uint32_t)) / sizeof(uint32_t);
    uint32_t i;

    for (i = 0; i < count; i++)
    {
        sum += words[i];
    }
    return sum;
}

/* ----------------------------------------------------------------------------
   1. RTC 备份域高速读写 (0 磨损，单周期访问，由备用电源/电容维系)
   ---------------------------------------------------------------------------- */
static void Save_To_RTC_BKP(const sys_params_t * p)
{
    PWR_BackupAccessCmd(ENABLE);
    RTC_WriteBackupRegister(RTC_BKP_DR1, p->magic);
    RTC_WriteBackupRegister(RTC_BKP_DR2, p->step_count);
    RTC_WriteBackupRegister(RTC_BKP_DR3, ((uint32_t)p->auto_sleep_sec << 16) |
                                         ((uint32_t)p->wrist_wake_en  << 8)  |
                                         (uint32_t)p->auto_sleep_en);
    RTC_WriteBackupRegister(RTC_BKP_DR4, ((uint32_t)p->last_year  << 24) |
                                         ((uint32_t)p->last_month << 16) |
                                         ((uint32_t)p->last_date  << 8)  |
                                         (uint32_t)p->alarm_en);
    RTC_WriteBackupRegister(RTC_BKP_DR5, ((uint32_t)p->alarm_hour << 8) |
                                         (uint32_t)p->alarm_min);
    RTC_WriteBackupRegister(RTC_BKP_DR6, p->checksum);
}

static bool Read_From_RTC_BKP(sys_params_t * p)
{
    uint32_t dr1 = RTC_ReadBackupRegister(RTC_BKP_DR1);
    uint32_t dr2, dr3, dr4, dr5, dr6;

    if (dr1 != PARAM_MAGIC_NUMBER)
    {
        return false;
    }

    dr2 = RTC_ReadBackupRegister(RTC_BKP_DR2);
    dr3 = RTC_ReadBackupRegister(RTC_BKP_DR3);
    dr4 = RTC_ReadBackupRegister(RTC_BKP_DR4);
    dr5 = RTC_ReadBackupRegister(RTC_BKP_DR5);
    dr6 = RTC_ReadBackupRegister(RTC_BKP_DR6);

    p->magic          = dr1;
    p->step_count     = dr2;
    p->auto_sleep_sec = (uint16_t)(dr3 >> 16);
    p->wrist_wake_en  = (uint8_t)(dr3 >> 8);
    p->auto_sleep_en  = (uint8_t)(dr3 & 0xFF);

    p->last_year      = (uint8_t)(dr4 >> 24);
    p->last_month     = (uint8_t)(dr4 >> 16);
    p->last_date      = (uint8_t)(dr4 >> 8);
    p->alarm_en       = (uint8_t)(dr4 & 0xFF);

    p->alarm_hour     = (uint8_t)(dr5 >> 8);
    p->alarm_min      = (uint8_t)(dr5 & 0xFF);
    p->reserved       = 0;
    p->checksum       = dr6;

    return (Calc_Checksum(p) == dr6);
}

/* ----------------------------------------------------------------------------
   2. STM32 内部 Flash 扇区 11 持久化存储 (永久不丢失)
   ---------------------------------------------------------------------------- */
static bool Save_To_Flash(const sys_params_t * p)
{
    uint32_t word_count = sizeof(sys_params_t) / sizeof(uint32_t);
    const uint32_t * words = (const uint32_t *)p;
    FLASH_Status status;
    uint32_t i;

    taskENTER_CRITICAL();
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    status = FLASH_EraseSector(FLASH_Sector_11, VoltageRange_3);
    if (status != FLASH_COMPLETE)
    {
        FLASH_Lock();
        taskEXIT_CRITICAL();
        printf("[ParamStorage] Flash Erase Sector 11 Failed (%d)!\r\n", status);
        return false;
    }

    for (i = 0; i < word_count; i++)
    {
        status = FLASH_ProgramWord(FLASH_PARAM_SECTOR_ADDR + (i * 4), words[i]);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            taskEXIT_CRITICAL();
            printf("[ParamStorage] Flash Program Word %u Failed (%d)!\r\n", (unsigned int)i, status);
            return false;
        }
    }

    FLASH_Lock();
    taskEXIT_CRITICAL();
    return true;
}

static bool Read_From_Flash(sys_params_t * p)
{
    const sys_params_t * flash_p = (const sys_params_t *)FLASH_PARAM_SECTOR_ADDR;

    if (flash_p->magic != PARAM_MAGIC_NUMBER)
    {
        return false;
    }

    if (Calc_Checksum(flash_p) != flash_p->checksum)
    {
        return false;
    }

    memcpy(p, flash_p, sizeof(sys_params_t));
    return true;
}

/* ----------------------------------------------------------------------------
   3. 高层业务接口
   ---------------------------------------------------------------------------- */
void Param_Storage_Init(void)
{
    bool loaded = false;
    uint8_t y = 0, m = 0, d = 0, wd = 0;

    /* 优先从 RTC 备份域恢复 */
    if (Read_From_RTC_BKP(&s_params))
    {
        loaded = true;
        printf("[ParamStorage] Restored parameters from RTC Backup Domain.\r\n");
    }
    /* 其次从 Flash 扇区恢复 */
    else if (Read_From_Flash(&s_params))
    {
        loaded = true;
        printf("[ParamStorage] Restored parameters from Flash Sector 11.\r\n");
    }

    /* 若皆为空或损坏，进行出厂默认初始化 */
    if (!loaded)
    {
        printf("[ParamStorage] First Boot or Corrupted Data. Initializing Defaults...\r\n");
        s_params.magic          = PARAM_MAGIC_NUMBER;
        s_params.step_count     = 0;
        s_params.auto_sleep_sec = 15;
        s_params.wrist_wake_en  = 1;
        s_params.auto_sleep_en  = 1;
        s_params.last_year      = 26;
        s_params.last_month     = 10;
        s_params.last_date      = 7;
        s_params.alarm_en       = 0;
        s_params.alarm_hour     = 8;
        s_params.alarm_min      = 0;
        s_params.reserved       = 0;
        s_params.checksum       = Calc_Checksum(&s_params);

        Param_Storage_Commit();
    }

    /* 4. 跨日检测：若当前日期与记录日期不同，当天步数自动归零 */
    RTC_F407_GetDate(&y, &m, &d, &wd);
    if (d != 0 && s_params.last_date != 0 && s_params.last_date != d)
    {
        printf("[ParamStorage] New Day Detected! Yesterday Steps: %u -> Reset Today to 0.\r\n",
               (unsigned int)s_params.step_count);
        s_params.step_count = 0;
        s_params.last_year  = y;
        s_params.last_month = m;
        s_params.last_date  = d;
        Param_Storage_Commit();
    }
    else
    {
        /* 恢复步数至 MPU6050 计步引擎 */
        MPU6050_SetStepCount(s_params.step_count);
    }

    /* 恢复电源管理器设置 */
    Power_Manager_SetTimeout(s_params.auto_sleep_sec);
    Power_Manager_SetWristWake(s_params.wrist_wake_en ? true : false);
    Power_Manager_SetAutoSleep(s_params.auto_sleep_en ? true : false);

    s_last_saved_steps = s_params.step_count;
    s_last_commit_tick = xTaskGetTickCount();

    printf("[ParamStorage] Active Params: Steps=%u, AutoSleep=%us, WristWake=%s\r\n",
           (unsigned int)s_params.step_count,
           (unsigned int)s_params.auto_sleep_sec,
           s_params.wrist_wake_en ? "ON" : "OFF");
}

sys_params_t * Param_Storage_GetParams(void)
{
    return &s_params;
}

bool Param_Storage_Commit(void)
{
    s_params.checksum = Calc_Checksum(&s_params);

    /* 1. 实时写入 RTC 备份域 (极速, 0 磨损) */
    Save_To_RTC_BKP(&s_params);

    /* 2. 同步写入 Flash 扇区 11 (非易失性永久备份) */
    Save_To_Flash(&s_params);

    s_last_saved_steps = s_params.step_count;
    s_last_commit_tick = xTaskGetTickCount();
    return true;
}

void Param_Storage_UpdateSteps(uint32_t steps)
{
    s_params.step_count = steps;
}

void Param_Storage_Sync(void)
{
    uint32_t curr_steps = MPU6050_GetStepCount();
    uint32_t now = xTaskGetTickCount();

    s_params.step_count = curr_steps;

    /* 步数有变化时，高频在 RTC 备份域中维持 (0 Flash 磨损) */
    if (curr_steps != s_last_saved_steps)
    {
        PWR_BackupAccessCmd(ENABLE);
        RTC_WriteBackupRegister(RTC_BKP_DR2, curr_steps);
        s_last_saved_steps = curr_steps;
    }

    /* 步数每累积增加 20 步，或每 5 分钟 (300s)，写入一次 Flash 扇区持久化 */
    if ((now - s_last_commit_tick) >= pdMS_TO_TICKS(300000))
    {
        Param_Storage_Commit();
    }
}
