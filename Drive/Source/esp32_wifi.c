/**
  ******************************************************************************
  * @file    esp32_wifi.c
  * @brief   STM32F407 + ESP32-C3 (USART2 DMA + 空闲中断 + 环形缓冲) 工业级网络驱动
  *          支持手机热点自动连接、阿里云 NTP 毫米级硬件 RTC 校时、TCP Server 与数据上报
  ******************************************************************************
  */

#include "esp32_wifi.h"
#include "ota_manager.h"
#include "aht20.h"
#include "mpu6050.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "rtc_f407.h"
#include "delay.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* DMA 硬件接收缓冲区与软件环形队列 */
static uint8_t  s_dma_rx_buf[ESP32_RX_DMA_BUF_SIZE];
static uint8_t  s_ring_buf[ESP32_RING_BUF_SIZE];
static volatile uint16_t s_ring_head = 0;
static volatile uint16_t s_ring_tail = 0;

/* FreeRTOS 同步原语与状态记录 */
static SemaphoreHandle_t s_wifi_sem = NULL;
static esp32_wifi_state_t s_wifi_state = WIFI_STATE_DISCONNECTED;
static bool s_esp32_online = false;
static char s_wifi_ip[20] = {0};
static uint8_t s_tcp_client_id = 0;
static bool s_tcp_client_connected = false;

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
            /* 满载丢弃旧字节，保障最新网络报文 */
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
    uint32_t wait_timeout;

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

    /* 4. 配置 USART2 (115200 8N1，与 ESP32 默认 AT 固件保持一致) */
    USART_DeInit(USART2);
    USART_InitStructure.USART_BaudRate            = 115200;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &USART_InitStructure);

    /* 5. 开启空闲总线中断 IDLE 与 DMA 接收请求 */
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);
    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);

    /* 6. 配置 USART2 中断优先级 (抢占 6，在 FreeRTOS 安全调用范围内) */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 7. 配置 DMA1_Stream5_Channel_4 (USART2_RX 硬件搬运) */
    DMA_Cmd(DMA1_Stream5, DISABLE);
    wait_timeout = 10000;
    while ((DMA_GetCmdStatus(DMA1_Stream5) != DISABLE) && (--wait_timeout));

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

    /* 8. 使能 USART2 串口 */
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
    uint32_t wait_timeout;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 清除溢出错误 ORE，防御通信堵塞 */
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
    {
        clear = USART2->SR;
        clear = USART2->DR;
        (void)clear;
    }

    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        /* 1. 软件序列读 SR/DR 清除 IDLE 标志 */
        clear = USART2->SR;
        clear = USART2->DR;
        (void)clear;

        /* 2. 暂停 DMA 并等待状态就绪 (STM32F4 硬件核心规范) */
        DMA_Cmd(DMA1_Stream5, DISABLE);
        wait_timeout = 10000;
        while ((DMA_GetCmdStatus(DMA1_Stream5) != DISABLE) && (--wait_timeout));

        /* 3. 计算本帧数据字节数 */
        rx_len = ESP32_RX_DMA_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Stream5);

        if (rx_len > 0)
        {
            /* 4. 截帧推入环形队列 */
            RingBuffer_Write(s_dma_rx_buf, rx_len);

            /* 5. 唤醒阻塞等待指令的 WiFi 任务 */
            if (s_wifi_sem != NULL)
            {
                xSemaphoreGiveFromISR(s_wifi_sem, &xHigherPriorityTaskWoken);
            }
        }

        /* 6. 重置 DMA 接收计数器并使能 */
        DMA_ClearFlag(DMA1_Stream5, DMA_FLAG_TCIF5 | DMA_FLAG_HTIF5 | DMA_FLAG_TEIF5 | DMA_FLAG_FEIF5 | DMA_FLAG_DMEIF5);
        DMA_SetCurrDataCounter(DMA1_Stream5, ESP32_RX_DMA_BUF_SIZE);
        DMA_Cmd(DMA1_Stream5, ENABLE);

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/* ============================================================================
   串口底层发送与指令应答函数（含双向全透明调试打印，对标原 F1 验证逻辑）
   ============================================================================ */
