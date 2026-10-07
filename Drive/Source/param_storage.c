/**
  ******************************************************************************
  * @file    param_storage.c
  * @brief   智能手表两级非易失性参数持久化存储实现（RTC备份域 + 内部Flash双保险）
  *          支持掉电步数保持、即时防丢固化、用户配置持久化存储
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
static uint32_t s_last_saved_steps = 0;
static uint32_t s_last_flash_steps = 0;
static uint32_t s_last_step_change_tick = 0;
static bool     s_need_flash_save = false;

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
   1. RTC 备份域高速读写 (避开 rtc_f407 使用的 DR0 和 DR1，使用 DR2 ~ DR7)
   ---------------------------------------------------------------------------- */
static void Save_To_RTC_BKP(const sys_params_t * p)
{
    const uint32_t * w = (const uint32_t *)p;
    PWR_BackupAccessCmd(ENABLE);
    RTC_WriteBackupRegister(RTC_BKP_DR2, w[0]); /* magic */
    RTC_WriteBackupRegister(RTC_BKP_DR3, w[1]); /* step_count */
    RTC_WriteBackupRegister(RTC_BKP_DR4, w[2]); /* auto_sleep & wrist_wake */
    RTC_WriteBackupRegister(RTC_BKP_DR5, w[3]); /* date & alarm_en */
    RTC_WriteBackupRegister(RTC_BKP_DR6, w[4]); /* alarm time */
    RTC_WriteBackupRegister(RTC_BKP_DR7, w[5]); /* checksum */
}

static bool Read_From_RTC_BKP(sys_params_t * p)
{
    uint32_t * w = (uint32_t *)p;
    w[0] = RTC_ReadBackupRegister(RTC_BKP_DR2);
    if (w[0] != PARAM_MAGIC_NUMBER)
    {
        return false;
    }

    w[1] = RTC_ReadBackupRegister(RTC_BKP_DR3);
    w[2] = RTC_ReadBackupRegister(RTC_BKP_DR4);
    w[3] = RTC_ReadBackupRegister(RTC_BKP_DR5);
    w[4] = RTC_ReadBackupRegister(RTC_BKP_DR6);
    w[5] = RTC_ReadBackupRegister(RTC_BKP_DR7);

    return (Calc_Checksum(p) == w[5]);
}

/* ----------------------------------------------------------------------------
   2. STM32 内部 Flash 扇区 11 持久化存储 (非易失性永久备份)
   ---------------------------------------------------------------------------- */
static bool Save_To_Flash(const sys_params_t * p)
{
    uint32_t word_count = sizeof(sys_params_t) / sizeof(uint32_t);
    const uint32_t * words = (const uint32_t *)p;
    FLASH_Status status;
    uint32_t i;

    /* 解锁 Flash 控制器 */
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* 擦除 Sector 11 (0x080E0000) */
    status = FLASH_EraseSector(FLASH_Sector_11, VoltageRange_3);
    if (status != FLASH_COMPLETE)
    {
        FLASH_Lock();
        printf("[ParamStorage] Flash Erase Sector 11 Error (%d)!\r\n", status);
        return false;
    }

    /* 写入 6 个 32位 字 (24字节) */
    for (i = 0; i < word_count; i++)
    {
        status = FLASH_ProgramWord(FLASH_PARAM_SECTOR_ADDR + (i * 4), words[i]);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            printf("[ParamStorage] Flash Program Word %u Error (%d)!\r\n", (unsigned int)i, status);
            return false;
        }
    }

    FLASH_Lock();

    /* 读出回显校验 */
    const sys_params_t * check_p = (const sys_params_t *)FLASH_PARAM_SECTOR_ADDR;
    if (check_p->magic == PARAM_MAGIC_NUMBER && check_p->step_count == p->step_count)
    {
        printf("[ParamStorage] >>> Persisted Steps: %u to Flash Sector 11! <<<\r\n",
               (unsigned int)p->step_count);
        return true;
    }

    printf("[ParamStorage] Flash Verification Failed!\r\n");
    return false;
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

    /* 1. 优先尝试从 Flash 扇区 11 读取 (即使断开所有电源拔下 USB，Flash 也永久留存) */
    if (Read_From_Flash(&s_params))
    {
        loaded = true;
        printf("[ParamStorage] Loaded Parameters from Flash Sector 11 (Steps: %u).\r\n",
               (unsigned int)s_params.step_count);
    }
    /* 2. 其次尝试从 RTC 备份域读取 (有纽扣电池时维持) */
    else if (Read_From_RTC_BKP(&s_params))
    {
        loaded = true;
        printf("[ParamStorage] Loaded Parameters from RTC Backup Domain (Steps: %u).\r\n",
               (unsigned int)s_params.step_count);
    }

    /* 3. 若首次烧录尚未保存过任何数据，初始化默认值并固化 */
    if (!loaded)
    {
        printf("[ParamStorage] First Boot. Initializing Default Settings...\r\n");
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

    /* 4. 彻底恢复步数至 MPU6050 计步引擎 (开机绝对不误清零) */
    MPU6050_SetStepCount(s_params.step_count);

    /* 5. 恢复电源管理器设置 */
    Power_Manager_SetTimeout(s_params.auto_sleep_sec);
    Power_Manager_SetWristWake(s_params.wrist_wake_en ? true : false);
    Power_Manager_SetAutoSleep(s_params.auto_sleep_en ? true : false);

    s_last_saved_steps = s_params.step_count;
    s_last_flash_steps = s_params.step_count;
    s_need_flash_save  = false;

    printf("\r\n=======================================================\r\n");
    printf(" [ParamStorage] State Restored Successfully!           \r\n");
    printf(" Current Steps : %u steps (Persistent across reboot)   \r\n", (unsigned int)s_params.step_count);
    printf(" Auto-Sleep    : %u s | Wrist-Wake: %s                 \r\n",
           (unsigned int)s_params.auto_sleep_sec,
           s_params.wrist_wake_en ? "ENABLED" : "DISABLED");
    printf("=======================================================\r\n\r\n");
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
    s_last_flash_steps = s_params.step_count;
    s_need_flash_save  = false;
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

    /* 1. 步数变化时：在 RTC 备份域中维持 (0 Flash 磨损) */
    if (curr_steps != s_last_saved_steps)
    {
        s_params.checksum = Calc_Checksum(&s_params);
        PWR_BackupAccessCmd(ENABLE);
        RTC_WriteBackupRegister(RTC_BKP_DR3, curr_steps);
        RTC_WriteBackupRegister(RTC_BKP_DR7, s_params.checksum);
        s_last_saved_steps = curr_steps;
        s_last_step_change_tick = now;
        s_need_flash_save = true;
    }

    /* 2. 及时固化至 Flash 扇区 11：
       - 当步数增加达到 3 步以上；
       - 或者走完一段路、停下来超过 1.5 秒未发生新步数时，
       立即触发 Flash 固化！保证即便此时拔电拔线，步数也已写入内部 Flash！ */
    if (s_need_flash_save)
    {
        if ((curr_steps >= s_last_flash_steps + 3) ||
            ((now - s_last_step_change_tick) >= pdMS_TO_TICKS(1500)))
        {
            Param_Storage_Commit();
        }
    }
}
