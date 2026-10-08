/**
  ******************************************************************************
  * @file    ota_manager.c
  * @brief   STM32F407 无线空中升级 (OTA) 暂存区管理器与协议引擎实现
  ******************************************************************************
  */

#include "ota_manager.h"
#include "stm32f4xx_flash.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_rtc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
   模块内部静态状态与上下文
   ============================================================================ */
static ota_state_t s_ota_state = OTA_STATE_IDLE;
static uint32_t    s_fw_size = 0;
static uint32_t    s_expected_crc = 0;
static uint32_t    s_written_bytes = 0;

/* 4 字节未对齐尾部缓冲区 */
static uint8_t     s_rem_buf[4];
static uint8_t     s_rem_len = 0;

/* ============================================================================
   标准 IEEE 802.3 CRC32 软件计算函数
   ============================================================================ */
static uint32_t OTA_CalcCRC32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
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
   初始化
   ============================================================================ */
void OTA_Manager_Init(void)
{
    s_ota_state = OTA_STATE_IDLE;
    s_fw_size = 0;
    s_expected_crc = 0;
    s_written_bytes = 0;
    s_rem_len = 0;
}

ota_state_t OTA_GetState(void)
{
    return s_ota_state;
}

uint32_t OTA_GetProgress(void)
{
    if (s_fw_size == 0) return 0;
    uint32_t progress = (s_written_bytes * 100U) / s_fw_size;
    return (progress > 100U) ? 100U : progress;
}

/* ============================================================================
   启动 OTA：参数校验与暂存区扇区擦除 (Sector 8~10)
   ============================================================================ */
bool OTA_Start(uint32_t total_size, uint32_t expected_crc)
{
    if (total_size < 4096U || total_size > OTA_MAX_FIRMWARE_SIZE)
    {
        printf("[OTA] Error: Invalid firmware size %u bytes!\r\n", total_size);
        s_ota_state = OTA_STATE_ERROR;
        return false;
    }

    printf("[OTA] Initializing OTA session: Size=%u, CRC=0x%08X\r\n", total_size, expected_crc);

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* 擦除 Sector 8 (128 KB, 0x08080000 ~ 0x0809FFFF) */
    printf("[OTA] Erasing Staging Sector 8...\r\n");
    if (FLASH_EraseSector(FLASH_Sector_8, VoltageRange_3) != FLASH_COMPLETE)
    {
        FLASH_Lock();
        printf("[OTA] Erase Sector 8 failed!\r\n");
        s_ota_state = OTA_STATE_ERROR;
        return false;
    }

    /* 若大于 128KB，擦除 Sector 9 (128 KB, 0x080A0000 ~ 0x080BFFFF) */
    if (total_size > (128U * 1024U))
    {
        printf("[OTA] Erasing Staging Sector 9...\r\n");
        if (FLASH_EraseSector(FLASH_Sector_9, VoltageRange_3) != FLASH_COMPLETE)
        {
            FLASH_Lock();
            printf("[OTA] Erase Sector 9 failed!\r\n");
            s_ota_state = OTA_STATE_ERROR;
            return false;
        }
    }

    /* 若大于 256KB，擦除 Sector 10 (128 KB, 0x080C0000 ~ 0x080DFFFF) */
    if (total_size > (256U * 1024U))
    {
        printf("[OTA] Erasing Staging Sector 10...\r\n");
        if (FLASH_EraseSector(FLASH_Sector_10, VoltageRange_3) != FLASH_COMPLETE)
        {
            FLASH_Lock();
            printf("[OTA] Erase Sector 10 failed!\r\n");
            s_ota_state = OTA_STATE_ERROR;
            return false;
        }
    }

    FLASH_Lock();

    s_fw_size       = total_size;
    s_expected_crc  = expected_crc;
    s_written_bytes = 0;
    s_rem_len       = 0;
    s_ota_state     = OTA_STATE_RECEIVING;

    printf("[OTA] Staging sectors ready. Waiting for firmware chunks...\r\n");
    return true;
}

/* ============================================================================
   分片写入暂存区 (支持任意对齐与字节长度)
   ============================================================================ */
