/**
  ******************************************************************************
  * @file    boot_iap.c
  * @brief   STM32F407 独立轻量级 Bootloader 核心跳转与 W25Q64 外部 Flash 搬运实现
  *          支持 W25Q64 硬件 SPI1 轮询读取、片内 Flash 扇区擦写、CRC32 双重校验与试运行回滚
  ******************************************************************************
  */

#include "boot_iap.h"
#include "stm32f4xx_flash.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_rtc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_spi.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
   W25Q64 底层极简硬件 SPI1 轮询驱动 (零中断、零 DMA、高内聚)
   ============================================================================ */
static uint8_t Boot_W25Q_ReadWriteByte(uint8_t tx_data)
{
    uint32_t timeout = 50000;
    while (SPI_I2S_GetFlagStatus(W25Q_SPI, SPI_FLAG_TXE) == RESET)
    {
        if (--timeout == 0) return 0xFF;
    }
    SPI_I2S_SendData(W25Q_SPI, tx_data);

    timeout = 50000;
    while (SPI_I2S_GetFlagStatus(W25Q_SPI, SPI_FLAG_RXNE) == RESET)
    {
        if (--timeout == 0) return 0xFF;
    }
    return (uint8_t)SPI_I2S_ReceiveData(W25Q_SPI);
}

bool Boot_W25Q_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef  SPI_InitStructure;

    /* 1. 使能 GPIOA 与 SPI1 时钟 */
    RCC_AHB1PeriphClockCmd(W25Q_CS_CLK | W25Q_SPI_PORT_CLK, ENABLE);
    RCC_APB2PeriphClockCmd(W25Q_SPI_CLK, ENABLE);

    /* 2. 配置片选引脚 CS (PA4): 普通推挽输出，默认拉高 */
    GPIO_InitStructure.GPIO_Pin   = W25Q_CS_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(W25Q_CS_PORT, &GPIO_InitStructure);
    W25Q_CS_HIGH();

    /* 3. 配置 SPI1 引脚 SCK (PA5), MISO (PA6), MOSI (PA7): 复用 AF5 */
    GPIO_PinAFConfig(W25Q_SPI_PORT, GPIO_PinSource5, W25Q_AF_SPI);
    GPIO_PinAFConfig(W25Q_SPI_PORT, GPIO_PinSource6, W25Q_AF_SPI);
    GPIO_PinAFConfig(W25Q_SPI_PORT, GPIO_PinSource7, W25Q_AF_SPI);

    GPIO_InitStructure.GPIO_Pin   = W25Q_PIN_SCK | W25Q_PIN_MISO | W25Q_PIN_MOSI;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(W25Q_SPI_PORT, &GPIO_InitStructure);

    /* 4. 配置 SPI1 主机模式: Mode 3 (CPOL=1, CPHA=1), 8位全双工, 分频系数 4 (21MHz) */
    SPI_I2S_DeInit(W25Q_SPI);
    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(W25Q_SPI, &SPI_InitStructure);

    SPI_Cmd(W25Q_SPI, ENABLE);

    /* 发送空字节拉齐总线 */
    Boot_W25Q_ReadWriteByte(0xFF);

    uint32_t id = Boot_W25Q_ReadID();
    if (id == 0xEF4017U)
    {
        printf("[BOOT W25Q] SPI Flash Online (JEDEC ID: 0xEF4017, Winbond 8MB).\r\n");
        return true;
    }
    else
    {
        printf("[BOOT W25Q] Warning: SPI Flash ID mismatch (Got: 0x%06X, Expected: 0xEF4017)!\r\n", id);
        return false;
    }
}

uint32_t Boot_W25Q_ReadID(void)
{
    uint32_t id = 0;
    W25Q_CS_LOW();
    Boot_W25Q_ReadWriteByte(0x9FU); /* Jedec ID 命令 */
    id |= (uint32_t)Boot_W25Q_ReadWriteByte(0xFF) << 16;
    id |= (uint32_t)Boot_W25Q_ReadWriteByte(0xFF) << 8;
    id |= (uint32_t)Boot_W25Q_ReadWriteByte(0xFF);
    W25Q_CS_HIGH();
    return id;
}

