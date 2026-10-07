/**
  ******************************************************************************
  * @file    ui_watch.c
  * @brief   STM32F407 手表 LVGL 9 UI 页面路由与生命周期管理
  ******************************************************************************
  */

#include "ui_watch.h"
#include "rtc_f407.h"
#include "aht20.h"
#include "mpu6050.h"
#include <stdio.h>

static ui_page_id_t s_current_page = UI_PAGE_CLOCK;
static lv_obj_t *   s_page_container = NULL;

/**
  * @brief  初始化手表 UI 框架，载入时钟表盘首页
  */
void UI_Watch_Init(void)
{
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B0F19), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 启动进入主表盘 */
    UI_Watch_SwitchPage(UI_PAGE_CLOCK);
}

/**
  * @brief  页面切换路由器
  * @param  target_page 目标页面枚举
  */
void UI_Watch_SwitchPage(ui_page_id_t target_page)
{
    lv_obj_t * scr;

    if (target_page >= UI_PAGE_COUNT)
    {
        return;
    }

    /* 销毁旧页面容器，释放所有旧控件内存 */
    /* 离开前清理小恐龙定时器，防止悬空内存泄漏 */
    ui_page_dino_cleanup();

    if (s_page_container != NULL)
    {
        lv_obj_delete(s_page_container);
        s_page_container = NULL;
    }

    s_current_page = target_page;
    scr = lv_screen_active();

    /* 创建全新的全屏基础页面容器 (240x240, 纯深色底, 不透明) */
    s_page_container = lv_obj_create(scr);
    lv_obj_set_size(s_page_container, 240, 240);
    lv_obj_set_pos(s_page_container, 0, 0);
    lv_obj_set_style_pad_all(s_page_container, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_page_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_page_container, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_page_container, lv_color_hex(0x0B0F19), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_page_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(s_page_container, LV_OBJ_FLAG_SCROLLABLE);

    /* 构建目标页面控件 */
    switch (target_page)
    {
        case UI_PAGE_CLOCK:
            ui_page_clock_create(s_page_container);
            break;

        case UI_PAGE_MENU:
            ui_page_menu_create(s_page_container);
            break;

        case UI_PAGE_TIMER:
            ui_page_timer_create(s_page_container);
            break;

        case UI_PAGE_LED:
            ui_page_led_create(s_page_container);
            break;

        case UI_PAGE_SENSOR:
            ui_page_sensor_create(s_page_container);
            break;

        case UI_PAGE_SETTING:
            ui_page_setting_create(s_page_container);
            break;

        case UI_PAGE_SET_TIME:
            ui_page_settime_create(s_page_container);
            break;

        case UI_PAGE_GAME:
            ui_page_dino_create(s_page_container);
            break;

        case UI_PAGE_EMOJI:
            ui_page_emoji_create(s_page_container);
            break;

        case UI_PAGE_LEVEL:
            ui_page_level_create(s_page_container);
            break;

        default:
            break;
    }
}

/**
  * @brief  按键事件全局路由分发
  * @param  key_val 1:KEY1(上/左), 2:KEY2(下/右), 3:KEY3(短按确认), 4:KEY3(长按返回), 5:SW1(板载)
  */
void UI_Watch_HandleKey(uint8_t key_val)
{
    if (key_val == 0) return;

    switch (s_current_page)
    {
        case UI_PAGE_CLOCK:
            ui_page_clock_handle_key(key_val);
            break;

        case UI_PAGE_MENU:
            ui_page_menu_handle_key(key_val);
            break;

        case UI_PAGE_TIMER:
            ui_page_timer_handle_key(key_val);
            break;

        case UI_PAGE_LED:
            ui_page_led_handle_key(key_val);
            break;

        case UI_PAGE_SENSOR:
            ui_page_sensor_handle_key(key_val);
            break;

        case UI_PAGE_SETTING:
            ui_page_setting_handle_key(key_val);
            break;

        case UI_PAGE_SET_TIME:
            ui_page_settime_handle_key(key_val);
            break;

        case UI_PAGE_LEVEL:
            ui_page_level_handle_key(key_val);
            break;

        case UI_PAGE_EMOJI:
            ui_page_emoji_handle_key(key_val);
            break;

        case UI_PAGE_GAME:
            ui_page_dino_handle_key(key_val);
            break;

        default:
            break;
    }
}

/**
  * @brief  100ms 周期更新（秒表高频计时与 UI 动画刷新）
  * @note   MPU6050 硬件解算已迁移至后台 Task_Sensor 50Hz 独立无阻塞运行，UI 层不再执行 I2C 通信
  */
void UI_Watch_Update_100ms(void)
{
    if (s_current_page == UI_PAGE_TIMER)
    {
        ui_page_timer_update_100ms();
    }
    else if (s_current_page == UI_PAGE_LEVEL)
    {
        ui_page_level_update_100ms();
    }
    else if (s_current_page == UI_PAGE_EMOJI)
    {
        ui_page_emoji_update_100ms();
    }
}

/**
  * @brief  1s 周期更新（时钟走时、页面传感器数据卡片刷新）
  * @note   AHT20 硬件采样已迁移至后台 Task_Sensor 1Hz 独立无阻塞运行，UI 层只做视图渲染
  */
void UI_Watch_Update_1s(void)
{
    /* 1. 推进硬件/软件时钟走时 */
    RTC_F407_Tick1s();

    /* 2. 页面视图组件 1s 数据卡片刷新 */
    if (s_current_page == UI_PAGE_CLOCK)
    {
        ui_page_clock_update_1s();
    }
    else if (s_current_page == UI_PAGE_SENSOR)
    {
        ui_page_sensor_update_1s();
    }
}

/**
  * @brief  获取当前激活页面 ID
  */
ui_page_id_t UI_Watch_GetCurrentPage(void)
{
    return s_current_page;
}
