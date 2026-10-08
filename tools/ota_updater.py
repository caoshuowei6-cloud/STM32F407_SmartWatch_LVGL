#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
===============================================================================
  STM32F407 SmartWatch Wireless OTA Push Tool (局域网无线 OTA 升级推送工具)
  双 MCU 架构: PC (TCP Socket) -> ESP32-C3 (8080 Server) -> STM32F407 (USART2 DMA)
===============================================================================
"""

import sys
import os
import time
import socket
import binascii
import argparse

DEFAULT_PORT = 8080
CHUNK_SIZE = 512
TIMEOUT_SECTOR_ERASE = 15.0  # 擦除 3 个 128KB 扇区约需 3~6 秒，留充足余量
TIMEOUT_PER_CHUNK = 5.0      # 单块写入超时


def calc_crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def print_progress(current: int, total: int, prefix="OTA Upload"):
    percent = (current / total) * 100.0
    bar_len = 40
    filled_len = int(bar_len * current // total)
    bar = "=" * filled_len + ("-" * (bar_len - filled_len))
    sys.stdout.write(f"\r{prefix}: [{bar}] {percent:5.1f}% ({current}/{total} B)")
    sys.stdout.flush()
    if current >= total:
        sys.stdout.write("\n")


def send_ota(ip: str, port: int, bin_path: str):
    if not os.path.isfile(bin_path):
        print(f"[ERROR] 固件文件不存在: {bin_path}")
        return False

    with open(bin_path, "rb") as f:
        fw_data = f.read()

    fw_size = len(fw_data)
    fw_crc = calc_crc32(fw_data)

    print(f"=======================================================")
    print(f"  STM32F407 智能手表无线 OTA 固件推送客户端")
    print(f"=======================================================")
    print(f"  目标手表 IP   : {ip}:{port}")
    print(f"  固件文件路径  : {bin_path}")
    print(f"  固件镜像大小  : {fw_size} 字节 ({fw_size / 1024:.1f} KB)")
    print(f"  固件 CRC32    : 0x{fw_crc:08X}")
    print(f"=======================================================\n")

    print(f"[*] 正在连接手表 TCP 8080 服务 ({ip}:{port})...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(8.0)
    try:
        s.connect((ip, port))
    except Exception as e:
        print(f"[ERROR] 连接失败: {e}")
        print("[TIP] 请确认手机热点已连接、电脑与手表在同一局域网，且手表屏幕显示 Wi-Fi 已联网。")
        return False

    print("[+] 连接成功！发送 OTA 握手指令与擦除请求...")

    # 1. 发送 OTA_START
    start_cmd = f"OTA_START:{fw_size}:{fw_crc:08X}\n".encode("utf-8")
    s.sendall(start_cmd)

    s.settimeout(TIMEOUT_SECTOR_ERASE)
    try:
        resp = s.recv(128).decode("utf-8", errors="ignore").strip()
    except socket.timeout:
        print("[ERROR] 等待暂存区扇区擦除超时！")
        s.close()
        return False

    if "OTA_OK:READY" not in resp:
        print(f"[ERROR] 手表拒绝升级请求，响应: {resp}")
        s.close()
        return False

    print("[+] 暂存区扇区擦除成功，开始分片流式推送固件...")

    # 2. 分片发送数据
    s.settimeout(TIMEOUT_PER_CHUNK)
    offset = 0
    start_time = time.time()

    while offset < fw_size:
        chunk = fw_data[offset : offset + CHUNK_SIZE]
        chunk_len = len(chunk)
        header = f"OTA_DATA:{chunk_len}\n".encode("utf-8")
        packet = header + chunk

        s.sendall(packet)

        # 等待对手表的 ACK
        resp = s.recv(128).decode("utf-8", errors="ignore").strip()
        if "OTA_OK:" not in resp:
            print(f"\n[ERROR] 写入分片失败 (offset {offset})，响应: {resp}")
            s.close()
            return False

        offset += chunk_len
        print_progress(offset, fw_size)

    elapsed = time.time() - start_time
    speed = (fw_size / 1024.0) / elapsed if elapsed > 0 else 0
    print(f"[+] 数据传输完毕！耗时: {elapsed:.2f}s (均速: {speed:.1f} KB/s)")

    # 3. 发送 OTA_END，要求手表端校验 CRC32
    print("[*] 正在请求手表端执行硬件/软件全镜像 CRC32 完整性校验...")
    s.sendall(b"OTA_END\n")
    s.settimeout(10.0)
    resp = s.recv(128).decode("utf-8", errors="ignore").strip()

    if "OTA_OK:VERIFIED" not in resp:
        print(f"[ERROR] 暂存区固件 CRC32 校验失败！响应: {resp}")
        s.close()
        return False

    print(f"[+] CRC32 校验 100% 一致！手表已置位 RTC 备份域升级标志位。")

    # 4. 发送 OTA_REBOOT 指令
    print("[*] 发送重启指令，通知手表进入 Bootloader 执行热升级...")
    s.sendall(b"OTA_REBOOT\n")
    try:
        resp = s.recv(128).decode("utf-8", errors="ignore").strip()
    except Exception:
        pass

    s.close()

    print("\n=======================================================")
    print("  [SUCCESS] OTA 无线推送升级已圆满完成！")
    print("  手表正在进入 Bootloader 搬运固件并自动跳转至新系统。")
    print("=======================================================\n")
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="STM32F407 SmartWatch OTA Wireless Updater")
    parser.add_argument("--ip", type=str, default="192.168.43.100", help="SmartWatch IP address (e.g. 192.168.43.100)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="SmartWatch TCP port (default: 8080)")
    parser.add_argument("--bin", type=str, default=r"Project\Objects\STM32F429.bin", help="Path to firmware .bin file")

    args = parser.parse_args()

    # 如果默认路径不存在，自动在当前目录或父目录寻找
    bin_file = args.bin
    if not os.path.exists(bin_file):
        candidates = [
            os.path.join(os.path.dirname(__file__), "..", "Project", "Objects", "STM32F429.bin"),
            os.path.join("Project", "Objects", "STM32F429.bin")
        ]
        for c in candidates:
            if os.path.exists(c):
                bin_file = c
                break

    success = send_ota(args.ip, args.port, bin_file)
    sys.exit(0 if success else 1)