static void ESP32_SendRaw(const uint8_t * data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        while ((USART2->SR & USART_FLAG_TXE) == 0);
        USART2->DR = data[i];
    }
    while ((USART2->SR & USART_FLAG_TC) == 0);
}

bool ESP32_SendCmdEx(const char * cmd, const char * expected_ack, uint32_t timeout_ms, char * out_resp, uint16_t out_resp_size)
{
    static char resp_buf[768];
    uint16_t resp_len = 0;
    TickType_t start_tick;
    uint8_t temp_buf[64];
    uint16_t read_bytes;

    RingBuffer_Clear();
    memset(resp_buf, 0, sizeof(resp_buf));

    /* 打印 TX 调试日志，完全透明 */
    printf("[ESP32 TX] >> %s", cmd);
    ESP32_SendRaw((const uint8_t *)cmd, strlen(cmd));

    if (expected_ack == NULL) return true;

    start_tick = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(timeout_ms))
    {
        /* 挂起 10ms 等待接收中断信号量 */
        if (s_wifi_sem != NULL)
        {
            xSemaphoreTake(s_wifi_sem, pdMS_TO_TICKS(10));
        }

        /* 提取环形缓冲区数据 */
        while ((read_bytes = RingBuffer_Read(temp_buf, sizeof(temp_buf) - 1)) > 0)
        {
            temp_buf[read_bytes] = '\0';

            /* 透明打印接收到的模组回复 */
            printf("%s", (char *)temp_buf);

            /* 累加到总应答缓冲区 */
            if (resp_len < (sizeof(resp_buf) - 1))
            {
                uint16_t copy_cnt = read_bytes;
                if (copy_cnt > (sizeof(resp_buf) - 1 - resp_len))
                {
                    copy_cnt = sizeof(resp_buf) - 1 - resp_len;
                }
                memcpy(resp_buf + resp_len, temp_buf, copy_cnt);
                resp_len += copy_cnt;
                resp_buf[resp_len] = '\0';
            }

            /* 检查目标应答 */
            if (expected_ack != NULL && strstr(resp_buf, expected_ack) != NULL)
            {
                if (out_resp != NULL && out_resp_size > 0)
                {
                    strncpy(out_resp, resp_buf, out_resp_size - 1);
                    out_resp[out_resp_size - 1] = '\0';
                }
                return true;
            }

            /* 检查错误返回 (除非期望的就是错误) */
            if (strstr(resp_buf, "ERROR") != NULL ||
                strstr(resp_buf, "FAIL")  != NULL ||
                strstr(resp_buf, "busy p...") != NULL)
            {
                if (out_resp != NULL && out_resp_size > 0)
                {
                    strncpy(out_resp, resp_buf, out_resp_size - 1);
                    out_resp[out_resp_size - 1] = '\0';
                }
                return false;
            }
        }
    }

    if (out_resp != NULL && out_resp_size > 0)
    {
        strncpy(out_resp, resp_buf, out_resp_size - 1);
        out_resp[out_resp_size - 1] = '\0';
    }
    printf("[ESP32] Timeout waiting for '%s'!\r\n", expected_ack);
    return false;
}

bool ESP32_SendCmd(const char * cmd, const char * expected_ack, uint32_t timeout_ms)
{
    return ESP32_SendCmdEx(cmd, expected_ack, timeout_ms, NULL, 0);
}

/* ============================================================================
   AP 连接与 IP 获取
   ============================================================================ */
