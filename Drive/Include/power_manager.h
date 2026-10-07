/**
  ******************************************************************************
  * @file    power_manager.h
  * @brief   穿戴设备级电源管理器接口（15s 无操作自动息屏、按键秒级唤醒与防误触拦截）
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
    PM_STATE_STANDBY = 1    /* 屏幕熄灭，低功耗待机 (功耗立降 70%) */
} pm_state_t;

/* 初始化电源管理器 (默认 15 秒超时息屏) */
void Power_Manager_Init(void);

/* UI 任务周期调用 (每 100ms 或 1s 检查超时) */
void Power_Manager_Update(void);

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
void       Power_Manager_SetAutoSleep(bool enable);
bool       Power_Manager_GetAutoSleep(void);

#endif /* __POWER_MANAGER_H */
