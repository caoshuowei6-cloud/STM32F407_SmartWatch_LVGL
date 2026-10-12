# STM32F407 + FreeRTOS + LVGL 9 企业级智能穿戴手表系统

[![Platform](https://img.shields.io/badge/Platform-STM32F407ZGT6-blue.svg)](https://www.st.com/)
[![Co-Processor](https://img.shields.io/badge/Co--Processor-ESP32--C3-red.svg)](https://www.espressif.com/)
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20V10.4.3-green.svg)](https://www.freertos.org/)
[![GUI](https://img.shields.io/badge/GUI-LVGL%209.x-orange.svg)](https://lvgl.io/)
[![Security](https://img.shields.io/badge/Security-AES--128--CBC%20%2B%20CRC32-brightgreen.svg)]()

本工程是一套面向商业级智能穿戴产品的完整嵌入式固件解决方案，采用 **STM32F407ZGT6（主控）+ ESP32-C3（Wi-Fi/BLE 协处理器）** 双 MCU 架构，构建了高可靠的 **多任务操作系统分层、LVGL 9 动效框架、cJSON 物模型上报** 与 **端到端 AES-128 加密空中 OTA 固件升级系统**。

---

## 🌟 核心技术亮点 (Core Features)

### 1. 企业级无线空中 OTA 固件升级与安全防篡改体系
* **AES-128-CBC 空中流式密文传输**：Wi-Fi 局域网全密文传输，防御 Wireshark 抓包嗅探与逆向破解；
* **片上在线零堆内存解密**：利用单片机局部栈空间（512 字节分片）原地执行 CBC 逆变换流式解密，**动态堆内存占用为 0**；
* **全镜像 IEEE 802.3 CRC32 双重校验**：
  * **第一重（APP 侧）**：暂存区 320KB 全镜像解密后校验，防御空中丢包、密钥错误与 Padding 错位；
  * **第二重（Bootloader 侧）**：热搬运后对 APP 运行区重新校验，防御 Flash 坏块与中途断电撕裂；
* **双镜像 Staging 暂存区防变砖**：升级失败系统自动留在原系统，掉电重启自动自愈恢复。

### 2. FreeRTOS 多任务调度优化与总线互斥保护
* **状态机全局按键互斥锁**：OTA 固件接收期间自动拦截并丢弃所有物理按键事件，杜绝 UI 大面积重绘对网络 DMA 任务的调度饥饿；
* **单 Flash 控制器总线访问互斥**：升级期间主动挂起后台参数 Flash 写入（`Param_Storage_Sync`），彻底杜绝总线并发争抢与硬件锁死。

### 3. cJSON 动态物模型序列化与指令下行解析
* **FreeRTOS 内存钩子接管**：通过 `cJSON_InitHooks` 将 cJSON 底层内存分配重定向至 FreeRTOS 的 `pvPortMalloc` 和 `vPortFree`，复用 `heap_4` 算法的相邻空闲块自动合并机制，**彻底解决单片机堆内存碎片与泄漏隐患**；
* **物模型双向交互**：
  * **上行**：结构化动态组装传感器数据（温湿度、步数、六轴姿态、电池电压）并推送给客户端；
  * **下行**：实时解析手机端发来的 JSON 远程控制指令（如 Ping 探活、外设控制等）。

### 4. 两级非易失性参数持久化（Wear-Leveling 磨损均衡）
* 单扇区（Sector 11, 128KB）划分 **5400 个追加写入槽位**，单次保存仅需 0.08ms，扇区使用寿命放大 5400 倍（突破 5400 万次安全写入）；
* 掉电步数自动恢复、过夜跨天 0 点自动归零自检。

---

## 📐 系统内存与 Flash 分区架构

```
┌─────────────────────────────────────────────────────────────┐
│                 STM32F407 片上 1MB Flash 分区布局            │
├─────────────┬──────────┬──────────────┬─────────────────────┤
│ 物理扇区     │ 扇区大小 │ 物理起始地址 │ 用途功能分配         │
├─────────────┼──────────┼──────────────┼─────────────────────┤
│ Sector 0~4  │  128 KB  │ 0x08000000   │ Bootloader 启动固件  │
│ Sector 5~7  │  384 KB  │ 0x08020000   │ APP 主系统活动运行区 │
│ Sector 8~10 │  384 KB  │ 0x08080000   │ OTA 固件解密暂存区   │
│ Sector 11   │  128 KB  │ 0x080E0000   │ Wear-Leveling参数区 │
└─────────────┴──────────┴──────────────┴─────────────────────┘
```

---

## 🛠️ 硬件与管脚分配 (Hardware Pinout)

| 外设模块 | 核心芯片 / 器件 | 接口类型 | STM32F407 对应引脚 |
| :--- | :--- | :--- | :--- |
| **显示屏幕** | ST7789 240x240 IPS 彩屏 | 8080 并口 / FSMC | FSMC_D0~D15, RS(PD11), CS(PD7) |
| **Wi-Fi 模组** | ESP32-C3 (AT 固件) | USART2 + DMA | TX(PA2), RX(PA3), 波特率 921600 |
| **六轴姿态** | MPU-6050 (DMP/姿态解算) | 软件模拟 I2C | SCL(PB8), SDA(PB9) |
| **温湿度计** | AHT20 | 软件模拟 I2C | SCL(PB6), SDA(PB7) |
| **系统心跳** | 状态指示 LED1 | GPIO 输出 | PC13 (低电平点亮) |
| **物理按键** | KEY1(上), KEY2(下), KEY3(确认) | GPIO 轮询 | PE3, PE4, PA0 (带消抖队列) |

---

## 🚀 编译与 OTA 推送指南

### 1. 固件编译 (Keil MDK)
* 打开 `Project/STM32F407.uvprojx`，使用 ARM Compiler 5 构建生成 `Project/Objects/STM32F429.bin`。

### 2. 空中 OTA 无线推送
确保手表正常开机并连上同一 Wi-Fi 热点（手表默认 TCP 端口为 8080）：

* **方式一：一键双击批处理（推荐）**
  直接双击根目录下的 `run_ota.bat`，直接按回车即可触发默认的 AES-128 加密空中推送。

* **方式二：Python 命令行推送**
  ```bash
  # 企业级 AES-128-CBC 密文空中升级
  python tools/ota_updater.py --ip 192.168.111.89 --encrypt

  # 标准明文兼容升级模式
  python tools/ota_updater.py --ip 192.168.111.89
  ```

---

## 📄 License
This project is released under the MIT License.
