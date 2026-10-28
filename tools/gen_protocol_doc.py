# -*- coding: utf-8 -*-
import docx
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
import os

doc = docx.Document()

# 页面边距设置 (标准页边距)
for s in doc.sections:
    s.top_margin = Inches(0.8)
    s.bottom_margin = Inches(0.8)
    s.left_margin = Inches(0.8)
    s.right_margin = Inches(0.8)

def set_cell_background(cell, fill_hex):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement('w:shd')
    shd.set(qn('w:val'), 'clear')
    shd.set(qn('w:color'), 'auto')
    shd.set(qn('w:fill'), fill_hex)
    tcPr.append(shd)

# ----------------- 标题与文档头 -----------------
title = doc.add_paragraph()
title.alignment = WD_ALIGN_PARAGRAPH.CENTER
run_title = title.add_run("GD32F4 工业现场测控系统\nRS-485 通信协议与帧格式规范")
run_title.font.name = "微软雅黑"
run_title.font.size = Pt(20)
run_title.font.bold = True
run_title.font.color.rgb = RGBColor(31, 78, 121)

sub = doc.add_paragraph()
sub.alignment = WD_ALIGN_PARAGRAPH.CENTER
run_sub = sub.add_run("基于高可靠对称定界、CRC16-Modbus 校验与 OTA 分包协议 V2.0")
run_sub.font.name = "微软雅黑"
run_sub.font.size = Pt(11)
run_sub.font.color.rgb = RGBColor(89, 89, 89)

doc.add_paragraph().paragraph_format.space_after = Pt(10)

# ----------------- 1. 概述与物理层规范 -----------------
h1 = doc.add_heading("1. 协议概述与物理层规范", level=1)
h1.style.font.name = "微软雅黑"
h1.style.font.color.rgb = RGBColor(31, 78, 121)

p1 = doc.add_paragraph()
p1.paragraph_format.line_spacing = 1.2
p1.add_run("本通信协议专为搭载 ").font.name = "宋体"
r = p1.add_run("GD32F470")
r.bold = True
p1.add_run(" 的高可靠工业现场测控终端定制，兼顾常规传感器精密采集（12位 ADC/DAC 模拟量）、现场 OLED 交互、参数自校准以及远程安全固件 OTA 在线升级等严苛工业场景。").font.name = "宋体"

table_phy = doc.add_table(rows=6, cols=3)
table_phy.alignment = WD_TABLE_ALIGNMENT.CENTER
headers = ["物理层参数", "配置值", "技术说明与工业考量"]
phy_data = [
    ["通信总线与工作模式", "RS-485 差分半双工 (Half-Duplex)", "单对双绞线差分传输，具备优异的共模抑制与抗工业电磁干扰能力"],
    ["单片机引脚与控制", "PA2 (TX), PA3 (RX), PA1 (DE/RE)", "DE 与 RE 硬件短接，单片机仅用 PA1 单引脚实现纳秒级收发流控"],
    ["缺省通信波特率", "115200 bps (支持 4800~115200)", "长线通信速率与信号完整性黄金平衡点；支持 Flash 掉电持久化配置"],
    ["数据位 / 停止位 / 校验", "8 位数据，1 位停止位，无校验 (8N1)", "由底层传输层提供字节流，整包完整性由协议层 CRC16 全包覆盖"],
    ["帧定界与断包机制", "串口 IDLE 空闲中断 (硬件断帧)", "检测总线空闲时间 > 1 字符帧瞬间触发，零 CPU 轮询，杜绝数据粘包"]
]

for col_idx, text in enumerate(headers):
    cell = table_phy.cell(0, col_idx)
    set_cell_background(cell, "1F4E79")
    run = cell.paragraphs[0].add_run(text)
    run.font.name = "微软雅黑"
    run.font.bold = True
    run.font.color.rgb = RGBColor(255, 255, 255)
    run.font.size = Pt(10)

for row_idx, row in enumerate(phy_data):
    for col_idx, text in enumerate(row):
        cell = table_phy.cell(row_idx + 1, col_idx)
        if row_idx % 2 == 1:
            set_cell_background(cell, "F2F5F8")
        run = cell.paragraphs[0].add_run(text)
        run.font.name = "宋体"
        run.font.size = Pt(9.5)

doc.add_paragraph().paragraph_format.space_after = Pt(10)

