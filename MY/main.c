/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F407 + LVGL 9 手表迁移主程序（阶段 1：超低脏矩形极致防闪烁版）
  ******************************************************************************
  */

#include "stm32f4xx.h"
#include "led.h"
#include "delay.h"
#include "key.h"
#include "usart.h"
#include "lvgl.h"
#include "lv_port_lcd_stm32.h"
#include <stdio.h>

/* UI 控件指针 */
static lv_obj_t * label_title;
static lv_obj_t * label_time;
static lv_obj_t * label_key_info;
static lv_obj_t * key_box;

/**
  * @brief  创建手表迁移阶段 1 验证界面（极致防闪烁架构）
  */
static void Create_Test_UI(void)
{
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x121824), LV_PART_MAIN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. 顶部标题 (静态常驻) */
    label_title = lv_label_create(scr);
    lv_label_set_text(label_title, "F407 WATCH (LVGL 9)");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 30);

    /* 2. 独立时间标签（固定宽度 100x28，自带不透明背景，跳秒时仅刷新这 2000 像素，耗时仅 1ms！） */
    label_time = lv_label_create(scr);
    lv_obj_set_size(label_time, 100, 28);
    lv_obj_center(label_time);
    lv_label_set_text(label_time, "12:00:00");
    lv_obj_set_style_text_color(label_time, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(label_time, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label_time, LV_OPA_COVER, LV_PART_MAIN); /* 100% 不透明，隔绝背景穿透 */
    lv_obj_set_style_radius(label_time, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(label_time, 4, LV_PART_MAIN);
    lv_obj_remove_flag(label_time, LV_OBJ_FLAG_SCROLLABLE);

    /* 3. 底部按键状态栏 (独立固定小卡片，200x38) */
    key_box = lv_obj_create(scr);
    lv_obj_set_size(key_box, 200, 38);
    lv_obj_align(key_box, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(key_box, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(key_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(key_box, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_border_width(key_box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(key_box, 8, LV_PART_MAIN);
    lv_obj_remove_flag(key_box, LV_OBJ_FLAG_SCROLLABLE);

    label_key_info = lv_label_create(key_box);
    lv_label_set_text(label_key_info, "Press any KEY...");
    lv_obj_set_style_text_color(label_key_info, lv_color_hex(0x34D399), LV_PART_MAIN);
    lv_obj_center(label_key_info);
}

/**
  * @brief  主函数入口
  */
int main(void)
{
    uint8_t key_val = 0;
    uint32_t sec_counter = 0;
    char time_str[16];

    /* 1. 板级外设初始化 */
    Delay_Init();       /* SysTick 1ms 初始化 (同时提供 lv_tick_inc 与按键时基) */
    LED_Init();         /* PC13 绿灯 (低电平亮) */
    KEY_Init();         /* 按键初始化 (PB1, PA6, PA4, PA15，上拉) */
    Usart_Config();     /* USART1 115200 8N1 */

    printf("\r\n=========================================\r\n");
    printf(" STM32F407 Watch LVGL 9 Micro-Dirty Refresh\r\n");
    printf("=========================================\r\n");

    /* 2. LVGL 核心与显示驱动初始化 */
    lv_init();
    lv_port_disp_init();

    /* 3. 创建测试 UI */
    Create_Test_UI();

    printf("LVGL UI Initialized successfully!\r\n");

    /* 4. 主循环 */
    while (1)
    {
        /* 获取 F1 移植过来的按键事件 */
        key_val = Key_GetNum();
        if (key_val != 0)
        {
            LED1_Toggle; /* 翻转板载 PC13 用户 LED */

            switch (key_val)
            {
                case 1:
                    printf("Key Event: KEY1 (PB1)\r\n");
                    lv_label_set_text(label_key_info, "KEY1: PB1 Pressed");
                    lv_obj_set_style_text_color(label_key_info, lv_color_hex(0xFBBF24), LV_PART_MAIN);
                    break;
                case 2:
                    printf("Key Event: KEY2 (PA6)\r\n");
                    lv_label_set_text(label_key_info, "KEY2: PA6 Pressed");
                    lv_obj_set_style_text_color(label_key_info, lv_color_hex(0x60A5FA), LV_PART_MAIN);
                    break;
                case 3:
                    printf("Key Event: KEY3 Short (PA4)\r\n");
                    lv_label_set_text(label_key_info, "KEY3: PA4 Short");
                    lv_obj_set_style_text_color(label_key_info, lv_color_hex(0x34D399), LV_PART_MAIN);
                    break;
                case 4:
                    printf("Key Event: KEY3 Long 1s (PA4)\r\n");
                    lv_label_set_text(label_key_info, "KEY3: Long Press 1s!");
                    lv_obj_set_style_text_color(label_key_info, lv_color_hex(0xF87171), LV_PART_MAIN);
                    break;
                case 5:
                    printf("Key Event: Board SW1 (PA15)\r\n");
                    lv_label_set_text(label_key_info, "SW1: PA15 Pressed");
                    lv_obj_set_style_text_color(label_key_info, lv_color_hex(0xA78BFA), LV_PART_MAIN);
                    break;
                default:
                    break;
            }
        }

        /* 秒计数更新：由于 label_time 独立固定且不透明，仅刷新 100x28 像素，耗时仅 1ms */
        static uint32_t last_tick = 0;
        uint32_t current_tick = lv_tick_get();
        if (current_tick - last_tick >= 1000)
        {
            last_tick = current_tick;
            sec_counter++;
            snprintf(time_str, sizeof(time_str), "%02lu:%02lu:%02lu",
                     (sec_counter / 3600) % 24,
                     (sec_counter / 60) % 60,
                     sec_counter % 60);
            lv_label_set_text(label_time, time_str);
        }

        /* LVGL 核心任务处理器 */
        lv_timer_handler();

        /* 短暂延时，释放 CPU */
        Delay_ms(5);
    }
}
