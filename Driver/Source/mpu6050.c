/**
  ******************************************************************************
  * @file    mpu6050.c
  * @brief   STM32F407 MPU6050 é©±åŠ¨ã€å§¿æ€äº’è¡¥æ»¤æ³ä¸åŠ¨æ€è¡æ¥ç®—æ³•å®ç (SCL=PF1, SDA=PF0)
  ******************************************************************************
  */

#include "mpu6050.h"
#include "bsp_board.h"
#include "delay.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>

/* é»˜è I2C åœ°å€ï¼ˆAD0 æ¥åœ°ä¸ 0xD0/0xD1ï¼ŒAD0 æ¥é«˜ç”µå¹³ä¸ 0xD2/0xD3ï¼ */
static uint8_t  s_mpu_addr = 0xD0;

/* å§¿æä¸è®¡æ¥å¸¸é‡ (å¯¹ç…§ F1 ç»å…¸å‚æ•°) */
#define GYRO_SCALE              16.4f    /* Â±2000 dps -> 16.4 LSB/(deg/s) */
#define ACCEL_SCALE             2048.0f  /* Â±16g -> 2048 LSB/g */
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

/* ---------------- æ¨¡æ‹Ÿ I2C åº•å±‚æ—¶åº (å¼•è„šæ˜ å°„ä¾æ‰˜ bsp_board.h è§£è) ---------------- */
/* é‡‡ç”¨æ ‡å‡†ç»ä¹…éªŒè¯çš„æ—¶åºæ¶æ„ï¼Œå¾ç§’çº§å»¶æ—¶ç¡ä¿æœé‚¦çº¿ä¸ä¸Šæ‹‰ç”µå¹³å¯é çˆ¬å */

static void MPU_I2C_W_SCL(uint8_t bit)
{
    if (bit) GPIO_SetBits(BOARD_MPU_I2C_PORT, BOARD_MPU_I2C_SCL_PIN);
    else     GPIO_ResetBits(BOARD_MPU_I2C_PORT, BOARD_MPU_I2C_SCL_PIN);
    Delay_us(5);
}

static void MPU_I2C_W_SDA(uint8_t bit)
{
    if (bit) GPIO_SetBits(BOARD_MPU_I2C_PORT, BOARD_MPU_I2C_SDA_PIN);
    else     GPIO_ResetBits(BOARD_MPU_I2C_PORT, BOARD_MPU_I2C_SDA_PIN);
    Delay_us(5);
}