bool OTA_WriteChunk(const uint8_t *data, uint16_t len)
{
    if (s_ota_state != OTA_STATE_RECEIVING)
    {
        return false;
    }

    if ((s_written_bytes + s_rem_len + len) > s_fw_size)
    {
        printf("[OTA] Error: Firmware overflow! Total would exceed %u bytes\r\n", s_fw_size);
        s_ota_state = OTA_STATE_ERROR;
        return false;
    }

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    uint16_t in_idx = 0;

    /* 1. 若上次遗留不足 4 字节，先补齐并写入 */
    while (s_rem_len > 0 && s_rem_len < 4 && in_idx < len)
    {
        s_rem_buf[s_rem_len++] = data[in_idx++];
        if (s_rem_len == 4)
        {
            uint32_t word = ((uint32_t)s_rem_buf[0]) |
                            (((uint32_t)s_rem_buf[1]) << 8) |
                            (((uint32_t)s_rem_buf[2]) << 16) |
                            (((uint32_t)s_rem_buf[3]) << 24);

            if (FLASH_ProgramWord(OTA_STAGING_BASE_ADDR + s_written_bytes, word) != FLASH_COMPLETE)
            {
                FLASH_Lock();
                printf("[OTA] Flash write error at 0x%08X!\r\n", OTA_STAGING_BASE_ADDR + s_written_bytes);
                s_ota_state = OTA_STATE_ERROR;
                return false;
            }
            s_written_bytes += 4;
            s_rem_len = 0;
        }
    }

    /* 2. 批量写入完整 4 字节 Word */
    while ((in_idx + 4) <= len)
    {
        uint32_t word = ((uint32_t)data[in_idx]) |
                        (((uint32_t)data[in_idx + 1]) << 8) |
                        (((uint32_t)data[in_idx + 2]) << 16) |
                        (((uint32_t)data[in_idx + 3]) << 24);

        if (FLASH_ProgramWord(OTA_STAGING_BASE_ADDR + s_written_bytes, word) != FLASH_COMPLETE)
        {
            FLASH_Lock();
            printf("[OTA] Flash write error at 0x%08X!\r\n", OTA_STAGING_BASE_ADDR + s_written_bytes);
            s_ota_state = OTA_STATE_ERROR;
            return false;
        }
        s_written_bytes += 4;
        in_idx += 4;
    }

    /* 3. 剩余不满 4 字节的数据存入 s_rem_buf 等待下一包 */
    while (in_idx < len)
    {
        s_rem_buf[s_rem_len++] = data[in_idx++];
    }

    FLASH_Lock();
    return true;
}

/* ============================================================================
   结束传输并校验 CRC32，成功后置位 RTC 升级标志
   ============================================================================ */