# ----------------- 2. 数据帧物理结构定义 -----------------
h2 = doc.add_heading("2. 数据帧物理结构规范 (Frame_t)", level=1)
h2.style.font.name = "微软雅黑"
h2.style.font.color.rgb = RGBColor(31, 78, 121)

p2 = doc.add_paragraph()
p2.paragraph_format.line_spacing = 1.2
p2.add_run("本协议采用“对称镜像头尾 + 变长数据载荷 + CRC16-Modbus 全包校验”架构。整帧长度为 ").font.name = "宋体"
r2 = p2.add_run("(13 + N) 字节")
r2.bold = True
p2.add_run("（其中 N 为有效载荷长度，最大支持 64 字节）：").font.name = "宋体"

table_frame = doc.add_table(rows=10, cols=5)
table_frame.alignment = WD_TABLE_ALIGNMENT.CENTER
frame_headers = ["字节偏移", "字段标识", "占用长度", "固定值 / 范围", "功能与设计考量"]
frame_data = [
    ["Byte 0~1", "head (帧头)", "2 字节 (uint16)", "0xA5B6", "起始魔数定界。总线电磁干扰下滑动窗口精确锁定帧首"],
    ["Byte 2~3", "dev_id (设备ID)", "2 字节 (uint16)", "0x0000~0xFFFF", "485 多机总线从机站号地址，0xFFFF 为全网广播寻址"],
    ["Byte 4", "frame_type (帧类型)", "1 字节 (uint8)", "0x01, 0x02, 0x05, 0xFF", "报文语义分类：命令帧、应答帧、周期心跳帧、异常报错帧"],
    ["Byte 5~6", "cmd (命令码)", "2 字节 (uint16)", "见命令字典表", "高字节区分业务大类 (0x01系统/0x02采控/0x05升级等)"],
    ["Byte 7", "len (载荷长度)", "1 字节 (uint8)", "0 ~ 64 (0x00~0x40)", "指示后续有效载荷 content 的实际字节数 N"],
    ["Byte 8", "ver (协议版本)", "1 字节 (uint8)", "0x02", "当前协议规范版本号 (V2.0)，防版本兼容性错乱"],
    ["Byte 9~(9+N-1)", "content (有效载荷)", "N 字节", "变长业务数据", "具体浮点电压、DAC输出值、校准参数或 64B OTA 固件块"],
    ["Byte (9+N)~(10+N)", "crc (校验码)", "2 字节 (uint16)", "动态计算 (大端)", "CRC16-Modbus 循环冗余校验，覆盖从 head 至 content 末尾"],
    ["Byte (11+N)~(12+N)", "tail (帧尾)", "2 字节 (uint16)", "0xB6A5", "结束魔数定界。与帧首形成高低字节镜像对称，物理防截断"]
]

for col_idx, text in enumerate(frame_headers):
    cell = table_frame.cell(0, col_idx)
    set_cell_background(cell, "1F4E79")
    run = cell.paragraphs[0].add_run(text)
    run.font.name = "微软雅黑"
    run.font.bold = True
    run.font.color.rgb = RGBColor(255, 255, 255)
    run.font.size = Pt(10)

for row_idx, row in enumerate(frame_data):
    for col_idx, text in enumerate(row):
        cell = table_frame.cell(row_idx + 1, col_idx)
        if row_idx % 2 == 1:
            set_cell_background(cell, "F2F5F8")
        run = cell.paragraphs[0].add_run(text)
        run.font.name = "宋体"
        run.font.size = Pt(9.5)

doc.add_paragraph().paragraph_format.space_after = Pt(10)

# ----------------- 3. 报文类型与 CRC16 算法 -----------------
h3 = doc.add_heading("3. 帧类型定义与 CRC16-Modbus 算法", level=1)
h3.style.font.name = "微软雅黑"
h3.style.font.color.rgb = RGBColor(31, 78, 121)

p3 = doc.add_paragraph()
p3.add_run("3.1 帧类型 (frame_type) 定义：").bold = True
types_p = doc.add_paragraph()
types_p.add_run("• 0x01 (FT_CMD) ：上位机主动下发的控制或查询命令帧\n"
              "• 0x02 (FT_RESP)：从机正常执行完成后的应答数据帧\n"
              "• 0x05 (FT_HEARTBEAT)：从机周期性向总线主动上报的运行健康心跳帧\n"
              "• 0xFF (FT_ERROR)：参数越界、CRC校验错、执行失败时的异常响应帧")

