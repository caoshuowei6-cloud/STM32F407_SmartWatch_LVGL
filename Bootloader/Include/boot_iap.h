/**
  ******************************************************************************
  * @file    boot_iap.h
  * @brief   STM32F407 独立轻量级 Bootloader 核心跳转与 W25Q64 外部 Flash 固件搬运头文件
  *          支持企业级双区容灾 (黄金备份区 + 分片暂存区)、CRC32 终审校验与试运行防变砖
  ******************************************************************************
  */

#ifndef __BOOT_IAP_H__
#define __BOOT_IAP_H__

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
   Flash 物理扇区拓扑定义 (STM32F407ZGT6 1MB 片内 Flash)
   ============================================================================ */
#define BOOT_ADDR                   0x08000000U  /* Bootloader 分区: Sector 0~4 (128 KB) */
#define APP_ADDR                    0x08020000U  /* APP 现行运行区基地址: Sector 5~7 (384 KB) */
#define APP_MAX_SIZE                (384U * 1024U) /* 393,216 字节 (App 最大规格) */

/* ============================================================================
   W25Q64 外部 SPI NOR Flash 双区容灾基地址 (8MB 外部 Flash)
   ============================================================================ */
#define W25Q_BACKUP_BASE_ADDR       0x400000U    /* 黄金固件容灾备份区物理基地址 (512 KB) */
#define W25Q_STAGING_BASE_ADDR      0x480000U    /* 新固件分片下载暂存区物理基地址 (512 KB) */

/* ============================================================================
   RTC 备份域生命周期控制寄存器 (严格与 App 侧 ota_manager.h 保持 100% 绝对一致)
   ============================================================================ */
#define BOOT_RTC_REG_MAGIC          RTC_BKP_DR6  /* OTA 控制与生命周期魔数 */
#define BOOT_RTC_REG_TRIAL_COUNT    RTC_BKP_DR7  /* 试运行失败计数器 (崩溃超3次自动回滚) */
#define BOOT_RTC_REG_FW_SIZE        RTC_BKP_DR8  /* 目标固件字节长度 */
#define BOOT_RTC_REG_FW_CRC         RTC_BKP_DR9  /* 目标固件全量期望 CRC32 */

#define OTA_MAGIC_UPGRADE_REQ       0x5A5A5A5AU  /* 升级就绪魔数: 固件已完整入库并通过 CRC 终审 */
#define OTA_MAGIC_TRIAL_BOOT        0x7E7E7E7EU  /* 试运行模式魔数: Bootloader 已搬运，新 App 试跑 */
#define OTA_MAGIC_CONFIRMED         0x00000000U  /* 升级转正确认魔数: 新 App 运行稳定 30s 正式提交 */
#define OTA_MAGIC_ROLLBACK_REQ      0xA5A5A5A5U  /* 紧急回滚魔数: 试运行崩溃，恢复 W25Q64 黄金备份 */

/* ============================================================================
   W25Q64 极简轻量级 SPI1 硬件引脚定义 (无中断、无 DMA、纯寄存器/库函数轮询)
   ============================================================================ */
#define W25Q_SPI                    SPI1
#define W25Q_SPI_CLK                RCC_APB2Periph_SPI1
#define W25Q_SPI_PORT_CLK           RCC_AHB1Periph_GPIOA
#define W25Q_CS_CLK                 RCC_AHB1Periph_GPIOA
#define W25Q_CS_PORT                GPIOA
#define W25Q_CS_PIN                 GPIO_Pin_4
#define W25Q_SPI_PORT               GPIOA
#define W25Q_PIN_SCK                GPIO_Pin_5
#define W25Q_PIN_MISO               GPIO_Pin_6
#define W25Q_PIN_MOSI               GPIO_Pin_7
#define W25Q_AF_SPI                 GPIO_AF_SPI1

#define W25Q_CS_LOW()               GPIO_ResetBits(W25Q_CS_PORT, W25Q_CS_PIN)
#define W25Q_CS_HIGH()              GPIO_SetBits(W25Q_CS_PORT, W25Q_CS_PIN)

/* ============================================================================
   核心 API 声明
   ============================================================================ */
bool     Boot_IsAppValid(uint32_t app_addr);
void     Boot_JumpToApp(uint32_t app_addr);
uint32_t Boot_CalculateCRC32_Internal(uint32_t addr, uint32_t len);
uint32_t Boot_CalculateCRC32_W25Q(uint32_t w25q_addr, uint32_t len);
bool     Boot_EraseAppSectors(uint32_t size);
bool     Boot_CopyFromW25QToApp(uint32_t w25q_src_addr, uint32_t size);
void     Boot_CheckAndUpdate(void);

/* W25Q64 底层极简轮询驱动 */
bool     Boot_W25Q_Init(void);
uint32_t Boot_W25Q_ReadID(void);
void     Boot_W25Q_Read(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead);

#endif /* __BOOT_IAP_H__ */
