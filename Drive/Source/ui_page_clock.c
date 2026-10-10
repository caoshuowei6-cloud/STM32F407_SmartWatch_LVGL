/**
  ******************************************************************************
  * @file    ui_page_clock.c
  * @brief   STM32F407 手表 LVGL 9 时钟主表盘页面（接入硬件 RTC、真实电量与真实计步）
  ******************************************************************************
  */

#include "ui_watch.h"
#include "rtc_f407.h"
#include "adc_battery.h"
#include "mpu6050.h"
#include <stdio.h>

static lv_obj_t * s_clock_time_label = NULL;
static lv_obj_t * s_label_date = NULL;
static lv_obj_t * s_label_bat = NULL;
static lv_obj_t * s_label_steps = NULL;

static lv_obj_t * s_btn_menu = NULL;
static lv_obj_t * s_btn_settime = NULL;
static lv_obj_t * s_label_btn_menu = NULL;
static lv_obj_t * s_label_btn_settime = NULL;

static uint8_t  s_clock_sel = 0; /* 0: 菜单 (MENU), 1: 时间设置 (SET TIME) */

/**
  * @brief  更新底部两个导航按钮的高亮聚焦样式
  */
static void Update_Button_Focus(void)
{
    if (s_btn_menu == NULL || s_btn_settime == NULL) return;

    if (s_clock_sel == 0)
    {
        /* 聚焦 MENU */
        lv_obj_set_style_bg_color(s_btn_menu, lv_color_hex(0x0284C7), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_btn_menu, lv_color_hex(0x38BDF8), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_btn_menu, 2, LV_PART_MAIN);

        /* 失焦 SET TIME */
        lv_obj_set_style_bg_color(s_btn_settime, lv_color_hex(0x1E293B), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_btn_settime, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_btn_settime, 1, LV_PART_MAIN);
    }
    else
    {
        /* 失焦 MENU */
        lv_obj_set_style_bg_color(s_btn_menu, lv_color_hex(0x1E293B), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_btn_menu, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_btn_menu, 1, LV_PART_MAIN);

        /* 聚焦 SET TIME */
        lv_obj_set_style_bg_color(s_btn_settime, lv_color_hex(0x0284C7), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_btn_settime, lv_color_hex(0x38BDF8), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_btn_settime, 2, LV_PART_MAIN);
    }
}

/**
  * @brief  构建主表盘页面
  */
