/**
  ******************************************************************************
  * @file    history_storage.h
  * @brief   智能手表离线传感器与健康历史数据存储系统 (借鉴新普惠工业级FlashRecordSys架构)
  * @note    硬件载体: W25Q64 外部 SPI Flash (分区块 2MB: 0x200000 ~ 0x3FFFFF)
  *          读写指针索引利用 STM32F407 RTC 备份寄存器 (BKP) 持久化，零 Flash 磨损秒级恢复
  ******************************************************************************
  */

#ifndef __HISTORY_STORAGE_H
#define __HISTORY_STORAGE_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
   Flash 存储分区与规格定义 (32 字节对齐，严格适配 Flash 256B 页与 4KB 扇区)
   ============================================================================ */
#define HISTORY_FLASH_BASE_ADDR         0x200000        /* 存储起始物理基地址 (2MB 偏移) */
#define HISTORY_FLASH_PARTITION_SIZE    0x200000        /* 历史数据分区总大小 (2MB) */
#define HISTORY_RECORD_SIZE             32              /* 单条快照长度 (32 字节紧凑对齐) */
#define HISTORY_RECORDS_PER_PAGE        8               /* 单页容纳条数 (256 / 32 = 8) */
#define HISTORY_RECORDS_PER_SECTOR      128             /* 单扇区容纳条数 (4096 / 32 = 128) */
#define HISTORY_MAX_RECORDS             65536           /* 分区最大容纳记录总数 (2MB / 32B) */

/* 默认数据采样归档间隔 (分钟) */
#define HISTORY_DEFAULT_INTERVAL_MIN    15              /* 默认每 15 分钟固化一条历史快照 */

/* 运动状态定义 */
typedef enum
{
    ACTIVITY_STILL      = 0,    /* 静止 / 休息 */
    ACTIVITY_WALKING    = 1,    /* 步行 */
    ACTIVITY_RUNNING    = 2,    /* 跑步 */
    ACTIVITY_SLEEPING   = 3     /* 睡眠 */
} history_activity_t;

/* 云端同步与标记位掩码 */
#define RECORD_FLAG_UPLOADED            0x01            /* 已成功上报云端 */
#define RECORD_FLAG_ALARM               0x02            /* 包含异常警报 (如高温、电量过低) */
#define RECORD_FLAG_MANUAL              0x04            /* 用户手动触发保存 */

/* ============================================================================
   单条历史记录结构体 (严格 32 字节，紧凑对齐，无跨页写入风险)
   ============================================================================ */
#pragma pack(1)
typedef struct
{
    uint32_t timestamp;         /* 时间戳 (UNIX 秒数，自 1970 或自定义基准) */
    int32_t  step_total;        /* 历史累计总步数 */
    int16_t  step_delta;        /* 本周期内新增增量步数 */
    int16_t  temp_x10;          /* 温度 * 10 (例如 25.4℃ -> 254) */
    uint16_t humi_x10;          /* 湿度 * 10 (例如 53.2% -> 532) */
    uint16_t bat_mv;            /* 电池电压 (mV) */
    uint8_t  bat_pct;           /* 电池百分比 (0~100) */
    uint8_t  activity_type;     /* 运动状态 (history_activity_t) */
    uint8_t  flags;             /* 标记位 (RECORD_FLAG_...) */
    uint8_t  reserved[11];      /* 工业级预留扩展字段 */
    uint16_t crc16;             /* 前 30 字节的 CRC16 校验和 */
} history_record_t;
#pragma pack()

/* ============================================================================
   系统外部接口声明
   ============================================================================ */
bool     History_Storage_Init(void);
bool     History_Storage_SaveRecord(history_record_t *rec);
bool     History_Storage_GetLatest(history_record_t *out_rec);
uint16_t History_Storage_ReadRecent(history_record_t *out_buf, uint16_t max_count);
uint32_t History_Storage_GetTotalCount(void);
uint32_t History_Storage_GetUnsyncedCount(void);
bool     History_Storage_GetNextUnsynced(history_record_t *out_rec);
void     History_Storage_AckUploaded(void);
void     History_Storage_Format(void);

/* 定时采样任务接口 (可置于 FreeRTOS 后台周期轮询) */
void     History_Storage_PeriodicCheck(void);
void     History_Storage_TriggerSnapshot(void);

#endif /* __HISTORY_STORAGE_H */
