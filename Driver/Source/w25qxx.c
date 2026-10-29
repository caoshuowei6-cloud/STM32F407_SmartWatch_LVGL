/**
  ******************************************************************************
  * @file    w25qxx.c
  * @brief   W25Q64 (64M-bit / 8M-Byte) SPI Flash 驱动实现 (硬件 SPI1 / APB2)
  ******************************************************************************
  */

#include "w25qxx.h"
#include <stdio.h>
#include <string.h>

/**
  * @brief  SPI1 读写一个字节 (硬件全双工收发)
  * @param  TxData: 发送的数据
  * @retval 接收到的数据
  */
static uint8_t W25QXX_ReadWriteByte(uint8_t TxData)
{
    uint32_t timeout = 50000;

    /* 等待发送缓冲区空 */
    while (SPI_I2S_GetFlagStatus(W25QXX_SPI, SPI_FLAG_TXE) == RESET)
    {
        if (--timeout == 0) return 0xFF;
    }

    /* 发送一个字节 */
    SPI_I2S_SendData(W25QXX_SPI, TxData);

    timeout = 50000;
    /* 等待接收缓冲区非空 */
    while (SPI_I2S_GetFlagStatus(W25QXX_SPI, SPI_FLAG_RXNE) == RESET)
    {
        if (--timeout == 0) return 0xFF;
    }

    /* 读取并返回接收到的字节 */
    return (uint8_t)SPI_I2S_ReceiveData(W25QXX_SPI);
}

/**
  * @brief  初始化 W25QXX 底层硬件 SPI1 与 GPIO
  */
void W25QXX_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef  SPI_InitStructure;

    /* 1. 使能 GPIOA 和 SPI1 时钟 */
    RCC_AHB1PeriphClockCmd(W25QXX_CS_CLK | W25QXX_SPI_PORT_CLK, ENABLE);
    RCC_APB2PeriphClockCmd(W25QXX_SPI_CLK, ENABLE);

    /* 2. 配置片选引脚 CS (PA4): 普通推挽输出，默认拉高 */
    GPIO_InitStructure.GPIO_Pin   = W25QXX_CS_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(W25QXX_CS_PORT, &GPIO_InitStructure);
    W25QXX_CS_HIGH();

    /* 3. 配置 SPI1 引脚 SCK (PA5), MISO (PA6), MOSI (PA7): 复用 AF5 */
    GPIO_PinAFConfig(W25QXX_SPI_PORT, W25QXX_PIN_SOURCE_SCK,  W25QXX_AF_SPI);
    GPIO_PinAFConfig(W25QXX_SPI_PORT, W25QXX_PIN_SOURCE_MISO, W25QXX_AF_SPI);
    GPIO_PinAFConfig(W25QXX_SPI_PORT, W25QXX_PIN_SOURCE_MOSI, W25QXX_AF_SPI);

    GPIO_InitStructure.GPIO_Pin   = W25QXX_PIN_SCK | W25QXX_PIN_MISO | W25QXX_PIN_MOSI;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(W25QXX_SPI_PORT, &GPIO_InitStructure);

    /* 4. 配置 SPI1 控制器: 主机模式、Mode 3 (CPOL=1, CPHA=1)、8位全双工 */
    SPI_I2S_DeInit(W25QXX_SPI);
    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4; /* 84MHz / 4 = 21MHz，初测高抗扰，实测后可升至 Prescaler_2(42MHz) */
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(W25QXX_SPI, &SPI_InitStructure);

    /* 5. 使能 SPI1 */
    SPI_Cmd(W25QXX_SPI, ENABLE);

    /* 6. 发送空闲字节拉高总线电平 */
    W25QXX_ReadWriteByte(0xFF);
}

/**
  * @brief  读取芯片 JEDEC ID
  * @retval 24 位 ID (W25Q64 典型值为 0xEF4017)
  */