bool OTA_Finish(void)
{
    if (s_ota_state != OTA_STATE_RECEIVING)
    {
        return false;
    }

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* 若末尾有不足 4 字节的数据，填充 0xFF 后写入 */
    if (s_rem_len > 0)
    {
        while (s_rem_len < 4)
        {
            s_rem_buf[s_rem_len++] = 0xFF;
        }
        uint32_t word = ((uint32_t)s_rem_buf[0]) |
                        (((uint32_t)s_rem_buf[1]) << 8) |
                        (((uint32_t)s_rem_buf[2]) << 16) |
                        (((uint32_t)s_rem_buf[3]) << 24);

        if (FLASH_ProgramWord(OTA_STAGING_BASE_ADDR + s_written_bytes, word) != FLASH_COMPLETE)
        {
            FLASH_Lock();
            s_ota_state = OTA_STATE_ERROR;
            return false;
        }
        s_written_bytes += s_rem_len;
        s_rem_len = 0;
    }

    FLASH_Lock();

    printf("[OTA] All chunks received. Total bytes: %u. Verifying CRC32...\r\n", s_fw_size);
    s_ota_state = OTA_STATE_VERIFYING;

    /* 校验暂存区中完整固件数据的 CRC32 */
    uint32_t calc_crc = OTA_CalcCRC32((const uint8_t *)OTA_STAGING_BASE_ADDR, s_fw_size);

    if (calc_crc != s_expected_crc)
    {
        printf("[OTA] CRC MISMATCH! Calculated: 0x%08X, Expected: 0x%08X\r\n", calc_crc, s_expected_crc);
        s_ota_state = OTA_STATE_ERROR;
        return false;
    }

    printf("[OTA] CRC VERIFIED SUCCESSFULLY (0x%08X)! Setting RTC flag...\r\n", calc_crc);

    /* 使能 RTC 备份域写入权限并置位标志 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    RTC_WriteBackupRegister(RTC_BKP_DR2, OTA_UPGRADE_REQ_MAGIC);
    RTC_WriteBackupRegister(RTC_BKP_DR3, s_fw_size);
    RTC_WriteBackupRegister(RTC_BKP_DR4, s_expected_crc);

    s_ota_state = OTA_STATE_READY_TO_REBOOT;
    printf("[OTA] Staging image ready. Ready to reboot into Bootloader!\r\n");
    return true;
}

void OTA_Abort(void)
{
    printf("[OTA] OTA session aborted.\r\n");
    s_ota_state = OTA_STATE_IDLE;
    s_fw_size = 0;
    s_expected_crc = 0;
    s_written_bytes = 0;
    s_rem_len = 0;
}

/* ============================================================================
   协议消息解析与处理 (对接 ESP32 8080 TCP Server)
   ============================================================================ */
bool OTA_ProcessMessage(const uint8_t *payload, uint16_t len, char *resp_buf, uint16_t resp_max_len)
{
    if (payload == NULL || len == 0 || resp_buf == NULL)
    {
        return false;
    }

    /* 1. OTA_START:<size>:<crc_hex> */
    if (len >= 10 && strncmp((const char *)payload, "OTA_START:", 10) == 0)
    {
        uint32_t size = 0;
        uint32_t crc = 0;
        if (sscanf((const char *)payload + 10, "%u:%x", &size, &crc) == 2)
        {
            if (OTA_Start(size, crc))
            {
                snprintf(resp_buf, resp_max_len, "OTA_OK:READY\r\n");
            }
            else
            {
                snprintf(resp_buf, resp_max_len, "OTA_ERR:START_FAILED\r\n");
            }
            return true;
        }
    }

    /* 2. OTA_DATA:<chunk_len>\n<binary_payload> */
    if (len >= 9 && strncmp((const char *)payload, "OTA_DATA:", 9) == 0)
    {
        const char *newline = strchr((const char *)payload, '\n');
        if (newline != NULL)
        {
            uint16_t chunk_len = (uint16_t)atoi((const char *)payload + 9);
            const uint8_t *bin_start = (const uint8_t *)newline + 1;
            uint16_t actual_bin_len = len - (uint16_t)(bin_start - payload);

            if (actual_bin_len >= chunk_len)
            {
                if (OTA_WriteChunk(bin_start, chunk_len))
                {
                    snprintf(resp_buf, resp_max_len, "OTA_OK:%u\r\n", s_written_bytes);
                }
                else
                {
                    snprintf(resp_buf, resp_max_len, "OTA_ERR:WRITE_FAILED\r\n");
                }
                return true;
            }
        }
    }

    /* 3. OTA_END */
    if (len >= 7 && strncmp((const char *)payload, "OTA_END", 7) == 0)
    {
        if (OTA_Finish())
        {
            snprintf(resp_buf, resp_max_len, "OTA_OK:VERIFIED\r\n");
        }
        else
        {
            snprintf(resp_buf, resp_max_len, "OTA_ERR:CRC_FAILED\r\n");
        }
        return true;
    }

    /* 4. OTA_REBOOT */
    if (len >= 10 && strncmp((const char *)payload, "OTA_REBOOT", 10) == 0)
    {
        if (s_ota_state == OTA_STATE_READY_TO_REBOOT)
        {
            snprintf(resp_buf, resp_max_len, "OTA_OK:REBOOTING\r\n");
            return true;
        }
        else
        {
            snprintf(resp_buf, resp_max_len, "OTA_ERR:NOT_READY\r\n");
            return true;
        }
    }

    /* 5. OTA_ABORT */
    if (len >= 9 && strncmp((const char *)payload, "OTA_ABORT", 9) == 0)
    {
        OTA_Abort();
        snprintf(resp_buf, resp_max_len, "OTA_OK:ABORTED\r\n");
        return true;
    }

    return false;
}
