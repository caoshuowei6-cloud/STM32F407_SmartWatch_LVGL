/**
  ******************************************************************************
  * @file    boot_iap.h
  * @brief   STM32F407 独立轻量级 Bootloader 核心跳转与镜像校验搬运头文件
  ******************************************************************************
  */

#ifndef __BOOT_IAP_H__
#define __BOOT_IAP_H__

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
   Flash 物理扇区拓扑定义 (STM32F407ZGT6 1MB Flash)
   ============================================================================ */
#define BOOT_ADDR          0x08000000U  /* Bootloader 分区: Sector 0~4 (128 KB) */
#define APP_ADDR           0x08020000U  /* APP 现行运行区: Sector 5~7 (384 KB)   */
#define STAGING_ADDR       0x08080000U  /* OTA 镜像暂存区: Sector 8~10 (384 KB)  */
#define PARAMS_ADDR        0x080E0000U  /* 磨损均衡参数区: Sector 11 (128 KB)   */

#define APP_MAX_SIZE       (384U * 1024U) /* 393,216 字节 */

/* ============================================================================
   RTC 备份域升级标志定义 (无需擦写 Flash，抗意外掉电与磨损)
   ============================================================================ */
#define OTA_UPGRADE_MAGIC  0x5A5A5A5AU  /* 触发升级请求魔数 */

/* ============================================================================
   核心 API 声明
   ============================================================================ */
bool     Boot_IsAppValid(uint32_t app_addr);
void     Boot_JumpToApp(uint32_t app_addr);
uint32_t Boot_CalculateCRC32(uint32_t addr, uint32_t len);
bool     Boot_EraseAppSectors(uint32_t size);
bool     Boot_CopyStagingToApp(uint32_t size);
void     Boot_CheckAndUpdate(void);

#endif /* __BOOT_IAP_H__ */
