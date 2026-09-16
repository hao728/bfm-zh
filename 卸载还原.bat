@echo off
chcp 65001 >nul
title BFM 中文美化版 - 卸载还原
setlocal enabledelayedexpansion

echo ============================================
echo   BFM 中文美化版 - 卸载还原程序
echo ============================================
echo.

:: WFM 正确部署位置：C:\windows\wfm.exe
set "TARGET_DIR=C:\windows"

:: 查找WFM目录
if not exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    for %%d in (C D E F G H) do (
        if exist "%%d:\windows\wfm.exe.bak_zh" (
            set "TARGET_DIR=%%d:\windows"
            goto :found
        )
    )
    echo [错误] 未找到备份文件，可能未安装过汉化版
    echo        正确位置应为 C:\windows\wfm.exe
    pause
    exit /b 1
)
:found
echo [信息] 目标目录: %TARGET_DIR%
echo.

choice /c YN /m "确认还原为原版 WFM？"
if errorlevel 2 (
    echo 已取消
    pause
    exit /b 0
)

echo.
:: ========== 关键修复：结束进程并验证退出 ==========
echo [1/3] 关闭 WFM...
taskkill /f /im wfm.exe >nul 2>&1
set "WAIT_COUNT=0"
:wait_exit
tasklist /fi "imagename eq wfm.exe" 2>nul | findstr /i "wfm.exe" >nul
if errorlevel 1 (
    echo       WFM 进程已结束
    goto :proc_killed
)
set /a WAIT_COUNT+=1
if %WAIT_COUNT% geq 10 (
    echo.
    echo [错误] 无法结束 WFM 进程！
    echo        请手动关闭 WFM 窗口后，按任意键重试
    pause >nul
    taskkill /f /im wfm.exe >nul 2>&1
    set "WAIT_COUNT=0"
    goto :wait_exit
)
ping -n 2 127.0.0.1 >nul
goto :wait_exit
:proc_killed
echo.

echo [2/3] 还原原版文件...
if exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    for %%A in ("%TARGET_DIR%\wfm.exe.bak_zh") do set "BAK_SIZE=%%~zA"
    move /y "%TARGET_DIR%\wfm.exe.bak_zh" "%TARGET_DIR%\wfm.exe" >nul
    for %%A in ("%TARGET_DIR%\wfm.exe") do set "DST_SIZE=%%~zA"
    if not "%BAK_SIZE%"=="%DST_SIZE%" (
        echo [错误] wfm.exe 还原失败！文件大小不匹配
        echo        备份大小: %BAK_SIZE% 字节，当前大小: %DST_SIZE% 字节
        pause
        exit /b 1
    )
    echo       已还原 wfm.exe (%BAK_SIZE% 字节，校验通过)
)
if exist "%TARGET_DIR%\libcdio.dll.bak_zh" (
    move /y "%TARGET_DIR%\libcdio.dll.bak_zh" "%TARGET_DIR%\libcdio.dll" >nul
    echo       已还原 libcdio.dll
)

echo [3/3] 重启 WFM...
start "" "%TARGET_DIR%\wfm.exe"
echo.

echo ============================================
echo   还原完成！已恢复为原版 WFM
echo ============================================
timeout /t 3 /nobreak >nul
exit /b 0
