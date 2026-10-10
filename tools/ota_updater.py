#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
===============================================================================
  STM32F407 SmartWatch Wireless OTA Push Tool (企业级无线 OTA 固件推送引擎)
  - 链路拓扑: PC (TCP Socket) -> ESP32-C3 (8080 Server) -> STM32F407 (USART2 DMA)
  - 传输模式: 
      1. 明文模式 (默认): 兼容老固件平滑升级 (OTA_START)
      2. 加密模式 (--encrypt): AES-128-CBC 空中流式对称密文传输 (OTA_START_AES)
  - 完整性校验: IEEE 802.3 CRC32 全镜像软硬件双校验
  - 容错保护: 分片传输超时自适应、传输期手表按键全局互斥锁
===============================================================================
"""

import sys
import os
import time
import socket
import binascii
import argparse

try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.backends import default_backend
    HAS_CRYPTO = True
except ImportError:
    HAS_CRYPTO = False

DEFAULT_PORT = 8080
CHUNK_SIZE = 512             # 单个分片大小 (必须为 16 的倍数，以配合 AES 块加密)
TIMEOUT_SECTOR_ERASE = 15.0  # 扇区擦除超时 (3 个 128KB 扇区约需 3~6s)
TIMEOUT_PER_CHUNK = 8.0      # 单块写入超时 (充足防御 Flash 写入与解密延迟)

# AES-128-CBC 预共享密钥与初始向量 (与 STM32 ota_manager.c 完全一致)
AES_KEY = bytes([
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
])

AES_IV = bytes([
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
])


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


def recv_until(s, keywords, timeout):
    s.settimeout(timeout)
    buf = ""
    start = time.time()
    while (time.time() - start) < timeout:
        try:
            data = s.recv(128).decode("utf-8", errors="ignore")
            if not data:
                break
            buf += data
            for kw in keywords:
                if kw in buf:
                    return buf
        except socket.timeout:
            break
    return buf


def encrypt_firmware(plain_data: bytes) -> tuple[bytes, int, int]:
    """
    对固件数据进行 16 字节对齐填充并使用 AES-128-CBC 加密
    返回: (加密后的密文字节流, 原始明文字节数, 原始明文CRC32)
    """
    if not HAS_CRYPTO:
        raise RuntimeError("缺少 cryptography 依赖库，请运行: pip install cryptography")

    plain_size = len(plain_data)
    plain_crc = calc_crc32(plain_data)

    # 16 字节对齐填充 (固件尾部填充 0xFF，与 STM32 Flash 擦除后默认电平一致)
    pad_len = (16 - (plain_size % 16)) % 16
    padded_data = plain_data + (b'\xFF' * pad_len)

    cipher = Cipher(algorithms.AES(AES_KEY), modes.CBC(AES_IV), backend=default_backend())
    encryptor = cipher.encryptor()
    enc_data = encryptor.update(padded_data) + encryptor.finalize()

    return enc_data, plain_size, plain_crc


def send_ota(ip: str, port: int, bin_path: str, encrypt: bool = False):
    if not os.path.isfile(bin_path):
        print(f"[ERROR] 固件文件不存在: {bin_path}")
        return False

    with open(bin_path, "rb") as f:
        fw_plain = f.read()

    plain_size = len(fw_plain)
    plain_crc = calc_crc32(fw_plain)

    if encrypt:
        # 执行 AES-128-CBC 固件加密
        fw_send, _, _ = encrypt_firmware(fw_plain)
        send_size = len(fw_send)
        start_cmd_str = f"OTA_START_AES:{plain_size}:{plain_crc:08X}\n"
        mode_str = "AES-128-CBC 密文空中流式加密"
    else:
        # 明文传输 (兼容老固件平滑过渡)
        fw_send = fw_plain
        send_size = plain_size
        start_cmd_str = f"OTA_START:{plain_size}:{plain_crc:08X}\n"
        mode_str = "标准明文流式传输 (兼容模式)"

    print(f"=======================================================")
    print(f"  STM32F407 智能手表企业级无线 OTA 固件推送引擎")
    print(f"=======================================================")
    print(f"  目标手表 IP     : {ip}:{port}")
    print(f"  固件文件路径    : {bin_path}")
    print(f"  明文固件大小    : {plain_size} 字节 ({plain_size / 1024:.1f} KB)")
    print(f"  明文固件 CRC32  : 0x{plain_crc:08X}")
    print(f"  传输安全协议    : {mode_str}")
    print(f"  空中推送总量    : {send_size} 字节")
    print(f"  手表互斥锁策略  : 升级期间按键全局硬件加锁保护")
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

    print("[+] TCP 链路建立成功！发送 OTA 握手指令与擦除请求...")

    # 1. 发送 OTA_START / OTA_START_AES 握手指令
    s.sendall(start_cmd_str.encode("utf-8"))

    resp = recv_until(s, ["OTA_OK:READY", "OTA_ERR:"], TIMEOUT_SECTOR_ERASE)
    if "OTA_OK:READY" not in resp:
        print(f"[ERROR] 手表拒绝升级请求或擦除超时，响应: {resp}")
        s.close()
        return False

    print("[+] 手表暂存区擦除完成！开始分片流式推送固件...")

    # 2. 分片推送数据
    offset = 0
    start_time = time.time()
    prefix_str = "AES OTA Upload" if encrypt else "Plain OTA Upload"

    while offset < send_size:
        chunk = fw_send[offset : offset + CHUNK_SIZE]
        chunk_len = len(chunk)
        header = f"OTA_DATA:{chunk_len}\n".encode("utf-8")
        packet = header + chunk

        s.sendall(packet)

        # 等待手表回复 ACK
        resp = recv_until(s, ["OTA_OK:", "OTA_ERR:"], TIMEOUT_PER_CHUNK)
        if "OTA_OK:" not in resp:
            print(f"\n[ERROR] 写入分片失败 (offset {offset})，响应: {resp}")
            s.close()
            return False

        offset += chunk_len
        print_progress(offset, send_size, prefix=prefix_str)

    elapsed = time.time() - start_time
    speed = (send_size / 1024.0) / elapsed if elapsed > 0 else 0
    print(f"[+] 数据推送完毕！耗时: {elapsed:.2f}s (有效均速: {speed:.1f} KB/s)")

    # 3. 发送 OTA_END，通知手表在线校验明文 CRC32
    print("[*] 正在请求手表端执行硬件/软件全镜像明文 CRC32 完整性校验...")
    s.sendall(b"OTA_END\n")
    resp = recv_until(s, ["OTA_OK:VERIFIED", "OTA_ERR:"], 10.0)

    if "OTA_OK:VERIFIED" not in resp:
        print(f"[ERROR] 暂存区固件 CRC32 校验失败！响应: {resp}")
        s.close()
        return False

    print(f"[+] 明文 CRC32 校验 100% 一致！手表已通过防篡改检测，RTC 备份域升级标志位已置位。")

    # 4. 发送 OTA_REBOOT 指令
    print("[*] 发送重启指令，通知手表进入 Bootloader 执行热升级...")
    s.sendall(b"OTA_REBOOT\n")
    recv_until(s, ["OTA_OK:REBOOTING"], 2.0)
    s.close()

    print("\n=======================================================")
    print("  [SUCCESS] OTA 无线固件升级推送圆满完成！")
    print("  手表正在进入 Bootloader 搬运固件并自动跳转至新系统。")
    print("=======================================================\n")
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="STM32F407 SmartWatch Wireless Dual-Mode OTA Pusher")
    parser.add_argument("--ip", type=str, default="192.168.111.89", help="SmartWatch IP address (default: 192.168.111.89)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="SmartWatch TCP port (default: 8080)")
    parser.add_argument("--bin", type=str, default=r"Project\Objects\STM32F429.bin", help="Path to firmware .bin file")
    parser.add_argument("--encrypt", action="store_true", help="Enable AES-128-CBC encryption mode (requires receiver with AES support)")

    args = parser.parse_args()

    # 自动定位 .bin 文件路径
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

    success = send_ota(args.ip, args.port, bin_file, encrypt=args.encrypt)
    sys.exit(0 if success else 1)