p_crc = doc.add_paragraph()
p_crc.add_run("3.2 CRC16-Modbus 校验算法标准：").bold = True
p_crc_desc = doc.add_paragraph()
p_crc_desc.add_run("算法多项式为 0xA001（逆序生成多项式 X^16 + X^15 + X^2 + 1），初值为 0xFFFF。校验计算范围严格覆盖从 Byte 0 (head) 起至有效载荷 (content) 末尾的全部字节。源码实现如下：")

code_box = doc.add_paragraph()
code_box.paragraph_format.left_indent = Inches(0.2)
code_run = code_box.add_run(
    "uint16_t crc16_modbus(const uint8_t* data, uint16_t len)\n"
    "{\n"
    "    uint16_t crc = 0xFFFF;\n"
    "    for (uint16_t i = 0; i < len; i++) {\n"
    "        crc ^= data[i];\n"
    "        for (uint8_t j = 0; j < 8; j++) {\n"
    "            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;\n"
    "            else              crc >>= 1;\n"
    "        }\n"
    "    }\n"
    "    return crc;\n"
    "}"
)
code_run.font.name = "Consolas"
code_run.font.size = Pt(8.5)
code_run.font.color.rgb = RGBColor(38, 38, 38)

doc.add_paragraph().paragraph_format.space_after = Pt(10)

# ----------------- 4. 完整命令功能码字典 -----------------
h4 = doc.add_heading("4. 完整业务命令码字典 (Command Codes)", level=1)
h4.style.font.name = "微软雅黑"
h4.style.font.color.rgb = RGBColor(31, 78, 121)

table_cmd = doc.add_table(rows=22, cols=4)
table_cmd.alignment = WD_TABLE_ALIGNMENT.CENTER
cmd_headers = ["业务大类", "命令码 (HEX)", "功能标识 (宏定义)", "详细业务动作与参数说明"]
cmd_data = [
    ["系统管理", "0x0101", "CMD_REBOOT", "软复位重启 MCU，复位前自动将运行参数固化至 Flash"],
    ["系统管理", "0x0104", "CMD_QUERY_VERSION", "查询固件版本信息 (返回 App/Bootloader 主次版本号)"],
    ["系统管理", "0x0105", "CMD_SET_TIME", "校准系统时间，载荷带 4 字节 Unix Epoch 秒级时间戳"],
    ["系统管理", "0x0106", "CMD_QUERY_TIME", "查询系统 RTC 当前时间戳"],
    ["系统管理", "0x01A1", "CMD_SET_ID", "修改设备 485 站号 ID，立即持久化保存至参数区"],
    ["系统管理", "0x01A2", "CMD_SET_BAUD", "修改串口波特率 (115200/19200/9600 等)，重启不丢失"],
    ["数据采控", "0x0201", "CMD_QUERY_CH0", "查询通道 0 (12位 ADC) 采样电压，返回 IEEE-754 浮点数"],
    ["数据采控", "0x0202", "CMD_QUERY_CH1", "查询通道 1 (12位 ADC) 采样电压，返回 IEEE-754 浮点数"],
    ["数据采控", "0x0221", "CMD_QUERY_CH2", "查询通道 2 (12位 ADC) 采样电压，返回 IEEE-754 浮点数"],
    ["数据采控", "0x0241", "CMD_SET_CH0_RATIO", "设定通道 0 传感器变比校准系数 (两点校准斜率)"],
    ["数据采控", "0x0242", "CMD_SET_CH1_RATIO", "设定通道 1 传感器变比校准系数 (两点校准斜率)"],
    ["数据采控", "0x0261", "CMD_SET_REPORT_INTERVAL", "设定遥测数据定时自动上报周期 (秒级/毫秒级)"],
    ["模拟输出", "0x0301", "CMD_SET_DAC", "设定 12 位 DAC 输出电压 (0~4095，对应 0~3.3V)"],
    ["自动上报", "0x0302", "CMD_AUTO_REPORT_START", "启动 3 通道采集数据自动轮询广播上报"],
    ["自动上报", "0x0303", "CMD_AUTO_REPORT_STOP", "停止数据主动上报，切回问答从机轮询模式"],
    ["门限告警", "0x0400", "CMD_QUERY_THRESH_ALL", "批量查询 3 通道独立的超限阈值配置"],
    ["门限告警", "0x0411", "CMD_SET_THRESH_CH0", "设定通道 0 上限报警阈值，超限联动 DAC 保护"],
    ["在线升级", "0x0501", "CMD_UPGRADE_REQ", "OTA 升级请求握手 (携带新固件总包长与全包 CRC32)"],
    ["在线升级", "0x0502", "CMD_UPGRADE_PREPARE", "准备固件写入，擦除片内 128KB 下载暂存区 (Sector)"],
    ["在线升级", "0x0503", "CMD_UPGRADE_EXEC", "下发固件分包 (每包 64 字节，带分包序号与局部校验)"],
    ["运行心跳", "0x8888", "CMD_HEARTBEAT", "周期性主动心跳报文，携带运行状态码与最新报警计数"]
]

