/**
  ******************************************************************************
  * @file    ui_page_settime.c
  * @brief   STM32F407 手表交互式屏幕时间设置页面实现
  ******************************************************************************
  */

#include "ui_watch.h"
#include "rtc_f407.h"
#include <stdio.h>

typedef enum {
    FIELD_HOUR = 0,
    FIELD_MIN,
    FIELD_SEC,
    FIELD_YEAR,
    FIELD_MONTH,
    FIELD_DAY,
    FIELD_COUNT
} settime_field_t;

static uint8_t s_active_field = FIELD_HOUR;

static int16_t s_set_hour  = 20;
static int16_t s_set_min   = 15;
static int16_t s_set_sec   = 0;
static int16_t s_set_year  = 26;
static int16_t s_set_month = 10;
static int16_t s_set_day   = 6;

static lv_obj_t * s_boxes[FIELD_COUNT];
static lv_obj_t * s_labels[FIELD_COUNT];

static void SetTime_Update_Field_Text(uint8_t field)
{
    char buf[12];
    if (field >= FIELD_COUNT || s_labels[field] == NULL) return;

    switch (field)
    {
        case FIELD_HOUR:
            snprintf(buf, sizeof(buf), "%02d", s_set_hour);
            break;
        case FIELD_MIN:
            snprintf(buf, sizeof(buf), "%02d", s_set_min);
            break;
        case FIELD_SEC:
            snprintf(buf, sizeof(buf), "%02d", s_set_sec);
            break;
        case FIELD_YEAR:
            snprintf(buf, sizeof(buf), "20%02d", s_set_year);
            break;
        case FIELD_MONTH:
            snprintf(buf, sizeof(buf), "%02d", s_set_month);
            break;
        case FIELD_DAY:
            snprintf(buf, sizeof(buf), "%02d", s_set_day);
            break;
        default:
            break;
    }
    lv_label_set_text(s_labels[field], buf);
}

