/**
  ******************************************************************************
  * @file    mpu6050.h
  * @brief   STM32F407 MPU6050 六轴传感器驱动与姿态/计步头文件 (SCL=PF1, SDA=PF0)
  ******************************************************************************
  */

#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* 初始化 MPU6050 (配置 PF0/PF1 模拟 I2C，唤醒芯片并配置量程) */
bool MPU6050_Init(void);

/* 周期性调用进行六轴数据读取、互补滤波姿态解算与动态计步 */
void MPU6050_Update(void);

/* 获取解算后的欧拉角 (俯仰角 Pitch, 横滚角 Roll, 航向角 Yaw) */
void MPU6050_GetAttitude(float * pitch, float * roll, float * yaw);

/* 获取、恢复与重置步数统计 */
uint32_t MPU6050_GetStepCount(void);
void     MPU6050_SetStepCount(uint32_t steps);
void     MPU6050_ResetStepCount(void);

/* 读取原始六轴数据 */
void MPU6050_GetRawData(int16_t * ax, int16_t * ay, int16_t * az,
                        int16_t * gx, int16_t * gy, int16_t * gz);

bool MPU6050_IsOnline(void);

#endif /* __MPU6050_H */
