/**
  ******************************************************************************
  * @file    w25qxx.h
  * @brief   W25Q64 (64M-bit / 8M-Byte) SPI Flash 驱动头文件 (硬件 SPI1 / APB2 42MHz)
  ******************************************************************************
  */

#ifndef __W25QXX_H
#define __W25QXX_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* W25Q64 芯片识别码 (Winbond JEDEC ID) */
#define W25Q64_JEDEC_ID         0xEF4017    /* 厂商ID: 0xEF (Winbond), 设备类型+容量: 0x4017 (64M-bit) */

/* W25Q64 物理参数 */
#define W25QXX_PAGE_SIZE        256         /* 1 页 = 256 字节 */
#define W25QXX_SECTOR_SIZE      4096        /* 1 扇区 = 4096 字节 (4KB) */
#define W25QXX_BLOCK_SIZE       65536       /* 1 块 = 65536 字节 (64KB) */
#define W25QXX_FLASH_SIZE       0x800000    /* 8M 字节 (64Mbit) */

/* 硬件引脚定义: 硬件 SPI1 (PA4: CS, PA5: SCK, PA6: MISO, PA7: MOSI) */
#define W25QXX_CS_PORT          GPIOA
#define W25QXX_CS_PIN           GPIO_Pin_4
#define W25QXX_CS_CLK           RCC_AHB1Periph_GPIOA

#define W25QXX_SPI              SPI1
#define W25QXX_SPI_CLK          RCC_APB2Periph_SPI1
#define W25QXX_SPI_PORT         GPIOA
#define W25QXX_SPI_PORT_CLK     RCC_AHB1Periph_GPIOA
#define W25QXX_PIN_SCK          GPIO_Pin_5
#define W25QXX_PIN_MISO         GPIO_Pin_6
#define W25QXX_PIN_MOSI         GPIO_Pin_7
#define W25QXX_PIN_SOURCE_SCK   GPIO_PinSource5
#define W25QXX_PIN_SOURCE_MISO  GPIO_PinSource6
#define W25QXX_PIN_SOURCE_MOSI  GPIO_PinSource7
#define W25QXX_AF_SPI           GPIO_AF_SPI1

/* 片选控制宏 */
#define W25QXX_CS_LOW()         GPIO_ResetBits(W25QXX_CS_PORT, W25QXX_CS_PIN)
#define W25QXX_CS_HIGH()        GPIO_SetBits(W25QXX_CS_PORT, W25QXX_CS_PIN)

/* 指令集定义 */
#define W25X_WriteEnable        0x06 
#define W25X_WriteDisable       0x04 
#define W25X_ReadStatusReg      0x05 
#define W25X_WriteStatusReg     0x01 
#define W25X_ReadData           0x03 
#define W25X_FastReadData       0x0B 
#define W25X_PageProgram        0x02 
#define W25X_SectorErase        0x20 
#define W25X_BlockErase         0xD8 
#define W25X_ChipErase          0xC7 
#define W25X_PowerDown          0xB9 
#define W25X_ReleasePowerDown   0xAB 
#define W25X_DeviceID           0xAB 
#define W25X_ManufactDeviceID   0x90 
#define W25X_JedecDeviceID      0x9F 

/* 驱动接口声明 */
void     W25QXX_Init(void);
uint32_t W25QXX_ReadID(void);
uint8_t  W25QXX_ReadSR(void);
void     W25QXX_Write_SR(uint8_t sr);
void     W25QXX_Write_Enable(void);
void     W25QXX_Write_Disable(void);
void     W25QXX_Wait_Busy(void);

void     W25QXX_Erase_Sector(uint32_t Dst_Addr);
void     W25QXX_Erase_Block(uint32_t Dst_Addr);
void     W25QXX_Erase_Chip(void);

void     W25QXX_Read(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead);
void     W25QXX_Write_Page(uint8_t *pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void     W25QXX_Write_NoCheck(uint8_t *pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void     W25QXX_Write(uint8_t *pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);

/* 自动化硬件校验测试：读取 ID、擦除一个测试扇区并写入测试魔数，读回校验 */
bool     W25QXX_SelfTest(void);

#endif /* __W25QXX_H */
