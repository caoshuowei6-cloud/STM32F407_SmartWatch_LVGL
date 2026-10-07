/**
  ******************************************************************************
  * @file    esp32_wifi.h
  * @brief   STM32F407 + ESP32-C3 (USART2 DMA + 空闲中断 + 环形缓冲) 驱动接口
  ******************************************************************************
  */

#ifndef __ESP32_WIFI_H
#define __ESP32_WIFI_H

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

#define ESP32_RX_DMA_BUF_SIZE     512   /* DMA 单次硬件循环接收缓冲区 */
#define ESP32_RING_BUF_SIZE       1024  /* 环形消息队列容量 (抗突发网络报文削峰) */

/* WiFi 运行状态枚举 */
typedef enum
{
    WIFI_STATE_DISCONNECTED = 0,    /* 未连接 / 离线 */
    WIFI_STATE_CONNECTING   = 1,    /* 正在连接路由器 AP */
    WIFI_STATE_CONNECTED    = 2,    /* 已获得 IP 地址 */
    WIFI_STATE_SERVER_READY = 3     /* TCP/UDP 云端通道已建立 */
} esp32_wifi_state_t;

/* 初始化与硬件接口 */
void ESP32_WiFi_Init(void);
bool ESP32_WiFi_IsOnline(void);
esp32_wifi_state_t ESP32_WiFi_GetState(void);

/* AT 指令与网络功能 */
bool ESP32_SendCmd(const char * cmd, const char * expected_ack, uint32_t timeout_ms);
bool ESP32_ConnectAP(const char * ssid, const char * pwd);
bool ESP32_SyncNTPTime(void);
bool ESP32_UploadSensorData(float temp, float humi, int32_t steps, float bat_vol);

/* FreeRTOS 后台任务定期轮询状态机 */
void ESP32_WiFi_TaskLoop(void);

#endif /* __ESP32_WIFI_H */
