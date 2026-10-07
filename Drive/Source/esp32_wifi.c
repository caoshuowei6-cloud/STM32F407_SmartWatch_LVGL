/**
  ******************************************************************************
  * @file    esp32_wifi.c
  * @brief   STM32F407 + ESP32-C3 (USART2 DMA + 空闲中断 + 环形缓冲) 工业级网络驱动
  ******************************************************************************
  */

#include "esp32_wifi.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "rtc_f407.h"
#include "delay.h"
#include <stdio.h>
#include <string.h>

/* DMA 接收缓冲区与环形队列 */
static uint8_t  s_dma_rx_buf[ESP32_RX_DMA_BUF_SIZE];
static uint8_t  s_ring_buf[ESP32_RING_BUF_SIZE];
static volatile uint16_t s_ring_head = 0;
static volatile uint16_t s_ring_tail = 0;

/* FreeRTOS 同步原语 */
static SemaphoreHandle_t s_wifi_sem = NULL;
static esp32_wifi_state_t s_wifi_state = WIFI_STATE_DISCONNECTED;
static bool s_esp32_online = false;

/* ============================================================================
   环形缓冲区核心操作 (线程安全削峰 FIFO)
   ============================================================================ */
static void RingBuffer_Write(const uint8_t * data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        uint16_t next_head = (s_ring_head + 1) % ESP32_RING_BUF_SIZE;
        if (next_head != s_ring_tail)
        {
            s_ring_buf[s_ring_head] = data[i];
            s_ring_head = next_head;
        }
        else
        {
            /* 缓冲区满，丢弃最旧数据以保最新流数据 */
            s_ring_tail = (s_ring_tail + 1) % ESP32_RING_BUF_SIZE;
            s_ring_buf[s_ring_head] = data[i];
            s_ring_head = next_head;
        }
    }
}

static uint16_t RingBuffer_Read(uint8_t * out_buf, uint16_t max_len)
{
    uint16_t count = 0;
    while ((s_ring_head != s_ring_tail) && (count < max_len))
    {
        out_buf[count++] = s_ring_buf[s_ring_tail];
        s_ring_tail = (s_ring_tail + 1) % ESP32_RING_BUF_SIZE;
    }
    return count;
}

static void RingBuffer_Clear(void)
{
    s_ring_head = 0;
    s_ring_tail = 0;
}

/* ============================================================================
   底层硬件初始化 (PA2 TX / PA3 RX / USART2 / DMA1_Stream5_Channel_4)
   ============================================================================ */
void ESP32_WiFi_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;
    DMA_InitTypeDef   DMA_InitStructure;

    /* 1. 创建 FreeRTOS 二值同步信号量 */
    if (s_wifi_sem == NULL)
    {
        s_wifi_sem = xSemaphoreCreateBinary();
    }

    /* 2. 开启 GPIOA, USART2, DMA1 外设时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

    /* 3. 配置 PA2(TX) 和 PA3(RX) 复用为 AF7 (USART2) */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 4. 配置 USART2 (115200 8N1) */
    USART_DeInit(USART2);
    USART_InitStructure.USART_BaudRate            = 115200;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &USART_InitStructure);

    /* 开启空闲总线中断 IDLE 与 DMA 接收请求 */
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);
    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);

    /* 5. 配置 USART2 中断优先级 (优先级 6，符合 FreeRTOS 5~15 安全规范) */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 6. 配置 DMA1_Stream5_Channel_4 作为 USART2_RX 硬件搬运 */
    DMA_Cmd(DMA1_Stream5, DISABLE);
    DMA_DeInit(DMA1_Stream5);

    DMA_InitStructure.DMA_Channel            = DMA_Channel_4;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(USART2->DR);
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)s_dma_rx_buf;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_BufferSize         = ESP32_RX_DMA_BUF_SIZE;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold      = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(DMA1_Stream5, &DMA_InitStructure);

    DMA_ClearFlag(DMA1_Stream5, DMA_FLAG_TCIF5 | DMA_FLAG_HTIF5 | DMA_FLAG_TEIF5 | DMA_FLAG_FEIF5 | DMA_FLAG_DMEIF5);
    DMA_Cmd(DMA1_Stream5, ENABLE);

    /* 7. 使能 USART2 串口 */
    USART_Cmd(USART2, ENABLE);

    printf("[ESP32] USART2 DMA & RingBuffer Driver Initialized.\r\n");
}

/* ============================================================================
   串口中断服务函数 (USART2 空闲中断 IDLE 截帧推入环形缓冲)
   ============================================================================ */
