/**
  ******************************************************************************
  * @file    history_storage.c
  * @brief   智能手表离线传感器与健康历史数据存储系统实现
  * @note    借鉴新普惠工业级 FlashRecordSys 架构，结合 W25Q64 与 STM32F407 RTC 备份域
  ******************************************************************************
  */

#include "history_storage.h"
#include "w25qxx.h"
#include "rtc_f407.h"
#include "mpu6050.h"
#include "aht20.h"
#include "adc_battery.h"
#include "stm32f4xx_rtc.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_rcc.h"
#include <stdio.h>
#include <string.h>

#define HISTORY_BKP_MAGIC           0x48495354      /* "HIST" 识别魔数 */
#define HISTORY_BKP_REG_WR_IDX      RTC_BKP_DR2     /* 写入指针备份寄存器 */
#define HISTORY_BKP_REG_RD_IDX      RTC_BKP_DR3     /* 同步指针备份寄存器 */
#define HISTORY_BKP_REG_TOTAL       RTC_BKP_DR4     /* 总计数备份寄存器 */
#define HISTORY_BKP_REG_MAGIC       RTC_BKP_DR5     /* 魔数校验备份寄存器 */

static uint32_t s_wr_index = 0;                     /* 当前写入游标 (0 ~ 65535) */
static uint32_t s_rd_index = 0;                     /* 当前读取/已同步游标 */
static uint32_t s_total_count = 0;                  /* 历史累计记录总数 */
static uint32_t s_last_step_total = 0;              /* 上一次采样时的累计步数 */
static uint8_t  s_last_saved_minute = 0xFF;         /* 上一次定时固化的分钟数 */
static bool     s_history_ready = false;            /* 模块就绪标志 */

/* ============================================================================
   内部辅助：标准 CRC16-MODBUS 校验算法 (保障工业级存储数据完整性)
   ============================================================================ */
static uint16_t History_CalcCRC16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc = (crc >> 1);
        }
    }
    return crc;
}

/* ============================================================================
   内部辅助：备份域访问与持久化读写
   ============================================================================ */
static void History_EnableBKP(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
}

static void History_SavePointersToBKP(void)
{
    History_EnableBKP();
    RTC_WriteBackupRegister(HISTORY_BKP_REG_WR_IDX, s_wr_index);
    RTC_WriteBackupRegister(HISTORY_BKP_REG_RD_IDX, s_rd_index);
    RTC_WriteBackupRegister(HISTORY_BKP_REG_TOTAL,  s_total_count);
    RTC_WriteBackupRegister(HISTORY_BKP_REG_MAGIC,  HISTORY_BKP_MAGIC);
}

/* ============================================================================
   时间日期转 UNIX 时间戳 (从 2000-01-01 开始标准换算)
   ============================================================================ */
static uint32_t History_DateTimeToUnix(const rtc_time_date_t *dt)
{
    uint32_t y = 2000 + dt->year;
    uint32_t m = dt->month;
    uint32_t d = dt->date;
    if (m <= 2)
    {
        y--;
        m += 12;
    }
    uint32_t days = 365 * y + y / 4 - y / 100 + y / 400 + (153 * (m + 1)) / 5 + d - 719469;
    return days * 86400 + dt->hours * 3600 + dt->minutes * 60 + dt->seconds;
}

/* ============================================================================
   系统初始化
   ============================================================================ */
bool History_Storage_Init(void)
{
    uint32_t magic;

    /* 1. 检查外部 SPI Flash 是否在线 */
    uint32_t flash_id = W25QXX_ReadID();
    if (flash_id != W25Q64_JEDEC_ID)
    {
        printf("[History] W25Q64 ID mismatch (0x%06X != 0x%06X), history storage disabled!\r\n",
               flash_id, W25Q64_JEDEC_ID);
        s_history_ready = false;
        return false;
    }

    /* 2. 读取 RTC 备份寄存器中的读写指针 */
    History_EnableBKP();
    magic = RTC_ReadBackupRegister(HISTORY_BKP_REG_MAGIC);

    if (magic == HISTORY_BKP_MAGIC)
    {
        s_wr_index    = RTC_ReadBackupRegister(HISTORY_BKP_REG_WR_IDX);
        s_rd_index    = RTC_ReadBackupRegister(HISTORY_BKP_REG_RD_IDX);
        s_total_count = RTC_ReadBackupRegister(HISTORY_BKP_REG_TOTAL);

        /* 边界安全防御校验 */
        if (s_wr_index >= HISTORY_MAX_RECORDS) s_wr_index = 0;
        if (s_rd_index >= HISTORY_MAX_RECORDS) s_rd_index = 0;

        printf("[History] Restored from RTC BKP: WR=%lu, RD=%lu, Total=%lu\r\n",
               s_wr_index, s_rd_index, s_total_count);
    }
    else
    {
        /* 首次上电或备份域电池断开，初始化首个扇区 */
        printf("[History] Cold boot / BKP uninitialized, formatting base sector...\r\n");
        s_wr_index    = 0;
        s_rd_index    = 0;
        s_total_count = 0;
        W25QXX_Erase_Sector(HISTORY_FLASH_BASE_ADDR);
        History_SavePointersToBKP();
    }

    s_last_step_total = MPU6050_GetStepCount();
    s_history_ready   = true;
    printf("[History] Industrial History Storage System initialized successfully (Partition 2MB @ 0x%06X).\r\n",
           HISTORY_FLASH_BASE_ADDR);
    return true;
}

