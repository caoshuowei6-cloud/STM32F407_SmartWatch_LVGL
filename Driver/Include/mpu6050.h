/**
  ******************************************************************************
  * @file    mpu6050.h
  * @brief   STM32F407 MPU6050 å…è½´ä¼ æ„Ÿå™¨é©±åŠ¨ä¸å§¿æ€/è®¡æ¥å¤´æ–‡ä»¶ (SCL=PF1, SDA=PF0)
  ******************************************************************************
  */

#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* åˆå‹åŒ– MPU6050 (é…ç½® PF0/PF1 æ¨¡æ‹Ÿ I2Cï¼Œå”¤é†’èŠ¯ç‰‡å¹¶é…ç½®é‡ç¨‹) */
bool MPU6050_Init(void);

/* å‘¨æœŸæ€§è°ƒç”¨è¿›è¡Œå…­è½´æ•°æè¯»å–ã€äº’è¡¥æ»¤æ³¢å§¿æ€è§£ç®—ä¸åŠ¨æè¡æ */
void MPU6050_Update(void);

/* è·å–è§£ç®—åçš„æ¬§æ‹‰è§ (ä¿ä»°è Pitch, æ¨æ»šè Roll, èˆå‘è Yaw) */
void MPU6050_GetAttitude(float * pitch, float * roll, float * yaw);

/* è·å–ã€æ¢å¤ä¸é‡ç½®æ­¥æ•°ç»Ÿè */
uint32_t MPU6050_GetStepCount(void);
void     MPU6050_SetStepCount(uint32_t steps);
void     MPU6050_ResetStepCount(void);

/* è¯»å–åŸå‹å…­è½´æ•°æ */
void MPU6050_GetRawData(int16_t * ax, int16_t * ay, int16_t * az,
                        int16_t * gx, int16_t * gy, int16_t * gz);

bool MPU6050_IsOnline(void);

/* µÍ¹¦ºÄÍÓÂİÒÇĞİÃß¿ØÖÆ (½ö±£Áô¼ÓËÙ¶È¼Æ£¬Á¢Ê¡ ~3.6mA) */
void MPU6050_EnterLowPowerMode(void);
void MPU6050_ExitLowPowerMode(void);
bool MPU6050_IsLowPowerMode(void);

#endif /* __MPU6050_H */
