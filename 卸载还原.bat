@echo off
chcp 65001 >nul
title BFM 中文美化版 - 卸载还原
setlocal enabledelayedexpansion

echo ============================================
echo   BFM 中文美化版 - 卸载还原
echo ============================================
echo.

set "TARGET_DIR=C:\windows"

if not exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    for %%d in (C D E F G H) do (
        if exist "%%d:\windows\wfm.exe.bak_zh" (
            set "TARGET_DIR=%%d:\windows"
            goto :found
        )
    )
    echo [Error] Backup not found
    pause
    exit /b 1
)
:found
echo [Info] Target: %TARGET_DIR%
echo.

choice /c YN /m "Restore original WFM?"
if errorlevel 2 (
    echo Cancelled
    pause
    exit /b 0
)

echo.
echo [1/3] Closing WFM...
taskkill /f /im wfm.exe >nul 2>&1
set "WAIT_COUNT=0"
:wait_exit
tasklist /fi "imagename eq wfm.exe" 2>nul | findstr /i "wfm.exe" >nul
if errorlevel 1 goto :proc_killed
set /a WAIT_COUNT+=1
if %WAIT_COUNT% geq 10 (
    echo [Error] Cannot close WFM, close manually then press any key
    pause >nul
    taskkill /f /im wfm.exe >nul 2>&1
    set "WAIT_COUNT=0"
)
ping -n 2 127.0.0.1 >nul
goto :wait_exit
:proc_killed
echo       WFM closed
echo.

echo [2/3] Restoring original...
if exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    for %%A in ("%TARGET_DIR%\wfm.exe.bak_zh") do set "BAK_SIZE=%%~zA"
    move /y "%TARGET_DIR%\wfm.exe.bak_zh" "%TARGET_DIR%\wfm.exe" >nul
    for %%A in ("%TARGET_DIR%\wfm.exe") do set "DST_SIZE=%%~zA"
    if not "%BAK_SIZE%"=="%DST_SIZE%" (
        echo [Error] wfm.exe restore failed
        pause
        exit /b 1
    )
    echo       wfm.exe restored (%BAK_SIZE% bytes)
)
if exist "%TARGET_DIR%\libcdio.dll.bak_zh" (
    move /y "%TARGET_DIR%\libcdio.dll.bak_zh" "%TARGET_DIR%\libcdio.dll" >nul
    echo       libcdio.dll restored
)

echo [3/3] Starting WFM...
start "" "%TARGET_DIR%\wfm.exe"
echo.
echo ============================================
echo   Restore complete!
echo ============================================
timeout /t 3 /nobreak >nul
exit /b 0