/* ============================================================================
   保存单条记录到 Flash (自动扇区轮换擦除与零磨损 BKP 索引同步)
   ============================================================================ */
bool History_Storage_SaveRecord(history_record_t *rec)
{
    if (!s_history_ready || rec == NULL) return false;

    /* 1. 若当前写入指针正好位于扇区起始边界，提前擦除该 4KB 扇区 (128条/扇区) */
    if ((s_wr_index % HISTORY_RECORDS_PER_SECTOR) == 0)
    {
        uint32_t sector_addr = HISTORY_FLASH_BASE_ADDR + (s_wr_index * HISTORY_RECORD_SIZE);
        W25QXX_Erase_Sector(sector_addr);
    }

    /* 2. 计算 CRC16 校验码并填入结构体末尾 */
    rec->crc16 = History_CalcCRC16((const uint8_t *)rec, HISTORY_RECORD_SIZE - 2);

    /* 3. 写入 W25Q64 Flash */
    uint32_t write_addr = HISTORY_FLASH_BASE_ADDR + (s_wr_index * HISTORY_RECORD_SIZE);
    W25QXX_Write((uint8_t *)rec, write_addr, HISTORY_RECORD_SIZE);

    /* 4. 更新游标并持久化保存至 RTC 备份寄存器 */
    s_wr_index = (s_wr_index + 1) % HISTORY_MAX_RECORDS;
    s_total_count++;

    /* 若写入追上读取游标 (环形溢出覆盖)，推动读游标以维持 FIFO 时序 */
    if (s_wr_index == s_rd_index)
    {
        s_rd_index = (s_rd_index + 1) % HISTORY_MAX_RECORDS;
    }

    History_SavePointersToBKP();
    return true;
}

/* ============================================================================
   读取最新的一条记录
   ============================================================================ */
bool History_Storage_GetLatest(history_record_t *out_rec)
{
    if (!s_history_ready || out_rec == NULL || s_total_count == 0) return false;

    uint32_t last_idx = (s_wr_index == 0) ? (HISTORY_MAX_RECORDS - 1) : (s_wr_index - 1);
    uint32_t read_addr = HISTORY_FLASH_BASE_ADDR + (last_idx * HISTORY_RECORD_SIZE);

    W25QXX_Read((uint8_t *)out_rec, read_addr, HISTORY_RECORD_SIZE);

    /* CRC16 校验验证 */
    uint16_t cal_crc = History_CalcCRC16((const uint8_t *)out_rec, HISTORY_RECORD_SIZE - 2);
    return (cal_crc == out_rec->crc16);
}

/* ============================================================================
   按时间倒序读取最近 N 条记录 (供给 LVGL 绘制 24小时 / 7天 趋势图表)
   ============================================================================ */
uint16_t History_Storage_ReadRecent(history_record_t *out_buf, uint16_t max_count)
{
    if (!s_history_ready || out_buf == NULL || max_count == 0 || s_total_count == 0) return 0;

    uint16_t valid_cnt = 0;
    uint32_t available = (s_total_count > HISTORY_MAX_RECORDS) ? HISTORY_MAX_RECORDS : s_total_count;
    uint16_t to_read   = (max_count > available) ? (uint16_t)available : max_count;
    uint32_t cur_idx   = (s_wr_index == 0) ? (HISTORY_MAX_RECORDS - 1) : (s_wr_index - 1);

    for (uint16_t i = 0; i < to_read; i++)
    {
        uint32_t read_addr = HISTORY_FLASH_BASE_ADDR + (cur_idx * HISTORY_RECORD_SIZE);
        W25QXX_Read((uint8_t *)&out_buf[valid_cnt], read_addr, HISTORY_RECORD_SIZE);

        /* 校验数据合法性 */
        uint16_t cal_crc = History_CalcCRC16((const uint8_t *)&out_buf[valid_cnt], HISTORY_RECORD_SIZE - 2);
        if (cal_crc == out_buf[valid_cnt].crc16)
        {
            valid_cnt++;
        }

        if (cur_idx == 0) cur_idx = HISTORY_MAX_RECORDS - 1;
        else              cur_idx--;
    }

    return valid_cnt;
}

/* ============================================================================
   未同步云端记录获取与确认 (断点续传与追帧核心)
   ============================================================================ */