bool ESP32_ConnectAP(const char * ssid, const char * pwd)
{
    char cmd[128];
    char resp[256];
    char * p_ip;

    printf("\r\n[ESP32] Connecting to Wi-Fi SSID: \"%s\"...\r\n", ssid);
    s_wifi_state = WIFI_STATE_CONNECTING;

    /* 确保处于 Station 单网卡模式 */
    ESP32_SendCmd("AT+CWMODE=1\r\n", "OK", 2000);

    /* 发送连接命令 (等待最多 20 秒) */
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    if (!ESP32_SendCmd(cmd, "OK", 20000))
    {
        printf("[ESP32] Wi-Fi Connect Failed / Timeout!\r\n");
        s_wifi_state = WIFI_STATE_DISCONNECTED;
        return false;
    }

    /* 查询获取的 IP 地址 */
    if (ESP32_SendCmdEx("AT+CIPSTA?\r\n", "OK", 2000, resp, sizeof(resp)))
    {
        p_ip = strstr(resp, "+CIPSTA:ip:\"");
        if (p_ip != NULL)
        {
            p_ip += strlen("+CIPSTA:ip:\"");
            char * p_end = strchr(p_ip, '\"');
            if (p_end != NULL)
            {
                uint16_t ip_len = p_end - p_ip;
                if (ip_len < sizeof(s_wifi_ip))
                {
                    memcpy(s_wifi_ip, p_ip, ip_len);
                    s_wifi_ip[ip_len] = '\0';
                }
            }
        }
    }

    s_wifi_state = WIFI_STATE_CONNECTED;
    s_esp32_online = true;
    printf("\r\n[ESP32] >>> Wi-Fi CONNECTED SUCCESSFULLY! <<<\r\n");
    printf("[ESP32] Local IP Address: %s\r\n\r\n", (s_wifi_ip[0] != '\0') ? s_wifi_ip : "Acquired");
    return true;
}

/* ============================================================================
   阿里云 NTP 网络时间自动同步与硬件 RTC 校准
   ============================================================================ */
bool ESP32_SyncNTPTime(void)
{
    char resp[256];
    char wday[8], mon[8];
    int day = 0, hour = 0, min = 0, sec = 0, year = 0;
    const char * months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char * wdays[]  = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    uint8_t m = 0, wd = 1;
    char * p;

    printf("[NTP] Querying Network Time (AT+CIPSNTPTIME?)... \r\n");

    /* 发送查询 NTP 时间指令 */
    if (!ESP32_SendCmdEx("AT+CIPSNTPTIME?\r\n", "OK", 3000, resp, sizeof(resp)))
    {
        printf("[NTP] Failed to get response for AT+CIPSNTPTIME?\r\n");
        return false;
    }

    p = strstr(resp, "+CIPSNTPTIME:");
    if (p == NULL)
    {
        return false;
    }

    /* 示例: +CIPSNTPTIME:Wed Oct 07 20:48:15 2026 */
    if (sscanf(p, "+CIPSNTPTIME:%3s %3s %d %d:%d:%d %d",
               wday, mon, &day, &hour, &min, &sec, &year) != 7)
    {
        printf("[NTP] Failed to parse NTP time format.\r\n");
        return false;
    }

    /* 年份合法性检查 (防止 1970 初始值) */
    if (year < 2024 || year > 2099)
    {
        printf("[NTP] Server time not ready yet (Reported Year: %d)\r\n", year);
        return false;
    }

    /* 转换英文月份为数字 */
    for (int i = 0; i < 12; i++)
    {
        if (strncmp(mon, months[i], 3) == 0)
        {
            m = (uint8_t)(i + 1);
            break;
        }
    }
    if (m == 0) m = 1;

    /* 转换星期为数字 (1:周一 ~ 7:周日) */
    for (int i = 0; i < 7; i++)
    {
        if (strncmp(wday, wdays[i], 3) == 0)
        {
            wd = (uint8_t)(i + 1);
            break;
        }
    }

    /* 写入 STM32F407 硬件日历 RTC */
    rtc_time_date_t dt;
    dt.year    = (uint8_t)(year - 2000);
    dt.month   = m;
    dt.date    = (uint8_t)day;
    dt.weekday = wd;
    dt.hours   = (uint8_t)hour;
    dt.minutes = (uint8_t)min;
    dt.seconds = (uint8_t)sec;

    RTC_F407_SetDateTime(&dt);

    printf("\r\n=======================================================\r\n");
    printf("   [NTP SUCCESS] Hardware RTC Calibrated Successfully! \r\n");
    printf("   Date/Time: 20%02d-%02d-%02d %02d:%02d:%02d (Week %d) \r\n",
           dt.year, dt.month, dt.date, dt.hours, dt.minutes, dt.seconds, dt.weekday);
    printf("=======================================================\r\n\r\n");

    return true;
}

