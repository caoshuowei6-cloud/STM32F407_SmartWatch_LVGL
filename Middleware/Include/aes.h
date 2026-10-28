/**
  ******************************************************************************
  * @file    aes.h
  * @brief   Tiny AES-128 CBC/ECB/CTR 加密/解密算法库头文件
  *          用于 STM32F407 无线 OTA 空中升级固件解密引擎
  ******************************************************************************
  */

#ifndef __AES_H__
#define __AES_H__

#include <stdint.h>
#include <stddef.h>

/* 配置启用 AES-128 与 CBC 模式 */
#define AES128 1
#define CBC 1
#define ECB 0
#define CTR 0

#define AES_BLOCKLEN 16
#define AES_KEYLEN 16
#define AES_keyExpSize 176

struct AES_ctx
{
    uint8_t RoundKey[AES_keyExpSize];
    uint8_t Iv[AES_BLOCKLEN];
};

void AES_init_ctx(struct AES_ctx * ctx, const uint8_t * key);
void AES_init_ctx_iv(struct AES_ctx * ctx, const uint8_t * key, const uint8_t * iv);
void AES_ctx_set_iv(struct AES_ctx * ctx, const uint8_t * iv);

void AES_CBC_encrypt_buffer(struct AES_ctx * ctx, uint8_t * buf, size_t length);
void AES_CBC_decrypt_buffer(struct AES_ctx * ctx, uint8_t * buf, size_t length);

#endif /* __AES_H__ */
