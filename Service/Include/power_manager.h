/**
  ******************************************************************************
  * @file    power_manager.h
  * @brief   穿戴设备级电源管理器接口（三级能耗状态机：活跃/待机/STOP深度休眠）
  ******************************************************************************
  */

#ifndef __POWER_MANAGER_H
#define __POWER_MANAGER_H

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

/* 电源运行状态 */
typedef enum {
    PM_STATE_ACTIVE  = 0,   /* 屏幕亮起，正常交互 */
    PM_STATE_STANDBY = 1,   /* 屏幕熄灭，低功耗待机 (功耗立降 70%, __WFI 节流) */
    PM_STATE_STOP    = 2    /* Cortex-M4 STOP 深度休眠 (切断 168MHz PLL，微安级功耗) */
} pm_state_t;

/* 初始化电源管理器 (默认 15 秒超时息屏) */
void Power_Manager_Init(void);

/* UI 任务周期调用 (每 100ms 或 1s 检查超时) */
void Power_Manager_Update(void);

/* 进入 Cortex-M4 STOP 深度休眠模式 (关断主 PLL，微安级功耗，EXTI 唤醒后自愈重配 168MHz 时钟) */
void Power_Manager_EnterStopMode(void);

/* 处理按键事件: 若处于息屏待机状态，点亮背光并拦截按键(防误触)，返回 true;
   若处于亮屏状态，刷新超时定时器并返回 false (放行给 UI 响应) */
bool Power_Manager_OnKey(uint8_t key_val);

/* 手动唤醒与熄屏 */
void Power_Manager_Wakeup(void);
void Power_Manager_Sleep(void);

/* 状态与配置查询/修改 */
pm_state_t Power_Manager_GetState(void);
void       Power_Manager_SetTimeout(uint16_t seconds);
uint16_t   Power_Manager_GetTimeout(void);
void       Power_Manager_SetStopTimeout(uint16_t seconds);
uint16_t   Power_Manager_GetStopTimeout(void);
void       Power_Manager_SetAutoSleep(bool enable);
bool       Power_Manager_GetAutoSleep(void);

/* 抬腕亮屏控制与高频姿态手势识别接口 */
void       Power_Manager_SetWristWake(bool enable);
bool       Power_Manager_GetWristWake(void);
void       Power_Manager_CheckWristAction(float pitch, float roll, int16_t gx, int16_t gy, int16_t gz);

#endif /* __POWER_MANAGER_H */
