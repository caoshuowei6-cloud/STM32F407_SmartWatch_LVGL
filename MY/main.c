/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F407 + LVGL 9 手表迁移主程序（阶段 1：显示与按键基准验证）
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
static lv_obj_t * card_box;

/**
  * @brief  创建手表迁移阶段 1 验证界面
  */
static void Create_Test_UI(void)
{
    /* 1. 设置主屏幕深色背景 (黑蓝微调 0x121824) */
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x121824), LV_PART_MAIN);

    /* 2. 创建中心圆角卡片 */
    card_box = lv_obj_create(scr);
    lv_obj_set_size(card_box, 220, 200);
    lv_obj_center(card_box);
    lv_obj_set_style_bg_color(card_box, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_color(card_box, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_box, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(card_box, 16, LV_PART_MAIN);
    lv_obj_remove_flag(card_box, LV_OBJ_FLAG_SCROLLABLE);

    /* 3. 顶部标题 */
    label_title = lv_label_create(card_box);
    lv_label_set_text(label_title, "F407 WATCH (LVGL 9)");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 5);

    /* 4. 模拟时间显示 */
    label_time = lv_label_create(card_box);
    lv_label_set_text(label_time, "12:00:00");
    lv_obj_set_style_text_color(label_time, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(label_time, LV_ALIGN_CENTER, 0, -10);

    /* 5. 按键状态指示 */
    label_key_info = lv_label_create(card_box);
    lv_label_set_text(label_key_info, "Press any KEY...");
    lv_obj_set_style_text_color(label_key_info, lv_color_hex(0x34D399), LV_PART_MAIN);
    lv_obj_align(label_key_info, LV_ALIGN_BOTTOM_MID, 0, -10);
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
    printf(" STM32F407 Watch LVGL 9 Init Starting... \r\n");
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

        /* 简单的秒计数更新，证明动画与定时刷新正常 */
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
