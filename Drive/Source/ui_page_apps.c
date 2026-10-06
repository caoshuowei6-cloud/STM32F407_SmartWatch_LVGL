/**
  ******************************************************************************
  * @file    ui_page_apps.c
  * @brief   STM32F407 手表 LVGL 9 子应用页面实现（秒表、LED、传感器、设置等）
  ******************************************************************************
  */

#include "ui_watch.h"
#include "led.h"
#include <stdio.h>

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
    lv_label_set_text(s_sensor_temp_label, "25.8 C   |   54.2 %RH");
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
    lv_label_set_text(s_sensor_mpu_label, "Pitch:+12.4  Roll:-3.2\nSteps: 1280 (Stage 4)");
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
    /* 阶段 4 传感器接通后，这里直接更新实时温湿度和陀螺仪数值 */
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
   5. 通用占位页 (GAME, EMOJI, LEVEL)
   ============================================================================ */
lv_obj_t * ui_page_placeholder_create(lv_obj_t * parent, const char * title, const char * desc)
{
    lv_obj_t * top_bar;
    lv_obj_t * label_title;
    lv_obj_t * center_card;
    lv_obj_t * lbl_desc;
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
    lv_label_set_text(label_title, title);
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xEC4899), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    center_card = lv_obj_create(parent);
    lv_obj_set_size(center_card, 216, 118);
    lv_obj_set_pos(center_card, 12, 40);
    lv_obj_set_style_bg_color(center_card, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(center_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(center_card, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_border_width(center_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(center_card, 12, LV_PART_MAIN);
    lv_obj_remove_flag(center_card, LV_OBJ_FLAG_SCROLLABLE);

    lbl_desc = lv_label_create(center_card);
    lv_label_set_text(lbl_desc, desc);
    lv_obj_set_style_text_color(lbl_desc, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_center(lbl_desc);

    btn_back = lv_obj_create(parent);
    lv_obj_set_size(btn_back, 216, 42);
    lv_obj_set_pos(btn_back, 12, 172);
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

    return parent;
}

void ui_page_placeholder_handle_key(uint8_t key_val)
{
    if (key_val == 3 || key_val == 4)
    {
        /* 返回主菜单 */
        UI_Watch_SwitchPage(UI_PAGE_MENU);
    }
}
