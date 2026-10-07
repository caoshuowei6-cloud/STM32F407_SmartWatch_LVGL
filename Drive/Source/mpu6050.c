/**
  ******************************************************************************
  * @file    mpu6050.c
  * @brief   STM32F407 MPU6050 驱动、姿态互补滤波与动态计步算法实现 (SCL=PF1, SDA=PF0)
  ******************************************************************************
  */

#include "mpu6050.h"
#include "delay.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>

/* 默认 I2C 地址（AD0 接地为 0xD0/0xD1，AD0 接高电平为 0xD2/0xD3） */
static uint8_t  s_mpu_addr = 0xD0;

/* 姿态与计步常量 (对照 F1 经典参数) */
#define GYRO_SCALE              16.4f    /* ±2000 dps -> 16.4 LSB/(deg/s) */
#define ACCEL_SCALE             2048.0f  /* ±16g -> 2048 LSB/g */
#define STEP_PEAK_THRESHOLD_G   0.20f
#define STEP_RESET_THRESHOLD_G  0.08f
#define STEP_MIN_INTERVAL_MS    300

static bool     s_mpu_online = false;
static float    s_pitch = 0.0f;
static float    s_roll  = 0.0f;
static float    s_yaw   = 0.0f;
static uint32_t s_step_count = 0;

static int16_t  s_raw_ax = 0, s_raw_ay = 0, s_raw_az = 0;
static int16_t  s_raw_gx = 0, s_raw_gy = 0, s_raw_gz = 0;

/* ---------------- 模拟 I2C 底层时序 (PF1=SCL, PF0=SDA) ---------------- */
/* 采用标准江科大/F1 经久验证的时序架构，微秒级延时确保杜邦线与上拉电平可靠爬升 */

static void MPU_I2C_W_SCL(uint8_t bit)
{
    if (bit) GPIO_SetBits(GPIOF, GPIO_Pin_1);
    else     GPIO_ResetBits(GPIOF, GPIO_Pin_1);
    Delay_us(5);
}

static void MPU_I2C_W_SDA(uint8_t bit)
{
    if (bit) GPIO_SetBits(GPIOF, GPIO_Pin_0);
    else     GPIO_ResetBits(GPIOF, GPIO_Pin_0);
    Delay_us(5);
}

static uint8_t MPU_I2C_R_SDA(void)
{
    uint8_t bit = GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_0);
    Delay_us(5);
    return bit;
}

static void MPU_I2C_Start(void)
{
    MPU_I2C_W_SDA(1);
    MPU_I2C_W_SCL(1);
    MPU_I2C_W_SDA(0);
    MPU_I2C_W_SCL(0);
}

static void MPU_I2C_Stop(void)
{
    MPU_I2C_W_SDA(0);
    MPU_I2C_W_SCL(1);
    MPU_I2C_W_SDA(1);
}

static void MPU_I2C_SendByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        MPU_I2C_W_SDA((byte & (0x80 >> i)) ? 1 : 0);
        MPU_I2C_W_SCL(1);
        MPU_I2C_W_SCL(0);
    }
}

static uint8_t MPU_I2C_ReceiveByte(void)
{
    uint8_t i, byte = 0x00;
    MPU_I2C_W_SDA(1);
    for (i = 0; i < 8; i++)
    {
        MPU_I2C_W_SCL(1);
        if (MPU_I2C_R_SDA()) byte |= (0x80 >> i);
        MPU_I2C_W_SCL(0);
    }
    return byte;
}

static void MPU_I2C_SendAck(uint8_t ack_bit)
{
    MPU_I2C_W_SDA(ack_bit);
    MPU_I2C_W_SCL(1);
    MPU_I2C_W_SCL(0);
}

static uint8_t MPU_I2C_ReceiveAck(void)
{
    uint8_t ack_bit;
    MPU_I2C_W_SDA(1);
    MPU_I2C_W_SCL(1);
    ack_bit = MPU_I2C_R_SDA();
    MPU_I2C_W_SCL(0);
    return ack_bit;
}

static void MPU6050_WriteReg(uint8_t reg, uint8_t data)
{
    MPU_I2C_Start();
    MPU_I2C_SendByte(s_mpu_addr);
    MPU_I2C_ReceiveAck();
    MPU_I2C_SendByte(reg);
    MPU_I2C_ReceiveAck();
    MPU_I2C_SendByte(data);
    MPU_I2C_ReceiveAck();
    MPU_I2C_Stop();
}