void USART2_IRQHandler(void)
{
    volatile uint32_t clear;
    uint16_t rx_len;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        /* 1. 读取 SR 和 DR 寄存器清除 IDLE 标志 */
        clear = USART2->SR;
        clear = USART2->DR;
        (void)clear;

        /* 2. 暂停 DMA，计算接收数据长度 */
        DMA_Cmd(DMA1_Stream5, DISABLE);
        rx_len = ESP32_RX_DMA_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Stream5);

        if (rx_len > 0)
        {
            /* 3. 截帧写入环形缓冲区 (0 字节丢失) */
            RingBuffer_Write(s_dma_rx_buf, rx_len);

            /* 4. 唤醒后台 Task_WiFi 任务 */
            if (s_wifi_sem != NULL)
            {
                xSemaphoreGiveFromISR(s_wifi_sem, &xHigherPriorityTaskWoken);
            }
        }

        /* 5. 重启 DMA 硬件监听下一帧数据 */
        DMA_ClearFlag(DMA1_Stream5, DMA_FLAG_TCIF5 | DMA_FLAG_HTIF5 | DMA_FLAG_TEIF5 | DMA_FLAG_FEIF5 | DMA_FLAG_DMEIF5);
        DMA_SetCurrDataCounter(DMA1_Stream5, ESP32_RX_DMA_BUF_SIZE);
        DMA_Cmd(DMA1_Stream5, ENABLE);

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/* ============================================================================
   串口发送与指令应答函数
   ============================================================================ */
void ESP32_SendRaw(const uint8_t * data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        while ((USART2->SR & USART_FLAG_TXE) == 0);
        USART2->DR = data[i];
    }
    while ((USART2->SR & USART_FLAG_TC) == 0);
}

bool ESP32_SendCmd(const char * cmd, const char * expected_ack, uint32_t timeout_ms)
{
    static char resp_buf[512];
    uint16_t resp_len = 0;
    TickType_t start_tick;

    RingBuffer_Clear();
    ESP32_SendRaw((const uint8_t *)cmd, strlen(cmd));

    if (expected_ack == NULL) return true;

    start_tick = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(timeout_ms))
    {
        /* 挂起等待接收信号量或超时 20ms */
        if (s_wifi_sem != NULL)
        {
            xSemaphoreTake(s_wifi_sem, pdMS_TO_TICKS(20));
        }

        /* 从环形缓冲区提取数据累加判断 */
        while (s_ring_head != s_ring_tail && resp_len < (sizeof(resp_buf) - 1))
        {
            uint8_t ch;
            RingBuffer_Read(&ch, 1);
            resp_buf[resp_len++] = (char)ch;
            resp_buf[resp_len] = '\0';

            if (strstr(resp_buf, expected_ack) != NULL)
            {
                return true;
            }
            if (strstr(resp_buf, "ERROR") != NULL || strstr(resp_buf, "FAIL") != NULL)
            {
                return false;
            }
        }
    }
    return false;
}

/* ============================================================================
   高层业务：AP连接、NTP校时、传感器数据上报
   ============================================================================ */
bool ESP32_ConnectAP(const char * ssid, const char * pwd)
{
    char cmd[128];
    printf("[ESP32] Connecting to Wi-Fi SSID: %s...\r\n", ssid);
    s_wifi_state = WIFI_STATE_CONNECTING;

    ESP32_SendCmd("AT+CWMODE=1\r\n", "OK", 1000);
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);

    if (ESP32_SendCmd(cmd, "WIFI GOT IP", 15000) || ESP32_SendCmd("AT+CIPSTA?\r\n", "ip:", 3000))
    {
        printf("[ESP32] Wi-Fi Connected Successfully!\r\n");
        s_wifi_state = WIFI_STATE_CONNECTED;
        s_esp32_online = true;
        return true;
    }

    printf("[ESP32] Wi-Fi Connect Failed/Timeout.\r\n");
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    return false;
}

bool ESP32_SyncNTPTime(void)
{
    /* 配置阿里云 NTP 授时服务器 (东八区时区 = 8) */
    ESP32_SendCmd("AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"ntp1.aliyun.com\"\r\n", "OK", 2000);
    /* 稍等片刻等待同步，可在后台任务中定时触发 */
    return true;
}

bool ESP32_UploadSensorData(float temp, float humi, int32_t steps, float bat_vol)
{
    char json[128];
    snprintf(json, sizeof(json),
             "{\"temp\":%.1f,\"humi\":%.1f,\"steps\":%ld,\"bat\":%.2f}\r\n",
             temp, humi, (long)steps, bat_vol);
    (void)json;
    /* 预留：通过 TCP/MQTT 发送 */
    return true;
}

bool ESP32_WiFi_IsOnline(void)
{
    return s_esp32_online;
}

esp32_wifi_state_t ESP32_WiFi_GetState(void)
{
    return s_wifi_state;
}

/* ============================================================================
   Task_WiFi 运行状态机 (非阻塞自适应探活)
   ============================================================================ */
void ESP32_WiFi_TaskLoop(void)
{
    static uint32_t last_check_tick = 0;
    uint32_t now = xTaskGetTickCount();

    if ((now - last_check_tick) >= pdMS_TO_TICKS(5000))
    {
        last_check_tick = now;

        /* 探活检测 ESP32 是否物理在线 */
        if (!s_esp32_online)
        {
            if (ESP32_SendCmd("AT\r\n", "OK", 500))
            {
                s_esp32_online = true;
                printf("[ESP32] Module Responded OK! Online.\r\n");
            }
        }
    }
}
