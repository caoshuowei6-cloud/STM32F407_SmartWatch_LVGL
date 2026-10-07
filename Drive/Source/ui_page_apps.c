/**
  ******************************************************************************
  * @file    ui_page_apps.c
  * @brief   STM32F407 手表 LVGL 9 子应用页面实现（秒表、LED、传感器、设置等）
  ******************************************************************************
  */

#include "ui_watch.h"
#include "led.h"
#include "aht20.h"
#include "mpu6050.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ============================================================================
   1. 秒表计时器 (TIMER)
   ============================================================================ */
static lv_obj_t * s_timer_label = NULL;
static lv_obj_t * s_timer_btns[4];
static uint8_t    s_timer_btn_sel = 1;  /* 0:BACK, 1:START, 2:STOP, 3:RESET */
static bool       s_timer_running = false;
static uint32_t   s_timer_tick100 = 0;  /* 100ms 计数单位 */

static void Timer_Update_Button_Focus(void)
{
    uint8_t i;
    for (i = 0; i < 4; i++)
    {
        if (s_timer_btns[i] == NULL) continue;
        if (i == s_timer_btn_sel)
        {
            lv_obj_set_style_bg_color(s_timer_btns[i], lv_color_hex(0x0284C7), LV_PART_MAIN);
            lv_obj_set_style_border_color(s_timer_btns[i], lv_color_hex(0x38BDF8), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_timer_btns[i], 2, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_bg_color(s_timer_btns[i], lv_color_hex(0x1E293B), LV_PART_MAIN);
            lv_obj_set_style_border_color(s_timer_btns[i], lv_color_hex(0x334155), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_timer_btns[i], 1, LV_PART_MAIN);
        }
    }
}

static void Timer_Format_Text(char * buf, size_t buf_size)
{
    uint32_t total_sec = s_timer_tick100 / 10;
    uint32_t min = (total_sec / 60) % 60;
    uint32_t sec = total_sec % 60;
    uint32_t dsec = s_timer_tick100 % 10;
    snprintf(buf, buf_size, "%02lu:%02lu.%lu", min, sec, dsec);
}

lv_obj_t * ui_page_timer_create(lv_obj_t * parent)
{
    lv_obj_t * top_bar;
    lv_obj_t * label_title;
    lv_obj_t * label_exit_hint;
    lv_obj_t * center_card;
    lv_obj_t * btns_box;
    static const char * btn_names[4] = {"BACK", "START", "STOP", "RESET"};
    char time_str[16];
    uint8_t i;
    lv_obj_t * hint;

    /* 顶部标头栏 (216x24) */
    top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 216, 24);
    lv_obj_set_pos(top_bar, 12, 10);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    label_title = lv_label_create(top_bar);
    lv_label_set_text(label_title, "STOPWATCH");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x0284C7), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    label_exit_hint = lv_label_create(top_bar);
    lv_label_set_text(label_exit_hint, "Hold K3 Exit");
    lv_obj_set_style_text_color(label_exit_hint, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(label_exit_hint, LV_ALIGN_RIGHT_MID, 0, 0);

    /* 中央时间卡片 (216x94, 固定不透明底，防跳变防闪烁) */
    center_card = lv_obj_create(parent);
    lv_obj_set_size(center_card, 216, 94);
    lv_obj_set_pos(center_card, 12, 40);
    lv_obj_set_style_bg_color(center_card, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(center_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(center_card, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_border_width(center_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(center_card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(center_card, 0, LV_PART_MAIN);
    lv_obj_remove_flag(center_card, LV_OBJ_FLAG_SCROLLABLE);

    s_timer_label = lv_label_create(center_card);
    lv_obj_set_size(s_timer_label, 160, 36);
    lv_obj_center(s_timer_label);
    lv_obj_set_style_text_font(s_timer_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_timer_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_timer_label, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_timer_label, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_timer_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(s_timer_label, LV_OBJ_FLAG_SCROLLABLE);

    Timer_Format_Text(time_str, sizeof(time_str));
    lv_label_set_text(s_timer_label, time_str);

    /* 底部 4 按钮横排容器 (216x46) */
    btns_box = lv_obj_create(parent);
    lv_obj_set_size(btns_box, 216, 46);
    lv_obj_set_pos(btns_box, 12, 148);
    lv_obj_set_style_bg_opa(btns_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btns_box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btns_box, 0, LV_PART_MAIN);
    lv_obj_remove_flag(btns_box, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0; i < 4; i++)
    {
        lv_obj_t * lbl;
        s_timer_btns[i] = lv_obj_create(btns_box);
        lv_obj_set_size(s_timer_btns[i], 50, 42);
        lv_obj_set_pos(s_timer_btns[i], i * 55, 0);
        lv_obj_set_style_bg_opa(s_timer_btns[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_timer_btns[i], 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_timer_btns[i], 0, LV_PART_MAIN);
        lv_obj_remove_flag(s_timer_btns[i], LV_OBJ_FLAG_SCROLLABLE);

        lbl = lv_label_create(s_timer_btns[i]);
        lv_label_set_text(lbl, btn_names[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_center(lbl);
    }

    /* 底部操作指引 */
    hint = lv_label_create(parent);
    lv_label_set_text(hint, "K1/K2: Select  K3: Trigger");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    Timer_Update_Button_Focus();
    return parent;
}

void ui_page_timer_handle_key(uint8_t key_val)
{
    char time_str[16];

    if (key_val == 1)
    {
        /* K1 左移 */
        s_timer_btn_sel = (s_timer_btn_sel + 3) % 4;
        Timer_Update_Button_Focus();
    }
    else if (key_val == 2)
    {
        /* K2 右移 */
        s_timer_btn_sel = (s_timer_btn_sel + 1) % 4;
        Timer_Update_Button_Focus();
    }
    else if (key_val == 3)
    {
        /* K3 触发选中按钮 */
        switch (s_timer_btn_sel)
        {
            case 0: /* BACK */
                UI_Watch_SwitchPage(UI_PAGE_MENU);
                break;
            case 1: /* START */
                s_timer_running = true;
                break;
            case 2: /* STOP */
                s_timer_running = false;
                break;
            case 3: /* RESET */
                s_timer_running = false;
                s_timer_tick100 = 0;
                Timer_Format_Text(time_str, sizeof(time_str));
                if (s_timer_label) lv_label_set_text(s_timer_label, time_str);
                break;
            default:
                break;
        }
    }
    else if (key_val == 4)
    {
        /* K3 长按直接返回菜单 */
        UI_Watch_SwitchPage(UI_PAGE_MENU);
    }
}

void ui_page_timer_update_100ms(void)
{
    char time_str[16];

    if (!s_timer_running || s_timer_label == NULL) return;

    s_timer_tick100++;
    Timer_Format_Text(time_str, sizeof(time_str));
    lv_label_set_text(s_timer_label, time_str);
}


/* ============================================================================
   2. LED 控制页 (LED)
   ============================================================================ */
static lv_obj_t * s_led_btns[3];
static lv_obj_t * s_led_indicator = NULL;
static lv_obj_t * s_led_status_label = NULL;
static uint8_t    s_led_btn_sel = 1; /* 0:BACK, 1:OFF, 2:ON */
static bool       s_led_state = false;

static void Led_Update_Button_Focus(void)
{
    uint8_t i;
    for (i = 0; i < 3; i++)
    {
        if (s_led_btns[i] == NULL) continue;
        if (i == s_led_btn_sel)
        {
            lv_obj_set_style_bg_color(s_led_btns[i], lv_color_hex(0x16A34A), LV_PART_MAIN);
            lv_obj_set_style_border_color(s_led_btns[i], lv_color_hex(0x4ADE80), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_led_btns[i], 2, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_bg_color(s_led_btns[i], lv_color_hex(0x1E293B), LV_PART_MAIN);
            lv_obj_set_style_border_color(s_led_btns[i], lv_color_hex(0x334155), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_led_btns[i], 1, LV_PART_MAIN);
        }
    }
}

static void Led_Apply_State(bool on)
{
    s_led_state = on;
    if (on)
    {
        LED1_ON;
        if (s_led_indicator)
        {
            lv_obj_set_style_bg_color(s_led_indicator, lv_color_hex(0x22C55E), LV_PART_MAIN);
            lv_obj_set_style_border_color(s_led_indicator, lv_color_hex(0x86EFAC), LV_PART_MAIN);
        }
        if (s_led_status_label)
        {
            lv_label_set_text(s_led_status_label, "PC13: ON (LOW)");
            lv_obj_set_style_text_color(s_led_status_label, lv_color_hex(0x4ADE80), LV_PART_MAIN);
        }
    }
    else
    {
        LED1_OFF;
        if (s_led_indicator)
        {
            lv_obj_set_style_bg_color(s_led_indicator, lv_color_hex(0x334155), LV_PART_MAIN);
            lv_obj_set_style_border_color(s_led_indicator, lv_color_hex(0x475569), LV_PART_MAIN);
        }
        if (s_led_status_label)
        {
            lv_label_set_text(s_led_status_label, "PC13: OFF (HIGH)");
            lv_obj_set_style_text_color(s_led_status_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
        }
    }
}

lv_obj_t * ui_page_led_create(lv_obj_t * parent)
{
    lv_obj_t * top_bar;
    lv_obj_t * label_title;
    lv_obj_t * center_card;
    lv_obj_t * btns_box;
    static const char * btn_names[3] = {"BACK", "OFF", "ON"};
    uint8_t i;
    lv_obj_t * hint;

    /* 顶部标头栏 (216x24) */
    top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 216, 24);
    lv_obj_set_pos(top_bar, 12, 10);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    label_title = lv_label_create(top_bar);
    lv_label_set_text(label_title, "LED CONTROLLER");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    /* 中央状态展示卡片 (216x100) */
    center_card = lv_obj_create(parent);
    lv_obj_set_size(center_card, 216, 100);
    lv_obj_set_pos(center_card, 12, 40);
    lv_obj_set_style_bg_color(center_card, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(center_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(center_card, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_border_width(center_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(center_card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(center_card, 0, LV_PART_MAIN);
    lv_obj_remove_flag(center_card, LV_OBJ_FLAG_SCROLLABLE);

    /* 发光圆点指示灯 (32x32) */
    s_led_indicator = lv_obj_create(center_card);
    lv_obj_set_size(s_led_indicator, 34, 34);
    lv_obj_align(s_led_indicator, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_radius(s_led_indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_led_indicator, 2, LV_PART_MAIN);
    lv_obj_remove_flag(s_led_indicator, LV_OBJ_FLAG_SCROLLABLE);

    /* 状态文字 (160x24) */
    s_led_status_label = lv_label_create(center_card);
    lv_obj_set_size(s_led_status_label, 160, 24);
    lv_obj_align(s_led_status_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_align(s_led_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(s_led_status_label, LV_OBJ_FLAG_SCROLLABLE);

    /* 底部 3 按钮横排容器 (216x46) */
    btns_box = lv_obj_create(parent);
    lv_obj_set_size(btns_box, 216, 46);
    lv_obj_set_pos(btns_box, 12, 150);
    lv_obj_set_style_bg_opa(btns_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btns_box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btns_box, 0, LV_PART_MAIN);
    lv_obj_remove_flag(btns_box, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0; i < 3; i++)
    {
        lv_obj_t * lbl;
        s_led_btns[i] = lv_obj_create(btns_box);
        lv_obj_set_size(s_led_btns[i], 66, 42);
        lv_obj_set_pos(s_led_btns[i], i * 75, 0);
        lv_obj_set_style_bg_opa(s_led_btns[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_led_btns[i], 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_led_btns[i], 0, LV_PART_MAIN);
        lv_obj_remove_flag(s_led_btns[i], LV_OBJ_FLAG_SCROLLABLE);

        lbl = lv_label_create(s_led_btns[i]);
        lv_label_set_text(lbl, btn_names[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_center(lbl);
    }

    /* 底部提示 */
    hint = lv_label_create(parent);
    lv_label_set_text(hint, "K1/K2: Select  K3: Trigger");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* 应用当前状态与聚焦样式 */
    Led_Apply_State(s_led_state);
    Led_Update_Button_Focus();

    return parent;
}

void ui_page_led_handle_key(uint8_t key_val)
{
    if (key_val == 1)
    {
        /* K1 左移 */
        s_led_btn_sel = (s_led_btn_sel + 2) % 3;
        Led_Update_Button_Focus();
    }
    else if (key_val == 2)
    {
        /* K2 右移 */
        s_led_btn_sel = (s_led_btn_sel + 1) % 3;
        Led_Update_Button_Focus();
    }
    else if (key_val == 3)
    {
        /* K3 触发 */
        switch (s_led_btn_sel)
        {
            case 0: /* BACK */
                UI_Watch_SwitchPage(UI_PAGE_MENU);
                break;
            case 1: /* OFF */
                Led_Apply_State(false);
                break;
            case 2: /* ON */
                Led_Apply_State(true);
                break;
            default:
                break;
        }
    }
    else if (key_val == 4)
    {
        /* K3 长按返回菜单 */
        UI_Watch_SwitchPage(UI_PAGE_MENU);
    }
}


/* ============================================================================
   3. 传感器数据中心 (MPU6050 & AHT20)
   ============================================================================ */
static lv_obj_t * s_sensor_temp_label = NULL;
static lv_obj_t * s_sensor_mpu_label = NULL;

lv_obj_t * ui_page_sensor_create(lv_obj_t * parent)
{
    lv_obj_t * top_bar;
    lv_obj_t * label_title;
    lv_obj_t * card_aht;
    lv_obj_t * card_mpu;
    lv_obj_t * btn_back;
    lv_obj_t * lbl_back;
    lv_obj_t * tag_aht;
    lv_obj_t * tag_mpu;

    /* 顶部标头栏 (216x24) */
    top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 216, 24);
    lv_obj_set_pos(top_bar, 12, 10);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    label_title = lv_label_create(top_bar);
    lv_label_set_text(label_title, "SENSORS DASHBOARD");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xF59E0B), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    /* 卡片 1: AHT20 环境温湿度 (216x54) */
    card_aht = lv_obj_create(parent);
    lv_obj_set_size(card_aht, 216, 54);
    lv_obj_set_pos(card_aht, 12, 38);
    lv_obj_set_style_bg_color(card_aht, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card_aht, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card_aht, lv_color_hex(0x3B82F6), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_aht, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card_aht, 10, LV_PART_MAIN);
    lv_obj_remove_flag(card_aht, LV_OBJ_FLAG_SCROLLABLE);

    tag_aht = lv_label_create(card_aht);
    lv_label_set_text(tag_aht, "AHT20 Temp & Humi");
    lv_obj_set_style_text_color(tag_aht, lv_color_hex(0x60A5FA), LV_PART_MAIN);
    lv_obj_align(tag_aht, LV_ALIGN_TOP_LEFT, 0, -6);

    s_sensor_temp_label = lv_label_create(card_aht);
    lv_label_set_text(s_sensor_temp_label, "Reading...");
    lv_obj_set_style_text_color(s_sensor_temp_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(s_sensor_temp_label, LV_ALIGN_BOTTOM_LEFT, 0, 6);

    /* 卡片 2: MPU6050 姿态角与步数 (216x68) */
    card_mpu = lv_obj_create(parent);
    lv_obj_set_size(card_mpu, 216, 68);
    lv_obj_set_pos(card_mpu, 12, 100);
    lv_obj_set_style_bg_color(card_mpu, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card_mpu, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card_mpu, lv_color_hex(0xF59E0B), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_mpu, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card_mpu, 10, LV_PART_MAIN);
    lv_obj_remove_flag(card_mpu, LV_OBJ_FLAG_SCROLLABLE);

    tag_mpu = lv_label_create(card_mpu);
    lv_label_set_text(tag_mpu, "MPU6050 Motion & Steps");
    lv_obj_set_style_text_color(tag_mpu, lv_color_hex(0xFBBF24), LV_PART_MAIN);
    lv_obj_align(tag_mpu, LV_ALIGN_TOP_LEFT, 0, -6);

    s_sensor_mpu_label = lv_label_create(card_mpu);
    lv_label_set_text(s_sensor_mpu_label, "Calculating...");
    lv_obj_set_style_text_color(s_sensor_mpu_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(s_sensor_mpu_label, LV_ALIGN_BOTTOM_LEFT, 0, 6);

    /* 底部返回按钮 (216x40) */
    btn_back = lv_obj_create(parent);
    lv_obj_set_size(btn_back, 216, 40);
    lv_obj_set_pos(btn_back, 12, 178);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0284C7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_back, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 8, LV_PART_MAIN);
    lv_obj_remove_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);

    lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK TO MENU (KEY3)");
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(lbl_back);

    /* 立即刷新一次真实数据 */
    ui_page_sensor_update_1s();

    return parent;
}

void ui_page_sensor_handle_key(uint8_t key_val)
{
    if (key_val == 3 || key_val == 4)
    {
        /* 按 KEY3 短按或长按返回菜单 */
        UI_Watch_SwitchPage(UI_PAGE_MENU);
    }
}

void ui_page_sensor_update_1s(void)
{
    char temp_buf[32];
    char mpu_buf[48];
    float t, h;
    float pitch, roll, yaw;
    uint32_t steps;

    if (s_sensor_temp_label == NULL || s_sensor_mpu_label == NULL) return;

    if (AHT20_IsOnline())
    {
        t = AHT20_GetLastTemp();
        h = AHT20_GetLastHumi();
        snprintf(temp_buf, sizeof(temp_buf), "%.1f C   |   %.1f %%RH", t, h);
    }
    else
    {
        snprintf(temp_buf, sizeof(temp_buf), "Offline (PB6/PB7)");
    }
    lv_label_set_text(s_sensor_temp_label, temp_buf);

    if (MPU6050_IsOnline())
    {
        MPU6050_GetAttitude(&pitch, &roll, &yaw);
        steps = MPU6050_GetStepCount();
        snprintf(mpu_buf, sizeof(mpu_buf), "P:%.1f R:%.1f Y:%.1f\nSteps: %lu",
                 pitch, roll, yaw, (unsigned long)steps);
    }
    else
    {
        snprintf(mpu_buf, sizeof(mpu_buf), "Offline (PF0/PF1)\nCheck VCC/GND/Wire");
    }
    lv_label_set_text(s_sensor_mpu_label, mpu_buf);
}


/* ============================================================================
   4. 系统信息与设置页 (SETTING)
   ============================================================================ */
lv_obj_t * ui_page_setting_create(lv_obj_t * parent)
{
    lv_obj_t * top_bar;
    lv_obj_t * label_title;
    lv_obj_t * card_info;
    lv_obj_t * info_text;
    lv_obj_t * btn_back;
    lv_obj_t * lbl_back;

    top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 216, 24);
    lv_obj_set_pos(top_bar, 12, 10);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    label_title = lv_label_create(top_bar);
    lv_label_set_text(label_title, "DEVICE INFO");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xA855F7), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    /* 硬件规格卡片 (216x126) */
    card_info = lv_obj_create(parent);
    lv_obj_set_size(card_info, 216, 126);
    lv_obj_set_pos(card_info, 12, 40);
    lv_obj_set_style_bg_color(card_info, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card_info, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card_info, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_info, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card_info, 10, LV_PART_MAIN);
    lv_obj_remove_flag(card_info, LV_OBJ_FLAG_SCROLLABLE);

    info_text = lv_label_create(card_info);
    lv_label_set_text(info_text,
        "MCU: STM32F407ZGT6\n"
        "Core: M4F @ 168MHz\n"
        "Screen: 240x240 ST7789\n"
        "GUI: LVGL 9.5.0-dev\n"
        "Keys: PB1/PA6/PA4/PA15\n"
        "Firmware: Stage 2 (OK)");
    lv_obj_set_style_text_color(info_text, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_center(info_text);

    /* 底部返回表盘按钮 */
    btn_back = lv_obj_create(parent);
    lv_obj_set_size(btn_back, 216, 40);
    lv_obj_set_pos(btn_back, 12, 178);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0284C7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_back, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 8, LV_PART_MAIN);
    lv_obj_remove_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);

    lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK TO CLOCK (KEY3)");
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(lbl_back);

    return parent;
}

void ui_page_setting_handle_key(uint8_t key_val)
{
    if (key_val == 3 || key_val == 4)
    {
        /* 返回时钟主表盘 */
        UI_Watch_SwitchPage(UI_PAGE_CLOCK);
    }
}


/* ============================================================================
   5. 水平仪应用 (SPIRIT LEVEL)
   ============================================================================ */
static lv_obj_t * s_level_bubble = NULL;
static lv_obj_t * s_level_target_ring = NULL;
static lv_obj_t * s_level_status_label = NULL;
static float      s_calib_pitch = 0.0f;
static float      s_calib_roll  = 0.0f;

lv_obj_t * ui_page_level_create(lv_obj_t * parent)
{
    lv_obj_t * top_bar;
    lv_obj_t * label_title;
    lv_obj_t * card_dial;
    lv_obj_t * outer_ring;
    lv_obj_t * mid_ring;
    lv_obj_t * cross_h;
    lv_obj_t * cross_v;
    lv_obj_t * center_dot;
    lv_obj_t * btn_back;
    lv_obj_t * lbl_back;
    lv_obj_t * hint;

    /* 顶部标头栏 (216x24) */
    top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 216, 24);
    lv_obj_set_pos(top_bar, 12, 10);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    label_title = lv_label_create(top_bar);
    lv_label_set_text(label_title, "SPIRIT LEVEL");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x06B6D4), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    /* 中央刻度卡片 (216x136) */
    card_dial = lv_obj_create(parent);
    lv_obj_set_size(card_dial, 216, 136);
    lv_obj_set_pos(card_dial, 12, 38);
    lv_obj_set_style_bg_color(card_dial, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card_dial, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card_dial, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_dial, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card_dial, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card_dial, 0, LV_PART_MAIN);
    lv_obj_remove_flag(card_dial, LV_OBJ_FLAG_SCROLLABLE);

    /* 刻度盘中心: (108, 56) */
    /* 外环圈 (100x100, 半径 50) */
    outer_ring = lv_obj_create(card_dial);
    lv_obj_set_size(outer_ring, 100, 100);
    lv_obj_set_pos(outer_ring, 108 - 50, 56 - 50);
    lv_obj_set_style_radius(outer_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(outer_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(outer_ring, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_border_width(outer_ring, 2, LV_PART_MAIN);
    lv_obj_remove_flag(outer_ring, LV_OBJ_FLAG_SCROLLABLE);

    /* 中圈 (60x60, 半径 30) */
    mid_ring = lv_obj_create(card_dial);
    lv_obj_set_size(mid_ring, 60, 60);
    lv_obj_set_pos(mid_ring, 108 - 30, 56 - 30);
    lv_obj_set_style_radius(mid_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mid_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(mid_ring, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_width(mid_ring, 1, LV_PART_MAIN);
    lv_obj_remove_flag(mid_ring, LV_OBJ_FLAG_SCROLLABLE);

    /* 十字基准线 */
    cross_h = lv_obj_create(card_dial);
    lv_obj_set_size(cross_h, 100, 1);
    lv_obj_set_pos(cross_h, 108 - 50, 56);
    lv_obj_set_style_bg_color(cross_h, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_width(cross_h, 0, LV_PART_MAIN);
    lv_obj_remove_flag(cross_h, LV_OBJ_FLAG_SCROLLABLE);

    cross_v = lv_obj_create(card_dial);
    lv_obj_set_size(cross_v, 1, 100);
    lv_obj_set_pos(cross_v, 108, 56 - 50);
    lv_obj_set_style_bg_color(cross_v, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_width(cross_v, 0, LV_PART_MAIN);
    lv_obj_remove_flag(cross_v, LV_OBJ_FLAG_SCROLLABLE);

    /* 内部中心目标圈 (28x28, 半径 14) */
    s_level_target_ring = lv_obj_create(card_dial);
    lv_obj_set_size(s_level_target_ring, 28, 28);
    lv_obj_set_pos(s_level_target_ring, 108 - 14, 56 - 14);
    lv_obj_set_style_radius(s_level_target_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_level_target_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_level_target_ring, lv_color_hex(0x0284C7), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_level_target_ring, 2, LV_PART_MAIN);
    lv_obj_remove_flag(s_level_target_ring, LV_OBJ_FLAG_SCROLLABLE);

    /* 中心红心点 (4x4) */
    center_dot = lv_obj_create(card_dial);
    lv_obj_set_size(center_dot, 4, 4);
    lv_obj_set_pos(center_dot, 108 - 2, 56 - 2);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(center_dot, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_border_width(center_dot, 0, LV_PART_MAIN);
    lv_obj_remove_flag(center_dot, LV_OBJ_FLAG_SCROLLABLE);

    /* 动态流体气泡 (20x20) */
    s_level_bubble = lv_obj_create(card_dial);
    lv_obj_set_size(s_level_bubble, 20, 20);
    lv_obj_set_pos(s_level_bubble, 108 - 10, 56 - 10);
    lv_obj_set_style_radius(s_level_bubble, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_level_bubble, lv_color_hex(0x0EA5E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_level_bubble, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_level_bubble, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_level_bubble, 2, LV_PART_MAIN);
    lv_obj_remove_flag(s_level_bubble, LV_OBJ_FLAG_SCROLLABLE);

    /* 实时角度与状态标签 (200x20) */
    s_level_status_label = lv_label_create(card_dial);
    lv_obj_set_size(s_level_status_label, 200, 20);
    lv_obj_set_pos(s_level_status_label, 8, 110);
    lv_obj_set_style_text_align(s_level_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_level_status_label, "Measuring...");
    lv_obj_set_style_text_color(s_level_status_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_remove_flag(s_level_status_label, LV_OBJ_FLAG_SCROLLABLE);

    /* 底部返回按钮 (216x40) */
    btn_back = lv_obj_create(parent);
    lv_obj_set_size(btn_back, 216, 40);
    lv_obj_set_pos(btn_back, 12, 178);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0284C7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_back, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 8, LV_PART_MAIN);
    lv_obj_remove_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);

    lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK TO MENU (KEY3)");
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(lbl_back);

    /* 操作指引 */
    hint = lv_label_create(parent);
    lv_label_set_text(hint, "K1: Zero  K2: Reset  K3: Exit");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    return parent;
}

void ui_page_level_handle_key(uint8_t key_val)
{
    float p, r, y;

    if (key_val == 1)
    {
        /* KEY1: 相对归零校准（将当前摆放姿态设为水平基准） */
        MPU6050_GetAttitude(&p, &r, &y);
        s_calib_pitch = p;
        s_calib_roll  = r;
    }
    else if (key_val == 2)
    {
        /* KEY2: 重置校准，恢复绝对地心引力水平 */
        s_calib_pitch = 0.0f;
        s_calib_roll  = 0.0f;
    }
    else if (key_val == 3 || key_val == 4)
    {
        /* KEY3: 返回应用菜单 */
        UI_Watch_SwitchPage(UI_PAGE_MENU);
    }
}

void ui_page_level_update_100ms(void)
{
    float raw_p, raw_r, raw_y;
    float p, r;
    float dx, dy, dist;
    const float max_radius = 40.0f; /* 限制在 100px 圈内 */
    int bx, by;
    char buf[36];

    if (s_level_bubble == NULL || s_level_status_label == NULL) return;

    MPU6050_GetAttitude(&raw_p, &raw_r, &raw_y);
    p = raw_p - s_calib_pitch;
    r = raw_r - s_calib_roll;

    /* 物理坐标映射：倾角转换为像素位移 (1° 对应 2.0 像素) */
    dx = -r * 2.0f;
    dy = p * 2.0f;

    dist = sqrtf(dx * dx + dy * dy);
    if (dist > max_radius)
    {
        dx = dx * (max_radius / dist);
        dy = dy * (max_radius / dist);
    }

    bx = (int)(108.0f + dx - 10.0f);
    by = (int)(56.0f + dy - 10.0f);
    lv_obj_set_pos(s_level_bubble, bx, by);

    /* 检测是否水平平衡（±2.0° 视为完美水平） */
    if (fabsf(p) <= 2.0f && fabsf(r) <= 2.0f)
    {
        /* 水平平衡：高亮翠绿 */
        lv_obj_set_style_bg_color(s_level_bubble, lv_color_hex(0x16A34A), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_level_bubble, lv_color_hex(0x4ADE80), LV_PART_MAIN);
        if (s_level_target_ring)
        {
            lv_obj_set_style_border_color(s_level_target_ring, lv_color_hex(0x22C55E), LV_PART_MAIN);
        }
        snprintf(buf, sizeof(buf), "[ LEVEL BALANCED (0.0) ]");
        lv_obj_set_style_text_color(s_level_status_label, lv_color_hex(0x4ADE80), LV_PART_MAIN);
    }
    else
    {
        /* 倾斜状态：高科技天蓝 */
        lv_obj_set_style_bg_color(s_level_bubble, lv_color_hex(0x0284C7), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_level_bubble, lv_color_hex(0x38BDF8), LV_PART_MAIN);
        if (s_level_target_ring)
        {
            lv_obj_set_style_border_color(s_level_target_ring, lv_color_hex(0x0284C7), LV_PART_MAIN);
        }
        snprintf(buf, sizeof(buf), "P: %+.1f deg | R: %+.1f deg", p, r);
        lv_obj_set_style_text_color(s_level_status_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    }
    lv_label_set_text(s_level_status_label, buf);
}


/* ============================================================================
   6. 动态表情包 / 电子萌宠 (CYBER PET EMOJI)
   ============================================================================ */
static lv_obj_t * s_emoji_eye_l = NULL;
static lv_obj_t * s_emoji_eye_r = NULL;
static lv_obj_t * s_emoji_eyebrow_l = NULL;
static lv_obj_t * s_emoji_eyebrow_r = NULL;
static lv_obj_t * s_emoji_blush_l = NULL;
static lv_obj_t * s_emoji_blush_r = NULL;
static lv_obj_t * s_emoji_mouth = NULL;
static lv_obj_t * s_emoji_mood_label = NULL;

static uint8_t  s_emoji_mode = 0;   /* 0:AUTO, 1:HAPPY, 2:WINK, 3:SURPRISED, 4:SLEEPY */
static uint16_t s_emoji_tick = 0;

lv_obj_t * ui_page_emoji_create(lv_obj_t * parent)
{
    lv_obj_t * top_bar;
    lv_obj_t * label_title;
    lv_obj_t * card_face;
    lv_obj_t * btn_back;
    lv_obj_t * lbl_back;
    lv_obj_t * hint;

    /* 顶部标头栏 (216x24) */
    top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 216, 24);
    lv_obj_set_pos(top_bar, 12, 10);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    label_title = lv_label_create(top_bar);
    lv_label_set_text(label_title, "CYBER PET");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xEC4899), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    s_emoji_mood_label = lv_label_create(top_bar);
    lv_label_set_text(s_emoji_mood_label, "MODE: AUTO");
    lv_obj_set_style_text_color(s_emoji_mood_label, lv_color_hex(0xF472B6), LV_PART_MAIN);
    lv_obj_align(s_emoji_mood_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* 呆萌脸庞主卡片 (216x136) */
    card_face = lv_obj_create(parent);
    lv_obj_set_size(card_face, 216, 136);
    lv_obj_set_pos(card_face, 12, 38);
    lv_obj_set_style_bg_color(card_face, lv_color_hex(0x181024), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card_face, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card_face, lv_color_hex(0x2D1B4E), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_face, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card_face, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card_face, 0, LV_PART_MAIN);
    lv_obj_remove_flag(card_face, LV_OBJ_FLAG_SCROLLABLE);

    /* 左/右 眉毛 (26x5) */
    s_emoji_eyebrow_l = lv_obj_create(card_face);
    lv_obj_set_size(s_emoji_eyebrow_l, 26, 5);
    lv_obj_set_pos(s_emoji_eyebrow_l, 54, 28);
    lv_obj_set_style_bg_color(s_emoji_eyebrow_l, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_set_style_radius(s_emoji_eyebrow_l, 3, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_emoji_eyebrow_l, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_emoji_eyebrow_l, LV_OBJ_FLAG_SCROLLABLE);

    s_emoji_eyebrow_r = lv_obj_create(card_face);
    lv_obj_set_size(s_emoji_eyebrow_r, 26, 5);
    lv_obj_set_pos(s_emoji_eyebrow_r, 136, 28);
    lv_obj_set_style_bg_color(s_emoji_eyebrow_r, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_set_style_radius(s_emoji_eyebrow_r, 3, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_emoji_eyebrow_r, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_emoji_eyebrow_r, LV_OBJ_FLAG_SCROLLABLE);

    /* 左/右 大眼睛 (30x30 圆形) */
    s_emoji_eye_l = lv_obj_create(card_face);
    lv_obj_set_size(s_emoji_eye_l, 30, 30);
    lv_obj_set_pos(s_emoji_eye_l, 52, 42);
    lv_obj_set_style_bg_color(s_emoji_eye_l, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_radius(s_emoji_eye_l, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_emoji_eye_l, lv_color_hex(0x7DD3FC), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_emoji_eye_l, 2, LV_PART_MAIN);
    lv_obj_remove_flag(s_emoji_eye_l, LV_OBJ_FLAG_SCROLLABLE);

    s_emoji_eye_r = lv_obj_create(card_face);
    lv_obj_set_size(s_emoji_eye_r, 30, 30);
    lv_obj_set_pos(s_emoji_eye_r, 134, 42);
    lv_obj_set_style_bg_color(s_emoji_eye_r, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_radius(s_emoji_eye_r, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_emoji_eye_r, lv_color_hex(0x7DD3FC), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_emoji_eye_r, 2, LV_PART_MAIN);
    lv_obj_remove_flag(s_emoji_eye_r, LV_OBJ_FLAG_SCROLLABLE);

    /* 左右 腮红 (18x8) */
    s_emoji_blush_l = lv_obj_create(card_face);
    lv_obj_set_size(s_emoji_blush_l, 18, 8);
    lv_obj_set_pos(s_emoji_blush_l, 36, 76);
    lv_obj_set_style_bg_color(s_emoji_blush_l, lv_color_hex(0xF472B6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_emoji_blush_l, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(s_emoji_blush_l, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_emoji_blush_l, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_emoji_blush_l, LV_OBJ_FLAG_SCROLLABLE);

    s_emoji_blush_r = lv_obj_create(card_face);
    lv_obj_set_size(s_emoji_blush_r, 18, 8);
    lv_obj_set_pos(s_emoji_blush_r, 162, 76);
    lv_obj_set_style_bg_color(s_emoji_blush_r, lv_color_hex(0xF472B6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_emoji_blush_r, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(s_emoji_blush_r, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_emoji_blush_r, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_emoji_blush_r, LV_OBJ_FLAG_SCROLLABLE);

    /* 萌趣小嘴 (24x8) */
    s_emoji_mouth = lv_obj_create(card_face);
    lv_obj_set_size(s_emoji_mouth, 24, 8);
    lv_obj_set_pos(s_emoji_mouth, 96, 88);
    lv_obj_set_style_bg_color(s_emoji_mouth, lv_color_hex(0xFB7185), LV_PART_MAIN);
    lv_obj_set_style_radius(s_emoji_mouth, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_emoji_mouth, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_emoji_mouth, LV_OBJ_FLAG_SCROLLABLE);

    /* 底部返回按钮 (216x40) */
    btn_back = lv_obj_create(parent);
    lv_obj_set_size(btn_back, 216, 40);
    lv_obj_set_pos(btn_back, 12, 178);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x9333EA), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0xC084FC), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_back, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 8, LV_PART_MAIN);
    lv_obj_remove_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);

    lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK TO MENU (KEY3)");
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(lbl_back);

    /* 底部提示 */
    hint = lv_label_create(parent);
    lv_label_set_text(hint, "K1: Mood  K2: Poke  K3: Exit");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    s_emoji_tick = 0;
    return parent;
}

void ui_page_emoji_handle_key(uint8_t key_val)
{
    static const char * mode_names[5] = {
        "MODE: AUTO", "MODE: HAPPY", "MODE: WINK", "MODE: SURPRISED", "MODE: SLEEPY"
    };

    if (key_val == 1)
    {
        /* KEY1: 切换心情模式 */
        s_emoji_mode = (s_emoji_mode + 1) % 5;
        s_emoji_tick = 0;
        if (s_emoji_mood_label)
        {
            lv_label_set_text(s_emoji_mood_label, mode_names[s_emoji_mode]);
        }
    }
    else if (key_val == 2)
    {
        /* KEY2: 触碰互动（立刻触发爱心放电眨眼） */
        s_emoji_tick = 38;
    }
    else if (key_val == 3 || key_val == 4)
    {
        /* KEY3: 返回应用菜单 */
        UI_Watch_SwitchPage(UI_PAGE_MENU);
    }
}

void ui_page_emoji_update_100ms(void)
{
    uint16_t phase;

    if (s_emoji_eye_l == NULL || s_emoji_eye_r == NULL) return;

    s_emoji_tick++;

    if (s_emoji_mode == 0)
    {
        /* AUTO 自动轮播动画 (循环周期 70 = 7秒) */
        phase = s_emoji_tick % 70;
    }
    else if (s_emoji_mode == 1)
    {
        /* HAPPY 模式 */
        phase = 55;
    }
    else if (s_emoji_mode == 2)
    {
        /* WINK 模式 */
        phase = 42;
    }
    else if (s_emoji_mode == 3)
    {
        /* SURPRISED 惊讶模式 */
        phase = 999;
    }
    else
    {
        /* SLEEPY 瞌睡模式 */
        phase = 888;
    }

    if (phase < 20)
    {
        /* 1. 呆萌注视直视 (2秒) */
        lv_obj_set_size(s_emoji_eye_l, 30, 30);
        lv_obj_set_pos(s_emoji_eye_l, 52, 42);
        lv_obj_set_size(s_emoji_eye_r, 30, 30);
        lv_obj_set_pos(s_emoji_eye_r, 134, 42);

        lv_obj_set_pos(s_emoji_eyebrow_l, 54, 28);
        lv_obj_set_pos(s_emoji_eyebrow_r, 136, 28);

        lv_obj_set_size(s_emoji_mouth, 24, 8);
        lv_obj_set_pos(s_emoji_mouth, 96, 88);
        lv_obj_set_style_bg_opa(s_emoji_blush_l, LV_OPA_40, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_emoji_blush_r, LV_OPA_40, LV_PART_MAIN);
    }
    else if (phase < 23)
    {
        /* 2. 瞬间快速眨眼 (0.3秒) */
        lv_obj_set_size(s_emoji_eye_l, 30, 4);
        lv_obj_set_pos(s_emoji_eye_l, 52, 55);
        lv_obj_set_size(s_emoji_eye_r, 30, 4);
        lv_obj_set_pos(s_emoji_eye_r, 134, 55);

        lv_obj_set_pos(s_emoji_eyebrow_l, 54, 32);
        lv_obj_set_pos(s_emoji_eyebrow_r, 136, 32);
    }
    else if (phase < 36)
    {
        /* 3. 俏皮向左看 (1.3秒) */
        lv_obj_set_size(s_emoji_eye_l, 30, 30);
        lv_obj_set_pos(s_emoji_eye_l, 46, 42);
        lv_obj_set_size(s_emoji_eye_r, 30, 30);
        lv_obj_set_pos(s_emoji_eye_r, 128, 42);

        lv_obj_set_pos(s_emoji_eyebrow_l, 48, 26);
        lv_obj_set_pos(s_emoji_eyebrow_r, 130, 30);

        lv_obj_set_size(s_emoji_mouth, 20, 8);
        lv_obj_set_pos(s_emoji_mouth, 98, 88);
    }
    else if (phase < 38)
    {
        /* 眨眼过渡 */
        lv_obj_set_size(s_emoji_eye_l, 30, 4);
        lv_obj_set_pos(s_emoji_eye_l, 52, 55);
        lv_obj_set_size(s_emoji_eye_r, 30, 4);
        lv_obj_set_pos(s_emoji_eye_r, 134, 55);
    }
    else if (phase < 52)
    {
        /* 4. 单眼放电眨眼 Wink (1.4秒) */
        lv_obj_set_size(s_emoji_eye_l, 30, 4);
        lv_obj_set_pos(s_emoji_eye_l, 52, 55);
        lv_obj_set_size(s_emoji_eye_r, 30, 30);
        lv_obj_set_pos(s_emoji_eye_r, 134, 42);

        lv_obj_set_pos(s_emoji_eyebrow_l, 54, 32);
        lv_obj_set_pos(s_emoji_eyebrow_r, 136, 24);

        lv_obj_set_size(s_emoji_mouth, 28, 12);
        lv_obj_set_pos(s_emoji_mouth, 94, 86);
        lv_obj_set_style_bg_opa(s_emoji_blush_l, LV_OPA_100, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_emoji_blush_r, LV_OPA_100, LV_PART_MAIN);
    }
    else if (phase < 70)
    {
        /* 5. 极度开心眯眼微笑 (^ v ^) (1.8秒) */
        lv_obj_set_size(s_emoji_eye_l, 30, 10);
        lv_obj_set_pos(s_emoji_eye_l, 52, 52);
        lv_obj_set_size(s_emoji_eye_r, 30, 10);
        lv_obj_set_pos(s_emoji_eye_r, 134, 52);

        lv_obj_set_pos(s_emoji_eyebrow_l, 54, 24);
        lv_obj_set_pos(s_emoji_eyebrow_r, 136, 24);

        lv_obj_set_size(s_emoji_mouth, 32, 16);
        lv_obj_set_pos(s_emoji_mouth, 92, 84);
        lv_obj_set_style_bg_color(s_emoji_mouth, lv_color_hex(0xF43F5E), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_emoji_blush_l, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_emoji_blush_r, LV_OPA_80, LV_PART_MAIN);
    }
    else if (phase == 999)
    {
        /* 惊讶模式 (O _ O) */
        lv_obj_set_size(s_emoji_eye_l, 34, 34);
        lv_obj_set_pos(s_emoji_eye_l, 50, 40);
        lv_obj_set_size(s_emoji_eye_r, 34, 34);
        lv_obj_set_pos(s_emoji_eye_r, 132, 40);

        lv_obj_set_pos(s_emoji_eyebrow_l, 54, 20);
        lv_obj_set_pos(s_emoji_eyebrow_r, 136, 20);

        lv_obj_set_size(s_emoji_mouth, 18, 18);
        lv_obj_set_pos(s_emoji_mouth, 99, 86);
        lv_obj_set_style_radius(s_emoji_mouth, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    }
    else if (phase == 888)
    {
        /* 瞌睡模式 (- _ -) */
        lv_obj_set_size(s_emoji_eye_l, 30, 6);
        lv_obj_set_pos(s_emoji_eye_l, 52, 54);
        lv_obj_set_size(s_emoji_eye_r, 30, 6);
        lv_obj_set_pos(s_emoji_eye_r, 134, 54);

        lv_obj_set_pos(s_emoji_eyebrow_l, 54, 34);
        lv_obj_set_pos(s_emoji_eyebrow_r, 136, 34);

        lv_obj_set_size(s_emoji_mouth, 16, 6);
        lv_obj_set_pos(s_emoji_mouth, 100, 92);
    }
}


/* ============================================================================
   7. 小恐龙跑酷游戏 (CHROME DINO RUN GAME - 33 FPS 实时物理引擎)
   ============================================================================ */
static lv_obj_t * s_dino_score_lbl = NULL;
static lv_obj_t * s_dino_cloud1 = NULL;
static lv_obj_t * s_dino_cloud2 = NULL;
static lv_obj_t * s_dino_ground_line = NULL;
static lv_obj_t * s_dino_player = NULL;
static lv_obj_t * s_dino_leg_l = NULL;
static lv_obj_t * s_dino_leg_r = NULL;
static lv_obj_t * s_dino_eye = NULL;
static lv_obj_t * s_dino_cactus = NULL;
static lv_obj_t * s_dino_over_card = NULL;
static lv_obj_t * s_dino_over_score_lbl = NULL;

static lv_timer_t * s_dino_timer = NULL;

static uint8_t  s_dino_game_over = 0;
static uint32_t s_dino_score = 0;
static uint32_t s_dino_hiscore = 0;
static float    s_dino_y = 144.0f;
static float    s_dino_vy = 0.0f;
static float    s_cactus_x = 240.0f;
static float    s_cactus_speed = 5.0f;
static uint8_t  s_cactus_w = 14;
static float    s_cloud1_x = 180.0f;
static float    s_cloud2_x = 70.0f;
static uint8_t  s_leg_phase = 0;

static void dino_game_restart(void)
{
    s_dino_game_over = 0;
    s_dino_score = 0;
    s_dino_y = 144.0f;
    s_dino_vy = 0.0f;
    s_cactus_x = 240.0f + (rand() % 40);
    s_cactus_speed = 5.0f;
    s_cactus_w = 14;
    s_leg_phase = 0;

    if (s_dino_player)
    {
        lv_obj_set_pos(s_dino_player, 24, 144);
        lv_obj_set_style_bg_color(s_dino_player, lv_color_hex(0x22C55E), LV_PART_MAIN);
    }
    if (s_dino_eye)
    {
        lv_obj_set_style_bg_color(s_dino_eye, lv_color_hex(0x0F172A), LV_PART_MAIN);
    }
    if (s_dino_cactus)
    {
        lv_obj_set_size(s_dino_cactus, s_cactus_w, 30);
        lv_obj_set_pos(s_dino_cactus, (int)s_cactus_x, 146);
    }
    if (s_dino_over_card)
    {
        lv_obj_add_flag(s_dino_over_card, LV_OBJ_FLAG_HIDDEN);
    }
}

static void dino_game_loop(lv_timer_t * timer)
{
    char buf[36];
    (void)timer;

    if (s_dino_player == NULL) return;

    /* 1. 轻云慢速漂移 (0.5 px/帧) */
    s_cloud1_x -= 0.6f;
    if (s_cloud1_x < -30.0f) s_cloud1_x = 245.0f;
    if (s_dino_cloud1) lv_obj_set_pos(s_dino_cloud1, (int)s_cloud1_x, 38);

    s_cloud2_x -= 0.4f;
    if (s_cloud2_x < -30.0f) s_cloud2_x = 250.0f;
    if (s_dino_cloud2) lv_obj_set_pos(s_dino_cloud2, (int)s_cloud2_x, 56);

    /* 2. 阵亡后冻结物理引擎 */
    if (s_dino_game_over) return;

    /* 3. 分数自增与速度曲线 */
    s_dino_score++;
    if (s_dino_score > s_dino_hiscore)
    {
        s_dino_hiscore = s_dino_score;
    }
    /* 速度从 5.0 渐增至 9.0 */
    s_cactus_speed = 5.0f + (float)(s_dino_score / 120) * 0.4f;
    if (s_cactus_speed > 9.0f) s_cactus_speed = 9.0f;

    /* 4. 障碍物飞驰与循环生成 */
    s_cactus_x -= s_cactus_speed;
    if (s_cactus_x < -28.0f)
    {
        s_cactus_x = 240.0f + (rand() % 40);
        s_cactus_w = (rand() % 2 == 0) ? 14 : 22;
        if (s_dino_cactus)
        {
            lv_obj_set_size(s_dino_cactus, s_cactus_w, 30);
        }
    }
    if (s_dino_cactus)
    {
        lv_obj_set_pos(s_dino_cactus, (int)s_cactus_x, 146);
    }

    /* 5. 恐龙重力抛物线积分 */
    s_dino_y += s_dino_vy;
    s_dino_vy += 1.15f; /* 重力加速度 g */
    if (s_dino_y >= 144.0f)
    {
        s_dino_y = 144.0f;
        s_dino_vy = 0.0f;
    }
    lv_obj_set_pos(s_dino_player, 24, (int)s_dino_y);

    /* 6. 踏步步态与悬空动作 */
    s_leg_phase++;
    if (s_dino_y < 144.0f)
    {
        /* 滞空：双脚向上收起 */
        if (s_dino_leg_l) lv_obj_set_pos(s_dino_leg_l, 4, 23);
        if (s_dino_leg_r) lv_obj_set_pos(s_dino_leg_r, 14, 23);
    }
    else
    {
        /* 地面小碎步踏步 */
        if ((s_leg_phase % 4) < 2)
        {
            if (s_dino_leg_l) lv_obj_set_pos(s_dino_leg_l, 4, 25);
            if (s_dino_leg_r) lv_obj_set_pos(s_dino_leg_r, 14, 21);
        }
        else
        {
            if (s_dino_leg_l) lv_obj_set_pos(s_dino_leg_l, 4, 21);
            if (s_dino_leg_r) lv_obj_set_pos(s_dino_leg_r, 14, 25);
        }
    }

    /* 7. AABB 碰撞检测 (小恐龙与仙人掌重叠判断) */
    /* 恐龙 X 范围 [28, 48], 仙人掌 X 范围 [s_cactus_x+2, s_cactus_x+s_cactus_w-2] */
    if ((s_cactus_x + 2 < 48.0f) && ((s_cactus_x + s_cactus_w - 2) > 28.0f))
    {
        if ((s_dino_y + 26.0f) > 150.0f)
        {
            /* 发生碰撞！GAME OVER */
            s_dino_game_over = 1;
            lv_obj_set_style_bg_color(s_dino_player, lv_color_hex(0xEF4444), LV_PART_MAIN);
            if (s_dino_eye)
            {
                lv_obj_set_style_bg_color(s_dino_eye, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            }

            if (s_dino_over_card)
            {
                lv_obj_remove_flag(s_dino_over_card, LV_OBJ_FLAG_HIDDEN);
                if (s_dino_over_score_lbl)
                {
                    snprintf(buf, sizeof(buf), "SCORE: %05u", (unsigned int)s_dino_score);
                    lv_label_set_text(s_dino_over_score_lbl, buf);
                }
            }
        }
    }

    /* 8. 刷新顶部记分牌 */
    if (s_dino_score_lbl)
    {
        snprintf(buf, sizeof(buf), "HI %05u  %05u", (unsigned int)s_dino_hiscore, (unsigned int)s_dino_score);
        lv_label_set_text(s_dino_score_lbl, buf);
    }
}

void ui_page_dino_cleanup(void)
{
    if (s_dino_timer != NULL)
    {
        lv_timer_del(s_dino_timer);
        s_dino_timer = NULL;
    }
    s_dino_score_lbl = NULL;
    s_dino_cloud1 = NULL;
    s_dino_cloud2 = NULL;
    s_dino_ground_line = NULL;
    s_dino_player = NULL;
    s_dino_leg_l = NULL;
    s_dino_leg_r = NULL;
    s_dino_eye = NULL;
    s_dino_cactus = NULL;
    s_dino_over_card = NULL;
    s_dino_over_score_lbl = NULL;
}

lv_obj_t * ui_page_dino_create(lv_obj_t * parent)
{
    lv_obj_t * top_bar;
    lv_obj_t * title_lbl;
    lv_obj_t * dino_head;
    lv_obj_t * hint;
    lv_obj_t * over_title;
    lv_obj_t * over_hint;

    /* 1. 顶部状态栏 (216x22) */
    top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 216, 22);
    lv_obj_set_pos(top_bar, 12, 10);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    title_lbl = lv_label_create(top_bar);
    lv_label_set_text(title_lbl, "DINO RUN");
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xFACC15), LV_PART_MAIN);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    s_dino_score_lbl = lv_label_create(top_bar);
    lv_label_set_text(s_dino_score_lbl, "HI 00000  00000");
    lv_obj_set_style_text_color(s_dino_score_lbl, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_align(s_dino_score_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    /* 2. 浮动白云 */
    s_dino_cloud1 = lv_obj_create(parent);
    lv_obj_set_size(s_dino_cloud1, 26, 10);
    lv_obj_set_pos(s_dino_cloud1, (int)s_cloud1_x, 38);
    lv_obj_set_style_bg_color(s_dino_cloud1, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dino_cloud1, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(s_dino_cloud1, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dino_cloud1, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_dino_cloud1, LV_OBJ_FLAG_SCROLLABLE);

    s_dino_cloud2 = lv_obj_create(parent);
    lv_obj_set_size(s_dino_cloud2, 20, 8);
    lv_obj_set_pos(s_dino_cloud2, (int)s_cloud2_x, 56);
    lv_obj_set_style_bg_color(s_dino_cloud2, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dino_cloud2, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(s_dino_cloud2, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dino_cloud2, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_dino_cloud2, LV_OBJ_FLAG_SCROLLABLE);

    /* 3. 跑道地面线 (240x2, y=176) */
    s_dino_ground_line = lv_obj_create(parent);
    lv_obj_set_size(s_dino_ground_line, 240, 2);
    lv_obj_set_pos(s_dino_ground_line, 0, 176);
    lv_obj_set_style_bg_color(s_dino_ground_line, lv_color_hex(0x475569), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dino_ground_line, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_dino_ground_line, LV_OBJ_FLAG_SCROLLABLE);

    /* 4. 小恐龙 (Dino 宽26, 高30) */
    s_dino_player = lv_obj_create(parent);
    lv_obj_set_size(s_dino_player, 26, 30);
    lv_obj_set_pos(s_dino_player, 24, 144);
    lv_obj_set_style_bg_color(s_dino_player, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dino_player, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_dino_player, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dino_player, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_dino_player, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_dino_player, LV_OBJ_FLAG_SCROLLABLE);

    /* 恐龙头部大嘴 */
    dino_head = lv_obj_create(s_dino_player);
    lv_obj_set_size(dino_head, 15, 12);
    lv_obj_set_pos(dino_head, 10, 0);
    lv_obj_set_style_bg_color(dino_head, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_set_style_radius(dino_head, 3, LV_PART_MAIN);
    lv_obj_set_style_border_width(dino_head, 0, LV_PART_MAIN);
    lv_obj_remove_flag(dino_head, LV_OBJ_FLAG_SCROLLABLE);

    /* 恐龙呆萌小眼 */
    s_dino_eye = lv_obj_create(s_dino_player);
    lv_obj_set_size(s_dino_eye, 3, 3);
    lv_obj_set_pos(s_dino_eye, 14, 3);
    lv_obj_set_style_bg_color(s_dino_eye, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_radius(s_dino_eye, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dino_eye, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_dino_eye, LV_OBJ_FLAG_SCROLLABLE);

    /* 恐龙左脚 */
    s_dino_leg_l = lv_obj_create(s_dino_player);
    lv_obj_set_size(s_dino_leg_l, 4, 7);
    lv_obj_set_pos(s_dino_leg_l, 4, 23);
    lv_obj_set_style_bg_color(s_dino_leg_l, lv_color_hex(0x16A34A), LV_PART_MAIN);
    lv_obj_set_style_radius(s_dino_leg_l, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dino_leg_l, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_dino_leg_l, LV_OBJ_FLAG_SCROLLABLE);

    /* 恐龙右脚 */
    s_dino_leg_r = lv_obj_create(s_dino_player);
    lv_obj_set_size(s_dino_leg_r, 4, 7);
    lv_obj_set_pos(s_dino_leg_r, 14, 23);
    lv_obj_set_style_bg_color(s_dino_leg_r, lv_color_hex(0x16A34A), LV_PART_MAIN);
    lv_obj_set_style_radius(s_dino_leg_r, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dino_leg_r, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_dino_leg_r, LV_OBJ_FLAG_SCROLLABLE);

    /* 5. 障碍物仙人掌 (鲜红醒目 0xEF4444) */
    s_dino_cactus = lv_obj_create(parent);
    lv_obj_set_size(s_dino_cactus, s_cactus_w, 30);
    lv_obj_set_pos(s_dino_cactus, (int)s_cactus_x, 146);
    lv_obj_set_style_bg_color(s_dino_cactus, lv_color_hex(0xEF4444), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dino_cactus, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_dino_cactus, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dino_cactus, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_dino_cactus, LV_OBJ_FLAG_SCROLLABLE);

    /* 6. 底部操作栏提示 */
    hint = lv_label_create(parent);
    lv_label_set_text(hint, "K1/K2: Jump   K3: Exit");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* 7. GAME OVER 结算浮动面板 (平时隐藏) */
    s_dino_over_card = lv_obj_create(parent);
    lv_obj_set_size(s_dino_over_card, 200, 100);
    lv_obj_set_pos(s_dino_over_card, 20, 60);
    lv_obj_set_style_bg_color(s_dino_over_card, lv_color_hex(0x181024), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dino_over_card, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_dino_over_card, lv_color_hex(0xF43F5E), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dino_over_card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_dino_over_card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_dino_over_card, 6, LV_PART_MAIN);
    lv_obj_remove_flag(s_dino_over_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dino_over_card, LV_OBJ_FLAG_HIDDEN);

    over_title = lv_label_create(s_dino_over_card);
    lv_label_set_text(over_title, "GAME OVER");
    lv_obj_set_style_text_color(over_title, lv_color_hex(0xEF4444), LV_PART_MAIN);
    lv_obj_align(over_title, LV_ALIGN_TOP_MID, 0, 4);

    s_dino_over_score_lbl = lv_label_create(s_dino_over_card);
    lv_label_set_text(s_dino_over_score_lbl, "SCORE: 00000");
    lv_obj_set_style_text_color(s_dino_over_score_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(s_dino_over_score_lbl, LV_ALIGN_CENTER, 0, 0);

    over_hint = lv_label_create(s_dino_over_card);
    lv_label_set_text(over_hint, "K1/K2: Replay   K3: Exit");
    lv_obj_set_style_text_color(over_hint, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(over_hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* 启动 33 FPS 实时游戏物理定时器 (30ms 周期) */
    dino_game_restart();
    s_dino_timer = lv_timer_create(dino_game_loop, 30, NULL);

    return parent;
}

void ui_page_dino_handle_key(uint8_t key_val)
{
    if (key_val == 1 || key_val == 2)
    {
        /* KEY1 或 KEY2：跳跃控制 */
        if (s_dino_game_over)
        {
            /* 死亡后一键复活开局 */
            dino_game_restart();
        }
        else
        {
            /* 处于地面时立即起跳 (真实初速度 -11.0f) */
            if (s_dino_y >= 143.0f)
            {
                s_dino_vy = -11.0f;
            }
        }
    }
    else if (key_val == 3 || key_val == 4)
    {
        /* KEY3：退出小恐龙，返回 8 应用主菜单 */
        ui_page_dino_cleanup();
        UI_Watch_SwitchPage(UI_PAGE_MENU);
    }
}

