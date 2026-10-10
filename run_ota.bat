@echo off
chcp 65001 >nul
title STM32F407 智能手表无线 OTA 一键推送工具

echo =======================================================
echo    STM32F407 智能手表无线 OTA 一键空中固件推送脚本
echo =======================================================
echo.

set WATCH_IP=192.168.111.89
set /p USER_IP="请输入手表当前 IP 地址 [直接回车默认: %WATCH_IP%]: "
if not "%USER_IP%"=="" set WATCH_IP=%USER_IP%

echo.
echo [*] 目标手表 IP: %WATCH_IP%
echo [*] 开始执行 OTA 无线固件推送...
echo.

python tools\ota_updater.py --ip %WATCH_IP% --bin Project\Objects\STM32F429.bin

echo.
pause
