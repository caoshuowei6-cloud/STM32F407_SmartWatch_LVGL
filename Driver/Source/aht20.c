/**
  ******************************************************************************
  * @file    aht20.c
  * @brief   STM32F407 AHT20 温湿度传感器驱动实现 (SCL=PB6, SDA=PB7)
  ******************************************************************************
  */

#include "aht20.h"
#include "bsp_board.h"
#include "delay.h"
#include "lvgl.h"
#include <stdio.h>

#define AHT20_ADDR_WRITE  0x70
#define AHT20_ADDR_READ   0x71

#define AHT_SCL_H()       GPIO_SetBits(BOARD_AHT_I2C_PORT, BOARD_AHT_I2C_SCL_PIN)
#define AHT_SCL_L()       GPIO_ResetBits(BOARD_AHT_I2C_PORT, BOARD_AHT_I2C_SCL_PIN)
#define AHT_SDA_H()       GPIO_SetBits(BOARD_AHT_I2C_PORT, BOARD_AHT_I2C_SDA_PIN)
#define AHT_SDA_L()       GPIO_ResetBits(BOARD_AHT_I2C_PORT, BOARD_AHT_I2C_SDA_PIN)
#define AHT_SDA_READ()    GPIO_ReadInputDataBit(BOARD_AHT_I2C_PORT, BOARD_AHT_I2C_SDA_PIN)

static float s_last_temp = 25.0f;
static float s_last_humi = 50.0f;
static bool  s_aht_online = false;

/* ---------------- 模拟 I2C 底层时序 (PB6 / PB7) ---------------- */

static void I2C_AHT_Delay(void)
{
    Delay_us(5);
}

static void I2C_AHT_Start(void)
{
    AHT_SDA_H();
    I2C_AHT_Delay();
    AHT_SCL_H();
    I2C_AHT_Delay();
    AHT_SDA_L();
    I2C_AHT_Delay();
    AHT_SCL_L();
    I2C_AHT_Delay();
}

static void I2C_AHT_Stop(void)
{
    AHT_SDA_L();
    I2C_AHT_Delay();
    AHT_SCL_H();
    I2C_AHT_Delay();
    AHT_SDA_H();
    I2C_AHT_Delay();
}

static bool I2C_AHT_SendByte(uint8_t byte)
{
    uint8_t i;
    bool ack;

    for (i = 0; i < 8; i++)
    {
        if (byte & 0x80) AHT_SDA_H();
        else             AHT_SDA_L();
        I2C_AHT_Delay();
        AHT_SCL_H();
        I2C_AHT_Delay();
        AHT_SCL_L();
        I2C_AHT_Delay();
        byte <<= 1;
    }

    /* 接收 ACK */
    AHT_SDA_H(); /* 释放总线 */
    I2C_AHT_Delay();
    AHT_SCL_H();
    I2C_AHT_Delay();
    ack = (AHT_SDA_READ() == 0);
    AHT_SCL_L();
    I2C_AHT_Delay();

    return ack;
}

static uint8_t I2C_AHT_ReceiveByte(bool ack)
{
    uint8_t i, byte = 0;

    AHT_SDA_H(); /* 释放总线供从机输出 */
    for (i = 0; i < 8; i++)
    {
        byte <<= 1;
        AHT_SCL_H();
        I2C_AHT_Delay();
        if (AHT_SDA_READ()) byte |= 0x01;
        AHT_SCL_L();
        I2C_AHT_Delay();
    }

    /* 发送应答 ACK / NACK */
    if (ack) AHT_SDA_L();
    else     AHT_SDA_H();
    I2C_AHT_Delay();
    AHT_SCL_H();
    I2C_AHT_Delay();
    AHT_SCL_L();
    I2C_AHT_Delay();
    AHT_SDA_H();

    return byte;
}

/* ---------------- AHT20 设备协议 ---------------- */