uint32_t History_Storage_GetUnsyncedCount(void)
{
    if (!s_history_ready) return 0;
    if (s_wr_index >= s_rd_index)
    {
        return (s_wr_index - s_rd_index);
    }
    else
    {
        return (HISTORY_MAX_RECORDS - (s_rd_index - s_wr_index));
    }
}

bool History_Storage_GetNextUnsynced(history_record_t *out_rec)
{
    if (!s_history_ready || out_rec == NULL) return false;
    if (s_rd_index == s_wr_index) return false; /* 全部已同步 */

    uint32_t read_addr = HISTORY_FLASH_BASE_ADDR + (s_rd_index * HISTORY_RECORD_SIZE);
    W25QXX_Read((uint8_t *)out_rec, read_addr, HISTORY_RECORD_SIZE);

    uint16_t cal_crc = History_CalcCRC16((const uint8_t *)out_rec, HISTORY_RECORD_SIZE - 2);
    return (cal_crc == out_rec->crc16);
}

void History_Storage_AckUploaded(void)
{
    if (!s_history_ready || s_rd_index == s_wr_index) return;

    s_rd_index = (s_rd_index + 1) % HISTORY_MAX_RECORDS;
    History_SavePointersToBKP();
}

uint32_t History_Storage_GetTotalCount(void)
{
    return s_total_count;
}

/* ============================================================================
   格式化存储分区
   ============================================================================ */
void History_Storage_Format(void)
{
    if (!s_history_ready) return;

    printf("[History] Formatting history storage partition (Erasing first sectors)...\r\n");
    s_wr_index    = 0;
    s_rd_index    = 0;
    s_total_count = 0;

    W25QXX_Erase_Sector(HISTORY_FLASH_BASE_ADDR);
    History_SavePointersToBKP();
    printf("[History] Format completed.\r\n");
}

/* ============================================================================
   主动采集一次当前手表各项传感器数据并固化到 Flash
   ============================================================================ */
void History_Storage_TriggerSnapshot(void)
{
    if (!s_history_ready) return;

    history_record_t rec;
    memset(&rec, 0, sizeof(rec));

    /* 1. 时间戳获取 */
    rtc_time_date_t dt;
    RTC_F407_GetDateTime(&dt);
    rec.timestamp = History_DateTimeToUnix(&dt);

    /* 2. 步数统计与周期增量计算 */
    uint32_t cur_steps = MPU6050_GetStepCount();
    rec.step_total     = (int32_t)cur_steps;
    if (cur_steps >= s_last_step_total)
    {
        rec.step_delta = (int16_t)(cur_steps - s_last_step_total);
    }
    else
    {
        rec.step_delta = (int16_t)cur_steps; /* 发生跨日清零时 */
    }
    s_last_step_total = cur_steps;

    /* 3. 温湿度采样 */
    float temp = AHT20_GetLastTemp();
    float humi = AHT20_GetLastHumi();
    rec.temp_x10 = (int16_t)(temp * 10.0f);
    rec.humi_x10 = (uint16_t)(humi * 10.0f);

    /* 4. 电池状态 */
    float vbat = ADC_Battery_GetVoltage();
    rec.bat_mv  = (uint16_t)(vbat * 1000.0f);
    rec.bat_pct = ADC_Battery_GetPercent();

    /* 5. 运动状态判定 (若周期内步数快速增长则判定为步行/运动) */
    if (rec.step_delta > 100) rec.activity_type = ACTIVITY_RUNNING;
    else if (rec.step_delta > 10) rec.activity_type = ACTIVITY_WALKING;
    else rec.activity_type = ACTIVITY_STILL;

    rec.flags = 0; /* 初始未上传 */

    /* 6. 保存快照 */
    if (History_Storage_SaveRecord(&rec))
    {
        printf("[History] Snapshot saved: Time=%02d:%02d, Steps=%ld(+%d), Temp=%.1fC, Humi=%.1f%%, Bat=%d%% (Idx=%lu)\r\n",
               dt.hours, dt.minutes, rec.step_total, rec.step_delta, temp, humi, rec.bat_pct, s_wr_index);
    }
}

/* ============================================================================
   周期性轮询服务 (可由 FreeRTOS 后台任务定期调用，每 15 分钟触发一次固化)
   ============================================================================ */
void History_Storage_PeriodicCheck(void)
{
    if (!s_history_ready) return;

    rtc_time_date_t dt;
    RTC_F407_GetDateTime(&dt);

    /* 检查是否到达 15 分钟整点 (0, 15, 30, 45) 且本分钟尚未保存过 */
    if ((dt.minutes % HISTORY_DEFAULT_INTERVAL_MIN) == 0 && dt.minutes != s_last_saved_minute)
    {
        s_last_saved_minute = dt.minutes;
        History_Storage_TriggerSnapshot();
    }
}
