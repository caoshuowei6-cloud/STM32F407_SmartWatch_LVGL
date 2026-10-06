/**
  ******************************************************************************
  * @file    ui_watch.h
  * @brief   STM32F407 手表 LVGL 9 UI 框架与页面管理头文件
  ******************************************************************************
  */

#ifndef __UI_WATCH_H
#define __UI_WATCH_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* 字体前置声明 */
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);

/* 页面枚举（对照原 F1 手表 8 大核心功能及扩展） */
typedef enum {
    UI_PAGE_CLOCK = 0,    /* 主表盘时钟页 */
    UI_PAGE_MENU,         /* 8 应用轮播主菜单 */
    UI_PAGE_TIMER,        /* 1. 秒表计时器 (TIMER) */
    UI_PAGE_LED,          /* 2. LED 开关控制 (LED) */
    UI_PAGE_SENSOR,       /* 3. 传感器数据中心 (MPU6050 & AHT20) */
    UI_PAGE_GAME,         /* 4. 小恐龙跑酷 (GAME) */
    UI_PAGE_EMOJI,        /* 5. 太空人/表情 (EMOJI) */
    UI_PAGE_LEVEL,        /* 6. 水平仪 (LEVEL) */
    UI_PAGE_SETTING,      /* 7. 系统信息与设置 (SETTING) */
    UI_PAGE_COUNT
} ui_page_id_t;

/* UI 全局管理器接口 */
void UI_Watch_Init(void);
void UI_Watch_SwitchPage(ui_page_id_t target_page);
void UI_Watch_HandleKey(uint8_t key_val);
void UI_Watch_Update_100ms(void);
void UI_Watch_Update_1s(void);
ui_page_id_t UI_Watch_GetCurrentPage(void);

/* 各子页面构建与事件内部接口 */
/* 1. 时钟主表盘 */
lv_obj_t * ui_page_clock_create(lv_obj_t * parent);
void ui_page_clock_handle_key(uint8_t key_val);
void ui_page_clock_update_1s(void);

/* 2. 8 应用主菜单 */
lv_obj_t * ui_page_menu_create(lv_obj_t * parent);
void ui_page_menu_handle_key(uint8_t key_val);

/* 3. 秒表 */
lv_obj_t * ui_page_timer_create(lv_obj_t * parent);
void ui_page_timer_handle_key(uint8_t key_val);
void ui_page_timer_update_100ms(void);

/* 4. LED 控制 */
lv_obj_t * ui_page_led_create(lv_obj_t * parent);
void ui_page_led_handle_key(uint8_t key_val);

/* 5. 传感器中心 (MPU6050 + AHT20) */
lv_obj_t * ui_page_sensor_create(lv_obj_t * parent);
void ui_page_sensor_handle_key(uint8_t key_val);
void ui_page_sensor_update_1s(void);

/* 6. 系统信息与设置 */
lv_obj_t * ui_page_setting_create(lv_obj_t * parent);
void ui_page_setting_handle_key(uint8_t key_val);

/* 7. 占位页面 (GAME, EMOJI, LEVEL) */
lv_obj_t * ui_page_placeholder_create(lv_obj_t * parent, const char * title, const char * desc);
void ui_page_placeholder_handle_key(uint8_t key_val);

#endif /* __UI_WATCH_H */
