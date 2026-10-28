/**
  ******************************************************************************
  * @file    bsp_ringbuffer.h
  * @brief   工业级通用环形缓冲区 (FIFO) 引擎 (借鉴新普惠采集器架构)
  * @note    支持单生产者-单消费者 (SPSC) 线程安全、削峰填谷与硬件丢包统计 (overflow_count)
  ******************************************************************************
  */

#ifndef __BSP_RINGBUFFER_H__
#define __BSP_RINGBUFFER_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint8_t *            buffer;         /* 数据存储指针 */
    uint16_t             capacity;       /* 缓冲区最大容量 */
    volatile uint16_t    head;           /* 写入索引 (由生产者/中断更新) */
    volatile uint16_t    tail;           /* 读取索引 (由消费者/任务更新) */
    volatile uint32_t    overflow_count; /* 溢出丢弃字节计数 (工业级通信质量诊断指标) */
} ring_buffer_t;

/* 核心 API */
void     RingBuffer_Init(ring_buffer_t *rb, uint8_t *storage, uint16_t capacity);
void     RingBuffer_Reset(ring_buffer_t *rb);
bool     RingBuffer_WriteByte(ring_buffer_t *rb, uint8_t byte);
uint16_t RingBuffer_Write(ring_buffer_t *rb, const uint8_t *data, uint16_t len);
bool     RingBuffer_ReadByte(ring_buffer_t *rb, uint8_t *byte);
uint16_t RingBuffer_Read(ring_buffer_t *rb, uint8_t *out_buf, uint16_t max_len);
uint16_t RingBuffer_GetCount(const ring_buffer_t *rb);
uint16_t RingBuffer_GetFree(const ring_buffer_t *rb);
uint32_t RingBuffer_GetOverflow(const ring_buffer_t *rb);

#endif /* __BSP_RINGBUFFER_H__ */
