/**
  ******************************************************************************
  * @file    ui_page_menu.c
  * @brief   STM32F407 手表 LVGL 9 八大应用轮播主菜单
  ******************************************************************************
  */

#include "ui_watch.h"
#include <stdio.h>

#define MENU_ITEM_TOTAL 8

typedef struct {
    const char * title;
    const char * subtitle;
    uint32_t     color_hex;
    ui_page_id_t target_page;
} menu_app_item_t;

static const menu_app_item_t s_menu_apps[MENU_ITEM_TOTAL] = {
    {"1. BACK",     "Return to Clock",        0x64748B, UI_PAGE_CLOCK},
    {"2. TIMER",    "Precision Stopwatch",    0x0284C7, UI_PAGE_TIMER},
    {"3. LED",      "Board LED Control",      0x16A34A, UI_PAGE_LED},
    {"4. MPU6050",  "6-Axis Attitude & Steps",0xD97706, UI_PAGE_SENSOR},
    {"5. GAME",     "Dino Runner Game",       0x9333EA, UI_PAGE_GAME},
    {"6. EMOJI",    "Astronaut Animation",    0xDC2626, UI_PAGE_EMOJI},
    {"7. LEVEL",    "Digital Spirit Level",   0x0D9488, UI_PAGE_LEVEL},
    {"8. AHT20",    "Temp & Humidity Sensor", 0x2563EB, UI_PAGE_SENSOR}
};

static uint8_t   s_menu_idx = 1; /* 默认选中第 2 项 TIMER */
static lv_obj_t * s_header_idx_label = NULL;
static lv_obj_t * s_center_card = NULL;
static lv_obj_t * s_card_badge = NULL;
static lv_obj_t * s_card_title = NULL;
static lv_obj_t * s_card_sub = NULL;

/**
  * @brief  刷新中央大卡片的内容（局部原子刷新，绝无闪烁）
  */
static void Update_Menu_Card(void)
{
    char idx_buf[24];
    const menu_app_item_t * item;

    if (s_center_card == NULL) return;

    item = &s_menu_apps[s_menu_idx];

    /* 顶部索引标头 */
    snprintf(idx_buf, sizeof(idx_buf), "APP [ %d / %d ]", s_menu_idx + 1, MENU_ITEM_TOTAL);
    lv_label_set_text(s_header_idx_label, idx_buf);

    /* 卡片边框颜色与徽章色块 */
    lv_obj_set_style_border_color(s_center_card, lv_color_hex(item->color_hex), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_card_badge, lv_color_hex(item->color_hex), LV_PART_MAIN);

    /* 卡片标题与说明 */
    lv_label_set_text(s_card_title, item->title);
    lv_label_set_text(s_card_sub, item->subtitle);
}

/**
  * @brief  构建应用主菜单页面
  */
