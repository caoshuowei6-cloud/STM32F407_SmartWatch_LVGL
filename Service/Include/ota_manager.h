/**
  ******************************************************************************
  * @file    ota_manager.h
  * @brief   STM32F407 工业级无线空中升级 (OTA) 管理器与双区容灾协议引擎
  * @note    存储载体: W25Q64 外部 SPI NOR Flash (8MB)
  *          - 0x400000 ~ 0x47FFFF (512KB): 黄金固件容灾备份区 (OTA_FLASH_BACKUP_ADDR)
  *          - 0x480000 ~ 0x4FFFFF (512KB): 新固件分片下载暂存区 (OTA_FLASH_STAGING_ADDR)
  *          - RTC 备份寄存器: DR6 ~ DR9 (严格隔离，杜绝与 history_storage DR2~5 冲突)
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
#define OTA_MAX_FIRMWARE_SIZE       (384U * 1024U) /* 单片机当前 App 最大规格 (393,216 字节) */

/* ============================================================================
   STM32 RTC 备份域寄存器分工 (DR6 ~ DR9 专供 OTA，DR2~DR5 专供 History)
   ============================================================================ */
#define OTA_RTC_REG_MAGIC           RTC_BKP_DR6  /* OTA 控制与生命周期魔数 */
#define OTA_RTC_REG_TRIAL_COUNT     RTC_BKP_DR7  /* 试运行失败计数器 (崩溃超3次自动回滚) */
#define OTA_RTC_REG_FW_SIZE         RTC_BKP_DR8  /* 目标固件字节长度 */
#define OTA_RTC_REG_FW_CRC          RTC_BKP_DR9  /* 目标固件全量期望 CRC32 */

/* OTA 状态与生命周期魔数定义 */
#define OTA_MAGIC_UPGRADE_REQ       0x5A5A5A5AU  /* 升级就绪魔数: 固件已完整入库并通过 CRC 终审 */
#define OTA_MAGIC_TRIAL_BOOT        0x7E7E7E7EU  /* 试运行模式魔数: Bootloader 已搬运，新 App 试跑 */
#define OTA_MAGIC_CONFIRMED         0x00000000U  /* 升级转正确认魔数: 新 App 运行稳定 30s 正式提交 */
#define OTA_MAGIC_ROLLBACK_REQ      0xA5A5A5A5U  /* 紧急回滚魔数: 试运行崩溃，恢复 W25Q64 黄金备份 */

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
