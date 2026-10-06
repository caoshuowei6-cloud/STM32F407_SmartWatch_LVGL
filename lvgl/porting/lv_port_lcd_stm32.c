/*
 * @Author: csw
 * @Date: 2026-10-06 17:40:12
 * @Description:  
 * @Copyright: Copyright (c) 2026 1060137882@qq.com, All Rights Reserved.
 * 
 */
#include "lv_port_lcd_stm32.h"
#include "lvgl.h"
#include "lcd_spi_154.h"    // 引入你原本的屏幕驱动头文件

#define MY_DISP_HOR_RES    240
#define MY_DISP_VER_RES    240

static lv_display_t * lcd_disp;

// 静态双缓冲（1/10 屏，占用 240 * 24 * 2 = 11.5KB，双缓冲共 23KB）
#define BUF_PIXEL_CNT   (MY_DISP_HOR_RES * (MY_DISP_VER_RES / 10))
static uint8_t buf1[BUF_PIXEL_CNT * 2];
static uint8_t buf2[BUF_PIXEL_CNT * 2];

/* 核心刷新回调函数：LVGL 画完一块区域后调用它 */
static void disp_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    uint32_t width  = area->x2 - area->x1 + 1;
    uint32_t height = area->y2 - area->y1 + 1;
    uint32_t size   = width * height;
    uint16_t * color_p = (uint16_t *)px_map;

    // 1. 设置 ST7789 绘制窗口坐标
    LCD_SetAddress(area->x1, area->y1, area->x2, area->y2);

    // 2. 将数据发送给 SPI3（直接复用驱动里的写缓冲区函数）
    LCD_WriteBuff(color_p, size);

    // 3. 通知 LVGL 这一块已经刷新完毕
    lv_display_flush_ready(disp);
}

void lv_port_disp_init(void)
{
    // 1. 初始化屏幕硬件（复用已验证的驱动）
    SPI_LCD_Init();

    // 2. 创建 LVGL 显示对象（LVGL 9 标准写法）
    lcd_disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);

    // 3. 设置刷新回调
    lv_display_set_flush_cb(lcd_disp, disp_flush_cb);

    // 4. 设置颜色格式（RGB565）
    lv_display_set_color_format(lcd_disp, LV_COLOR_FORMAT_RGB565);

    // 5. 绑定静态局部双缓冲
    lv_display_set_buffers(lcd_disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}