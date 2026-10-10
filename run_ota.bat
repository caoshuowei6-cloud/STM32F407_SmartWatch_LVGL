@echo off
title SmartWatch Wireless OTA Updater
echo =======================================================
echo    STM32F407 SmartWatch Wireless OTA Push Tool
echo =======================================================
echo.

set WATCH_IP=192.168.111.89
set /p USER_IP="Target Watch IP [Enter for default %WATCH_IP%]: "
if not "%USER_IP%"=="" set WATCH_IP=%USER_IP%

echo.
echo [*] Target Watch IP: %WATCH_IP%
echo [*] Starting OTA push...
echo.

python tools\ota_updater.py --ip %WATCH_IP%

echo.
pause