bool AHT20_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    uint8_t status;

    /* 1. 初始化板级温湿度传感器 I2C 引脚 开漏输出上拉 */
    RCC_AHB1PeriphClockCmd(BOARD_AHT_I2C_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = BOARD_AHT_I2C_SCL_PIN | BOARD_AHT_I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_AHT_I2C_PORT, &GPIO_InitStructure);

    AHT_SCL_H();
    AHT_SDA_H();
    Delay_ms(40); /* 上电等待 > 40ms */

    /* 2. 读取状态字，检查是否需要校准 */
    I2C_AHT_Start();
    if (!I2C_AHT_SendByte(AHT20_ADDR_READ))
    {
        I2C_AHT_Stop();
        printf("[AHT20] Sensor not responding on PB6/PB7!\r\n");
        s_aht_online = false;
        return false;
    }

    status = I2C_AHT_ReceiveByte(false);
    I2C_AHT_Stop();

    /* 检查 Bit3 是否为 1 (已校准) */
    if ((status & 0x08) == 0)
    {
        /* 发送校准命令 0xBE 0x08 0x00 */
        I2C_AHT_Start();
        I2C_AHT_SendByte(AHT20_ADDR_WRITE);
        I2C_AHT_SendByte(0xBE);
        I2C_AHT_SendByte(0x08);
        I2C_AHT_SendByte(0x00);
        I2C_AHT_Stop();
        Delay_ms(15);
    }

    s_aht_online = true;
    printf("[AHT20] Initialized successfully on PB6/PB7!\r\n");
    return true;
}

bool AHT20_Read(float * temperature, float * humidity)
{
    uint8_t raw[6];
    uint32_t raw_h, raw_t;
    uint8_t timeout = 20;

    if (!s_aht_online)
    {
        static uint32_t last_retry_tick = 0;
        uint32_t now = lv_tick_get();
        if (last_retry_tick != 0 && (now - last_retry_tick) < 3000)
        {
            return false;
        }
        last_retry_tick = now;
        if (!AHT20_Init()) return false;
    }

    /* 1. 触发测量命令 0xAC 0x33 0x00 */
    I2C_AHT_Start();
    if (!I2C_AHT_SendByte(AHT20_ADDR_WRITE))
    {
        I2C_AHT_Stop();
        return false;
    }
    I2C_AHT_SendByte(0xAC);
    I2C_AHT_SendByte(0x33);
    I2C_AHT_SendByte(0x00);
    I2C_AHT_Stop();

    /* 2. 等待测量完成 (约 75ms) */
    Delay_ms(75);

    /* 3. 轮询读取 6 字节数据 */
    while (timeout--)
    {
        I2C_AHT_Start();
        if (I2C_AHT_SendByte(AHT20_ADDR_READ))
        {
            raw[0] = I2C_AHT_ReceiveByte(true);
            /* 检查 Busy 标志 (Bit7) */
            if ((raw[0] & 0x80) == 0)
            {
                raw[1] = I2C_AHT_ReceiveByte(true);
                raw[2] = I2C_AHT_ReceiveByte(true);
                raw[3] = I2C_AHT_ReceiveByte(true);
                raw[4] = I2C_AHT_ReceiveByte(true);
                raw[5] = I2C_AHT_ReceiveByte(false); /* 最后一个字节 NACK */
                I2C_AHT_Stop();
                break;
            }
        }
        I2C_AHT_Stop();
        Delay_ms(5);
    }

    if (timeout == 0)
    {
        return false;
    }

    /* 4. 解析温湿度 20 位原始数据 */
    raw_h = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | ((uint32_t)raw[3] >> 4);
    raw_t = (((uint32_t)raw[3] & 0x0F) << 16) | ((uint32_t)raw[4] << 8) | (uint32_t)raw[5];

    s_last_humi = ((float)raw_h * 100.0f) / 1048576.0f;
    s_last_temp = (((float)raw_t * 200.0f) / 1048576.0f) - 50.0f;

    if (temperature) *temperature = s_last_temp;
    if (humidity)    *humidity    = s_last_humi;

    return true;
}

float AHT20_GetLastTemp(void)
{
    return s_last_temp;
}

float AHT20_GetLastHumi(void)
{
    return s_last_humi;
}

bool AHT20_IsOnline(void)
{
    return s_aht_online;
}
