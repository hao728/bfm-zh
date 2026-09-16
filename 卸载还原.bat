@echo off
chcp 65001 >nul
title BFM 中文美化版 - 卸载还原
setlocal enabledelayedexpansion

echo ============================================
echo   BFM 中文美化版 - 卸载还原程序
echo ============================================
echo.

set "TARGET_DIR=C:\Program Files\Winlator\WFM"

:: 查找WFM目录
if not exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    for %%d in (C D E F G H) do (
        if exist "%%d:\Program Files\Winlator\WFM\wfm.exe.bak_zh" (
            set "TARGET_DIR=%%d:\Program Files\Winlator\WFM"
            goto :found
        )
    )
    echo [错误] 未找到备份文件，可能未安装过汉化版
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
echo [1/3] 关闭 WFM...
taskkill /f /im wfm.exe >nul 2>&1
timeout /t 1 /nobreak >nul

echo [2/3] 还原原版文件...
if exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    move /y "%TARGET_DIR%\wfm.exe.bak_zh" "%TARGET_DIR%\wfm.exe" >nul
    echo       已还原 wfm.exe
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
