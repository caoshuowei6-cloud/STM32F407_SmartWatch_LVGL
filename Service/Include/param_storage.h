/**
  ******************************************************************************
  * @file    param_storage.h
  * @brief   智能手表两级非易失性参数持久化存储系统（RTC备份域 + 内部Flash Wear-Leveling磨损均衡）
  *          支持掉电步数保持、即时防丢固化、5400槽位磨损均衡长寿命架构
  ******************************************************************************
  */

#ifndef __PARAM_STORAGE_H
#define __PARAM_STORAGE_H

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

#define PARAM_MAGIC_NUMBER      0xA55A5AA5
#define FLASH_PARAM_SECTOR_ADDR 0x080E0000 /* STM32F407 Sector 11 首地址 (128KB) */
#define PARAM_MAX_SLOTS         5400       /* 128KB 扇区可容纳 5400 个无擦除追加写入槽位 */

/* 系统持久化参数结构体 (对齐 4 字节，共 24 字节) */
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

/* 初始化参数存储系统 (开机自动扫描 Wear-Leveling 槽位恢复最新参数) */
void Param_Storage_Init(void);

/* 获取当前参数镜像引用 */
sys_params_t * Param_Storage_GetParams(void);

/* 手动提交保存所有参数 (极速写入 RTC 备份域 + Flash 槽位追加写入，耗时 <0.1ms) */
bool Param_Storage_Commit(void);

/* 后台低频维护与步数同步 (在 Sensor 任务中周期调用，及时自适应固化) */
void Param_Storage_Sync(void);

/* 更新最新步数 */
void Param_Storage_UpdateSteps(uint32_t steps);

/* 获取当前扇区磨损均衡槽位使用索引 (0 ~ 5400) */
uint32_t Param_Storage_GetSlotUsage(void);

#endif /* __PARAM_STORAGE_H */