lv_obj_t * ui_page_menu_create(lv_obj_t * parent)
{
    lv_obj_t * top_bar;
    lv_obj_t * label_title;
    lv_obj_t * bottom_bar;
    lv_obj_t * label_nav;
    lv_obj_t * btn_enter;
    lv_obj_t * label_enter;
    lv_obj_t * label_arrow_l;
    lv_obj_t * label_arrow_r;

    /* 1. 顶部标头栏 (216x24) */
    top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 216, 24);
    lv_obj_set_pos(top_bar, 12, 10);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    label_title = lv_label_create(top_bar);
    lv_label_set_text(label_title, "MAIN MENU");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    s_header_idx_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(s_header_idx_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(s_header_idx_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* 2. 中央功能焦点卡片 (216x118, 不透明深色底) */
    s_center_card = lv_obj_create(parent);
    lv_obj_set_size(s_center_card, 216, 118);
    lv_obj_set_pos(s_center_card, 12, 40);
    lv_obj_set_style_bg_color(s_center_card, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_center_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_center_card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_center_card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_center_card, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_center_card, LV_OBJ_FLAG_SCROLLABLE);

    /* 2.1 顶部色块小徽章 (50x8) */
    s_card_badge = lv_obj_create(s_center_card);
    lv_obj_set_size(s_card_badge, 48, 6);
    lv_obj_align(s_card_badge, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_border_width(s_card_badge, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_card_badge, 3, LV_PART_MAIN);
    lv_obj_remove_flag(s_card_badge, LV_OBJ_FLAG_SCROLLABLE);

    /* 2.2 功能大标题 (固定宽 180x28, 不透明) */
    s_card_title = lv_label_create(s_center_card);
    lv_obj_set_size(s_card_title, 190, 28);
    lv_obj_align(s_card_title, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_text_align(s_card_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_card_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_card_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_remove_flag(s_card_title, LV_OBJ_FLAG_SCROLLABLE);

    /* 2.3 功能小说明 (固定宽 180x24) */
    s_card_sub = lv_label_create(s_center_card);
    lv_obj_set_size(s_card_sub, 190, 24);
    lv_obj_align(s_card_sub, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_text_align(s_card_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_card_sub, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_remove_flag(s_card_sub, LV_OBJ_FLAG_SCROLLABLE);

    /* 3. 底部导航与确认按钮栏 (216x46) */
    bottom_bar = lv_obj_create(parent);
    lv_obj_set_size(bottom_bar, 216, 44);
    lv_obj_set_pos(bottom_bar, 12, 166);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bottom_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* 3.1 居中确认按钮 */
    btn_enter = lv_obj_create(bottom_bar);
    lv_obj_set_size(btn_enter, 130, 42);
    lv_obj_center(btn_enter);
    lv_obj_set_style_bg_color(btn_enter, lv_color_hex(0x0284C7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_enter, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_enter, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_enter, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_enter, 8, LV_PART_MAIN);
    lv_obj_remove_flag(btn_enter, LV_OBJ_FLAG_SCROLLABLE);

    label_enter = lv_label_create(btn_enter);
    lv_label_set_text(label_enter, "OPEN (KEY3)");
    lv_obj_set_style_text_color(label_enter, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(label_enter);

    /* 3.2 左右切换提示箭头 */
    label_arrow_l = lv_label_create(bottom_bar);
    lv_label_set_text(label_arrow_l, "< K1");
    lv_obj_set_style_text_color(label_arrow_l, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(label_arrow_l, LV_ALIGN_LEFT_MID, 4, 0);

    label_arrow_r = lv_label_create(bottom_bar);
    lv_label_set_text(label_arrow_r, "K2 >");
    lv_obj_set_style_text_color(label_arrow_r, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(label_arrow_r, LV_ALIGN_RIGHT_MID, -4, 0);

    /* 4. 底部微型提示 */
    label_nav = lv_label_create(parent);
    lv_label_set_text(label_nav, "Hold KEY3 (1s): Back Clock");
    lv_obj_set_style_text_color(label_nav, lv_color_hex(0x475569), LV_PART_MAIN);
    lv_obj_align(label_nav, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* 初始显示 */
    Update_Menu_Card();

    return parent;
}

/**
  * @brief  处理应用菜单按键事件
  */
void ui_page_menu_handle_key(uint8_t key_val)
{
    if (key_val == 1)
    {
        /* KEY1: 上一项 (向左循环) */
        s_menu_idx = (s_menu_idx + MENU_ITEM_TOTAL - 1) % MENU_ITEM_TOTAL;
        Update_Menu_Card();
    }
    else if (key_val == 2)
    {
        /* KEY2: 下一项 (向右循环) */
        s_menu_idx = (s_menu_idx + 1) % MENU_ITEM_TOTAL;
        Update_Menu_Card();
    }
    else if (key_val == 3)
    {
        /* KEY3 短按: 进入选中功能页面 */
        UI_Watch_SwitchPage(s_menu_apps[s_menu_idx].target_page);
    }
    else if (key_val == 4)
    {
        /* KEY3 长按: 快速返回主时钟表盘 */
        UI_Watch_SwitchPage(UI_PAGE_CLOCK);
    }
}
