#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
==============================================================================
  CmBacktrace 故障堆栈解析与在线诊断工具 (STM32F407 SmartWatch)
==============================================================================
功能:
  1. 远程故障注入测试: 向手表发送 {"cmd":"test_fault"} 验证死机自动捕获与堆栈回溯
  2. 自动地址反查 (addr2line): 传入串口打印的 PC 地址，自动解析出源码文件名、函数名与精确行号
==============================================================================
"""

import sys
import os
import time
import socket
import subprocess
import re
import argparse

AXF_PATH = os.path.join(os.path.dirname(__file__), "..", "Project", "Objects", "STM32F429.axf")
ADDR2LINE_DEFAULT = r"D:\Enviorment\DevEnv\GNU-tools-for-STM32\bin\arm-none-eabi-addr2line.exe"

def extract_hex_addresses(text):
    """从任意文本或串口日志中智能提取所有 8 位十六进制地址 (如 00000000, 08021c84)"""
    tokens = re.findall(r'\b(?:0x)?([0-9a-fA-F]{8})\b', text)
    # 过滤出合法的 Flash / NULL 地址 (以 00 或 08 开头)
    valid = [t if t.startswith("0x") else f"0x{t}" for t in tokens if t.startswith(("00", "08"))]
    return valid

def resolve_addresses(addresses, axf_file=AXF_PATH, addr2line_exe=ADDR2LINE_DEFAULT):
    if not os.path.exists(axf_file):
        print(f"[Error] Target ELF/AXF file not found: {axf_file}")
        return

    # Check addr2line executable
    if not os.path.exists(addr2line_exe):
        # Fallback to PATH
        addr2line_exe = "arm-none-eabi-addr2line"

    print("\n" + "=" * 65)
    print("      CmBacktrace 崩溃堆栈自动反查 (Address -> Source Line)      ")
    print("=" * 65)
    print(f"[*] 符号表文件: {os.path.abspath(axf_file)}")
    print(f"[*] 解析工具:   {addr2line_exe}\n")

    cmd = [addr2line_exe, "-e", axf_file, "-a", "-f", "-C"] + addresses
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        lines = proc.stdout.strip().splitlines()

        # Output format of addr2line -a -f:
        # Address
        # Function
        # File:Line
        i = 0
        depth = 0
        while i < len(lines):
            addr = lines[i].strip() if i < len(lines) else "??"
            func = lines[i+1].strip() if i+1 < len(lines) else "??"
            loc  = lines[i+2].strip() if i+2 < len(lines) else "??"
            i += 3
            print(f"  #{depth:02d} 地址: {addr}")
            print(f"      函数: {func}()")
            print(f"      位置: {loc}")
            print("  " + "-" * 55)
            depth += 1

    except Exception as e:
        print(f"[Error] Failed to execute addr2line: {e}")
    print("=" * 65 + "\n")

def trigger_remote_fault(ip, port=8080):
    print(f"[*] Connecting to SmartWatch TCP Server at {ip}:{port}...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5.0)
        s.connect((ip, port))
        cmd = '{"cmd":"test_fault"}\r\n'
        print(f"[*] Sending intentional fault trigger command: {cmd.strip()}")
        s.sendall(cmd.encode("utf-8"))
        print("[+] Trigger packet sent! Waiting for MCU HardFault trigger...")
        time.sleep(1.0)
        s.close()
    except Exception as e:
        print(f"[-] Failed to send trigger: {e}")

def main():
    parser = argparse.ArgumentParser(description="CmBacktrace 故障诊断与堆栈解析工具")
    parser.add_argument("--ip", type=str, help="手表 IP 地址 (用于远程触发故障测试)")
    parser.add_argument("--port", type=int, default=8080, help="手表 TCP 端口 (默认 8080)")
    parser.add_argument("--trigger", action="store_true", help="触发远程非法内存访问故障")
    parser.add_argument("--addr", nargs="*", help="需要反查的十六进制 PC 地址列表，或直接粘贴整行串口日志")

    # 0. 极致偷懒模式：如果命令行带了任何文本（无论是否带 --addr，或者直接粘贴了串口整行）
    all_cli_text = " ".join(sys.argv[1:])
    if not any(arg in sys.argv for arg in ["--ip", "--trigger"]):
        cli_addrs = extract_hex_addresses(all_cli_text)
        if cli_addrs:
            resolve_addresses(cli_addrs)
            return

    args, unknown = parser.parse_known_args()

    if args.trigger:
        if not args.ip:
            print("[Error] --ip argument is required when using --trigger.")
            sys.exit(1)
        trigger_remote_fault(args.ip, args.port)
        return

    # 1. 命令行传递了 --addr
    if args.addr or unknown:
        raw_text = " ".join(args.addr if args.addr else []) + " " + " ".join(unknown)
        addrs = extract_hex_addresses(raw_text)
        if addrs:
            resolve_addresses(addrs)
            return
        else:
            print("[Error] 未能在输入中提取到有效的 8 位十六进制地址。")
            return

    # 2. 如果没有任何参数，提供极简交互：直接粘贴串口输出即可
    print("=" * 65)
    print("  CmBacktrace 交互式堆栈反查工具 (无需手动挑地址，整行直接贴)")
    print("=" * 65)
    try:
        raw_input = input("[*] 请直接粘贴串口打印的那一行日志（或包含地址的整段文本）:\n> ").strip()
        if raw_input:
            addrs = extract_hex_addresses(raw_input)
            if addrs:
                resolve_addresses(addrs)
            else:
                print("[-] 未在粘贴的内容中找到 08xxxxxx 崩溃地址。")
        else:
            print("[*] 退出工具。")
    except (EOFError, KeyboardInterrupt):
        pass

if __name__ == "__main__":
    main()