uint32_t W25QXX_ReadID(void)
{
    uint32_t id = 0;

    W25QXX_CS_LOW();

    W25QXX_ReadWriteByte(W25X_JedecDeviceID);   /* 发送 0x9F 指令 */
    id |= (uint32_t)W25QXX_ReadWriteByte(0xFF) << 16; /* 厂商 ID: 0xEF */
    id |= (uint32_t)W25QXX_ReadWriteByte(0xFF) << 8;  /* 存储器类型: 0x40 */
    id |= (uint32_t)W25QXX_ReadWriteByte(0xFF);       /* 容量 ID: 0x17 */

    W25QXX_CS_HIGH();

    return id;
}

/**
  * @brief  读取状态寄存器 1 (SR1)
  * @retval 状态寄存器值 (BIT0: WIP 忙标志, BIT1: WEL 写使能)
  */
uint8_t W25QXX_ReadSR(void)
{
    uint8_t byte = 0;
    W25QXX_CS_LOW();
    W25QXX_ReadWriteByte(W25X_ReadStatusReg);
    byte = W25QXX_ReadWriteByte(0xFF);
    W25QXX_CS_HIGH();
    return byte;
}

/**
  * @brief  等待芯片空闲 (WIP 位清零)
  */
void W25QXX_Wait_Busy(void)
{
    uint32_t timeout = 500000;
    while ((W25QXX_ReadSR() & 0x01) == 0x01)
    {
        if (--timeout == 0) break;
    }
}

/**
  * @brief  开启写使能 (发送 0x06 指令)
  */
void W25QXX_Write_Enable(void)
{
    W25QXX_CS_LOW();
    W25QXX_ReadWriteByte(W25X_WriteEnable);
    W25QXX_CS_HIGH();
}

/**
  * @brief  关闭写使能 (发送 0x04 指令)
  */
void W25QXX_Write_Disable(void)
{
    W25QXX_CS_LOW();
    W25QXX_ReadWriteByte(W25X_WriteDisable);
    W25QXX_CS_HIGH();
}

/**
  * @brief  擦除一个 4KB 扇区
  * @param  Dst_Addr: 扇区地址 (范围 0 ~ 0x7FF000)
  */
void W25QXX_Erase_Sector(uint32_t Dst_Addr)
{
    W25QXX_Write_Enable();
    W25QXX_Wait_Busy();

    W25QXX_CS_LOW();
    W25QXX_ReadWriteByte(W25X_SectorErase);
    W25QXX_ReadWriteByte((uint8_t)((Dst_Addr) >> 16));
    W25QXX_ReadWriteByte((uint8_t)((Dst_Addr) >> 8));
    W25QXX_ReadWriteByte((uint8_t)Dst_Addr);
    W25QXX_CS_HIGH();

    W25QXX_Wait_Busy();
}

/**
  * @brief  擦除一个 64KB 块 (Block)
  * @param  Dst_Addr: 块起始物理地址
  */
void W25QXX_Erase_Block(uint32_t Dst_Addr)
{
    W25QXX_Write_Enable();
    W25QXX_Wait_Busy();

    W25QXX_CS_LOW();
    W25QXX_ReadWriteByte(W25X_BlockErase);
    W25QXX_ReadWriteByte((uint8_t)((Dst_Addr) >> 16));
    W25QXX_ReadWriteByte((uint8_t)((Dst_Addr) >> 8));
    W25QXX_ReadWriteByte((uint8_t)Dst_Addr);
    W25QXX_CS_HIGH();

    W25QXX_Wait_Busy();
}

/**
  * @brief  连续读取 SPI Flash 数据
  * @param  pBuffer: 数据接收缓冲区
  * @param  ReadAddr: 起始读取地址 (24位)
  * @param  NumByteToRead: 读取字节数
  */
void W25QXX_Read(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead)
{
    W25QXX_CS_LOW();

    W25QXX_ReadWriteByte(W25X_ReadData);
    W25QXX_ReadWriteByte((uint8_t)((ReadAddr) >> 16));
    W25QXX_ReadWriteByte((uint8_t)((ReadAddr) >> 8));
    W25QXX_ReadWriteByte((uint8_t)ReadAddr);

    while (NumByteToRead--)
    {
        *pBuffer++ = W25QXX_ReadWriteByte(0xFF);
    }

    W25QXX_CS_HIGH();
}