static uint8_t MPU_I2C_R_SDA(void)
{
    uint8_t bit = GPIO_ReadInputDataBit(BOARD_MPU_I2C_PORT, BOARD_MPU_I2C_SDA_PIN);
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
    MPU_I2C_SendAck(1); /* è¯»å•å­—èŠ‚å‘é NACK */
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

/* ---------------- MPU6050 åèä¸è§£ç® ---------------- */

bool MPU6050_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    uint8_t who;

    /* 1. åˆå‹åŒ– GPIOF PF0(SDA), PF1(SCL) å¼æ¼è¾“å‡ºä¸Šæ‹ */
    /* 1. åˆå‹åŒ–æ¿çº§å®šä¹‰ GPIO SCL/SDA å¼æ¼ä¸Šæ‹ */
    RCC_AHB1PeriphClockCmd(BOARD_MPU_I2C_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = BOARD_MPU_I2C_SCL_PIN | BOARD_MPU_I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_MPU_I2C_PORT, &GPIO_InitStructure);

    MPU_I2C_W_SCL(1);
    MPU_I2C_W_SDA(1);
    Delay_ms(30); /* ç­‰å¾…ä¼ æ„Ÿå™¨ä¸Šç”µä¾›ç”µå»ºç« */

    /* 2. è‡åŠ¨æ¢æµ I2C è®¾å‡åœ°å (0xD0 è¿˜æ˜¯ 0xD2) */
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

    /* 3. å”¤é†’é…ç½®èŠç‰‡å¯„å­˜å™¨ï¼ˆä¸¥æ ¼å¹é½ F1 éªŒè¯é…ç½®ï¼ */
    MPU6050_WriteReg(0x6B, 0x01); /* PWR_MGMT_1: å”¤é†’ï¼Œé‰æ‹© X è½´é™€èºä»ªä½œä¸ºæ—¶é’Ÿæº */
    Delay_ms(10);
    MPU6050_WriteReg(0x6C, 0x00); /* PWR_MGMT_2: å…è½´å…¨å¼ */
    MPU6050_WriteReg(0x19, 0x04); /* SMPLRT_DIV: é‡‡æ ·ç‡åˆ†é¢ (200Hz) */
    MPU6050_WriteReg(0x1A, 0x06); /* CONFIG: ä½éšæ»¤æ³¢å™¨ 5Hz */
    MPU6050_WriteReg(0x1B, 0x18); /* GYRO_CONFIG: æ»¡é‡ç¨ Â±2000 dps */
    MPU6050_WriteReg(0x1C, 0x18); /* ACCEL_CONFIG: æ»¡é‡ç¨ Â±16g */

    /* 4. è¯»å– WHO_AM_I éªŒè¯å¹¶æ‰“å°æ—¥å¿ */
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

    /* é€è½´é«˜ä½å­—èŠ‚è¯»å– (å¯¹é½ F1 ç¨³å®šå®ç°) */
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
  * @brief  å§¿æäº’è¡¥æ»¤æ³¢è§£ç®—ä¸åŠ¨æè¡æ¥å„ç†ï¼ˆå»ºè® 20ms~50ms å‘¨æœŸè°ƒç”¨ï¼
  */

static bool s_mpu_low_power = false;

void MPU6050_EnterLowPowerMode(void)
{
    if (!s_mpu_online || s_mpu_low_power) return;

    /* ÇĞÖÁÄÚ²¿ 8MHz Õñµ´Æ÷Ê±ÖÓÔ´£¬²¢¹Ø±Õ X/Y/Z 3 ÖáÍÓÂİÒÇµçÔ´ (Á¢Ê¡ ~3.6mA) */
    MPU6050_WriteReg(0x6B, 0x00); /* PWR_MGMT_1: Internal 8MHz clock */
    MPU6050_WriteReg(0x6C, 0x07); /* PWR_MGMT_2: Standby XG, YG, ZG (ÍÓÂİÒÇ¶Ïµç) */
    s_mpu_low_power = true;
}

void MPU6050_ExitLowPowerMode(void)
{
    if (!s_mpu_online || !s_mpu_low_power) return;

    /* »Ö¸´Ñ¡Ôñ X ÖáÍÓÂİÒÇ×÷Îª¸ß¾«¶ÈÊ±ÖÓÔ´£¬²¢»Ö¸´ 6 ÖáÈ«¿ª */
    MPU6050_WriteReg(0x6B, 0x01); /* PWR_MGMT_1: X-gyro clock */
    MPU6050_WriteReg(0x6C, 0x00); /* PWR_MGMT_2: All 6 axes active */
    s_mpu_low_power = false;
}

bool MPU6050_IsLowPowerMode(void)
{
    return s_mpu_low_power;
}
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

    /* 1. åŠ¨æè¡æ¥ç®—æ³ (å»é™¤ä½é‘é‡åŠ›åˆ†é‡ï¼Œæ£æµ‹ç‰¹å¾æ³¢å³) */
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

    /* 2. å§¿æäº’è¡¥æ»¤æ³¢è§£ç® */
    roll_g  = s_roll  + ((float)s_raw_gx / GYRO_SCALE) * dt;
    pitch_g = s_pitch + ((float)s_raw_gy / GYRO_SCALE) * dt;
    yaw_g   = s_yaw   + ((float)s_raw_gz / GYRO_SCALE) * dt;

    roll_a  = atan2f((float)s_raw_ay, (float)s_raw_az) * 180.0f / 3.14159265f;
    pitch_a = atan2f(-(float)s_raw_ax, (float)s_raw_az) * 180.0f / 3.14159265f;

    if (s_mpu_low_power)
    {
        /* µÍ¹¦ºÄ´ı»úÄ£Ê½ÏÂÍÓÂİÒÇÒÑ¹Ø±Õ£¬Ö±½ÓÀûÓÃÖØÁ¦¼ÓËÙ¶È¼Æ¸üĞÂ¸©Ñö/ºá¹ö½Ç (0 ÍÓÂİÒÇÆ¯ÒÆ) */
        s_roll  = roll_a;
        s_pitch = pitch_a;
    }
    else
    {
        s_roll  = 0.96f * roll_g + 0.04f * roll_a;
        s_pitch = 0.96f * pitch_g + 0.04f * pitch_a;
        s_yaw   = yaw_g;
    }
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