static void SetTime_Update_Focus_Styles(void)
{
    uint8_t i;
    for (i = 0; i < FIELD_COUNT; i++)
    {
        if (s_boxes[i] == NULL) continue;
        if (i == s_active_field)
        {
            lv_obj_set_style_bg_color(s_boxes[i], lv_color_hex(0x0284C7), LV_PART_MAIN);
            lv_obj_set_style_border_color(s_boxes[i], lv_color_hex(0x38BDF8), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_boxes[i], 2, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_bg_color(s_boxes[i], lv_color_hex(0x1E293B), LV_PART_MAIN);
            lv_obj_set_style_border_color(s_boxes[i], lv_color_hex(0x334155), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_boxes[i], 1, LV_PART_MAIN);
        }
    }
}

lv_obj_t * ui_page_settime_create(lv_obj_t * parent)
{
    lv_obj_t * top_bar;
    lv_obj_t * label_title;
    lv_obj_t * card_time;
    lv_obj_t * tag_time;
    lv_obj_t * lbl_c1;
    lv_obj_t * lbl_c2;
    lv_obj_t * card_date;
    lv_obj_t * tag_date;
    lv_obj_t * lbl_d1;
    lv_obj_t * lbl_d2;
    lv_obj_t * btn_save;
    lv_obj_t * lbl_save;
    lv_obj_t * hint;
    rtc_time_date_t dt;
    uint8_t i;

    /* 从硬件 RTC 加载当前实际时间 */
    RTC_F407_GetDateTime(&dt);
    s_set_hour  = dt.hours;
    s_set_min   = dt.minutes;
    s_set_sec   = dt.seconds;
    s_set_year  = dt.year;
    s_set_month = dt.month;
    s_set_day   = dt.date;
    if (s_set_year < 24 || s_set_year > 50) s_set_year = 26;

    s_active_field = FIELD_HOUR;

    /* 顶部标头栏 (216x24) */
    top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 216, 24);
    lv_obj_set_pos(top_bar, 12, 10);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    label_title = lv_label_create(top_bar);
    lv_label_set_text(label_title, "SET TIME & DATE");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xA855F7), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    /* 1. 时间卡片 (216x58) -> [HH] : [MM] : [SS] */
    card_time = lv_obj_create(parent);
    lv_obj_set_size(card_time, 216, 58);
    lv_obj_set_pos(card_time, 12, 38);
    lv_obj_set_style_bg_color(card_time, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card_time, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card_time, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_time, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card_time, 10, LV_PART_MAIN);
    lv_obj_remove_flag(card_time, LV_OBJ_FLAG_SCROLLABLE);

    tag_time = lv_label_create(card_time);
    lv_label_set_text(tag_time, "TIME");
    lv_obj_set_style_text_color(tag_time, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(tag_time, LV_ALIGN_LEFT_MID, 4, 0);

    /* 时 [HH] */
    s_boxes[FIELD_HOUR] = lv_obj_create(card_time);
    lv_obj_set_size(s_boxes[FIELD_HOUR], 46, 36);
    lv_obj_set_pos(s_boxes[FIELD_HOUR], 48, 11);
    lv_obj_set_style_radius(s_boxes[FIELD_HOUR], 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_boxes[FIELD_HOUR], 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_boxes[FIELD_HOUR], LV_OBJ_FLAG_SCROLLABLE);

    s_labels[FIELD_HOUR] = lv_label_create(s_boxes[FIELD_HOUR]);
    lv_obj_set_style_text_color(s_labels[FIELD_HOUR], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_labels[FIELD_HOUR]);

    lbl_c1 = lv_label_create(card_time);
    lv_label_set_text(lbl_c1, ":");
    lv_obj_set_style_text_color(lbl_c1, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_pos(lbl_c1, 98, 20);

    /* 分 [MM] */
    s_boxes[FIELD_MIN] = lv_obj_create(card_time);
    lv_obj_set_size(s_boxes[FIELD_MIN], 46, 36);
    lv_obj_set_pos(s_boxes[FIELD_MIN], 108, 11);
    lv_obj_set_style_radius(s_boxes[FIELD_MIN], 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_boxes[FIELD_MIN], 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_boxes[FIELD_MIN], LV_OBJ_FLAG_SCROLLABLE);

    s_labels[FIELD_MIN] = lv_label_create(s_boxes[FIELD_MIN]);
    lv_obj_set_style_text_color(s_labels[FIELD_MIN], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_labels[FIELD_MIN]);

    lbl_c2 = lv_label_create(card_time);
    lv_label_set_text(lbl_c2, ":");
    lv_obj_set_style_text_color(lbl_c2, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_pos(lbl_c2, 158, 20);

    /* 秒 [SS] */
    s_boxes[FIELD_SEC] = lv_obj_create(card_time);
    lv_obj_set_size(s_boxes[FIELD_SEC], 46, 36);
    lv_obj_set_pos(s_boxes[FIELD_SEC], 166, 11);
    lv_obj_set_style_radius(s_boxes[FIELD_SEC], 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_boxes[FIELD_SEC], 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_boxes[FIELD_SEC], LV_OBJ_FLAG_SCROLLABLE);

    s_labels[FIELD_SEC] = lv_label_create(s_boxes[FIELD_SEC]);
    lv_obj_set_style_text_color(s_labels[FIELD_SEC], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_labels[FIELD_SEC]);

    /* 2. 日期卡片 (216x58) -> [20YY] - [MM] - [DD] */
    card_date = lv_obj_create(parent);
    lv_obj_set_size(card_date, 216, 58);
    lv_obj_set_pos(card_date, 12, 102);
    lv_obj_set_style_bg_color(card_date, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card_date, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card_date, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_date, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card_date, 10, LV_PART_MAIN);
    lv_obj_remove_flag(card_date, LV_OBJ_FLAG_SCROLLABLE);

    tag_date = lv_label_create(card_date);
    lv_label_set_text(tag_date, "DATE");
    lv_obj_set_style_text_color(tag_date, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(tag_date, LV_ALIGN_LEFT_MID, 4, 0);

    /* 年 [20YY] */
    s_boxes[FIELD_YEAR] = lv_obj_create(card_date);
    lv_obj_set_size(s_boxes[FIELD_YEAR], 56, 36);
    lv_obj_set_pos(s_boxes[FIELD_YEAR], 44, 11);
    lv_obj_set_style_radius(s_boxes[FIELD_YEAR], 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_boxes[FIELD_YEAR], 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_boxes[FIELD_YEAR], LV_OBJ_FLAG_SCROLLABLE);

    s_labels[FIELD_YEAR] = lv_label_create(s_boxes[FIELD_YEAR]);
    lv_obj_set_style_text_color(s_labels[FIELD_YEAR], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_labels[FIELD_YEAR]);

    lbl_d1 = lv_label_create(card_date);
    lv_label_set_text(lbl_d1, "-");
    lv_obj_set_style_text_color(lbl_d1, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_pos(lbl_d1, 104, 20);

    /* 月 [MM] */
    s_boxes[FIELD_MONTH] = lv_obj_create(card_date);
    lv_obj_set_size(s_boxes[FIELD_MONTH], 42, 36);
    lv_obj_set_pos(s_boxes[FIELD_MONTH], 114, 11);
    lv_obj_set_style_radius(s_boxes[FIELD_MONTH], 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_boxes[FIELD_MONTH], 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_boxes[FIELD_MONTH], LV_OBJ_FLAG_SCROLLABLE);

    s_labels[FIELD_MONTH] = lv_label_create(s_boxes[FIELD_MONTH]);
    lv_obj_set_style_text_color(s_labels[FIELD_MONTH], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_labels[FIELD_MONTH]);

    lbl_d2 = lv_label_create(card_date);
    lv_label_set_text(lbl_d2, "-");
    lv_obj_set_style_text_color(lbl_d2, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_pos(lbl_d2, 160, 20);

    /* 日 [DD] */
    s_boxes[FIELD_DAY] = lv_obj_create(card_date);
    lv_obj_set_size(s_boxes[FIELD_DAY], 42, 36);
    lv_obj_set_pos(s_boxes[FIELD_DAY], 170, 11);
    lv_obj_set_style_radius(s_boxes[FIELD_DAY], 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_boxes[FIELD_DAY], 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_boxes[FIELD_DAY], LV_OBJ_FLAG_SCROLLABLE);

    s_labels[FIELD_DAY] = lv_label_create(s_boxes[FIELD_DAY]);
    lv_obj_set_style_text_color(s_labels[FIELD_DAY], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_labels[FIELD_DAY]);

    /* 3. 保存按钮 (216x40) */
    btn_save = lv_obj_create(parent);
    lv_obj_set_size(btn_save, 216, 40);
    lv_obj_set_pos(btn_save, 12, 168);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x16A34A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_save, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_save, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_save, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_save, 8, LV_PART_MAIN);
    lv_obj_remove_flag(btn_save, LV_OBJ_FLAG_SCROLLABLE);

    lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "SAVE & APPLY (KEY3)");
    lv_obj_set_style_text_color(lbl_save, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(lbl_save);

    /* 4. 底部指引 */
    hint = lv_label_create(parent);
    lv_label_set_text(hint, "K1: +1 | K2: Next | Hold K3: Cancel");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    for (i = 0; i < FIELD_COUNT; i++)
    {
        SetTime_Update_Field_Text(i);
    }
    SetTime_Update_Focus_Styles();

    return parent;
}

void ui_page_settime_handle_key(uint8_t key_val)
{
    if (key_val == 1)
    {
        /* KEY1: 当前字段递增 +1 */
        switch (s_active_field)
        {
            case FIELD_HOUR:
                s_set_hour = (s_set_hour + 1) % 24;
                break;
            case FIELD_MIN:
                s_set_min = (s_set_min + 1) % 60;
                break;
            case FIELD_SEC:
                s_set_sec = (s_set_sec + 1) % 60;
                break;
            case FIELD_YEAR:
                s_set_year = (s_set_year >= 35) ? 24 : (s_set_year + 1);
                break;
            case FIELD_MONTH:
                s_set_month = (s_set_month % 12) + 1;
                break;
            case FIELD_DAY:
                s_set_day = (s_set_day % 31) + 1;
                break;
            default:
                break;
        }
        SetTime_Update_Field_Text(s_active_field);
    }
    else if (key_val == 2)
    {
        /* KEY2: 切换到下一个字段 */
        s_active_field = (s_active_field + 1) % FIELD_COUNT;
        SetTime_Update_Focus_Styles();
    }
    else if (key_val == 3)
    {
        /* KEY3 短按: 保存并写入硬件 RTC */
        rtc_time_date_t dt;
        dt.hours   = s_set_hour;
        dt.minutes = s_set_min;
        dt.seconds = s_set_sec;
        dt.year    = s_set_year;
        dt.month   = s_set_month;
        dt.date    = s_set_day;
        dt.weekday = 2; /* 星期二 */

        RTC_F407_SetDateTime(&dt);
        printf("[RTC] User updated time to 20%02d-%02d-%02d %02d:%02d:%02d\r\n",
               s_set_year, s_set_month, s_set_day, s_set_hour, s_set_min, s_set_sec);

        /* 成功返回主时钟表盘 */
        UI_Watch_SwitchPage(UI_PAGE_CLOCK);
    }
    else if (key_val == 4)
    {
        /* KEY3 长按: 取消并返回表盘 */
        UI_Watch_SwitchPage(UI_PAGE_CLOCK);
    }
}