/**
  * @brief  在指定页内写入不超过 256 字节的数据
  * @param  pBuffer: 发送缓冲区指针
  * @param  WriteAddr: 写入起始地址
  * @param  NumByteToWrite: 写入字节数 (<=256)
  */
void W25QXX_Write_Page(uint8_t *pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    W25QXX_Write_Enable();

    W25QXX_CS_LOW();
    W25QXX_ReadWriteByte(W25X_PageProgram);
    W25QXX_ReadWriteByte((uint8_t)((WriteAddr) >> 16));
    W25QXX_ReadWriteByte((uint8_t)((WriteAddr) >> 8));
    W25QXX_ReadWriteByte((uint8_t)WriteAddr);

    while (NumByteToWrite--)
    {
        W25QXX_ReadWriteByte(*pBuffer++);
    }

    W25QXX_CS_HIGH();
    W25QXX_Wait_Busy();
}

/**
  * @brief  写入任意长度数据 (跨页自动分包，无需用户关心 256 字节页边界)
  * @param  pBuffer: 待写入数据
  * @param  WriteAddr: 起始地址
  * @param  NumByteToWrite: 字节总数
  */
void W25QXX_Write_NoCheck(uint8_t *pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    uint16_t pageremain;
    pageremain = 256 - WriteAddr % 256; /* 单页剩余空间 */

    if (NumByteToWrite <= pageremain) pageremain = NumByteToWrite;

    while (1)
    {
        W25QXX_Write_Page(pBuffer, WriteAddr, pageremain);
        if (NumByteToWrite == pageremain) break;
        else
        {
            pBuffer += pageremain;
            WriteAddr += pageremain;

            NumByteToWrite -= pageremain;
            if (NumByteToWrite > 256) pageremain = 256;
            else pageremain = NumByteToWrite;
        }
    }
}

/**
  * @brief  写入任意长度数据 (自动分页管理)
  */
void W25QXX_Write(uint8_t *pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    W25QXX_Write_NoCheck(pBuffer, WriteAddr, NumByteToWrite);
}

/**
  * @brief  自动化上电自检：读取 ID、并在末尾安全扇区测试擦写与读回
  * @retval true: 自检 100% 成功; false: 失败
  */
bool W25QXX_SelfTest(void)
{
    uint32_t id = W25QXX_ReadID();
    printf("\r\n[W25Q64] Probing Flash JEDEC ID...\r\n");
    printf("[W25Q64] Device ID read back: 0x%06X (Expected: 0x%06X)\r\n", id, W25Q64_JEDEC_ID);

    if (id != W25Q64_JEDEC_ID)
    {
        printf("[W25Q64 ERROR] ID mismatch or hardware unconnected!\r\n");
        return false;
    }

    /* 使用芯片最末尾的第 2047 扇区 (0x7FF000) 执行安全自检测试 */
    const uint32_t test_addr = 0x7FF000;
    const uint8_t  test_write[8] = {0xAA, 0x55, 0x12, 0x34, 0x56, 0x78, 0xDE, 0xAD};
    uint8_t        test_read[8]  = {0};

    printf("[W25Q64] Erasing test sector (0x%06X)...\r\n", test_addr);
    W25QXX_Erase_Sector(test_addr);

    printf("[W25Q64] Programming test signature...\r\n");
    W25QXX_Write_Page((uint8_t *)test_write, test_addr, sizeof(test_write));

    printf("[W25Q64] Reading back verified data...\r\n");
    W25QXX_Read(test_read, test_addr, sizeof(test_read));

    if (memcmp(test_write, test_read, sizeof(test_write)) == 0)
    {
        printf("[W25Q64 SUCCESS] R/W loopback test PASSED! Hardware 100%% Operational.\r\n");
        return true;
    }
    else
    {
        printf("[W25Q64 ERROR] Readback failed: read [%02X %02X %02X %02X...]\r\n",
               test_read[0], test_read[1], test_read[2], test_read[3]);
        return false;
    }
}
