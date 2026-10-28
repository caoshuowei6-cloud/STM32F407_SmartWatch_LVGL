/**
  ******************************************************************************
  * @file    bsp_ringbuffer.c
  * @brief   工业级通用环形缓冲区 (FIFO) 引擎实现
  ******************************************************************************
  */

#include "bsp_ringbuffer.h"
#include <string.h>

void RingBuffer_Init(ring_buffer_t *rb, uint8_t *storage, uint16_t capacity)
{
    if (rb == 0 || storage == 0 || capacity == 0) return;
    rb->buffer         = storage;
    rb->capacity       = capacity;
    rb->head           = 0;
    rb->tail           = 0;
    rb->overflow_count = 0;
}

void RingBuffer_Reset(ring_buffer_t *rb)
{
    if (rb == 0) return;
    rb->head = 0;
    rb->tail = 0;
}

bool RingBuffer_WriteByte(ring_buffer_t *rb, uint8_t byte)
{
    if (rb == 0 || rb->buffer == 0) return false;

    uint16_t next_head = (rb->head + 1) % rb->capacity;
    if (next_head != rb->tail)
    {
        rb->buffer[rb->head] = byte;
        rb->head = next_head;
        return true;
    }
    else
    {
        /* 满载处理：覆盖旧数据，保证最新数据，并累计丢包计数 */
        rb->overflow_count++;
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->buffer[rb->head] = byte;
        rb->head = next_head;
        return false;
    }
}

uint16_t RingBuffer_Write(ring_buffer_t *rb, const uint8_t *data, uint16_t len)
{
    if (rb == 0 || data == 0 || len == 0) return 0;

    uint16_t written = 0;
    for (uint16_t i = 0; i < len; i++)
    {
        RingBuffer_WriteByte(rb, data[i]);
        written++;
    }
    return written;
}

bool RingBuffer_ReadByte(ring_buffer_t *rb, uint8_t *byte)
{
    if (rb == 0 || byte == 0) return false;
    if (rb->head == rb->tail) return false;

    *byte = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    return true;
}

uint16_t RingBuffer_Read(ring_buffer_t *rb, uint8_t *out_buf, uint16_t max_len)
{
    if (rb == 0 || out_buf == 0 || max_len == 0) return 0;

    uint16_t count = 0;
    while ((rb->head != rb->tail) && (count < max_len))
    {
        out_buf[count++] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
    }
    return count;
}

uint16_t RingBuffer_GetCount(const ring_buffer_t *rb)
{
    if (rb == 0) return 0;
    if (rb->head >= rb->tail)
    {
        return (rb->head - rb->tail);
    }
    else
    {
        return (rb->capacity - (rb->tail - rb->head));
    }
}

uint16_t RingBuffer_GetFree(const ring_buffer_t *rb)
{
    if (rb == 0) return 0;
    return (rb->capacity - 1) - RingBuffer_GetCount(rb);
}

uint32_t RingBuffer_GetOverflow(const ring_buffer_t *rb)
{
    if (rb == 0) return 0;
    return rb->overflow_count;
}
