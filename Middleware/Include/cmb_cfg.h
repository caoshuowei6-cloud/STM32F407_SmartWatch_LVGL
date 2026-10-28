/*
 * This file is part of the CmBacktrace Library.
 *
 * Copyright (c) 2016-2020, Armink, <armink.ztl@gmail.com>
 *
 * Function: It is the configure head file for this library.
 */

#ifndef _CMB_CFG_H_
#define _CMB_CFG_H_

#include <stdio.h>
#include <stdbool.h>

/* 打印输出重定向到串口 printf */
#define cmb_println(...)               do { printf(__VA_ARGS__); printf("\r\n"); } while (0)

/* 启用操作系统支持 */
#define CMB_USING_OS_PLATFORM

/* 操作系统类型: FreeRTOS */
#define CMB_OS_PLATFORM_TYPE           CMB_OS_PLATFORM_FREERTOS

/* CPU 内核: Cortex-M4 (支持硬件 FPU 与浮点寄存器探测) */
#define CMB_CPU_PLATFORM_TYPE          CMB_CPU_ARM_CORTEX_M4

/* 启用堆栈内存转储信息 */
#define CMB_USING_DUMP_STACK_INFO

/* 打印语言: 英文 (兼容各类串口助手字符集) */
#define CMB_PRINT_LANGUAGE             CMB_PRINT_LANGUAGE_ENGLISH

#endif /* _CMB_CFG_H_ */
