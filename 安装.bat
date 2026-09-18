@echo off
chcp 65001 >nul
title BFM 中文美化版 - 安装程序
setlocal enabledelayedexpansion

echo ============================================
echo   BFM 中文美化版 v1.2.1-zh.3 安装程序
echo ============================================
echo.

set "SCRIPT_DIR=%~dp0"
set "TARGET_DIR=C:\windows"

if not exist "%TARGET_DIR%\wfm.exe" (
    echo [Info] WFM not found, searching...
    for %%d in (C D E F G H) do (
        if exist "%%d:\windows\wfm.exe" (
            set "TARGET_DIR=%%d:\windows"
            goto :found
        )
    )
    echo [Error] WFM not found
    pause
    exit /b 1
)
:found
echo [Info] Target: %TARGET_DIR%
echo.

echo [0/5] Closing WFM...
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

if exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    echo [Warning] Backup exists, reinstall overwrites
    choice /c YN /m "Continue?"
    if errorlevel 2 (
        echo Cancelled
        pause
        exit /b 0
    )
)

echo [1/5] Backing up original...
if not exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    copy /y "%TARGET_DIR%\wfm.exe" "%TARGET_DIR%\wfm.exe.bak_zh" >nul
    echo       wfm.exe backed up
)
if exist "%TARGET_DIR%\libcdio.dll" (
    if not exist "%TARGET_DIR%\libcdio.dll.bak_zh" (
        copy /y "%TARGET_DIR%\libcdio.dll" "%TARGET_DIR%\libcdio.dll.bak_zh" >nul
        echo       libcdio.dll backed up
    )
)
echo.

echo [2/5] Checking 7-Zip...
set "SEVENZIP_DIR=Z:\opt\apps\7-Zip"
if exist "%SEVENZIP_DIR%\7z.exe" (
    echo       7-Zip found
) else if exist "%SCRIPT_DIR%7z.exe" (
    echo       Installing 7-Zip (portable)...
    if not exist "%SEVENZIP_DIR%" mkdir "%SEVENZIP_DIR%"
    copy /y "%SCRIPT_DIR%7z.exe" "%SEVENZIP_DIR%\" >nul
    if exist "%SCRIPT_DIR%7z.dll" copy /y "%SCRIPT_DIR%7z.dll" "%SEVENZIP_DIR%\" >nul
    if exist "%SEVENZIP_DIR%\7z.exe" (
        echo       7-Zip installed
    ) else (
        echo       [Warning] 7-Zip copy failed
    )
) else if exist "%SCRIPT_DIR%7z*.exe" (
    echo       Launching 7-Zip installer (please install manually to Z:\opt\apps\7-Zip)...
    for %%f in ("%SCRIPT_DIR%7z*.exe") do start "" "%%f"
    echo       [Info] 7-Zip installer opened, install it to Z:\opt\apps\7-Zip
) else (
    echo       [Notice] 7-Zip not found, compression features will be limited
)
echo.

echo [3/5] Installing files...
for %%A in ("%SCRIPT_DIR%wfm.exe") do set "SRC_SIZE=%%~zA"
copy /y "%SCRIPT_DIR%wfm.exe" "%TARGET_DIR%\wfm.exe" >nul
for %%A in ("%TARGET_DIR%\wfm.exe") do set "DST_SIZE=%%~zA"
if not "%SRC_SIZE%"=="%DST_SIZE%" (
    echo [Error] wfm.exe copy failed
    pause
    exit /b 1
)
echo       wfm.exe copied (%SRC_SIZE% bytes)

if exist "%SCRIPT_DIR%libcdio.dll" (
    for %%A in ("%SCRIPT_DIR%libcdio.dll") do set "SRC2=%%~zA"
    copy /y "%SCRIPT_DIR%libcdio.dll" "%TARGET_DIR%\libcdio.dll" >nul
    for %%A in ("%TARGET_DIR%\libcdio.dll") do set "DST2=%%~zA"
    if "!SRC2!"=="!DST2!" echo       libcdio.dll copied (!SRC2! bytes)
    if not "!SRC2!"=="!DST2!" echo       [Warning] libcdio.dll copy failed
)
echo.

echo [4/5] Starting WFM...
start "" "%TARGET_DIR%\wfm.exe"
echo.

echo [5/5] Verifying...
ping -n 3 127.0.0.1 >nul
if exist "%TARGET_DIR%\wfm.exe" (
    echo       Verified
) else (
    echo       [Warning] wfm.exe not found
)
echo.
echo ============================================
echo   Install complete!
echo ============================================
timeout /t 3 /nobreak >nul
exit /b 0
