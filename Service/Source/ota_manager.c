/**
  ******************************************************************************
  * @file    ota_manager.c
  * @brief   STM32F407 工业级无线空中升级 (OTA) 管理器与双区容灾协议引擎实现
  * @note    存储载体: W25Q64 外部 SPI NOR Flash (8MB)
  *          - 0x400000: 黄金固件备份区 (OTA_W25Q_BACKUP_BASE_ADDR)
  *          - 0x480000: 新固件分片暂存区 (OTA_W25Q_STAGING_BASE_ADDR)
  *          - RTC 备份域: DR6 ~ DR9 (严格独立隔离)
  ******************************************************************************
  */

#include "ota_manager.h"
#include "w25qxx.h"
#include "aes.h"
#include "stm32f4xx_flash.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_rtc.h"
#include "stm32f4xx_usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "delay.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
   模块内部静态状态与上下文 (含 AES-128-CBC 预共享加密密钥与向量)
   ============================================================================ */
static const uint8_t s_ota_aes_key[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

static const uint8_t s_ota_aes_iv[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

static struct AES_ctx s_aes_ctx;
static bool           s_aes_enabled   = false;
static ota_state_t    s_ota_state     = OTA_STATE_IDLE;
static uint32_t       s_fw_size       = 0;
static uint32_t       s_expected_crc  = 0;
static uint32_t       s_written_bytes = 0;

/* ============================================================================
   标准 IEEE 802.3 CRC32 软件计算函数
   ============================================================================ */
static uint32_t OTA_CalcCRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
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
    return crc;
}

/* ============================================================================
   初始化与自保状态探活
   ============================================================================ */
void OTA_Manager_Init(void)
{
    s_ota_state     = OTA_STATE_IDLE;
    s_fw_size       = 0;
    s_expected_crc  = 0;
    s_written_bytes = 0;
    s_aes_enabled   = false;

    /* 开启 PWR 时钟以访问 RTC 备份域 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    uint32_t magic = RTC_ReadBackupRegister(OTA_RTC_REG_MAGIC);
    uint32_t trial_cnt = RTC_ReadBackupRegister(OTA_RTC_REG_TRIAL_COUNT);

    if (magic == OTA_MAGIC_TRIAL_BOOT)
    {
        trial_cnt++;
        RTC_WriteBackupRegister(OTA_RTC_REG_TRIAL_COUNT, trial_cnt);
        printf("\r\n=======================================================\r\n");
        printf(" [OTA TRIAL BOOT] Device running in TRIAL MODE (Attempt %u/3)\r\n", trial_cnt);
        printf(" Awaiting stable 30s confirmation before committing to permanent.\r\n");
        printf("=======================================================\r\n\r\n");

        if (trial_cnt > 3)
        {
            printf("[OTA ROLLBACK] Warning: Trial boot failed > 3 times! Triggering golden rollback...\r\n");
            RTC_WriteBackupRegister(OTA_RTC_REG_MAGIC, OTA_MAGIC_ROLLBACK_REQ);
            OTA_SafeReboot();
        }
    }
    else if (magic == OTA_MAGIC_CONFIRMED)
    {
        /* 正常生产稳定固件运行状态 */
    }
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

uint32_t OTA_GetReceivedBytes(void)
{
    return s_written_bytes;
}

/* ============================================================================
   启动 OTA 会话：参数校验与 W25Q64 暂存区扇区快速擦除
   ============================================================================ */
bool OTA_Start(uint32_t total_size, uint32_t expected_crc, bool enable_aes)
{
    if (total_size < 4096U || total_size > OTA_MAX_FIRMWARE_SIZE)
    {
        printf("[OTA] Error: Invalid firmware size %u bytes (Allowed: 4KB ~ %uKB)!\r\n",
               total_size, OTA_MAX_FIRMWARE_SIZE / 1024U);
        s_ota_state = OTA_STATE_ERROR;
        return false;
    }

    /* 确认 W25Q64 物理硬件在线 */
    uint32_t flash_id = W25QXX_ReadID();
    if (flash_id != W25Q64_JEDEC_ID)
    {
        printf("[OTA] Error: W25Q64 ID mismatch (0x%06X != 0x%06X)! OTA halted.\r\n",
               flash_id, W25Q64_JEDEC_ID);
        s_ota_state = OTA_STATE_ERROR;
        return false;
    }

    printf("[OTA] Initializing Session: Size=%u B, CRC=0x%08X, Mode=%s\r\n",
           total_size, expected_crc, enable_aes ? "AES-128-CBC" : "Plaintext");
    printf("[OTA] Target Media: W25Q64 External SPI Flash (Staging Area: 0x%06X)\r\n",
           OTA_W25Q_STAGING_BASE_ADDR);

    /* 快速擦除暂存区 (优先 64KB 块擦除，余量 4KB 扇区擦除) */
    uint32_t erased = 0;
    while (erased < total_size)
    {
        uint32_t curr_addr = OTA_W25Q_STAGING_BASE_ADDR + erased;
        if (((total_size - erased) >= 65536U) && ((curr_addr % 65536U) == 0))
        {
            printf("[OTA] Erasing 64KB Block at 0x%06X...\r\n", curr_addr);
            W25QXX_Erase_Block(curr_addr);
            erased += 65536U;
        }
        else
        {
            W25QXX_Erase_Sector(curr_addr);
            erased += 4096U;
        }
    }

    s_fw_size       = total_size;
    s_expected_crc  = expected_crc;
    s_written_bytes = 0;
    s_aes_enabled   = enable_aes;
    s_ota_state     = OTA_STATE_RECEIVING;

    if (s_aes_enabled)
    {
        AES_init_ctx_iv(&s_aes_ctx, s_ota_aes_key, s_ota_aes_iv);
        printf("[OTA] AES-128-CBC decryptor active. Ready to stream chunks into W25Q64...\r\n");
    }
    else
    {
        printf("[OTA] Plaintext streamer active. Ready to stream chunks into W25Q64...\r\n");
    }

    return true;
}

/* ============================================================================
   分片流式写入 W25Q64 暂存区 (完全不占用片内 Flash 总线，支持 AES 在线解密)
   ============================================================================ */
bool OTA_WriteChunk(const uint8_t *data, uint16_t len)
{
    if (s_ota_state != OTA_STATE_RECEIVING || data == NULL || len == 0)
    {
        return false;
    }

    const uint8_t *src_data = data;
    uint16_t valid_len = len;
    uint8_t dec_buf[512];

    if (s_aes_enabled)
    {
        if (len > sizeof(dec_buf) || (len % 16) != 0)
        {
            printf("[OTA] Error: Chunk len %u must be <= 512 and 16-byte aligned for AES!\r\n", len);
            s_ota_state = OTA_STATE_ERROR;
            return false;
        }

        /* 在线 AES-128-CBC 解密 */
        memcpy(dec_buf, data, len);
        AES_CBC_decrypt_buffer(&s_aes_ctx, dec_buf, len);
        src_data = dec_buf;

        /* 计算本次解密后有效明文长度 (自动丢弃末尾 16 字节对齐的填充字节) */
        if ((s_written_bytes + valid_len) > s_fw_size)
        {
            valid_len = (uint16_t)(s_fw_size - s_written_bytes);
        }
    }
    else
    {
        if ((s_written_bytes + len) > s_fw_size)
        {
            printf("[OTA] Error: Firmware overflow! Current=%u + Chunk=%u > Total=%u\r\n",
                   s_written_bytes, len, s_fw_size);
            s_ota_state = OTA_STATE_ERROR;
            return false;
        }
        valid_len = len;
    }

    /* 直接写入 W25Q64 外部 SPI Flash 暂存区 */
    uint32_t write_addr = OTA_W25Q_STAGING_BASE_ADDR + s_written_bytes;
    W25QXX_Write((uint8_t *)src_data, write_addr, valid_len);
    s_written_bytes += valid_len;

    return true;
}

/* ============================================================================
   当前稳定运行固件黄金备份 (将片内 Flash App 镜像原封不动复制到 W25Q64 0x400000)
   ============================================================================ */
bool OTA_BackupCurrentApp(uint32_t app_size)
{
    if (app_size == 0 || app_size > OTA_MAX_FIRMWARE_SIZE)
    {
        app_size = OTA_MAX_FIRMWARE_SIZE;
    }

    printf("\r\n[OTA BACKUP] Backing up current running App (0x%08X, %u B) to W25Q64 0x%06X...\r\n",
           OTA_APP_BASE_ADDR, app_size, OTA_W25Q_BACKUP_BASE_ADDR);

    /* 1. 擦除 W25Q64 黄金备份区 */
    uint32_t erased = 0;
    while (erased < app_size)
    {
        uint32_t addr = OTA_W25Q_BACKUP_BASE_ADDR + erased;
        if (((app_size - erased) >= 65536U) && ((addr % 65536U) == 0))
        {
            W25QXX_Erase_Block(addr);
            erased += 65536U;
        }
        else
        {
            W25QXX_Erase_Sector(addr);
            erased += 4096U;
        }
    }

    /* 2. 块式读取片内 Flash 并写入 W25Q64 备份区 */
    uint8_t copy_buf[256];
    uint32_t copied = 0;
    while (copied < app_size)
    {
        uint16_t chunk = (uint16_t)(((app_size - copied) > sizeof(copy_buf)) ? sizeof(copy_buf) : (app_size - copied));
        memcpy(copy_buf, (const void *)(OTA_APP_BASE_ADDR + copied), chunk);
        W25QXX_Write(copy_buf, OTA_W25Q_BACKUP_BASE_ADDR + copied, chunk);
        copied += chunk;
    }

    printf("[OTA BACKUP] SUCCESS: %u bytes mirrored to golden backup zone!\r\n", copied);
    return true;
}

/* ============================================================================
   结束传输：全量 CRC32 终审校验 -> 触发黄金固件备份 -> 置位 RTC 升级标志
   ============================================================================ */
bool OTA_Finish(void)
{
    if (s_ota_state != OTA_STATE_RECEIVING)
    {
        return false;
    }

    if (s_written_bytes != s_fw_size)
    {
        printf("[OTA] Error: Firmware incomplete! Written %u / %u bytes.\r\n",
               s_written_bytes, s_fw_size);
        s_ota_state = OTA_STATE_ERROR;
        return false;
    }

    printf("[OTA] All chunks received (%u bytes). Reading back from W25Q64 for full CRC32...\r\n", s_fw_size);
    s_ota_state = OTA_STATE_VERIFYING;

    /* 从 W25Q64 暂存区流式读回并计算全局 CRC32 */
    uint8_t read_buf[256];
    uint32_t calc_crc = 0xFFFFFFFFU;
    uint32_t remaining = s_fw_size;
    uint32_t curr_read_addr = OTA_W25Q_STAGING_BASE_ADDR;

    while (remaining > 0)
    {
        uint16_t read_len = (uint16_t)((remaining > sizeof(read_buf)) ? sizeof(read_buf) : remaining);
        W25QXX_Read(read_buf, curr_read_addr, read_len);
        calc_crc = OTA_CalcCRC32_Update(calc_crc, read_buf, read_len);
        curr_read_addr += read_len;
        remaining -= read_len;
    }
    calc_crc = ~calc_crc;

    if (calc_crc != s_expected_crc)
    {
        printf("[OTA] CRC MISMATCH! Computed: 0x%08X, Expected: 0x%08X\r\n", calc_crc, s_expected_crc);
        printf("[OTA] Image corrupted or tampered! Staging image will NOT be applied.\r\n");
        s_ota_state = OTA_STATE_ERROR;
        return false;
    }

    printf("[OTA] CRC32 VERIFIED 100%% SUCCESS (0x%08X)!\r\n", calc_crc);

    /* 启动旧固件黄金容灾备份 */
    s_ota_state = OTA_STATE_BACKING_UP;
    OTA_BackupCurrentApp(s_fw_size);

    /* 写入 RTC 备份寄存器 (DR6 ~ DR9 专属通道) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    RTC_WriteBackupRegister(OTA_RTC_REG_MAGIC, OTA_MAGIC_UPGRADE_REQ);
    RTC_WriteBackupRegister(OTA_RTC_REG_TRIAL_COUNT, 0);
    RTC_WriteBackupRegister(OTA_RTC_REG_FW_SIZE, s_fw_size);
    RTC_WriteBackupRegister(OTA_RTC_REG_FW_CRC, s_expected_crc);

    s_ota_state = OTA_STATE_READY_TO_REBOOT;
    printf("\r\n=======================================================\r\n");
    printf("   [OTA READY] Dual-zone protection ready!\r\n");
    printf("   Magic=0x%08X written to RTC_BKP_DR6.\r\n", OTA_MAGIC_UPGRADE_REQ);
    printf("   Call OTA_SafeReboot() to execute upgrade.\r\n");
    printf("=======================================================\r\n\r\n");

    return true;
}

void OTA_Abort(void)
{
    printf("[OTA] OTA session aborted.\r\n");
    s_ota_state     = OTA_STATE_IDLE;
    s_fw_size       = 0;
    s_expected_crc  = 0;
    s_written_bytes = 0;
}

/* ============================================================================
   首次启动转正确认与防变砖安全关断复位
   ============================================================================ */
void OTA_Confirm_Success(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    uint32_t magic = RTC_ReadBackupRegister(OTA_RTC_REG_MAGIC);
    if (magic == OTA_MAGIC_TRIAL_BOOT)
    {
        RTC_WriteBackupRegister(OTA_RTC_REG_MAGIC, OTA_MAGIC_CONFIRMED);
        RTC_WriteBackupRegister(OTA_RTC_REG_TRIAL_COUNT, 0);
        printf("\r\n=======================================================\r\n");
        printf(" [OTA SUCCESS] New firmware verified stable for 30s!   \r\n");
        printf(" RTC Magic cleared to 0. Promotion to permanent COMPLETE.\r\n");
        printf("=======================================================\r\n\r\n");
    }
}

void OTA_SafeReboot(void)
{
    printf("[OTA] Safe reboot sequence initiating...\r\n");

    /* 等待串口 1 缓冲区完全清空发完，确保 PC 端完整收到重启日志 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);

    /* 挂起调度并全局关闭中断 */
    __disable_irq();

    /* 纯 CPU 寄存器空循环延时 20ms，严禁调用依赖 SysTick 中断的 Delay_ms */
    for (volatile uint32_t i = 0; i < 500000; i++)
    {
        __NOP();
    }

    /* 触发 Cortex-M4 内核级软复位，100% 自动秒级重启进入 Bootloader */
    NVIC_SystemReset();
}

/* ============================================================================
   协议消息解析与处理 (对接 ESP32 8080 TCP Server 及 云端下发通道)
   ============================================================================ */
bool OTA_ProcessMessage(const uint8_t *payload, uint16_t len, char *resp_buf, uint16_t resp_max_len)
{
    if (payload == NULL || len == 0 || resp_buf == NULL)
    {
        return false;
    }

    /* 1. OTA_START:<size>:<crc_hex> 或 OTA_START_AES:<size>:<crc_hex> */
    if (len >= 10 && strncmp((const char *)payload, "OTA_START", 9) == 0)
    {
        bool is_aes = false;
        const char *p = (const char *)payload + 9;
        if (strncmp(p, "_AES:", 5) == 0)
        {
            is_aes = true;
            p += 5;
        }
        else if (*p == ':')
        {
            is_aes = false;
            p += 1;
        }
        else
        {
            p = NULL;
        }

        uint32_t size = 0;
        uint32_t crc = 0;
        if (p != NULL && sscanf(p, "%u:%x", &size, &crc) == 2)
        {
            if (OTA_Start(size, crc, is_aes))
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