static uint8_t MPU6050_ReadReg(uint8_t reg)
{
    uint8_t data;

    MPU_I2C_Start();
    MPU_I2C_SendByte(s_mpu_addr);
    MPU_I2C_ReceiveAck();
    MPU_I2C_SendByte(reg);
    MPU_I2C_ReceiveAck();

    MPU_I2C_Start();
    MPU_I2C_SendByte(s_mpu_addr | 0x01);
    MPU_I2C_ReceiveAck();
    data = MPU_I2C_ReceiveByte();
    MPU_I2C_SendAck(1); /* 读单字节发送 NACK */
    MPU_I2C_Stop();

    return data;
}

static bool MPU6050_ProbeAddr(uint8_t addr)
{
    uint8_t ack;
    MPU_I2C_Start();
    MPU_I2C_SendByte(addr);
    ack = MPU_I2C_ReceiveAck();
    MPU_I2C_Stop();
    return (ack == 0);
}

/* ---------------- MPU6050 协议与解算 ---------------- */

bool MPU6050_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    uint8_t who;

    /* 1. 初始化 GPIOF PF0(SDA), PF1(SCL) 开漏输出上拉 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOF, &GPIO_InitStructure);

    MPU_I2C_W_SCL(1);
    MPU_I2C_W_SDA(1);
    Delay_ms(30); /* 等待传感器上电供电建立 */

    /* 2. 自动探测 I2C 设备地址 (0xD0 还是 0xD2) */
    if (MPU6050_ProbeAddr(0xD0))
    {
        s_mpu_addr = 0xD0;
    }
    else if (MPU6050_ProbeAddr(0xD2))
    {
        s_mpu_addr = 0xD2;
    }
    else
    {
        printf("[MPU6050] No ACK received at 0xD0 or 0xD2 on PF0/PF1!\r\n");
        s_mpu_online = false;
        return false;
    }

    /* 3. 唤醒配置芯片寄存器（严格对齐 F1 验证配置） */
    MPU6050_WriteReg(0x6B, 0x01); /* PWR_MGMT_1: 唤醒，选择 X 轴陀螺仪作为时钟源 */
    Delay_ms(10);
    MPU6050_WriteReg(0x6C, 0x00); /* PWR_MGMT_2: 六轴全开 */
    MPU6050_WriteReg(0x19, 0x04); /* SMPLRT_DIV: 采样率分频 (200Hz) */
    MPU6050_WriteReg(0x1A, 0x06); /* CONFIG: 低通滤波器 5Hz */
    MPU6050_WriteReg(0x1B, 0x18); /* GYRO_CONFIG: 满量程 ±2000 dps */
    MPU6050_WriteReg(0x1C, 0x18); /* ACCEL_CONFIG: 满量程 ±16g */

    /* 4. 读取 WHO_AM_I 验证并打印日志 */
    who = MPU6050_ReadReg(0x75);
    printf("[MPU6050] Connected at addr 0x%02X, WHO_AM_I = 0x%02X\r\n", s_mpu_addr, who);

    s_mpu_online = true;
    return true;
}

void MPU6050_GetRawData(int16_t * ax, int16_t * ay, int16_t * az,
                        int16_t * gx, int16_t * gy, int16_t * gz)
{
    uint8_t h, l;

    if (!s_mpu_online)
    {
        static uint32_t last_retry_tick = 0;
        uint32_t now = lv_tick_get();
        if (last_retry_tick != 0 && (now - last_retry_tick) < 3000)
        {
            return;
        }
        last_retry_tick = now;
        if (!MPU6050_Init()) return;
    }

    /* 逐轴高低字节读取 (对齐 F1 稳定实现) */
    h = MPU6050_ReadReg(0x3B);
    l = MPU6050_ReadReg(0x3C);
    s_raw_ax = (int16_t)((h << 8) | l);

    h = MPU6050_ReadReg(0x3D);
    l = MPU6050_ReadReg(0x3E);
    s_raw_ay = (int16_t)((h << 8) | l);

    h = MPU6050_ReadReg(0x3F);
    l = MPU6050_ReadReg(0x40);
    s_raw_az = (int16_t)((h << 8) | l);

    h = MPU6050_ReadReg(0x43);
    l = MPU6050_ReadReg(0x44);
    s_raw_gx = (int16_t)((h << 8) | l);

    h = MPU6050_ReadReg(0x45);
    l = MPU6050_ReadReg(0x46);
    s_raw_gy = (int16_t)((h << 8) | l);

    h = MPU6050_ReadReg(0x47);
    l = MPU6050_ReadReg(0x48);
    s_raw_gz = (int16_t)((h << 8) | l);

    if (ax) *ax = s_raw_ax;
    if (ay) *ay = s_raw_ay;
    if (az) *az = s_raw_az;
    if (gx) *gx = s_raw_gx;
    if (gy) *gy = s_raw_gy;
    if (gz) *gz = s_raw_gz;
}