for col_idx, text in enumerate(cmd_headers):
    cell = table_cmd.cell(0, col_idx)
    set_cell_background(cell, "1F4E79")
    run = cell.paragraphs[0].add_run(text)
    run.font.name = "微软雅黑"
    run.font.bold = True
    run.font.color.rgb = RGBColor(255, 255, 255)
    run.font.size = Pt(10)

for row_idx, row in enumerate(cmd_data):
    for col_idx, text in enumerate(row):
        cell = table_cmd.cell(row_idx + 1, col_idx)
        if row_idx % 2 == 1:
            set_cell_background(cell, "F2F5F8")
        run = cell.paragraphs[0].add_run(text)
        run.font.name = "宋体"
        run.font.size = Pt(9)

doc.add_paragraph().paragraph_format.space_after = Pt(10)

# ----------------- 5. 典型报文收发实战示例 -----------------
h5 = doc.add_heading("5. 典型通信报文收发实战示例 (Hex 逐字节解析)", level=1)
h5.style.font.name = "微软雅黑"
h5.style.font.color.rgb = RGBColor(31, 78, 121)

p5 = doc.add_paragraph()
p5.paragraph_format.line_spacing = 1.2

p5.add_run("【示例 1：上位机查询从机 (ID: 0xBB1C) 通道 0 模拟量采样值】\n").bold = True
p5.add_run("• 上位机下发查询命令帧 (无载荷 N=0, 总长 13 字节)：\n"
           "  A5 B6   BB 1C   01   02 01   00   02   8C 4D   B6 A5\n"
           "  [帧头]  [设备] [CMD] [查CH0] [长0] [V2] [CRC16] [帧尾]\n\n"
           "• 从机应答帧 (当前采样电压 3.30V，IEEE-754 浮点 HEX 为 0x40533333, N=4, 总长 17 字节)：\n"
           "  A5 B6   BB 1C   02   02 01   04   02   40 53 33 33   E2 1F   B6 A5\n"
           "  [帧头]  [设备] [RESP][查CH0] [长4] [V2]  [3.30V浮点]  [CRC16] [帧尾]\n\n")

p5.add_run("【示例 2：上位机下发 DAC 目标输出电压 (2048 对应中位 1.65V)】\n").bold = True
p5.add_run("• 下发控制帧 (载荷 2 字节 uint16: 0x0800 = 2048, N=2, 总长 15 字节)：\n"
           "  A5 B6   BB 1C   01   03 01   02   02   08 00   7A 32   B6 A5\n"
           "  [帧头]  [设备] [CMD] [设DAC] [长2] [V2] [2048]  [CRC16] [帧尾]\n\n"
           "• 从机应答确认成功帧 (执行 OK，返回相同参数回显)：\n"
           "  A5 B6   BB 1C   02   03 01   02   02   08 00   6F 11   B6 A5\n\n")

p5.add_run("【示例 3：从机 1 秒定时广播运行心跳帧】\n").bold = True
p5.add_run("• 主动心跳帧 (cmd: 0x8888, 载荷带运行秒数与报警计数)：\n"
           "  A5 B6   BB 1C   05   88 88   04   02   00 00 00 00   5D 8C   B6 A5\n"
           "  [帧头]  [设备] [心跳][HEART] [长4] [V2] [系统状态0]  [CRC16] [帧尾]\n")

doc.add_paragraph().paragraph_format.space_after = Pt(10)

# ----------------- 6. 工业底层驱动保障设计 -----------------
h6 = doc.add_heading("6. 底层驱动时序与抗粘包/断包核心设计", level=1)
h6.style.font.name = "微软雅黑"
h6.style.font.color.rgb = RGBColor(31, 78, 121)

