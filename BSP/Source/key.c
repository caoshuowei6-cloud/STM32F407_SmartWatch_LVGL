/**
  ******************************************************************************
  * @file    key.c
  * @brief   按键输入驱动（适配 STM32F407 标准库）
  ******************************************************************************
  * 按键功能说明：
  * - KEY1 (PB1)  : 短按抬起上报 1
  * - KEY2 (PC0)  : 短按抬起上报 2 (原 PA6 腾出给 SPI1_MISO)
  * - KEY3 (PC1)  : 短按抬起上报 3；若长按超过 1000ms 直接上报 4 (原 PA4 腾出给 SPI1_CS)
  * - 板载SW1(PA15): 短按抬起上报 5 (核心板板载独立按键)
  ******************************************************************************
  */

#include "key.h"

volatile uint8_t Key_Num = 0;

static volatile uint16_t Key3_PressMs = 0;
static volatile uint8_t  Key3_LongReported = 0;

void KEY_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /* 使能按键相关 GPIO 时钟 (GPIOB, GPIOC, GPIOA) */
    RCC_AHB1PeriphClockCmd(KEY1_CLK | KEY2_CLK | KEY3_CLK | KEY_BOARD_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;       /* 内部上拉，按下为低电平 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    /* 初始化 KEY1 (PB1) */
    GPIO_InitStructure.GPIO_Pin = KEY1_PIN;
    GPIO_Init(KEY1_PORT, &GPIO_InitStructure);

    /* 初始化 KEY2 (PC0), KEY3 (PC1) */
    GPIO_InitStructure.GPIO_Pin = KEY2_PIN | KEY3_PIN;
    GPIO_Init(KEY2_PORT, &GPIO_InitStructure);

    /* 初始化 板载SW1 (PA15) */
    GPIO_InitStructure.GPIO_Pin = KEY_BOARD_PIN;
    GPIO_Init(KEY_BOARD_PORT, &GPIO_InitStructure);
}

/**
  * @brief  KEY3 独立长按检测，放在 1ms 定时中断中调用
  */
void Key3_Tick(void)
{
    if (GPIO_ReadInputDataBit(KEY3_PORT, KEY3_PIN) == 0)
    {
        if (Key3_PressMs < 60000)
        {
            Key3_PressMs++;
        }

        if ((Key3_PressMs >= KEY3_LONG_PRESS_MS) && (Key3_LongReported == 0))
        {
            Key_Num = 4;            /* 产生长按键值 4 */
            Key3_LongReported = 1;
        }
    }
    else
    {
        Key3_PressMs = 0;
    }
}

/**
  * @brief  获取按键值（原子操作清除）
  * @retval 0: 无按键，1~5: 对应按键事件
  */
uint8_t Key_GetNum(void)
{
    uint8_t Temp;

    __disable_irq();
    Temp = Key_Num;
    Key_Num = 0;
    __enable_irq();

    return Temp;
}

/**
  * @brief  读取当前按键物理电平状态
  */
static uint8_t Key_GetState(void)
{
    if (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == 0)
    {
        return 1;
    }
    else if (GPIO_ReadInputDataBit(KEY2_PORT, KEY2_PIN) == 0)
    {
        return 2;
    }
    else if (GPIO_ReadInputDataBit(KEY3_PORT, KEY3_PIN) == 0)
    {
        return 3;
    }
    else if (GPIO_ReadInputDataBit(KEY_BOARD_PORT, KEY_BOARD_PIN) == 0)
    {
        return 5;
    }
    else
    {
        return 0;
    }
}

/**
  * @brief  按键定时消抖与释放检测，放在 1ms 中断中调用（内置 20ms 降频采样）
  */
void Key_Tick(void)
{
    static uint8_t Count = 0;
    static uint8_t CurrentState = 0;
    static uint8_t PreState = 0;

    Count++;
    if (Count >= 20)
    {
        Count = 0;
        PreState = CurrentState;
        CurrentState = Key_GetState();

        /* 检测按键松开瞬间 (释放触发) */
        if (PreState != 0 && CurrentState == 0)
        {
            if ((PreState == 3) && (Key3_LongReported != 0))
            {
                /* 刚才已经上报了长按 4，松开时不重复上报短按 3 */
                Key3_LongReported = 0;
            }
            else
            {
                Key_Num = PreState;
            }
        }
    }
}