/* ============================================================================
   TCP Server 与传感器数据上传（对标原 F1 8080 服务与 JSON 格式）
   ============================================================================ */
static bool ESP32_StartTCPServer(uint16_t port)
{
    char cmd[40];
    bool ok = true;

    ok &= ESP32_SendCmd("AT+CIPMUX=1\r\n", "OK", 2000);
    snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%d\r\n", port);
    ok &= ESP32_SendCmd(cmd, "OK", 3000);
    /* 保持 CIPDINFO=0，使 +IPD 报文格式标准化为 +IPD,<id>,<len>:<data> */
    ESP32_SendCmd("AT+CIPDINFO=0\r\n", "OK", 1000);

    if (ok)
    {
        s_wifi_state = WIFI_STATE_SERVER_READY;
        printf("[ESP32] TCP Server started on port %d.\r\n", port);
    }
    return ok;
}

/* 内部辅助：等待数据发送完成响应 (避免调用 SendCmdEx 清空环形缓冲冲掉后续报文) */
static bool ESP32_WaitSendAck(uint32_t timeout_ms)
{
    TickType_t start_tick = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(timeout_ms))
    {
        uint8_t ack_buf[64];
        uint16_t r = RingBuffer_Read(ack_buf, sizeof(ack_buf) - 1);
        if (r > 0)
        {
            ack_buf[r] = '\0';
            if (strstr((char *)ack_buf, "SEND OK") != NULL)
            {
                return true;
            }
            if (strstr((char *)ack_buf, "FAIL") != NULL || strstr((char *)ack_buf, "ERROR") != NULL)
            {
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

bool ESP32_SendTCPResponse(uint8_t client_id, const char *resp)
{
    char send_cmd[32];
    uint16_t len = (uint16_t)strlen(resp);
    snprintf(send_cmd, sizeof(send_cmd), "AT+CIPSEND=%d,%d\r\n", client_id, len);
    if (!ESP32_SendCmd(send_cmd, ">", 2000))
    {
        return false;
    }
    ESP32_SendRaw((const uint8_t *)resp, len);
    return ESP32_WaitSendAck(2000);
}

static void ESP32_CheckClientEvent(void)
{
    static uint8_t buf[1024];
    uint16_t len = RingBuffer_Read(buf, sizeof(buf) - 1);
    uint8_t cid;

    if (len > 0)
    {
        buf[len] = '\0';

        /* 1. 检查客户端连接事件 */
        if (sscanf((char *)buf, "%hhu,CONNECT", &cid) == 1 || sscanf((char *)buf, "+LINK_CONN:%hhu", &cid) == 1)
        {
            s_tcp_client_id = cid;
            s_tcp_client_connected = true;
            printf("[ESP32] TCP Client #%d connected!\r\n", cid);
            /* 不在此处提前 return，因为第一包数据可能紧跟在 CONNECT 后到达 */
        }

        /* 2. 检查客户端断开事件 */
        if (sscanf((char *)buf, "%hhu,DISCONNECT", &cid) == 1 || strstr((char *)buf, "CLOSED") != NULL)
        {
            s_tcp_client_connected = false;
            printf("[ESP32] TCP Client disconnected.\r\n");
            return;
        }

        /* 3. 检查 +IPD 传入数据流 (兼容标准模式与扩展信息模式) */
        char *ipd_pos = strstr((char *)buf, "+IPD,");
        if (ipd_pos != NULL)
        {
            int link_id = 0;
            int data_len = 0;
            char *colon_pos = strchr(ipd_pos, ':');
            if (colon_pos != NULL && (colon_pos - ipd_pos) < 64)
            {
                if (sscanf(ipd_pos, "+IPD,%d,%d", &link_id, &data_len) >= 2)
                {
                    s_tcp_client_id = (uint8_t)link_id;
                    s_tcp_client_connected = true;

                    const uint8_t *payload = (const uint8_t *)(colon_pos + 1);
                    uint16_t payload_len = len - (uint16_t)(payload - buf);

                    /* 若 UART 分包截断导致长度不足，短时轮询补齐 */
                    uint32_t wait_start = xTaskGetTickCount();
                    while (payload_len < (uint16_t)data_len && (xTaskGetTickCount() - wait_start) < pdMS_TO_TICKS(100))
                    {
                        uint16_t more = RingBuffer_Read(buf + len, sizeof(buf) - 1 - len);
                        if (more > 0)
                        {
                            len += more;
                            buf[len] = '\0';
                            payload_len = len - (uint16_t)(payload - buf);
                        }
                        else
                        {
                            vTaskDelay(pdMS_TO_TICKS(2));
                        }
                    }

                    if (payload_len > (uint16_t)data_len)
                    {
                        payload_len = (uint16_t)data_len;
                    }

                    char resp[64] = {0};
                    if (OTA_ProcessMessage(payload, payload_len, resp, sizeof(resp)))
                    {
                        ESP32_SendTCPResponse((uint8_t)link_id, resp);

                        if (strstr(resp, "REBOOTING") != NULL)
                        {
                            printf("[OTA] Rebooting into Bootloader in 300ms...\r\n");
                            vTaskDelay(pdMS_TO_TICKS(300));
                            NVIC_SystemReset();
                        }
                    }
                }
            }
        }
    }
}

bool ESP32_UploadSensorData(float temp, float humi, int32_t steps, float pitch, float roll)
{
    char json[160];
    char send_cmd[32];
    uint16_t json_len;

    if (!s_tcp_client_connected) return false;

    snprintf(json, sizeof(json),
             "{\"temp\":%.1f,\"humi\":%.1f,\"steps\":%ld,\"pitch\":%.1f,\"roll\":%.1f}\r\n",
             temp, humi, (long)steps, pitch, roll);
    json_len = strlen(json);

    snprintf(send_cmd, sizeof(send_cmd), "AT+CIPSEND=%d,%d\r\n", s_tcp_client_id, json_len);
    if (!ESP32_SendCmd(send_cmd, ">", 2000))
    {
        s_tcp_client_connected = false;
        return false;
    }

    ESP32_SendRaw((const uint8_t *)json, json_len);
    return ESP32_WaitSendAck(2000);
}

bool ESP32_WiFi_IsOnline(void)
{
    return s_esp32_online;
}

esp32_wifi_state_t ESP32_WiFi_GetState(void)
{
    return s_wifi_state;
}

const char * ESP32_WiFi_GetIP(void)
{
    return s_wifi_ip;
}

/* ============================================================================
   Task_WiFi 主控状态机 (自动启动探活 -> 连热点 -> 阿里云 NTP 校时 -> 启动服务)
   ============================================================================ */
typedef enum
{
    TASK_STATE_INIT = 0,
    TASK_STATE_CONNECT_HOTSPOT,
    TASK_STATE_SYNC_NTP,
    TASK_STATE_START_SERVER,
    TASK_STATE_RUNNING
} wifi_task_state_t;

void ESP32_WiFi_TaskLoop(void)
{
    static wifi_task_state_t s_curr_step = TASK_STATE_INIT;
    static uint32_t s_last_upload_tick = 0;
    static uint32_t s_last_ntp_sync_tick = 0;
    static uint8_t s_init_retries = 0;

    switch (s_curr_step)
    {
        case TASK_STATE_INIT:
        {
            printf("\r\n[Task_WiFi] Probing ESP32-C3 AT response...\r\n");

            /* 发送探活指令 */
            if (!ESP32_SendCmd("AT\r\n", "OK", 1000))
            {
                s_init_retries++;
                printf("[Task_WiFi] AT no response (attempt %d). Trying AT+RST...\r\n", s_init_retries);
                ESP32_SendCmd("AT+RST\r\n", "ready", 4000);
                vTaskDelay(pdMS_TO_TICKS(1000));

                if (!ESP32_SendCmd("AT\r\n", "OK", 1000))
                {
                    printf("[Task_WiFi] ESP32 not responding yet, will retry in 3s...\r\n");
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    break;
                }
            }

            s_esp32_online = true;
            printf("[Task_WiFi] >>> ESP32-C3 ONLINE! Turning off echo (ATE0)... <<<\r\n");
            ESP32_SendCmd("ATE0\r\n", "OK", 1000);

            s_curr_step = TASK_STATE_CONNECT_HOTSPOT;
            break;
        }

        case TASK_STATE_CONNECT_HOTSPOT:
        {
            if (ESP32_ConnectAP(WIFI_STA_SSID, WIFI_STA_PASS))
            {
                s_curr_step = TASK_STATE_SYNC_NTP;
            }
            else
            {
                printf("[Task_WiFi] Retrying Wi-Fi in 5 seconds...\r\n");
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
            break;
        }

        case TASK_STATE_SYNC_NTP:
        {
            printf("\r\n[Task_WiFi] Enabling Aliyun SNTP (ntp.aliyun.com, UTC+8)...\r\n");
            ESP32_SendCmd("AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"ntp.ntsc.ac.cn\"\r\n", "OK", 2000);

            /* 等待 2 秒供 NTP 网络对时完成 */
            vTaskDelay(pdMS_TO_TICKS(2000));

            if (ESP32_SyncNTPTime())
            {
                s_last_ntp_sync_tick = xTaskGetTickCount();
                s_curr_step = TASK_STATE_START_SERVER;
            }
            else
            {
                printf("[Task_WiFi] NTP sync not ready yet, proceeding to start server and will retry later...\r\n");
                s_curr_step = TASK_STATE_START_SERVER;
            }
            break;
        }

        case TASK_STATE_START_SERVER:
        {
            ESP32_StartTCPServer(8080);
            s_curr_step = TASK_STATE_RUNNING;
            break;
        }

        case TASK_STATE_RUNNING:
        {
            /* 1. 检查客户端连接/断开事件 */
            ESP32_CheckClientEvent();

            /* 2. 定期 1 秒向连接的客户端上报数据 (OTA 期间暂停上报，独占带宽防冲突) */
            if ((xTaskGetTickCount() - s_last_upload_tick) >= pdMS_TO_TICKS(1000))
            {
                s_last_upload_tick = xTaskGetTickCount();
                if (s_tcp_client_connected && OTA_GetState() == OTA_STATE_IDLE)
                {
                    float pitch = 0.0f, roll = 0.0f, yaw = 0.0f;
                    MPU6050_GetAttitude(&pitch, &roll, &yaw);
                    ESP32_UploadSensorData(AHT20_GetLastTemp(),
                                           AHT20_GetLastHumi(),
                                           (int32_t)MPU6050_GetStepCount(),
                                           pitch, roll);
                }
            }

            /* 3. 每一小时 (3600s) 自动重新校准一次 NTP 时间 */
            if ((xTaskGetTickCount() - s_last_ntp_sync_tick) >= pdMS_TO_TICKS(3600000))
            {
                s_last_ntp_sync_tick = xTaskGetTickCount();
                ESP32_SyncNTPTime();
            }

            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        }

        default:
            s_curr_step = TASK_STATE_INIT;
            break;
    }
}