/**
  * @brief  姿态互补滤波解算与动态计步处理（建议 20ms~50ms 周期调用）
  */
void MPU6050_Update(void)
{
    static uint32_t last_tick = 0;
    static uint32_t last_step_tick = 0;
    static float    gravity_magnitude = 1.0f;
    static uint8_t  step_armed = 1;

    uint32_t now;
    float dt;
    float accel_magnitude;
    float dynamic_accel;
    float roll_g, pitch_g, yaw_g;
    float roll_a, pitch_a;

    MPU6050_GetRawData(&s_raw_ax, &s_raw_ay, &s_raw_az,
                       &s_raw_gx, &s_raw_gy, &s_raw_gz);

    if (!s_mpu_online) return;

    now = lv_tick_get();
    if (last_tick == 0)
    {
        last_tick = now;
        accel_magnitude = sqrtf((float)s_raw_ax * s_raw_ax +
                                (float)s_raw_ay * s_raw_ay +
                                (float)s_raw_az * s_raw_az) / ACCEL_SCALE;
        gravity_magnitude = (accel_magnitude > 0.1f) ? accel_magnitude : 1.0f;
        return;
    }

    dt = (now - last_tick) / 1000.0f;
    if (dt <= 0.001f || dt > 0.5f) dt = 0.05f;
    last_tick = now;

    /* 1. 动态计步算法 (去除低频重力分量，检测特征波峰) */
    accel_magnitude = sqrtf((float)s_raw_ax * s_raw_ax +
                            (float)s_raw_ay * s_raw_ay +
                            (float)s_raw_az * s_raw_az) / ACCEL_SCALE;
    gravity_magnitude += 0.04f * (accel_magnitude - gravity_magnitude);
    dynamic_accel = accel_magnitude - gravity_magnitude;

    if (dynamic_accel < STEP_RESET_THRESHOLD_G)
    {
        step_armed = 1;
    }
    else if (step_armed && (dynamic_accel >= STEP_PEAK_THRESHOLD_G) &&
             (last_step_tick == 0 || (now - last_step_tick) >= STEP_MIN_INTERVAL_MS))
    {
        s_step_count++;
        last_step_tick = now;
        step_armed = 0;
    }

    /* 2. 姿态互补滤波解算 */
    roll_g  = s_roll  + ((float)s_raw_gx / GYRO_SCALE) * dt;
    pitch_g = s_pitch + ((float)s_raw_gy / GYRO_SCALE) * dt;
    yaw_g   = s_yaw   + ((float)s_raw_gz / GYRO_SCALE) * dt;

    roll_a  = atan2f((float)s_raw_ay, (float)s_raw_az) * 180.0f / 3.14159265f;
    pitch_a = atan2f(-(float)s_raw_ax, (float)s_raw_az) * 180.0f / 3.14159265f;

    s_roll  = 0.96f * roll_g + 0.04f * roll_a;
    s_pitch = 0.96f * pitch_g + 0.04f * pitch_a;
    s_yaw   = yaw_g;
}

void MPU6050_GetAttitude(float * pitch, float * roll, float * yaw)
{
    if (pitch) *pitch = s_pitch;
    if (roll)  *roll  = s_roll;
    if (yaw)   *yaw   = s_yaw;
}

uint32_t MPU6050_GetStepCount(void)
{
    return s_step_count;
}

void MPU6050_SetStepCount(uint32_t steps)
{
    s_step_count = steps;
}

void MPU6050_ResetStepCount(void)
{
    s_step_count = 0;
}

bool MPU6050_IsOnline(void)
{
    return s_mpu_online;
}
