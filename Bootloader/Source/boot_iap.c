/**
  ******************************************************************************
  * @file    boot_iap.c
  * @brief   STM32F407 独立轻量级 Bootloader 核心跳转与固件校验搬运实现
  ******************************************************************************
  */

#include "boot_iap.h"
#include "stm32f4xx_flash.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_rtc.h"
#include <stdio.h>

/* ============================================================================
   标准 IEEE 802.3 CRC32 软件计算 (对齐 Python binascii.crc32 与通用 OTA 工具)
   ============================================================================ */
uint32_t Boot_CalculateCRC32(uint32_t addr, uint32_t len)
{
    const uint8_t *p_data = (const uint8_t *)addr;
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= p_data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* ============================================================================
   合法性检查：MSP 栈顶指针与 Reset_Handler PC 入口校验
   ============================================================================ */
bool Boot_IsAppValid(uint32_t app_addr)
{
    uint32_t sp = *(__IO uint32_t *)app_addr;
    uint32_t pc = *(__IO uint32_t *)(app_addr + 4);

    /* 1. MSP 必须严格落在 STM32F407 内部 SRAM (0x20000000 ~ 0x20020000, 128KB) */
    if (sp < 0x20000000U || sp > 0x20020000U)
    {
        return false;
    }

    /* 2. PC 入口必须落在 APP Flash 物理区间 (0x08020000 ~ 0x08080000) */
    if (pc < app_addr || pc >= (app_addr + APP_MAX_SIZE))
    {
        return false;
    }

    /* 3. Cortex-M 架构强制要求 Thumb 指令模式，PC 地址最低位 (LSB) 必须为 1 */
    if ((pc & 0x01U) == 0U)
    {
        return false;
    }

    return true;
}

/* ============================================================================
   APP 运行区扇区擦除 (Sector 5~7, 逐扇区按需擦除)
   ============================================================================ */
bool Boot_EraseAppSectors(uint32_t size)
{
    FLASH_Status status;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* 基础擦除：Sector 5 (128 KB, 0x08020000 ~ 0x0803FFFF) */
    printf("[BOOT] Erasing Sector 5 (128KB)...\r\n");
    status = FLASH_EraseSector(FLASH_Sector_5, VoltageRange_3);
    if (status != FLASH_COMPLETE)
    {
        FLASH_Lock();
        printf("[BOOT] Erase Sector 5 failed! Status: %d\r\n", status);
        return false;
    }

    /* 若固件大于 128KB，擦除 Sector 6 (128 KB, 0x08040000 ~ 0x0805FFFF) */
    if (size > (128U * 1024U))
    {
        printf("[BOOT] Erasing Sector 6 (128KB)...\r\n");
        status = FLASH_EraseSector(FLASH_Sector_6, VoltageRange_3);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            printf("[BOOT] Erase Sector 6 failed! Status: %d\r\n", status);
            return false;
        }
    }

    /* 若固件大于 256KB，擦除 Sector 7 (128 KB, 0x08060000 ~ 0x0807FFFF) */
    if (size > (256U * 1024U))
    {
        printf("[BOOT] Erasing Sector 7 (128KB)...\r\n");
        status = FLASH_EraseSector(FLASH_Sector_7, VoltageRange_3);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            printf("[BOOT] Erase Sector 7 failed! Status: %d\r\n", status);
            return false;
        }
    }

    FLASH_Lock();
    return true;
}

/* ============================================================================
   暂存区固件直拷搬运 (Zero-RAM Overhead Word Stream Copy)
   从 0x08080000 (Sector 8) 逐字烧写到 0x08020000 (Sector 5)，并即时回读比对
   ============================================================================ */
bool Boot_CopyStagingToApp(uint32_t size)
{
    uint32_t words_count = (size + 3U) / 4U;
    FLASH_Status status;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    printf("[BOOT] Copying %u bytes (%u words) from Staging to APP...\r\n", size, words_count);

    for (uint32_t i = 0; i < words_count; i++)
    {
        uint32_t src_word_addr = STAGING_ADDR + (i * 4U);
        uint32_t dst_word_addr = APP_ADDR + (i * 4U);
        uint32_t data = *(__IO uint32_t *)src_word_addr;

        status = FLASH_ProgramWord(dst_word_addr, data);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            printf("[BOOT] ProgramWord error at 0x%08X! Status: %d\r\n", dst_word_addr, status);
            return false;
        }

        /* 即时回读双重比对 */
        if (*(__IO uint32_t *)dst_word_addr != data)
        {
            FLASH_Lock();
            printf("[BOOT] Verify mismatch at 0x%08X (wrote 0x%08X, read 0x%08X)!\r\n",
                   dst_word_addr, data, *(__IO uint32_t *)dst_word_addr);
            return false;
        }
    }

    FLASH_Lock();
    printf("[BOOT] Staging-to-APP copy complete.\r\n");
    return true;
}

/* ============================================================================
   严谨的 Cortex-M4 应用程序安全跳转
   彻底清理 NVIC 中断挂起、关闭 SysTick、重设时钟并设置 VTOR 与 MSP
   ============================================================================ */
