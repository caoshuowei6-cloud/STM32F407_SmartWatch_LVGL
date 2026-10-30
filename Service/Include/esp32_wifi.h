/**
  ******************************************************************************
  * @file    esp32_wifi.h
  * @brief   STM32F407 + ESP32-C3 (USART2 DMA + 空闲中断 + 环形缓冲) 工业级网络驱动
  *          支持手机热点自动连接、阿里云 NTP 毫米级硬件 RTC 校时、EMQX MQTT 云端直连与离线同步
  ******************************************************************************
  */

#ifndef __ESP32_WIFI_H
#define __ESP32_WIFI_H

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

#define ESP32_RX_DMA_BUF_SIZE     1024  /* DMA 单次硬件循环接收缓冲区 */
#define ESP32_RING_BUF_SIZE       2048  /* 环形消息队列容量 (抗突发网络报文削峰与OTA传输) */

/* 默认 Wi-Fi 热点配置 */
#ifndef WIFI_STA_SSID
#define WIFI_STA_SSID "csw"
#endif

#ifndef WIFI_STA_PASS
#define WIFI_STA_PASS "87654321"
#endif

/* ============================================================================
   阿里云 EMQX 物联网 Broker 与 MQTT 主题拓扑定义
   ============================================================================ */
#ifndef MQTT_BROKER_HOST
#define MQTT_BROKER_HOST        "121.196.153.141"   /* 阿里云 ECS 专属服务器公网 IP */
#endif

#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT        1883                /* 标准非加密 MQTT 端口 */
#endif

#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID          "smartwatch_csw_01" /* 设备全网唯一 Client ID */
#endif

/* MQTT 业务主题层级解耦定义 */
#define MQTT_TOPIC_TELEMETRY    "smartwatch/csw/telemetry" /* 实时遥测数据流 (温湿度/计步/姿态/电量) */
#define MQTT_TOPIC_HISTORY      "smartwatch/csw/history"   /* W25Q64 离线历史批量同步通道 */
#define MQTT_TOPIC_CMD          "smartwatch/csw/cmd"       /* 云端下行控制指令 (震动/寻表/OTA通知) */

/* WiFi 与 MQTT 运行状态枚举 */
typedef enum
{
    WIFI_STATE_DISCONNECTED     = 0,    /* 未连接 / 离线 */
    WIFI_STATE_CONNECTING       = 1,    /* 正在连接路由器 AP */
    WIFI_STATE_CONNECTED        = 2,    /* 已获得 IP 地址 */
    WIFI_STATE_MQTT_CONNECTING  = 3,    /* 正在握手阿里云 EMQX Broker */
    WIFI_STATE_MQTT_CONNECTED   = 4,    /* 企业级端云通道就绪 (MQTT 在线遥测中) */
    WIFI_STATE_SERVER_READY     = 5     /* 本地 TCP 服务就绪 (局域网调试兼容) */
} esp32_wifi_state_t;

/* 初始化与硬件接口 */
void        ESP32_WiFi_Init(void);
bool        ESP32_WiFi_IsOnline(void);
esp32_wifi_state_t ESP32_WiFi_GetState(void);
const char * ESP32_WiFi_GetIP(void);
uint32_t    ESP32_WiFi_GetOverflowCount(void);

/* AT 指令与网络功能 */
bool        ESP32_SendCmd(const char * cmd, const char * expected_ack, uint32_t timeout_ms);
bool        ESP32_SendCmdEx(const char * cmd, const char * expected_ack, uint32_t timeout_ms, char * out_resp, uint16_t out_resp_size);
bool        ESP32_ConnectAP(const char * ssid, const char * pwd);
bool        ESP32_SyncNTPTime(void);

/* MQTT 原生客户端核心 API */
bool        ESP32_MQTT_Connect(const char *broker_ip, uint16_t port, const char *client_id);
bool        ESP32_MQTT_Subscribe(const char *topic, uint8_t qos);
bool        ESP32_MQTT_Publish(const char *topic, const char *payload, uint8_t qos);
bool        ESP32_MQTT_IsConnected(void);

/* 传感器实时遥测与离线历史同步 */
bool        ESP32_UploadSensorData(float temp, float humi, int32_t steps, float pitch, float roll);
bool        ESP32_MQTT_SyncHistory(void);

/* FreeRTOS 后台任务定期轮询状态机 */
void        ESP32_WiFi_TaskLoop(void);

#endif /* __ESP32_WIFI_H */