p6 = doc.add_paragraph()
p6.paragraph_format.line_spacing = 1.3
p6.add_run("1. 串口 IDLE 空闲中断零拷贝断帧：\n").bold = True
p6.add_run("   由于 RS-485 现场通信数据包长度不一，传统逐字节扫描或固定长度接收会导致粘包或缓冲区溢出。本系统依托 GD32F4 硬件总线空闲检测机制，当检测到 RX 差分总线维持高电平超过整整 1 个字符时间（115200 波特率下约 87 微秒）即瞬间拉起 IDLE 中断，单片机在 ISR 中即时切出完整报文包，彻底消灭粘包。\n\n")

p6.add_run("2. TBE + TC 双中断纳秒级流控切换 (PA1 控制 DE/RE)：\n").bold = True
p6.add_run("   RS-485 是典型半双工总线。若在发送完最后一个字节后仅凭 TBE（发送缓冲区空）就切回接收模式，会导致移位寄存器中最后一位停止位被物理腰斩；若使用纯软件 delay 则会错过从机的快速应答。本系统在中间字节发送时利用 TBE 满速流水线填数，最后一个字节发出后使能 TC（传输彻底完成）中断，在电平真正离开引脚的瞬间将 PA1 拉低切回接收态，实现绝不吞字节且零延迟切流控。\n\n")

p6.add_run("3. 软件对称双定界安全防御：\n").bold = True
p6.add_run("   工业现场接触器打火、大电机启动常伴随电磁脉冲干扰。协议栈不仅依赖硬件 IDLE 中断，还在应用层软件状态机中严格核验帧首 (0xA5B6) 与帧尾 (0xB6A5) 的镜像对称性。任何因偶发噪声导致的畸变帧、错位帧均在 0 拷贝阶段直接被物理识别并抛弃，保障工业生产现场通信的极端稳健。")

# ----------------- 7. 波特率自适应协商与持久化 -----------------
h7 = doc.add_heading("7. 波特率自适应协商、平滑热切换与 Flash 重启持久化", level=1)
h7.style.font.name = "微软雅黑"
h7.style.font.color.rgb = RGBColor(31, 78, 121)

p7 = doc.add_paragraph()
p7.paragraph_format.line_spacing = 1.3
p7.add_run("1. 工业现场长线与高带宽的工程痛点：\n").bold = True
p7.add_run("   在工业现场，RS-485 经常面临双绞线长达数百米、电磁环境恶劣的工况。设备默认通常运行在保守抗干扰的低波特率（如 19200 或 9600 bps）以死保通信连通性；然而，当需要进行远程固件 OTA 升级或大批量遥测历史数据回读时，低波特率会导致传输耗时长达数分钟。为此，本系统设计了一套“动态指令协商切换 + 平滑热重载 + 掉电持久化”的波特率自适应闭环机制。\n\n")

p7.add_run("2. 四步原子切换时序设计（杜绝通信死锁）：\n").bold = True
p7.add_run("   波特率切换最常见的工程致命 Bug 是：单片机刚收到指令就立刻修改寄存器，导致上位机收不到应答帧而陷入“两端波特率不匹配”的永久断联。本系统在 Function.c 中实现了严格的四步原子切换：\n"
           "   • 第 1 步 (合法性检查与 Flash 固化)：接收 CMD_SET_BAUD (0x01A2) 命令，核验目标编码（4800/9600/19200/115200），调用 DataChannel_SaveParam() 立即写入内部 Flash 参数区持久化；\n"
           "   • 第 2 步 (原波特率应答确认)：在硬件寄存器变更前，依然使用“当前原波特率”向主机发出 FT_RESP 应答帧，通知上位机“从机已就绪，请同步切波特率”；\n"
           "   • 第 3 步 (物理发空等待)：调用 while(send_busy); delay_1ms(20); 确保应答帧最后一位停止位彻底脱离 RS-485 差分引脚；\n"
           "   • 第 4 步 (硬件分频器平滑热重载)：调用 usart_set_baudrate() 在纳秒级重载 USART 硬件波特率分频寄存器，实现零重启平滑切频。\n\n")

p7.add_run("3. 开机断电自愈恢复机制：\n").bold = True
p7.add_run("   在系统每次上电复位进入 UsrFunction() 初始化流程时，单片机首先从 Flash 参数区读出上次保存的波特率编码并自动生效。即使野外现场发生突发停电，设备重新通电后依然保持与工控上位机一致的波特率，彻底免去现场人工开盖拨码配置。")

out_path = os.path.join(os.path.expanduser("~"), "Desktop", "GD32F4_RS485_Protocol_Specification_V2.docx")
doc.save(out_path)
print("[+] Document successfully created at:", out_path)

