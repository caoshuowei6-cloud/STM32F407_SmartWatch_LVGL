# -*- coding: utf-8 -*-
"""
生成《智能手表项目面试全攻略 - 已提问核心答辩精编.docx》
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

def build_doc1():
    doc = docx.Document()
    
    # Page setup - 2cm margins
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
    r_title = title_p.add_run("基于STM32F407与ESP32的双MCU智能手表项目\n面试答辩全攻略 —— 已提问核心考点深度精编")
    set_run_font(r_title, "微软雅黑", 18, True, (31, 73, 125))

    sub_p = doc.add_paragraph()
    sub_p.paragraph_format.space_after = Pt(14)
    sub_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r_sub = sub_p.add_run("适用岗位：嵌入式软件工程师 / 物联网固件开发工程师 | 整理日期：2026-10-16")
    set_run_font(r_sub, "微软雅黑", 10, False, (120, 120, 120))

    # Introduction block
    add_callout_box(doc, "文档使用指南", 
        "本文档严格基于你在准备过程中向 AI 提出过的所有技术疑问与实战痛点进行深度复盘整理。涵盖 CmBacktrace 故障捕获、安全 OTA 双分区容灾、低功耗动态状态机、一阶互补滤波与多维时空窗手势识别、FreeRTOS 内存钩子等核心技术。"
        "每一个问题均包含【底层技术大白话拆解】、【1 分钟标准应答模板】以及【面试官连环追问与防守技巧】，可直接背诵并作为明天的临场底牌！", 
        box_type="tip")

    # =========================================================================
    # 第一篇：系统稳定性与 CmBacktrace 黑匣子故障诊断
    # =========================================================================
    add_styled_heading(doc, "第一篇：系统稳定性与 CmBacktrace 黑匣子故障诊断", 1)

    # Q1
    add_styled_heading(doc, "Q1：什么是 CmBacktrace？死机捕获的底层硬件原理是什么？", 2)
    add_body_p(doc, "【核心痛点】在传统的 Cortex-M 单片机开发中，程序一旦产生野指针、除零或内存越界，硬件会自动触发 HardFault 中断，默认死在 while(1) 循环中。脱离调试器后，开发者完全不知道是哪一行代码挂掉的。")
    add_body_p(doc, "【底层原理】CmBacktrace（ARM Cortex-M 崩溃追踪库）的硬件接管机制分为三步：")
    add_body_p(doc, "1. 异常向量接管：将 CPU 的 HardFault_Handler、BusFault_Handler、UsageFault_Handler 接管至 CmBacktrace 的汇编入口。")
    add_body_p(doc, "2. 寄存器快照与硬件解码：在进入中断的瞬间，读取 Cortex-M 内核系统控制块（SCB）的 CFSR（可配置故障状态寄存器）、HFSR、MMFAR 等寄存器，硬件级自动解码出是除以零（DIVBYZERO）、未对齐访问（UNALIGNED）、还是非法总线访问（BusFault）。")
    add_body_p(doc, "3. 线程栈帧逆向展开（Stack Unwinding）：根据进入异常时 LR 寄存器的 EXC_RETURN 判定当前是 MSP（主栈）还是 PSP（线程栈），逆向扫描 FreeRTOS 崩溃任务的任务控制块（TCB）栈帧，提取出死机瞬间及之前调用链的 PC（程序计数器）和 LR 地址。")
    add_callout_box(doc, "Q1 面试应答模板", 
        "“在智能手表开发中，脱机运行排查 HardFault 是行业痛点。我引入了 CmBacktrace 库并结合 FreeRTOS 做了任务感知。\n"
        "当系统发生严重故障时，底层异常向量立即拦截，通过硬件解析 SCB->CFSR 寄存器，秒级判断出故障根因（如除以零或非法内存访问），同时自动提取崩溃任务名称（如 Task_WiFi）。\n"
        "更重要的是，它能逆向回溯任务调用栈，将包含 PC/LR 调用链的 16 进制机器码地址打印出来，为后续离线反查提供了完整的数据支撑。”", 
        "answer")

    # Q2
    add_styled_heading(doc, "Q2：既然 CmBacktrace 这么好用，为什么芯片原厂或标准库不直接集成？", 2)
    add_body_p(doc, "【考官意图】考查你对芯片厂商设计哲学、库函数轻量性与商业利益的理解。")
    add_body_p(doc, "【核心原因分析】：")
    add_body_p(doc, "1. 标准库必须追求“最小公共子集”与绝对零侵入：芯片原厂（如 ST、NXP）提供的 HAL/标准外设库只负责驱动硬件，不能强行绑定第三方组件。集成栈回溯需要强制占用堆栈空间，并依赖特定的打印串口/日志输出接口，侵入了用户的代码设计。")
    add_body_p(doc, "2. RTOS 碎片化严重：不同的项目用不同的操作系统（裸机、FreeRTOS、RT-Thread、uC/OS-III）。不同 RTOS 的 TCB 结构体、栈帧布局、任务切换机制完全不同，原厂标准库无法预设适配所有系统。")
    add_body_p(doc, "3. 商业闭环诉求：各大原厂有自己的付费或生态硬件调试工具（如 ST-Link、J-Link、Keil ULINK）。原厂更倾向于引导用户购买硬件仿真器在集成开发环境内单步调试。")

    # Q3
    add_styled_heading(doc, "Q3：这种软件堆栈回溯机制比传统硬件调试器（J-Link / ST-Link）好在哪里？", 2)
    add_body_p(doc, "【核心对比】：这是面试官最容易追问的实战对比题。")
    
    # Table for Q3
    tbl3 = doc.add_table(rows=5, cols=3)
    tbl3.alignment = WD_TABLE_ALIGNMENT.CENTER
    tbl3.autofit = False
    widths3 = [Inches(1.5), Inches(2.5), Inches(2.5)]
    format_table_header(tbl3.rows[0], widths3, ["对比维度", "传统硬件调试器 (J-Link/ST-Link)", "CmBacktrace + 自动化脚本"])
    format_table_row(tbl3.rows[1], widths3, ["使用场景", "必须拖着 SWD 排线，仅限实验室工位桌面调试", "零物理线缆依赖，产品量产出厂后、野外脱机运行均可生效"])
    format_table_row(tbl3.rows[2], widths3, ["偶发Bug捕获\n(海森堡Bug)", "打断点会改变系统运行节拍，偶发性时序Bug一插调试器往往不复现", "平时零性能开销、不占中断，仅在死机一瞬间抓拍现场，捕获率 100%"], is_even=True)
    format_table_row(tbl3.rows[3], widths3, ["RTOS 调用栈", "停在 HardFault_Handler 死循环，MSP/PSP 切换后 Call Stack 常为乱码", "原生感知 FreeRTOS，直接打印出 Task_WiFi 任务名、栈顶与剩余水位"])
    format_table_row(tbl3.rows[4], widths3, ["运维与协作", "无法远程排查，必须要求客户返厂寄修", "用户或测试人员只需把串口/网络日志发给研发，10秒内精确定位行号"], is_even=True)

    add_callout_box(doc, "Q3 总结金句", 
        "“调试器解决的是‘实验室桌面开发期’的问题；而 CmBacktrace 搭配离线反查工具，解决的是‘产品量产后、脱离调试器野外运行、以及长周期偶发性死机’的黑匣子定位难题。”", 
        "answer")

    # Q4
    add_styled_heading(doc, "Q4：这个功能原本是骑行码表项目的，迁移到智能手表工程里合适吗？", 2)
    add_body_p(doc, "【核心回答】不仅合适，而且在智能手表上比在码表上更具技术合理性与必要性！")
    add_body_p(doc, "1. 外壳密封度更高：智能手表为了满足生活防水甚至游泳级防水，整机都是打胶密封的，表面绝无可能外露 SWD 调试引脚，天生就是“密封黑盒子”。")
    add_body_p(doc, "2. 软件复杂度呈指数级提升：手表集成了 LVGL 图形界面动态内存渲染、ESP32 异步网络通信、cJSON 物模型频繁反序列化、FreeRTOS 多任务并发调度。多任务并发与频繁的动态内存申请是内存越界和空指针的高危区，更需要死机黑匣子保驾护航。")
    add_body_p(doc, "3. 形成工业级 DevOps 闭环：手表具备 ESP32 WiFi 与 AES-128 OTA 在线升级能力。死机捕获 -> 定位源码行号修复 -> 远程 OTA 推送热补丁，构成了真正完整的工业级闭环。")

    # Q5
    add_styled_heading(doc, "Q5：离线堆栈反查工具（addr2line + Python）具体怎么使用？原理是什么？", 2)
    add_body_p(doc, "【使用方式】：")
    add_body_p(doc, "1. 交互模式：运行 python tools/fault_analyzer.py，直接将串口日志中包含 Show more call stack info 的整行文本直接粘贴并按回车。")
    add_body_p(doc, "2. 命令行模式：python tools/fault_analyzer.py 08021c98 08034082 08022ad4 080266a4。")
    add_body_p(doc, "【底层原理】：")
    add_body_p(doc, "1. Keil 编译生成的 .axf 文件（ELF 格式）中包含了完整的 DWARF 调试符号表，详细记录了芯片 Flash 中每一个机器码地址与 C 源码文件、函数、具体行号的映射关系。")
    add_body_p(doc, "2. Python 脚本通过正则表达式自动从杂乱的日志中抽取出以 08 开头的 32 位 Hex 地址，然后调用 arm-none-eabi-addr2line.exe -e STM32F429.axf -a -f -C <地址列表>。")
    add_body_p(doc, "3. 工具在毫秒级内输出死机行号（如实测精准定位至 esp32_wifi.c:658 的 num / den 除以零语句），彻底消除了人工去翻看 Map 文件反汇编的繁琐过程。")

    # Q6
    add_styled_heading(doc, "Q6：为什么向 0xFFFFFFF0 写数据没有死机，而除以零能 100% 触发故障？", 2)
    add_body_p(doc, "【STM32 总线矩阵特性】：0xFFFFFFF0 位于 Cortex-M4 预留的厂商未映射地址空间。在 STM32F407 未开启 MPU（内存保护单元）的情况下，内部总线矩阵对该区域采取了 RAZ/WI 策略（Read-As-Zero / Write-Ignored，读返回零，写静默忽略），总线不报错，因此 CPU 正常运行，心跳依然在跳。")
    add_body_p(doc, "【除零硬件陷阱】：Cortex-M4 默认对除以零指令不拦截（返回结果 0）。但只要在系统内核寄存器 SCB->CCR 中将第 4 位置 1（SCB_CCR_DIV_0_TRP_Msk），即可强行打开 CPU 硬件除零陷阱。此后只要汇编指令执行 SDIV/UDIV 遇到分母为 0，硬件会在 1 个时钟周期内抛出 UsageFault 异常，100% 稳定被 CmBacktrace 捕获。")

    # =========================================================================
    # 第二篇：安全 OTA 在线升级与存储容灾
    # =========================================================================
    add_styled_heading(doc, "第二篇：安全 OTA 在线升级与存储容灾架构", 1)

    # Q7
    add_styled_heading(doc, "Q7：什么是 AES-128 在线流式硬件解密？为什么能做到“零堆内存开销”？", 2)
    add_body_p(doc, "【大白话通俗理解】：")
    add_body_p(doc, "传统全包解密做法需要开辟 335KB 的内存空间把整个固件存下来整体解密，而 STM32F407 总共只有 128KB 内部 SRAM，物理上绝对无法放下。")
    add_body_p(doc, "流式做法（Streaming Pipeline）将固件变成“流水线”：")
    add_body_p(doc, "1. 上位机每次以 512 字节分片（Chunk）下发密文（512 恰好是 AES 16 字节分组块的整数倍）。")
    add_body_p(doc, "2. 固件接收端复用栈上的一个固定 512 字节局部变量 uint8_t dec_buf[512]，原地执行 AES-128-CBC 块解密。")
    add_body_p(doc, "3. 解密后的明文流水线对接片上 Flash 硬件编程控制器，按 32-bit Word 写入 Flash 暂存区。")
    add_body_p(doc, "【零堆内存开销】：全程使用固定栈空间或静态缓冲区，不调用一次 malloc() 或 pvPortMalloc()，彻底杜绝了频繁申请释放导致的堆内存碎片化问题。")

    # Q8
    add_styled_heading(doc, "Q8：STM32F407 本身到底有没有硬件加密外设？被问到怎么防守？", 2)
    add_callout_box(doc, "Q8 必背防守话术（体现对芯片选型深刻理解）", 
        "“是的，STM32 家族中只有带‘5’或‘7’尾号的型号（如 STM32F415 / F417 / F437）才内置 CRYP 硬件加密加速器，而普通 STM32F407 本身没有 CRYP 硬件外设。\n"
        "我们简历中所指的‘硬件流式解密’，是指整个加解密数据流打通了硬件数据链路：\n"
        "前端由 USART2 DMA 硬件外设实现零 CPU 拷贝的数据分包接收；中间解密采用高效查表法对 512 字节定长栈缓冲原地解密；后端直接对接 STM32 片上 Flash 硬件编程控制器流水线落盘。\n"
        "在实际工程测试中，512 字节软件解密耗时仅 1.2ms，完全被 Flash 扇区编程的物理耗时（约 4~8ms）所覆盖，因此无需采购昂贵的带硬件加密的芯片，也完全满足了无线 OTA 的实时性与安全性需求。”", 
        "trap")

    # Q9
    add_styled_heading(doc, "Q9：什么是“全镜像双重 CRC32 校验”？为什么单重校验在工业级场景下不够？", 2)
    add_body_p(doc, "【为什么单重不够】：许多初学者只在接收端做一次 CRC 校验。这存在巨大盲区——“网络传输不出错，不代表 Flash 物理擦写不出错”。如果 Flash 扇区存在物理老化坏块，或者写入中途电源轻微抖动产生位翻转（Bit-flip），代码直接跑飞。")
    add_body_p(doc, "【双重校验闭环设计】：")
    add_body_p(doc, "1. 第一重校验（暂存区 Staging CRC32）：Bootloader 开机第一步，对暂存区（Sector 8~10，0x08080000）的 335KB 镜像做全盘计算。防御网络丢包、传输错位和 AES 密钥解密错误。只有暂存区 100% 完整，才允许擦除原 APP！")
    add_body_p(doc, "2. 第二重校验（运行区 APP CRC32）：固件从暂存区搬运写入到 APP 运行区（Sector 5~7，0x08020000）后，Bootloader 对 APP 运行区的物理数据再做一次全镜像 CRC32 校验。确保 Flash 物理扇区没有写入坏块。")
    add_body_p(doc, "两重校验全部吻合原文件 CRC，Bootloader 才会擦除升级标志位并跳转，确保了系统 100% 的升级可靠性。")

    # Q10
    add_styled_heading(doc, "Q10：为什么升级时要做“按键互斥锁”？如果不做会发生什么严重后果？", 2)
    add_body_p(doc, "【痛点】：OTA 固件接收需持续数十秒。如果此时用户按键或屏幕误触：")
    add_body_p(doc, "1. 优先级为 2 的 UI 任务被唤醒，去重绘界面、读取字体、刷新屏幕，严重抢占 CPU 算力，导致低优先级的 USART2 DMA 接收缓冲区来不及腾出空间而发生 FIFO 溢出丢包。")
    add_body_p(doc, "2. UI 渲染与固件烧录同时争抢内部 Flash 和系统总线，极易引发总线死锁冲突。")
    add_body_p(doc, "【设计实现】：在 Task_UI 按键分发函数最顶层检查 if (OTA_GetState() == OTA_STATE_RECEIVING)，只要处于升级接收态，按键事件被直接丢弃拦截并打印警告日志，确保 OTA 期间 CPU 和总线带宽 100% 独占。")

    # Q11 & Q12
    add_styled_heading(doc, "Q11 & Q12：如果 OTA 标志位在变化时设备掉电，或者在擦除标志位中断电，会变砖吗？", 2)
    add_body_p(doc, "【核心结论】：绝对不会变砖！有三重铜墙铁壁保障：")
    add_body_p(doc, "1. 在我们工程中，标志位写在 RTC 备份寄存器（RTC_BKP_DR2）中，物理本质是低功耗 SRAM，单周期汇编指令 STR 纳秒级写入，根本不需要执行慢速的“扇区擦除”，天然没有擦除中途断电的物理时间窗口。")
    add_body_p(doc, "2. 哪怕在其他工程中标志位存放在 Flash，擦除中途断电导致数据变成 0xFFFFFFFF 或半擦除乱码：Bootloader 判定条件是严格匹配魔数 if (upgrade_flag == 0x5A5A5A5A)。只要不是该魔数，Bootloader 判定为普通开机，直接跳过擦写代码，安全跳转执行原旧 APP。旧 APP 毫发无损！")
    add_body_p(doc, "3. Bootloader 部署在只读隔离扇区（Sector 0~1，0x08000000），任何升级流程都不会擦写 Bootloader，引导程序永远常驻，断电后依然能自检恢复。")

    # Q13 & Q14
    add_styled_heading(doc, "Q13 & Q14：RTC 备份寄存器是 32 位吗？与 STM32F1 相比有什么区别？", 2)
    add_body_p(doc, "【32 位确认】：STM32F407 的 RTC 备份寄存器（RTC_BKP_DR0 ~ DR19 共 20 个）是原生的 32 位（uint32_t），共提供 80 字节存储区。")
    add_body_p(doc, "【与 F1 架构差异】：老款 STM32F1 的 BKP 备份寄存器只有 16 位（uint16_t），存一个 32 位整数必须拆成高低两个寄存器；而 STM32F4 原生支持 32 位读写（RTC_ReadBackupRegister / RTC_WriteBackupRegister），单条指令即可存入一个完整的 4 字节魔数或步数。")

    # Q15
    add_styled_heading(doc, "Q15：手表的步数存在哪里？备份域掉电会丢失吗？两级存储是如何设计的？", 2)
    add_body_p(doc, "【备份域掉电特性】：")
    add_body_p(doc, "1. 主电源 VDD 断开后，芯片内部电源选择器在纳秒级自动无缝切换到 VBAT 引脚。只要有纽扣电池供电（电流仅 1~2uA），备份域数据永远不丢（可维持 3~5 年）。")
    add_body_p(doc, "2. 若连纽扣电池也拔除（彻底全断电），备份域数据会清零。")
    add_body_p(doc, "【两级持久化容灾架构（L1 + L2）】：")
    add_body_p(doc, "为了防止纽扣电池耗尽导致步数丢失，同时避免频繁擦写 Flash 损坏芯片，我们在 param_storage.c 中设计了两级架构：")
    add_body_p(doc, "L1 缓存层（RTC 备份域）：负责高频变动的步数记录，随走随存，耗时 <0.1ms，写入寿命无限。")
    add_body_p(doc, "L2 固化层（内部 Flash 扇区 11 磨损均衡）：划分 5400 个 24 字节槽位（Slot），低频追加写入（遇到 0xFFFFFFFF 空白槽位直接写，无需擦除）。单扇区擦写寿命从 10 万次直接倍增至 5400 万次！")
    add_body_p(doc, "开机自愈机制：开机先读 RTC 备份域；若读出无效（彻底断电），系统自动反向扫描 Flash 5400 个槽位，定位最后一个带合法校验和的数据块，完整恢复历史步数！")

    # =========================================================================
    # 第三篇：动态低功耗与 FreeRTOS 内存管理
    # =========================================================================
    add_styled_heading(doc, "第三篇：动态低功耗与 FreeRTOS 内存管理", 1)

    # Q16
    add_styled_heading(doc, "Q16：三级电源状态机是如何设计的？怎么把整机电流从 115mA 降到 28mA？", 2)
    add_body_p(doc, "【三级状态机划分】：")
    add_body_p(doc, "1. 运行态（Active）：LCD 背光全开，LVGL 维持 1ms 高频轮询与 100ms 动效流水线，保证 60 FPS 极速交互。整机功耗约 115 mA。")
    add_body_p(doc, "2. 待机态（Standby）：15 秒无操作超时自动触发。硬件切断 LCD 背光回路（直接削减 60mA）；软件动态挂起 LVGL 渲染流水线，UI 任务切换为 50ms 慢轮询，CPU 占用率从 30% 暴跌至 <0.5%。整机功耗立降至 28 mA（暴降 75.6%），同时后台网络和抬腕算法依然保活。")
    add_body_p(doc, "3. 休眠态（Sleep）：所有任务处于延时或阻塞状态时，FreeRTOS 空闲任务钩子执行 __WFI() 指令。Cortex-M4 硬件立即门控暂停 CPU 时钟取指。任意中断（SysTick、按键、DMA）在 12 个时钟周期内零延迟瞬时唤醒。")

    # Q17
    add_styled_heading(doc, "Q17：STM32 官方有几种低功耗模式？区别是什么？为什么选 Sleep 而不选 Stop？", 2)
    
    # Table for Q17
    tbl17 = doc.add_table(rows=4, cols=6)
    tbl17.alignment = WD_TABLE_ALIGNMENT.CENTER
    tbl17.autofit = False
    widths17 = [Inches(1.0), Inches(0.9), Inches(1.1), Inches(1.0), Inches(1.2), Inches(1.3)]
    format_table_header(tbl17.rows[0], widths17, ["低功耗模式", "CPU状态", "时钟与外设", "SRAM数据", "典型功耗", "唤醒后表现"])
    format_table_row(tbl17.rows[1], widths17, ["睡眠 (Sleep)", "停止门控", "全部正常开启", "完全保留", "mA 级", "零延迟，从原代码行继续执行"])
    format_table_row(tbl17.rows[2], widths17, ["停止 (Stop)", "停止", "1.2V时钟全关", "完全保留", "几十~几百 uA", "时钟退回HSI，需重配主频后继续执行"], is_even=True)
    format_table_row(tbl17.rows[3], widths17, ["待机 (Standby)", "断电", "全部关闭", "全部丢失", "2~3 uA", "等同于硬件复位重启，从 main 重新跑"])

    add_callout_box(doc, "Q17 面试官追问防守", 
        "“为什么不用 Stop 模式？”\n"
        "“因为进入 Stop 模式会关闭主 PLL 时钟，这会导致 FreeRTOS 的 SysTick 滴答定时器停止，系统时间基准被破坏，外部 ESP32 WiFi 异步 TCP 通信被打断，唤醒还需要耗费时间重置时钟树。\n"
        "而‘息屏切背光 + LVGL 挂起 + WFI 休眠’方案，既保留了微秒级即时响应和网络长连接心跳，又将电流压减了 75.6%，是多任务穿戴设备的最优工程权衡。”", 
        "answer")

    # Q18
    add_styled_heading(doc, "Q18：待机模式（Standby）和停止模式（Stop）分别在什么工业场景下使用？", 2)
    add_body_p(doc, "【停止模式（Stop）】：适用于需要保留变量与现场、频繁被外部事件唤醒的设备。如智能门锁（按键/刷卡瞬间唤醒开锁，内存中状态机不丢失）、骑行码表（停车休眠，车轮转动触发脉冲立即唤醒继续记里程）、低功耗传感节点。")
    add_body_p(doc, "【待机模式（Standby）】：适用于长周期休眠、醒来可以接受复位重新初始化的场景。如电子墨水屏价签（每天仅唤醒一次刷新，平日 2uA 待机，纽扣电池用 5 年）、手持设备软关机（长按开机键触发 WKUP 引脚复位开机）、野外资产追踪器。")

    # Q19
    add_styled_heading(doc, "Q19：什么是 FreeRTOS 内存钩子（cJSON_Hooks）？为什么能杜绝物模型解析内存碎片化？", 2)
    add_body_p(doc, "【痛点】：默认 cJSON 使用标准库的 malloc() 和 free()。在 FreeRTOS 多任务环境下，标准库 malloc 既非线程安全（并发容易踩坏指针死机），又不支持微小空闲内存块合并，在频繁解析 JSON 物模型时产生海量碎片，导致堆枯竭。")
    add_body_p(doc, "【挂接钩子】：在 esp32_wifi.c 初始化时，调用 cJSON_InitHooks(&hooks)，将 malloc_fn 指向 pvPortMalloc，free_fn 指向 vPortFree。")
    add_body_p(doc, "【核心效果】：")
    add_body_p(doc, "1. 统一接入 FreeRTOS 的 heap_4.c 内存管理方案。heap_4 自带相邻空闲块自动合并（Block Coalescing）机制，JSON 树销毁后零碎小块立刻缝合成大内存块，从根本上杜绝碎片化。")
    add_body_p(doc, "2. 原生线程安全：pvPortMalloc 自带调度锁保护，支持多任务并发解析。")
    add_body_p(doc, "3. 可通过 xPortGetFreeHeapSize() 统一监控动态内存水位，杜绝内存泄漏。")

    # =========================================================================
    # 第四篇：传感器滤波算法与多维时空窗手势识别
    # =========================================================================
    add_styled_heading(doc, "第四篇：传感器滤波算法与多维时空窗手势识别", 1)

    # Q20
    add_styled_heading(doc, "Q20：一阶互补滤波的原理与公式是什么？为什么不用扩展卡尔曼滤波？", 2)
    add_body_p(doc, "【物理互补特性】：陀螺仪高频动态响应极好，但存在低频零偏积分漂移；加速度计通过测量地球重力低频绝对基准极准，但高频震动噪声极大。两者在频域完美互补。")
    add_body_p(doc, "【公式实现】：Angle = 0.96 * (Angle_last + Gyro * dt) + 0.04 * Accel_Angle。")
    add_body_p(doc, "96% 的权重给予陀螺仪积分，保障了手势与姿态的丝滑跟手；4% 的权重给予加速度计倾角，持续将长期温漂拉回重力垂线。")
    add_body_p(doc, "【对比卡尔曼】：卡尔曼滤波涉及多阶矩阵协方差更新与求逆运算，占用宝贵的浮点算力与电池能耗；一阶互补滤波仅需两次浮点乘加，耗时 <1us，能达到卡尔曼滤波 95% 以上的效果，性价比极高。")

    # Q21
    add_styled_heading(doc, "Q21：抬腕亮屏与波峰计步的“多维时空窗算法”是如何防止误判的？", 2)
    add_body_p(doc, "【抬腕亮屏 4 重特征漏斗】：")
    add_body_p(doc, "1. 时域启动特征：翻腕角速度合矢量突变（|gx|+|gy| > 2200），标定手腕开始翻转动作。")
    add_body_p(doc, "2. 时间窗约束（150ms ~ 900ms）：过滤过快（拍桌、甩手）或过慢（推门）的非看表动作。")
    add_body_p(doc, "3. 空间几何视线锥：要求欧拉角处于人眼舒适区（Pitch 处于 -35°~40°，Roll 处于 -40°~40°）。")
    add_body_p(doc, "4. 凝视稳定检测与防抖冷却：角速度衰减变小且在视线区稳定保持 3 个周期（约 60ms）才点亮屏幕，唤醒后加入 2 秒防抖冷却期，彻底消除误触发。")
    add_body_p(doc, "【波峰计步算法】：")
    add_body_p(doc, "1. 动态自适应去除重力：计算三轴合加速度并用滑动低通提取重力分量，相减得到人体运动纯动态加速度。")
    add_body_p(doc, "2. 迟滞双阈值波峰检测：设置 0.20g 波峰阈值与 0.08g 复位阈值，只有波谷回弹后才允许计下一步，杜绝临界点抖动误计。")
    add_body_p(doc, "3. 300ms 生理步频时间窗：人类极限步频不超过每秒 3.5 步，300ms 以内的高频震动（坐车颠簸、敲击键盘）全部被硬件级滤除。")

    out_path = r"C:\Users\LENOVO\Desktop\智能手表项目面试全攻略-已提问核心答辩精编.docx"
    doc.save(out_path)
    print(f"[SUCCESS] Document 1 generated at: {out_path}")

if __name__ == "__main__":
    build_doc1()
