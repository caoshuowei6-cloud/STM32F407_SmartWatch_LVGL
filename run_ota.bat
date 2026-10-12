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
echo Select OTA Push Mode:
echo   [1] AES-128-CBC Encrypted Mode (Default, High Security)
echo   [2] Plaintext Mode (Legacy Compatibility)
set /p OTA_MODE="Select mode [1 or 2, Enter for 1]: "

echo.
if "%OTA_MODE%"=="2" (
    echo [*] Pushing with Plaintext Compatibility Mode...
    python tools\ota_updater.py --ip %WATCH_IP%
) else (
    echo [*] Pushing with Enterprise AES-128-CBC Encryption...
    python tools\ota_updater.py --ip %WATCH_IP% --encrypt
)

echo.
pause
