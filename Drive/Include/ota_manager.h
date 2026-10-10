/**
  ******************************************************************************
  * @file    ota_manager.h
  * @brief   STM32F407 无线空中升级 (OTA) 暂存区管理器与协议引擎
  ******************************************************************************
  */

#ifndef __OTA_MANAGER_H__
#define __OTA_MANAGER_H__

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
   Flash 分区与魔数常量定义
   ============================================================================ */
#define OTA_STAGING_BASE_ADDR   0x08080000U  /* OTA 镜像暂存区: Sector 8~10 (384 KB) */
#define OTA_MAX_FIRMWARE_SIZE   (384U * 1024U) /* 393,216 字节 */
#define OTA_UPGRADE_REQ_MAGIC   0x5A5A5A5AU  /* 升级触发魔数 (写入 RTC_BKP_DR2) */

typedef enum
{
    OTA_STATE_IDLE = 0,
    OTA_STATE_RECEIVING,
    OTA_STATE_VERIFYING,
    OTA_STATE_READY_TO_REBOOT,
    OTA_STATE_ERROR
} ota_state_t;

/* ============================================================================
   核心 API 声明
   ============================================================================ */
void        OTA_Manager_Init(void);
ota_state_t OTA_GetState(void);
uint32_t    OTA_GetProgress(void); /* 0 ~ 100 % */

bool        OTA_Start(uint32_t total_size, uint32_t expected_crc, bool enable_aes);
bool        OTA_WriteChunk(const uint8_t *data, uint16_t len);
bool        OTA_Finish(void);
void        OTA_Abort(void);

/* 处理来自 ESP32 TCP Server 的 OTA 命令与数据流 */
bool        OTA_ProcessMessage(const uint8_t *payload, uint16_t len, char *resp_buf, uint16_t resp_max_len);

#endif /* __OTA_MANAGER_H__ */