lv_obj_t * ui_page_clock_create(lv_obj_t * parent)
{
    lv_obj_t * top_box;
    lv_obj_t * center_card;
    lv_obj_t * steps_box;
    lv_obj_t * label_hint;
    rtc_time_date_t dt;
    char time_buf[16];
    char date_buf[20];
    char bat_buf[16];
    char steps_buf[20];
    uint8_t bat;
    uint32_t steps;

    /* 读取硬件 RTC、电量与计步 */
    RTC_F407_GetDateTime(&dt);
    bat = ADC_Battery_GetPercent();
    steps = MPU6050_GetStepCount();

    /* 1. 顶部状态栏小容器 (216x24) */
    top_box = lv_obj_create(parent);
    lv_obj_set_size(top_box, 216, 24);
    lv_obj_set_pos(top_box, 12, 10);
    lv_obj_set_style_bg_opa(top_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_box, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_box, LV_OBJ_FLAG_SCROLLABLE);

    s_label_date = lv_label_create(top_box);
    snprintf(date_buf, sizeof(date_buf), "%02d-%02d D:%d", dt.month, dt.date, dt.weekday);
    lv_label_set_text(s_label_date, date_buf);
    lv_obj_set_style_text_color(s_label_date, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(s_label_date, LV_ALIGN_LEFT_MID, 0, 0);

    s_label_bat = lv_label_create(top_box);
    snprintf(bat_buf, sizeof(bat_buf), "BAT %d%%", bat);
    lv_label_set_text(s_label_bat, bat_buf);
    lv_obj_set_style_text_color(s_label_bat, lv_color_hex(0x10B981), LV_PART_MAIN);
    lv_obj_align(s_label_bat, LV_ALIGN_RIGHT_MID, 0, 0);

    /* OTA 新固件验证标记: V2.0 亮黄色高亮居中徽标 */
    lv_obj_t * label_ver = lv_label_create(top_box);
    lv_label_set_text(label_ver, "V2.0");
    lv_obj_set_style_text_color(label_ver, lv_color_hex(0xFBBF24), LV_PART_MAIN);
    lv_obj_align(label_ver, LV_ALIGN_CENTER, 0, 0);

    /* 2. 中央大卡片 (216x104, 不透明深色底) */
    center_card = lv_obj_create(parent);
    lv_obj_set_size(center_card, 216, 104);
    lv_obj_set_pos(center_card, 12, 40);
    lv_obj_set_style_bg_color(center_card, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(center_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(center_card, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_border_width(center_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(center_card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(center_card, 0, LV_PART_MAIN);
    lv_obj_remove_flag(center_card, LV_OBJ_FLAG_SCROLLABLE);

    /* 2.1 居中时间标签 (固定 150x34，不透明，使用 montserrat_20 字体，杜绝跳秒闪烁) */
    s_clock_time_label = lv_label_create(center_card);
    lv_obj_set_size(s_clock_time_label, 150, 34);
    lv_obj_align(s_clock_time_label, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_text_font(s_clock_time_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_clock_time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_clock_time_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_clock_time_label, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_clock_time_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(s_clock_time_label, LV_OBJ_FLAG_SCROLLABLE);

    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", dt.hours, dt.minutes, dt.seconds);
    lv_label_set_text(s_clock_time_label, time_buf);

    /* 2.2 计步小徽章 (130x26) */
    steps_box = lv_obj_create(center_card);
    lv_obj_set_size(steps_box, 130, 26);
    lv_obj_align(steps_box, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(steps_box, lv_color_hex(0x064E3B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(steps_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(steps_box, lv_color_hex(0x059669), LV_PART_MAIN);
    lv_obj_set_style_border_width(steps_box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(steps_box, 13, LV_PART_MAIN);
    lv_obj_set_style_pad_all(steps_box, 0, LV_PART_MAIN);
    lv_obj_remove_flag(steps_box, LV_OBJ_FLAG_SCROLLABLE);

    s_label_steps = lv_label_create(steps_box);
    snprintf(steps_buf, sizeof(steps_buf), "Steps: %lu", steps);
    lv_label_set_text(s_label_steps, steps_buf);
    lv_obj_set_style_text_color(s_label_steps, lv_color_hex(0x6EE7B7), LV_PART_MAIN);
    lv_obj_center(s_label_steps);

    /* 3. 底部导航按钮 1: MENU 菜单入口 (100x48) */
    s_btn_menu = lv_obj_create(parent);
    lv_obj_set_size(s_btn_menu, 102, 48);
    lv_obj_set_pos(s_btn_menu, 14, 160);
    lv_obj_set_style_bg_opa(s_btn_menu, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_menu, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_btn_menu, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_btn_menu, LV_OBJ_FLAG_SCROLLABLE);

    s_label_btn_menu = lv_label_create(s_btn_menu);
    lv_label_set_text(s_label_btn_menu, "MENU");
    lv_obj_set_style_text_color(s_label_btn_menu, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_label_btn_menu);

    /* 4. 底部导航按钮 2: SET TIME 时间设置 (100x48) */
    s_btn_settime = lv_obj_create(parent);
    lv_obj_set_size(s_btn_settime, 102, 48);
    lv_obj_set_pos(s_btn_settime, 124, 160);
    lv_obj_set_style_bg_opa(s_btn_settime, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_settime, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_btn_settime, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_btn_settime, LV_OBJ_FLAG_SCROLLABLE);

    s_label_btn_settime = lv_label_create(s_btn_settime);
    lv_label_set_text(s_label_btn_settime, "SET TIME");
    lv_obj_set_style_text_color(s_label_btn_settime, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_label_btn_settime);

    /* 底部操作提示 (240x20) */
    label_hint = lv_label_create(parent);
    lv_label_set_text(label_hint, "K1/K2: Select  K3: Enter");
    lv_obj_set_style_text_color(label_hint, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(label_hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    /* 初始高亮状态 */
    Update_Button_Focus();

    return parent;
}

/**
  * @brief  处理主表盘按键事件
  */
void ui_page_clock_handle_key(uint8_t key_val)
{
    if (key_val == 1 || key_val == 2)
    {
        /* KEY1 或 KEY2 切换选中按钮 */
        s_clock_sel = (s_clock_sel == 0) ? 1 : 0;
        Update_Button_Focus();
    }
    else if (key_val == 3)
    {
        /* KEY3 短按确认 */
        if (s_clock_sel == 0)
        {
            UI_Watch_SwitchPage(UI_PAGE_MENU);
        }
        else
        {
            /* 直接进入交互式时间设置页 */
            UI_Watch_SwitchPage(UI_PAGE_SET_TIME);
        }
    }
    else if (key_val == 4)
    {
        /* KEY3 长按 1s: 快捷直达应用主菜单 */
        UI_Watch_SwitchPage(UI_PAGE_MENU);
    }
}

/**
  * @brief  时钟走时更新（每秒调用一次，精准同步硬件 RTC、电池与步数）
  */
void ui_page_clock_update_1s(void)
{
    rtc_time_date_t dt;
    char time_buf[16];
    char date_buf[20];
    char bat_buf[16];
    char steps_buf[20];
    uint8_t bat;
    uint32_t steps;

    if (s_clock_time_label == NULL) return;

    /* 1. 从硬件 RTC 获取精确时间 */
    RTC_F407_GetDateTime(&dt);
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", dt.hours, dt.minutes, dt.seconds);
    lv_label_set_text(s_clock_time_label, time_buf);

    /* 2. 更新日期 */
    if (s_label_date != NULL)
    {
        snprintf(date_buf, sizeof(date_buf), "%02d-%02d D:%d", dt.month, dt.date, dt.weekday);
        lv_label_set_text(s_label_date, date_buf);
    }

    /* 3. 更新电池电量 */
    if (s_label_bat != NULL)
    {
        bat = ADC_Battery_GetPercent();
        snprintf(bat_buf, sizeof(bat_buf), "BAT %d%%", bat);
        lv_label_set_text(s_label_bat, bat_buf);
    }

    /* 4. 更新步数 */
    if (s_label_steps != NULL)
    {
        steps = MPU6050_GetStepCount();
        snprintf(steps_buf, sizeof(steps_buf), "Steps: %lu", steps);
        lv_label_set_text(s_label_steps, steps_buf);
    }
}
