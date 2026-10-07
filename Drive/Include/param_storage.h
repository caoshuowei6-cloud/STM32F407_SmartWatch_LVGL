/**
  ******************************************************************************
  * @file    param_storage.h
  * @brief   智能手表两级非易失性参数持久化存储系统（RTC备份域 + 内部Flash双保险）
  *          支持掉电步数保持、次日自动清零、用户配置持久化存储
  ******************************************************************************
  */

#ifndef __PARAM_STORAGE_H
#define __PARAM_STORAGE_H

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

#define PARAM_MAGIC_NUMBER      0xA55A5AA5
#define FLASH_PARAM_SECTOR_ADDR 0x080E0000 /* STM32F407 Sector 11 首地址 (128KB) */

/* 系统持久化参数结构体 (对齐 4 字节) */
typedef struct {
    uint32_t magic;          /* 魔数: PARAM_MAGIC_NUMBER */
    uint32_t step_count;     /* 累积步数 */
    uint16_t auto_sleep_sec; /* 息屏倒计时 (秒) */
    uint8_t  wrist_wake_en;  /* 抬腕亮屏使能 (1:开, 0:关) */
    uint8_t  auto_sleep_en;  /* 自动息屏使能 */
    uint8_t  last_year;      /* 上次记录的日期，用于次日 0 点步数自动归零 */
    uint8_t  last_month;
    uint8_t  last_date;
    uint8_t  alarm_en;       /* 闹钟开关 */
    uint8_t  alarm_hour;     /* 闹钟时 */
    uint8_t  alarm_min;      /* 闹钟分 */
    uint16_t reserved;       /* 预留字节 */
    uint32_t checksum;       /* 累加校验和 (防止断电撕裂损坏) */
} sys_params_t;

/* 初始化参数存储系统 (开机自动校验并恢复步数、跨日检测与参数配置) */
void Param_Storage_Init(void);

/* 获取当前参数镜像引用 */
sys_params_t * Param_Storage_GetParams(void);

/* 手动提交保存所有参数 (写入 RTC 备份域 + Flash 扇区 11) */
bool Param_Storage_Commit(void);

/* 后台低频维护与步数同步 (建议在 Sensor 任务中每 5~10 秒调用一次，无 Flash 磨损维护) */
void Param_Storage_Sync(void);

/* 更新最新步数 */
void Param_Storage_UpdateSteps(uint32_t steps);

#endif /* __PARAM_STORAGE_H */