void Boot_W25Q_Read(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead)
{
    W25Q_CS_LOW();
    Boot_W25Q_ReadWriteByte(0x03U); /* Read Data 指令 */
    Boot_W25Q_ReadWriteByte((uint8_t)((ReadAddr) >> 16));
    Boot_W25Q_ReadWriteByte((uint8_t)((ReadAddr) >> 8));
    Boot_W25Q_ReadWriteByte((uint8_t)ReadAddr);

    while (NumByteToRead--)
    {
        *pBuffer++ = Boot_W25Q_ReadWriteByte(0xFF);
    }
    W25Q_CS_HIGH();
}

/* ============================================================================
   标准 IEEE 802.3 CRC32 计算函数 (与 App 及 Python 保持绝对一致)
   ============================================================================ */
uint32_t Boot_CalculateCRC32_Internal(uint32_t addr, uint32_t len)
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

uint32_t Boot_CalculateCRC32_W25Q(uint32_t w25q_addr, uint32_t len)
{
    uint8_t buf[256];
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t remaining = len;
    uint32_t curr_addr = w25q_addr;

    while (remaining > 0)
    {
        uint16_t chunk = (uint16_t)((remaining > sizeof(buf)) ? sizeof(buf) : remaining);
        Boot_W25Q_Read(buf, curr_addr, chunk);

        for (uint16_t i = 0; i < chunk; i++)
        {
            crc ^= buf[i];
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

        curr_addr += chunk;
        remaining -= chunk;
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
   APP 运行区扇区擦除 (Sector 5~7, 按需擦除)
   ============================================================================ */
bool Boot_EraseAppSectors(uint32_t size)
{
    FLASH_Status status;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* 基础擦除：Sector 5 (128 KB, 0x08020000 ~ 0x0803FFFF) */
    printf("[BOOT] Erasing Flash Sector 5 (128KB)...\r\n");
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
        printf("[BOOT] Erasing Flash Sector 6 (128KB)...\r\n");
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
        printf("[BOOT] Erasing Flash Sector 7 (128KB)...\r\n");
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
   从 W25Q64 流式搬运至片内 Flash (含逐字回读实时比对)
   ============================================================================ */
bool Boot_CopyFromW25QToApp(uint32_t w25q_src_addr, uint32_t size)
{
    FLASH_Status status;
    uint8_t  chunk_buf[256];
    uint32_t copied = 0;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    printf("[BOOT] Copying %u bytes from W25Q64 0x%06X to App Flash 0x%08X...\r\n",
           size, w25q_src_addr, APP_ADDR);

    while (copied < size)
    {
        uint16_t chunk_len = (uint16_t)(((size - copied) > sizeof(chunk_buf)) ? sizeof(chunk_buf) : (size - copied));
        Boot_W25Q_Read(chunk_buf, w25q_src_addr + copied, chunk_len);

        /* 填充尾部不足 4 字节为 0xFF (擦除态) */
        uint16_t word_count = (chunk_len + 3U) / 4U;
        if ((chunk_len % 4U) != 0)
        {
            memset(chunk_buf + chunk_len, 0xFF, (word_count * 4U) - chunk_len);
        }

        /* 逐字烧写 */
        for (uint16_t i = 0; i < word_count; i++)
        {
            uint32_t word_data;
            memcpy(&word_data, chunk_buf + (i * 4U), 4);
            uint32_t dst_addr = APP_ADDR + copied + (i * 4U);

            status = FLASH_ProgramWord(dst_addr, word_data);
            if (status != FLASH_COMPLETE)
            {
                FLASH_Lock();
                printf("[BOOT] ProgramWord error at 0x%08X! Status: %d\r\n", dst_addr, status);
                return false;
            }

            /* 即时回读比对 */
            if (*(__IO uint32_t *)dst_addr != word_data)
            {
                FLASH_Lock();
                printf("[BOOT] Verify mismatch at 0x%08X (Wrote: 0x%08X, Read: 0x%08X)!\r\n",
                       dst_addr, word_data, *(__IO uint32_t *)dst_addr);
                return false;
            }
        }

        copied += chunk_len;
    }

    FLASH_Lock();
    printf("[BOOT] Firmware stream copy and byte-level verification complete.\r\n");
    return true;
}

/* ============================================================================
   纯汇编原子跳转函数 (彻底消除切换 MSP 后编译器访问旧栈帧的崩栈风险)
   ============================================================================ */
__asm void Boot_JumpToAppAsm(uint32_t app_msp, uint32_t jump_pc)
{
    MSR MSP, r0
    BX r1
}

/* ============================================================================
   严谨的 Cortex-M4 应用程序安全跳转
   ============================================================================ */
void Boot_JumpToApp(uint32_t app_addr)
{
    if (!Boot_IsAppValid(app_addr))
    {
        printf("[BOOT] Error: App at 0x%08X is invalid! Jump aborted.\r\n", app_addr);
        return;
    }

    printf("[BOOT] Jumping to APP (0x%08X)...\r\n\r\n", app_addr);

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

    /* 4. 指令与内存同步屏障 */
    __DSB();
    __ISB();

    /* 5. 中断向量表重映射到目标 APP 起始地址 */
    SCB->VTOR = app_addr;

    /* 6. 取出 APP 栈顶与 Reset_Handler 入口地址 */
    uint32_t app_msp = *(__IO uint32_t *)app_addr;
    uint32_t jump_pc = *(__IO uint32_t *)(app_addr + 4U);

    /* 7. 使用纯汇编原子执行 MSP 切换与分支跳转 */
    Boot_JumpToAppAsm(app_msp, jump_pc);

    /* 正常流程不会执行到此处 */
    while (1)
    {
    }
}

/* ============================================================================
   开机自检与企业级双区 OTA 状态机 (升级搬运 / 试运行守护 / 崩溃自动回滚)
   ============================================================================ */
void Boot_CheckAndUpdate(void)
{
    /* 1. 开启电源与备份域时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* 2. 读取 RTC_BKP_DR6 核心生命周期魔数 */
    uint32_t magic = RTC_ReadBackupRegister(BOOT_RTC_REG_MAGIC);

    /* 3. 初始化外部 SPI Flash (W25Q64) */
    bool w25q_ok = Boot_W25Q_Init();

    if (magic == OTA_MAGIC_UPGRADE_REQ)
    {
        uint32_t fw_size      = RTC_ReadBackupRegister(BOOT_RTC_REG_FW_SIZE);
        uint32_t expected_crc = RTC_ReadBackupRegister(BOOT_RTC_REG_FW_CRC);

        printf("\r\n=======================================================\r\n");
        printf("   [BOOT] UPGRADE REQUEST DETECTED (DR6=0x5A5A5A5A)!     \r\n");
        printf("   Firmware Size: %u bytes (%u KB)                     \r\n", fw_size, fw_size / 1024U);
        printf("   Expected CRC : 0x%08X                               \r\n", expected_crc);
        printf("=======================================================\r\n");

        if (!w25q_ok || fw_size < 4096U || fw_size > APP_MAX_SIZE)
        {
            printf("[BOOT] Error: W25Q offline or illegal size (%u B)! Aborting upgrade.\r\n", fw_size);
            RTC_WriteBackupRegister(BOOT_RTC_REG_MAGIC, 0);
        }
        else
        {
            /* 步骤 1: 校验 W25Q64 暂存区镜像的完整性 */
            printf("[BOOT] Verifying W25Q64 Staging Area CRC32 (0x%06X)...\r\n", W25Q_STAGING_BASE_ADDR);
            uint32_t staging_crc = Boot_CalculateCRC32_W25Q(W25Q_STAGING_BASE_ADDR, fw_size);

            if (staging_crc != expected_crc)
            {
                printf("[BOOT] CRC MISMATCH! Computed: 0x%08X, Expected: 0x%08X\r\n", staging_crc, expected_crc);
                printf("[BOOT] Upgrade CANCELLED. Staging image is corrupted or incomplete!\r\n");
                RTC_WriteBackupRegister(BOOT_RTC_REG_MAGIC, 0);
            }
            else
            {
                printf("[BOOT] Staging CRC32 100%% VERIFIED (0x%08X)!\r\n", staging_crc);

                /* 步骤 2: 擦除片内 Flash APP 扇区 (Sector 5~7) */
                if (!Boot_EraseAppSectors(fw_size))
                {
                    printf("[BOOT] Critical: App sector erase failed! Upgrade aborted.\r\n");
                }
                else
                {
                    /* 步骤 3: 从 W25Q64 暂存区搬运至片内 Flash */
                    if (!Boot_CopyFromW25QToApp(W25Q_STAGING_BASE_ADDR, fw_size))
                    {
                        printf("[BOOT] Critical: Firmware copy failed! Triggering golden recovery...\r\n");
                        Boot_CopyFromW25QToApp(W25Q_BACKUP_BASE_ADDR, fw_size);
                    }
                    else
                    {
                        /* 步骤 4: 校验新写入片内 Flash 的运行固件 CRC32 */
                        printf("[BOOT] Verifying newly installed App in internal Flash...\r\n");
                        uint32_t app_crc = Boot_CalculateCRC32_Internal(APP_ADDR, fw_size);

                        if (app_crc == expected_crc)
                        {
                            printf("\r\n=======================================================\r\n");
                            printf("   [BOOT] OTA UPGRADE SUCCESS! Installed to Flash.     \r\n");
                            printf("   Switching to TRIAL-BOOT MODE (30s trial protection). \r\n");
                            printf("=======================================================\r\n\r\n");

                            /* 置位试运行魔数与复位试运行计数器 */
                            RTC_WriteBackupRegister(BOOT_RTC_REG_MAGIC, OTA_MAGIC_TRIAL_BOOT);
                            RTC_WriteBackupRegister(BOOT_RTC_REG_TRIAL_COUNT, 0);
                        }
                        else
                        {
                            printf("[BOOT] Critical: App CRC mismatch (0x%08X != 0x%08X)! Rolling back...\r\n",
                                   app_crc, expected_crc);
                            Boot_EraseAppSectors(fw_size);
                            Boot_CopyFromW25QToApp(W25Q_BACKUP_BASE_ADDR, fw_size);
                            RTC_WriteBackupRegister(BOOT_RTC_REG_MAGIC, 0);
                        }
                    }
                }
            }
        }
    }
    else if (magic == OTA_MAGIC_ROLLBACK_REQ)
    {
        printf("\r\n=======================================================\r\n");
        printf("   [BOOT ALERT] EMERGENCY ROLLBACK REQUEST DETECTED!    \r\n");
        printf("   Trial boot failed repeatedly (>3 times).             \r\n");
        printf("   Restoring Golden Backup Firmware from W25Q 0x%06X... \r\n", W25Q_BACKUP_BASE_ADDR);
        printf("=======================================================\r\n");

        if (w25q_ok)
        {
            Boot_EraseAppSectors(APP_MAX_SIZE);
            Boot_CopyFromW25QToApp(W25Q_BACKUP_BASE_ADDR, APP_MAX_SIZE);
            RTC_WriteBackupRegister(BOOT_RTC_REG_MAGIC, OTA_MAGIC_CONFIRMED);
            RTC_WriteBackupRegister(BOOT_RTC_REG_TRIAL_COUNT, 0);
            printf("[BOOT] Golden Backup restored successfully! Device recovered.\r\n\r\n");
        }
    }
    else if (magic == OTA_MAGIC_TRIAL_BOOT)
    {
        uint32_t trial_cnt = RTC_ReadBackupRegister(BOOT_RTC_REG_TRIAL_COUNT);
        printf("[BOOT] System running in Trial-Boot Mode (Trial attempts: %u).\r\n", trial_cnt);
    }
    else
    {
        printf("[BOOT] Normal production boot (DR6 = 0x%08X).\r\n", magic);
    }

    /* 4. 执行跳转：若 App 有效则跳转；若因掉电受损则尝试黄金备份自动保活 */
    if (Boot_IsAppValid(APP_ADDR))
    {
        Boot_JumpToApp(APP_ADDR);
    }
    else
    {
        printf("[BOOT] Fatal: App vector at 0x%08X is invalid!\r\n", APP_ADDR);
        if (w25q_ok)
        {
            printf("[BOOT] Self-Healing: Attempting automatic Golden Backup restore from W25Q...\r\n");
            Boot_EraseAppSectors(APP_MAX_SIZE);
            Boot_CopyFromW25QToApp(W25Q_BACKUP_BASE_ADDR, APP_MAX_SIZE);
            if (Boot_IsAppValid(APP_ADDR))
            {
                printf("[BOOT] Self-Healing SUCCESS! Jumping to restored Golden App...\r\n");
                Boot_JumpToApp(APP_ADDR);
            }
        }

        printf("[BOOT] Recovery exhausted. Entering Bootloader safe standby loop.\r\n");
        while (1)
        {
        }
    }
}
