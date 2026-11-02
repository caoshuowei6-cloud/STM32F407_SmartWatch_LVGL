/**
  ******************************************************************************
  * @file    ota_manager.h
  * @brief   STM32F407 工业级无线空中升级 (OTA) 管理器与双区容灾协议引擎
  * @note    存储载体: W25Q64 外部 SPI NOR Flash (8MB)
  *          - 0x400000 ~ 0x47FFFF (512KB): 黄金固件容灾备份区 (OTA_W25Q_BACKUP_BASE_ADDR)
  *          - 0x480000 ~ 0x4FFFFF (512KB): 新固件分片下载暂存区 (OTA_W25Q_STAGING_BASE_ADDR)
  *          - 0x500000 ~ 0x500FFF (4KB):   OTA 状态元数据 Slot A (OTA_W25Q_META_SLOT_A)
  *          - 0x501000 ~ 0x501FFF (4KB):   OTA 状态元数据 Slot B (OTA_W25Q_META_SLOT_B)
  *          - Flash A/B 乒乓交替存储 (递增序列号 + 结构体 CRC32)，彻底消除掉电撕裂，摆脱电池依赖
  ******************************************************************************
  */

#ifndef __OTA_MANAGER_H__
#define __OTA_MANAGER_H__

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
   Flash 分区与物理基地址定义 (适配 W25Q64 外部 SPI Flash 与 片内 Flash)
   ============================================================================ */
#define OTA_APP_BASE_ADDR           0x08020000U  /* 片内 Flash 运行区基地址 (Sector 5~7, 384KB) */
#define OTA_W25Q_BACKUP_BASE_ADDR   0x400000U    /* W25Q64 黄金固件备份区物理基地址 (512 KB) */
#define OTA_W25Q_STAGING_BASE_ADDR  0x480000U    /* W25Q64 新固件下载暂存区物理基地址 (512 KB) */
#define OTA_W25Q_META_SLOT_A        0x500000U    /* W25Q64 OTA 元数据槽位 A (4KB Sector 128) */
#define OTA_W25Q_META_SLOT_B        0x501000U    /* W25Q64 OTA 元数据槽位 B (4KB Sector 129) */
#define OTA_MAX_FIRMWARE_SIZE       (384U * 1024U) /* 单片机当前 App 最大规格 (393,216 字节) */

/* ============================================================================
   OTA 元数据结构体与魔数定义 (A/B 乒乓双区防掉电撕裂存储引擎)
   ============================================================================ */
#define OTA_META_MAGIC_STRUCT       0x4D455441U  /* 结构体标识魔数: ASCII "META" */

/* OTA 状态与生命周期魔数定义 */
#define OTA_MAGIC_UPGRADE_REQ       0x5A5A5A5AU  /* 升级就绪魔数: 固件已完整入库并通过 CRC 终审 */
#define OTA_MAGIC_TRIAL_BOOT        0x7E7E7E7EU  /* 试运行模式魔数: Bootloader 已搬运，新 App 试跑 */
#define OTA_MAGIC_CONFIRMED         0x00000000U  /* 升级转正确认魔数: 新 App 运行稳定 30s 正式提交 */
#define OTA_MAGIC_ROLLBACK_REQ      0xA5A5A5A5U  /* 紧急回滚魔数: 试运行崩溃，恢复 W25Q64 黄金备份 */

#pragma pack(push, 4)
typedef struct
{
    uint32_t struct_magic;   /* 0x4D455441 ("META") */
    uint32_t seq;            /* 递增版本/事务序列号 (1, 2, 3...) */
    uint32_t ota_magic;      /* 状态魔数: UPGRADE_REQ / TRIAL_BOOT / CONFIRMED / ROLLBACK_REQ */
    uint32_t trial_count;    /* 试运行失败计数器 (0, 1, 2, 3) */
    uint32_t fw_size;        /* 目标固件字节长度 */
    uint32_t fw_crc;         /* 目标固件全量期望 CRC32 */
    uint32_t reserved[2];    /* 8 字节对齐保留 */
    uint32_t struct_crc;     /* 结构体自身校验 CRC32 (从 struct_magic 到 reserved[1], 共 28 字节) */
} ota_metadata_t;
#pragma pack(pop)

/* OTA 运行状态枚举 */
typedef enum
{
    OTA_STATE_IDLE = 0,             /* 空闲就绪态 */
    OTA_STATE_RECEIVING,            /* 分片流式接收中 (写入 W25Q64 暂存区) */
    OTA_STATE_VERIFYING,            /* 全量 CRC32 终审查验中 */
    OTA_STATE_BACKING_UP,           /* 当前运行固件备份至 W25Q64 黄金区 */
    OTA_STATE_READY_TO_REBOOT,      /* 升级准备完毕，等待受控软复位 */
    OTA_STATE_ERROR                 /* 异常故障态 (校验不一致/写溢出) */
} ota_state_t;

/* ============================================================================
   核心 API 声明
   ============================================================================ */
void        OTA_Manager_Init(void);
ota_state_t OTA_GetState(void);
uint32_t    OTA_GetProgress(void);  /* 0 ~ 100 % */
uint32_t    OTA_GetReceivedBytes(void);

/* Flash A/B 乒乓元数据持久化接口 (防掉电撕裂) */
bool        OTA_Metadata_Read(ota_metadata_t *out_meta);
bool        OTA_Metadata_Write(const ota_metadata_t *new_meta);

/* 传输层会话控制接口 */
bool        OTA_Start(uint32_t total_size, uint32_t expected_crc, bool enable_aes);
bool        OTA_WriteChunk(const uint8_t *data, uint16_t len);
bool        OTA_Finish(void);
void        OTA_Abort(void);

/* 容灾与生命周期管理接口 (企业级防变砖) */
bool        OTA_BackupCurrentApp(uint32_t app_size);
void        OTA_Confirm_Success(void);
void        OTA_SafeReboot(void);

/* 处理来自 ESP32 / 4G 模组的数据包与控制指令 */
bool        OTA_ProcessMessage(const uint8_t *payload, uint16_t len, char *resp_buf, uint16_t resp_max_len);

#endif /* __OTA_MANAGER_H__ */
