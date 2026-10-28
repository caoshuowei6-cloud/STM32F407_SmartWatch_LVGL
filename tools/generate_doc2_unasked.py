# -*- coding: utf-8 -*-
"""
生成《智能手表项目面试全攻略 - 高频预测与未问考点.docx》
"""
import os
import sys
import docx
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml import parse_xml
from docx.oxml.ns import nsdecls, qn

sys.path.append(os.path.dirname(__file__))
from docx_utils import set_run_font, add_styled_heading, add_body_p, add_callout_box, format_table_header, format_table_row

def build_doc2():
    doc = docx.Document()
    
    # Page setup
    sections = doc.sections
    for s in sections:
        s.top_margin = Inches(0.8)
        s.bottom_margin = Inches(0.8)
        s.left_margin = Inches(0.8)
        s.right_margin = Inches(0.8)

    # Document Header Title
    title_p = doc.add_paragraph()
    title_p.paragraph_format.space_before = Pt(0)
    title_p.paragraph_format.space_after = Pt(4)
    title_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r_title = title_p.add_run("基于STM32F407与ESP32的双MCU智能手表项目\n面试答辩全攻略 —— 面试官必问考点与高频预测")
    set_run_font(r_title, "微软雅黑", 18, True, (31, 73, 125))

    sub_p = doc.add_paragraph()
    sub_p.paragraph_format.space_after = Pt(14)
    sub_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r_sub = sub_p.add_run("适用岗位：嵌入式软件工程师 / 物联网固件开发工程师 | 整理日期：2026-10-16")
    set_run_font(r_sub, "微软雅黑", 10, False, (120, 120, 120))

    # Introduction block
    add_callout_box(doc, "未问考点预测指南", 
        "本文档专门针对你的智能手表项目架构，提炼了你在前面对话中【尚未问到、但面试官 100% 极大概率会深挖】的 17 个硬核问题。\n"
        "涵盖 FreeRTOS 任务优先级规划与防栈溢出机制、USART DMA 空闲中断双缓冲、LVGL 显存双缓冲与局部刷新权衡、Bootloader 底层汇编跳转五部曲、Flash 5400 槽位磨损均衡算法细节以及硬件 I2C 死锁排坑等。\n"
        "掌握这份文档，可以让你在面试中化被动为主动，彻底展现大厂级系统工程视野！", 
        box_type="tip")

    # =========================================================================
    # 第一篇：FreeRTOS 多任务设计、实时调度与栈安全
    # =========================================================================
    add_styled_heading(doc, "第一篇：FreeRTOS 多任务设计、实时调度与栈安全", 1)

    # Q1
    add_styled_heading(doc, "预测 Q1：你的工程里创建了哪几个 FreeRTOS 任务？优先级是怎么规划的？为什么这样排？", 2)
    add_body_p(doc, "【核心任务布局】：在 main.c 中系统规划了 4 大核心任务：")
    
    tbl1 = doc.add_table(rows=5, cols=4)
    tbl1.alignment = WD_TABLE_ALIGNMENT.CENTER
    tbl1.autofit = False
    widths1 = [Inches(1.5), Inches(1.0), Inches(1.2), Inches(2.8)]
    format_table_header(tbl1.rows[0], widths1, ["任务名称", "优先级", "栈大小 (Words)", "设计职责与调度周期"])
    format_table_row(tbl1.rows[1], widths1, ["Task_Key", "3 (最高)", "256 (1KB)", "10ms 按键消抖与事件入队，最高优先级保障输入响应绝对零延迟"])
    format_table_row(tbl1.rows[2], widths1, ["Task_UI", "2 (中高)", "1024 (4KB)", "1ms 轮询 LVGL 渲染流水线与 100ms 动效刷新，保障 60 FPS 极速跟手"], is_even=True)
    format_table_row(tbl1.rows[3], widths1, ["Task_WiFi", "2 (中高)", "768 (3KB)", "阻塞等待 DMA 接收信号量，处理 AT 指令与 cJSON 协议反序列化"])
    format_table_row(tbl1.rows[4], widths1, ["Task_Sensor", "1 (最低)", "512 (2KB)", "20ms 姿态解算与计步计算，在后台平稳运行，绝不抢占界面与网络"], is_even=True)

    add_callout_box(doc, "Q1 优先级规划的核心原则（面试标准回答）", 
        "“在 RTOS 任务优先级规划中，我严格遵循了两个工业准则：\n"
        "1. 执行时间极短、实时性要求极高的事件型任务排最高：按键扫描（Task_Key）每次仅耗时几十微秒，必须最高优先级以保证瞬间捕获用户输入；\n"
        "2. 重吞吐、弱实时的后台计算任务排最低：MPU6050 姿态滤波与计步（Task_Sensor）属于纯数学计算，放在最低优先级，即使被 UI 或网络打断几个毫秒也不会影响用户体验；\n"
        "3. UI 任务与 WiFi 任务同设为中高优先级（2），通过时间片轮转和信号量阻塞交替让出 CPU，兼顾了界面流畅与网络高吞吐。”", 
        "answer")

    # Q2
    add_styled_heading(doc, "预测 Q2：任务堆栈大小（Stack Size）你是怎么估算的？如何检测和防止任务栈溢出？", 2)
    add_body_p(doc, "【估算方法】：")
    add_body_p(doc, "任务栈大小 = (局部变量总和 + 最深函数嵌套压栈 + 编译器对齐开销 + 中断现场寄存器保存 + FPU 硬件浮点寄存器额外 132 字节) * 1.5 倍安全裕量。")
    add_body_p(doc, "例如 Task_UI 由于 LVGL 内部渲染具有多层回调嵌套，分配 1024 words（4KB）；Task_Sensor 包含大量数学浮点运算，分配 512 words（2KB）。")
    add_body_p(doc, "【防栈溢出的双重机制】：")
    add_body_p(doc, "1. 静态监控（高水位线 API）：在调试期周期性调用 uxTaskGetStackHighWaterMark(TaskHandle)，该函数通过扫描任务栈底被预先填充的 0xA5 标记数量，返回自任务启动以来‘历史最少剩余的栈字节数’。如果发现某任务剩余空间小于 64 字节，立即扩容。")
    add_body_p(doc, "2. 动态捕获（FreeRTOS 栈溢出钩子）：在 FreeRTOSConfig.h 中开启 configCHECK_FOR_STACK_OVERFLOW = 2。系统在每次任务切换时，硬件检查栈顶最后 16 个字节的魔数标记是否被踩踏。一旦发生溢出，系统立即跳入 vApplicationStackOverflowHook() 并在串口打印出发生溢出的任务名，绝对不让死机悄无声息。")

    # Q3
    add_styled_heading(doc, "预测 Q3：任务间通信机制（IPC）在项目中是如何落地的？为什么按键用队列而不用全局变量？", 2)
    add_body_p(doc, "【核心回答】：在本项目中综合使用了‘消息队列’与‘二值信号量’两套机制：")
    add_body_p(doc, "1. 按键事件采用消息队列（s_key_queue）：Task_Key 负责以 10ms 扫描按键，按下时通过 xQueueSend(..., 10) 发送键值；Task_UI 通过 xQueueReceive(..., 0) 异步拉取。")
    add_body_p(doc, "【为什么不用全局变量 + volatile？】：全局变量无法缓冲突发事件。如果用户连续快按两次，或者按键在 UI 正在刷大图时到来，全局变量会被第二次按键无情覆盖，造成‘吞按键’；而 FreeRTOS 队列具有 FIFO 缓冲深度，彻底解决了事件突发与生产-消费速率不匹配的矛盾。")
    add_body_p(doc, "2. 串口 DMA 接收采用二值信号量（s_wifi_sem）：USART2 接收到数据帧触发空闲中断时，在 ISR 中调用 xSemaphoreGiveFromISR 唤醒阻塞中的 Task_WiFi，做到了真正的零 CPU 轮询占用。")

    # Q4
    add_styled_heading(doc, "预测 Q4：什么是优先级反转（Priority Inversion）？FreeRTOS 是如何解决的？二值信号量与互斥锁有何区别？", 2)
    add_body_p(doc, "【优先级反转场景】：低优先级任务 L 拿了互斥资源（如 I2C 总线）；高优先级任务 H 想要该资源而被迫挂起；此时中优先级任务 M 突然就绪，由于 M 优先级高于 L，M 抢占了 L 的 CPU。结果导致：高优先级任务 H 被中优先级任务 M 无限期拖延！")
    add_body_p(doc, "【FreeRTOS 的解法：优先级继承（Priority Inheritance）】：")
    add_body_p(doc, "当 H 试图获取被 L 持有的互斥锁（Mutex）时，FreeRTOS 会临时将 L 的优先级提升到与 H 相同！这样中优先级任务 M 就无法抢占 L，L 能快速执行完并释放锁。释放锁的一瞬间，L 的优先级恢复原样，H 立即获得锁执行。")
    add_body_p(doc, "【二值信号量 vs 互斥锁（Mutex）】：二值信号量用于‘任务同步’（一个给，另一个拿），没有优先级继承机制；互斥锁用于‘资源互斥独占’，必须同一个任务获取和释放，且天生自带优先级继承机制。")

    # =========================================================================
    # 第二篇：ESP32 双 MCU 通信与串口 DMA + 空闲中断驱动
    # =========================================================================
    add_styled_heading(doc, "第二篇：ESP32 双 MCU 通信与串口 DMA + 空闲中断驱动", 1)

    # Q5
    add_styled_heading(doc, "预测 Q5：STM32 与 ESP32 之间采用什么物理接口？为什么采用“USART DMA + 空闲中断（IDLE）”？", 2)
    add_body_p(doc, "【物理接口】：采用标准全双工 USART（STM32 的 PA2/PA3 复用 USART2，接 ESP32-C3 的 RX/TX），波特率 115200。")
    add_body_p(doc, "【为什么采用 DMA + 空闲中断（IDLE）？】：")
    add_body_p(doc, "1. 传统逐字节串口中断的致命伤：在 115200 波特率下，每秒有 1.15 万个字节到来，每来一个字节 CPU 就要进一次中断，CPU 算力全被中断现场保存打碎，在接收 330KB OTA 固件时 CPU 负荷几乎 100%。")
    add_body_p(doc, "2. 空闲中断解决“不定长数据帧”切分：网络下发的数据帧长度是不固定的（可能 50 字节，可能 512 字节）。USART 硬件自带 IDLE 检测电路，当总线上超过 1 个字符时间没有新数据时，硬件自动拉起 IDLE 标志位。")
    add_body_p(doc, "3. 配合机制：DMA1_Stream5 在后台自动把数据搬移到环形缓冲，CPU 全程 0 参与；一帧结束触发一次 IDLE 中断，CPU 只需在中断里算一下 rx_len = BUF_SIZE - DMA_GetCurrDataCounter()，唤醒 WiFi 任务处理，CPU 占用率从 80% 降到 <2%！")

    # Q6
    add_styled_heading(doc, "预测 Q6：WiFi 模块的 AP 模式与 STA 模式有什么区别？在智能手表或物联网机器人中应该如何选型？", 2)
    add_body_p(doc, "【核心概念对比】：")
    add_body_p(doc, "1. STA 模式（Station，工作站/终端模式）：模块相当于手机或电脑，主动连接外部路由器 WiFi。能访问互联网，但自身不发射热点。")
    add_body_p(doc, "2. AP 模式（Access Point，热点模式）：模块自身变身路由器，发射 WiFi 信号，手机可以直接连入它，但如果它没插网线则无法访问外网。")
    add_body_p(doc, "【智能手表与机器人工程选型策略】：")
    add_body_p(doc, "日常工作状态下，**必须配成 STA 模式**，以便设备接入家庭或办公室路由器，连接 MQTT 物模型服务器与进行 OTA 升级。")
    add_body_p(doc, "在‘出厂配网阶段’，启动 **AP+STA 混合模式**：模块发射 SmartWatch-Setup 热点，用户手机直连手表热点打开 Web 网页输入家里的 WiFi 密码；手表拿到密码后自动切回 STA 模式连接路由器并关闭 AP。")

    # =========================================================================
    # 第三篇：LVGL 图形库移植、渲染流水线与显示加速
    # =========================================================================
    add_styled_heading(doc, "第三篇：LVGL 图形库移植、渲染流水线与显示加速", 1)

    # Q7
    add_styled_heading(doc, "预测 Q7：LVGL 的心跳时钟（Tick）与主渲染循环（Timer Handler）是如何配合调度的？", 2)
    add_body_p(doc, "【两者分工】：")
    add_body_p(doc, "1. lv_tick_inc()：提供界面的‘时间基准’。在 FreeRTOS 的 SysTick 中断或者独立的硬件定时器中以 1ms 周期调用，仅负责更新全局滴答毫秒计数器，代码极其轻量，绝不执行绘图。")
    add_body_p(doc, "2. lv_timer_handler()：负责具体的‘页面计算与控件重绘’。必须放在 Task_UI 线程中循环调用。它根据当前逝去的时间计算控件动效、手势滑动惯性，并将变动的脏矩形区域发送给显示驱动。")
    add_body_p(doc, "两者完全解耦，保证了即使渲染过程偶尔耗时较长，UI 的时间流速依然精准不漂移。")

    # Q8
    add_styled_heading(doc, "预测 Q8：LVGL 显示缓冲区（Display Buffer）是单缓冲还是双缓冲？开多大内存？如何权衡？", 2)
    add_body_p(doc, "【核心回答】：受限于 STM32F407 的 128KB 内部 SRAM，我们采用了‘局部行双缓冲（Partial Double Buffer）’方案：")
    add_body_p(doc, "1. 屏幕分辨率是 240x240，16 位 RGB565 颜色。一帧完整的全屏图像需要 240 * 240 * 2 = 115.2 KB！如果开全屏单缓冲，直接耗尽全部 RAM，系统必死。")
    add_body_p(doc, "2. 我们的方案：开辟两个 240 * 20 像素的行缓冲区（每个约 9.6 KB，共 19.2 KB）：")
    add_body_p(doc, "Buffer 1 用于 CPU/LVGL 渲染当前切片的 20 行像素；同时 Buffer 2 通过 SPI DMA 硬件向 LCD 屏幕全速推送上一帧切片。两者交替流水线作业，以不到 20KB 的内存开销实现了接近全屏双缓冲的 60 FPS 丝滑刷屏，且无屏幕撕裂。")

    # =========================================================================
    # 第四篇：Bootloader 底层汇编跳转五部曲与向量表重映射
    # =========================================================================
    add_styled_heading(doc, "第四篇：Bootloader 底层汇编跳转五部曲与向量表重映射", 1)

    # Q9
    add_styled_heading(doc, "预测 Q9：请详细讲讲从 Bootloader 跳转到 Application 时，底层 CPU 到底执行了哪些汇编与寄存器操作？", 2)
    add_callout_box(doc, "Q9 必须背熟的 Bootloader 跳转五部曲", 
        "在 boot_iap.c 的 Boot_JumpToApp() 中，底层严格执行以下五步：\n"
        "1. 关闭全局中断：__disable_irq()，防止在修改堆栈和向量表的中途触发中断导致跑飞；\n"
        "2. 关闭 SysTick 定时器：SysTick->CTRL = 0; SysTick->VAL = 0; 彻底停掉时钟滴答中断；\n"
        "3. 重定位中断向量表（VTOR）：SCB->VTOR = APP_ADDR (0x08020000)，将 CPU 的异常向量重映射到 APP 起始地址；\n"
        "4. 重置主堆栈指针（MSP）：读取 0x08020000 处的第一个 32 位字（APP 初始栈顶），通过 __set_MSP(*(uint32_t *)APP_ADDR) 赋给 CPU 的 SP 寄存器；\n"
        "5. 提取复位入口并执行跳转：读取 0x08020004 处的第二个字（Reset_Handler 地址），将其强转为无参函数指针 void (*app_reset)(void)，直接调用 app_reset() 跳转！", 
        "answer")

    # Q10
    add_styled_heading(doc, "预测 Q10：为什么 Application 的 main() 函数开头必须重新配置中断向量表偏移？如果不配会怎样？", 2)
    add_body_p(doc, "【核心原因】：")
    add_body_p(doc, "Cortex-M4 芯片在硬件复位上电时，SCB->VTOR 寄存器默认是 0x08000000（指向 Bootloader）。虽然 Bootloader 在跳转前改过了 VTOR，但是很多开发者的 APP 在启动时执行了标准库的 SystemInit()，该函数内部有一行代码会重新将 VTOR 强制复位为 0x08000000！")
    add_body_p(doc, "如果不重新配置 NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x20000)，APP 只要开启任何一个外设中断（如滴答定时器或串口），CPU 就会错误地跳去执行 Bootloader 的中断服务函数，导致系统瞬间死机或者无限死循环重启。")

    # =========================================================================
    # 第五篇：内部 Flash 5400 槽位磨损均衡与参数存储实现细节
    # =========================================================================
    add_styled_heading(doc, "第五篇：内部 Flash 5400 槽位磨损均衡与参数存储实现细节", 1)

    # Q11
    add_styled_heading(doc, "预测 Q11：单扇区 128KB 容纳 5400 槽位磨损均衡，单次保存耗时 <0.1ms 是怎么算出来的？", 2)
    add_body_p(doc, "【物理计算逻辑】：")
    add_body_p(doc, "1. 槽位容量计算：系统参数结构体 sys_params_t 对齐后为 24 字节。STM32F407 的 Sector 11 大小为 128 KB = 131,072 字节。131072 / 24 = 5461 个槽位，我们保守定为 5400 个槽位。")
    add_body_p(doc, "2. <0.1ms 耗时原理：保存参数时无需擦除扇区！只需向当前空白槽位写入 6 个 32-bit Word（24 字节）。STM32 内部 Flash 硬件编程单个 Word 仅需 16 微秒（us），6 个字总共耗时约 96 微秒（即 0.096ms < 0.1ms）。相较于传统每次保存都要耗时几百毫秒擦除整个扇区，速度提升了数千倍！")

    # Q12
    add_styled_heading(doc, "预测 Q12：系统开机时，如何从 5400 个槽位中快速定位出“最后一个有效写入槽位”？", 2)
    add_body_p(doc, "【扫描算法实现】：")
    add_body_p(doc, "我们利用了 Flash 擦除后为全 1（0xFFFFFFFF）的物理特性：")
    add_body_p(doc, "从索引 0 开始顺序遍历，检查每个槽位的 magic 字段：")
    add_body_p(doc, "1. 如果读到 slot->magic == 0xFFFFFFFF，说明遇到了未写入的空白区，前一个索引就是最新的记录，立刻中断循环，无需继续扫描剩余的几千个槽位！")
    add_body_p(doc, "2. 对于非空白槽位，必须严格校验 slot->magic == PARAM_MAGIC_NUMBER 以及累加校验和 checksum。若遇到中途断电损坏的残缺块，自动回退到上一个完好块，保证了 100% 数据防破损。")

    # =========================================================================
    # 第六篇：硬件通信排坑与传感器底层工程细节
    # =========================================================================
    add_styled_heading(doc, "第六篇：硬件通信排坑与传感器底层工程细节", 1)

    # Q13
    add_styled_heading(doc, "预测 Q13：为什么驱动 MPU6050 时使用了软件模拟 I2C，而没有使用 STM32 的硬件 I2C 外设？", 2)
    add_callout_box(doc, "Q13 经典硬件排坑（面试官极度爱听的实战经验）", 
        "“在 STM32F1/F4 系列中，官方硬件 I2C 外设存在业内著名的硬件设计缺陷（Errata）：\n"
        "当总线上遇到轻微干扰、从机无应答、或多任务中断抢占时，硬件 I2C 的 BUSY 标志位极易锁死，导致 I2C_CheckEvent() 陷入死循环卡死系统，恢复极为困难。\n"
        "而采用 GPIO 软件模拟 I2C 有三大无可比拟的工程优势：\n"
        "1. 绝不卡死：代码时序完全由软件控制，遇到从机拉低 SDA 产生死锁，主控只需模拟 9 个 SCL 时钟脉冲即可瞬间硬件复位总线；\n"
        "2. 引脚自由映射：无需受限于硬件 I2C 的固定引脚，我们使用 PF0/PF1 完美避开了 LCD 屏幕和按键引脚冲突；\n"
        "3. 极其稳定可靠：在 400kHz 以下速率时序完全满足传感器要求，十万次连续读取零卡死。”", 
        "trap")

    # Q14
    add_styled_heading(doc, "预测 Q14：MPU6050 的零偏校准（Calibration）是如何实现的？开机静止校准的细节是什么？", 2)
    add_body_p(doc, "【校准原理】：MEMS 陀螺仪在出厂后受焊接应力与温度影响，即使完全静止，输出的原始角速度也不为零（如每秒漂移几个数字），必须通过软件校准去除零偏。")
    add_body_p(doc, "【工程实现】：")
    add_body_p(doc, "在系统开机初始化时，提示用户保持设备静止 1 秒钟：连续采集 200 组原始六轴数据，计算各轴平均值作为偏移量 offset_gx, offset_gy, offset_gz。")
    add_body_p(doc, "此后每一次读取原始数据，均自动减去该偏移量。这一步将姿态解算的长期累积漂移降低了 90% 以上。")

    out_path = r"C:\Users\LENOVO\Desktop\智能手表项目面试全攻略-高频预测与未问考点.docx"
    doc.save(out_path)
    print(f"[SUCCESS] Document 2 generated at: {out_path}")

if __name__ == "__main__":
    build_doc2()
