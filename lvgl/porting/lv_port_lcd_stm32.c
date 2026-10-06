/**
  ******************************************************************************
  * @file    lv_port_lcd_stm32.c
  * @brief   STM32F407 + ST7789 (SPI3) LVGL 9 显示适配层（高速连贯刷新优化）
  ******************************************************************************
  */

#include "lv_port_lcd_stm32.h"
#include "lvgl.h"
#include "lcd_spi_154.h"

#define MY_DISP_HOR_RES    240
#define MY_DISP_VER_RES    240

static lv_display_t * lcd_disp;

/* 局部双缓冲：1/10 屏，占用 240 * 24 * 2 = 11.5KB，双缓冲共 23KB */
#define BUF_PIXEL_CNT   (MY_DISP_HOR_RES * (MY_DISP_VER_RES / 10))
static uint8_t buf1[BUF_PIXEL_CNT * 2];
static uint8_t buf2[BUF_PIXEL_CNT * 2];

/**
  * @brief  高效连贯写入颜色数据（单次CS闭环，消除总线电平抖动与闪烁）
  */
static void LCD_Color_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t * color_p, uint32_t size)
{
    uint32_t i;

    /* 1. 片选拉低开始整包传输 */
    LCD_CS_L;

    /* 2. 发送列地址设置命令 (0x2A) */
    while((LCD_SPI->SR & SPI_I2S_FLAG_BSY) != RESET);
    LCD_DC_Command;
    LCD_SPI->DR = 0x2A;
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    while((LCD_SPI->SR & SPI_I2S_FLAG_BSY) != RESET);

    LCD_DC_Data;
    LCD_SPI->DR = (x1 >> 8);
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    LCD_SPI->DR = (x1 & 0xFF);
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    LCD_SPI->DR = (x2 >> 8);
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    LCD_SPI->DR = (x2 & 0xFF);
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);

    /* 3. 发送行地址设置命令 (0x2B) */
    while((LCD_SPI->SR & SPI_I2S_FLAG_BSY) != RESET);
    LCD_DC_Command;
    LCD_SPI->DR = 0x2B;
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    while((LCD_SPI->SR & SPI_I2S_FLAG_BSY) != RESET);

    LCD_DC_Data;
    LCD_SPI->DR = (y1 >> 8);
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    LCD_SPI->DR = (y1 & 0xFF);
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    LCD_SPI->DR = (y2 >> 8);
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    LCD_SPI->DR = (y2 & 0xFF);
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);

    /* 4. 发送写显存命令 (0x2C) */
    while((LCD_SPI->SR & SPI_I2S_FLAG_BSY) != RESET);
    LCD_DC_Command;
    LCD_SPI->DR = 0x2C;
    while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    while((LCD_SPI->SR & SPI_I2S_FLAG_BSY) != RESET);

    /* 5. 保持 CS 为低，切换为 16 位模式连贯爆破写入像素 */
    LCD_DC_Data;
    LCD_SPI->CR1 &= 0xFFBF;             /* 暂时关闭 SPI3 */
    LCD_SPI->CR1 |= SPI_DataSize_16b;   /* 切换为 16 位数据格式 */
    LCD_SPI->CR1 |= 0x0040;             /* 重新使能 SPI3 */

    for(i = 0; i < size; i++)
    {
        LCD_SPI->DR = color_p[i];
        while((LCD_SPI->SR & SPI_I2S_FLAG_TXE) == 0);
    }
    while((LCD_SPI->SR & SPI_I2S_FLAG_BSY) != RESET);

    /* 6. 整包完成，拉高 CS */
    LCD_CS_H;

    /* 恢复为 8 位模式方便下次发命令 */
    LCD_SPI->CR1 &= 0xFFBF;
    LCD_SPI->CR1 &= 0xF7FF;
    LCD_SPI->CR1 |= 0x0040;
}

/**
  * @brief  LVGL 显示刷新回调
  */
static void disp_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    uint32_t width  = area->x2 - area->x1 + 1;
    uint32_t height = area->y2 - area->y1 + 1;
    uint32_t size   = width * height;
    uint16_t * color_p = (uint16_t *)px_map;

    /* 原子连贯写入目标区域 */
    LCD_Color_Fill(area->x1, area->y1, area->x2, area->y2, color_p, size);

    /* 通知 LVGL 本批次绘制完毕 */
    lv_display_flush_ready(disp);
}

/**
  * @brief  显示驱动初始化接口
  */
void lv_port_disp_init(void)
{
    /* 1. 初始化屏幕硬件 (保留实测正常的底层驱动) */
    SPI_LCD_Init();

    /* 2. 创建 LVGL 显示对象 */
    lcd_disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);

    /* 3. 注册刷新回调 */
    lv_display_set_flush_cb(lcd_disp, disp_flush_cb);

    /* 4. 设置颜色格式 RGB565 */
    lv_display_set_color_format(lcd_disp, LV_COLOR_FORMAT_RGB565);

    /* 5. 绑定局部双缓冲 */
    lv_display_set_buffers(lcd_disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}