void Boot_JumpToApp(uint32_t app_addr)
{
    if (!Boot_IsAppValid(app_addr))
    {
        printf("[BOOT] Error: App at 0x%08X is invalid! Jump aborted.\r\n", app_addr);
        return;
    }

    printf("[BOOT] Preparing safe jump to APP (0x%08X)...\r\n", app_addr);

    /* 1. 全局关中断：防止跳转瞬间触发旧向量表 ISR */
    __disable_irq();

    /* 2. 彻底关闭并复位 SysTick 定时器 */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 3. 清除所有 NVIC 中断使能 (ICER) 与挂起标志 (ICPR) */
    for (uint8_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    /* 4. 复位 RCC 时钟树至出厂状态 (恢复 HSI 16MHz，关闭 PLL 与所有外设时钟) */
    RCC_DeInit();

    /* 5. 指令与内存同步屏障 */
    __DSB();
    __ISB();

    /* 6. 中断向量表重映射到目标 APP 起始地址 */
    SCB->VTOR = app_addr;

    /* 7. 重置主栈指针 MSP */
    __set_MSP(*(__IO uint32_t *)app_addr);

    /* 8. 取出 Reset_Handler 入口地址并执行跳转 */
    uint32_t jump_pc = *(__IO uint32_t *)(app_addr + 4U);
    void (*app_entry)(void) = (void (*)(void))jump_pc;
    app_entry();

    /* 正常流程不会执行到此处 */
    while (1)
    {
    }
}

/* ============================================================================
   开机自检与 OTA 升级判断状态机
   ============================================================================ */
void Boot_CheckAndUpdate(void)
{
    /* 使能电源与备份域时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    uint32_t upgrade_flag = RTC_ReadBackupRegister(RTC_BKP_DR2);

    if (upgrade_flag == OTA_UPGRADE_MAGIC)
    {
        uint32_t fw_size     = RTC_ReadBackupRegister(RTC_BKP_DR3);
        uint32_t expected_crc = RTC_ReadBackupRegister(RTC_BKP_DR4);

        printf("[BOOT] Upgrade flag DETECTED! Size=%u bytes, Expected CRC=0x%08X\r\n",
               fw_size, expected_crc);

        if (fw_size < 4096U || fw_size > APP_MAX_SIZE)
        {
            printf("[BOOT] Error: Firmware size %u is out of legal bounds! Aborting.\r\n", fw_size);
            RTC_WriteBackupRegister(RTC_BKP_DR2, 0);
        }
        else
        {
            /* 步骤 1: 校验暂存区镜像的完整性 */
            printf("[BOOT] Verifying Staging Area CRC32...\r\n");
            uint32_t staging_crc = Boot_CalculateCRC32(STAGING_ADDR, fw_size);

            if (staging_crc != expected_crc)
            {
                printf("[BOOT] Staging CRC MISMATCH! Got 0x%08X, Expected 0x%08X\r\n",
                       staging_crc, expected_crc);
                printf("[BOOT] Upgrade cancelled. Staging firmware corrupted!\r\n");
                RTC_WriteBackupRegister(RTC_BKP_DR2, 0);
            }
            else
            {
                printf("[BOOT] Staging CRC verified OK! Proceeding to Flash update.\r\n");

                /* 步骤 2: 擦除 APP 运行区扇区 */
                if (!Boot_EraseAppSectors(fw_size))
                {
                    printf("[BOOT] Critical: Flash erase failed! Upgrade aborted.\r\n");
                }
                else
                {
                    /* 步骤 3: 逐字直拷搬运 */
                    if (!Boot_CopyStagingToApp(fw_size))
                    {
                        printf("[BOOT] Critical: Firmware copy failed!\r\n");
                    }
                    else
                    {
                        /* 步骤 4: 校验新写入的 APP 运行区 CRC32 */
                        printf("[BOOT] Verifying newly installed APP CRC32...\r\n");
                        uint32_t app_crc = Boot_CalculateCRC32(APP_ADDR, fw_size);

                        if (app_crc == expected_crc)
                        {
                            printf("\r\n=======================================================\r\n");
                            printf("   [BOOT] OTA UPGRADE SUCCESS! Version installed.     \r\n");
                            printf("=======================================================\r\n\r\n");
                            /* 清除升级标志位，防止死循环重复升级 */
                            RTC_WriteBackupRegister(RTC_BKP_DR2, 0);
                        }
                        else
                        {
                            printf("[BOOT] Critical: Newly written APP CRC mismatch! Got 0x%08X\r\n", app_crc);
                        }
                    }
                }
            }
        }
    }
    else
    {
        printf("[BOOT] Normal boot (no upgrade flag).\r\n");
    }

    /* 最终安全跳转进 APP */
    if (Boot_IsAppValid(APP_ADDR))
    {
        Boot_JumpToApp(APP_ADDR);
    }
    else
    {
        printf("[BOOT] FATAL: No valid APP firmware found at 0x%08X!\r\n", APP_ADDR);
        printf("[BOOT] Entering Bootloader idle recovery loop.\r\n");
        while (1)
        {
            /* 保持在 Bootloader 安全状态 */
        }
    }
}
