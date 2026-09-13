@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul
title BFM-Zh 卸载/还原程序

echo ============================================
echo   BFM-Zh 中文美化版 卸载/还原程序
echo ============================================
echo.

:: --- 配置 ---
set "INSTALL_DIR=C:\windows"
set "EXE_NAME=wfm.exe"
set "DLL_NAME=libcdio.dll"
set "BAK_EXE=%EXE_NAME%.bak"
set "BAK_DLL=%DLL_NAME%.bak"

:: --- Step 1: 关闭运行中的 WFM ---
echo [1/4] 关闭运行中的 WFM...
taskkill /f /im %EXE_NAME% >nul 2>&1
timeout /t 1 /nobreak >nul
echo [完成] 已关闭 WFM。
echo.

:: --- Step 2: 还原或删除 ---
echo [2/4] 还原原版文件...
if exist "%INSTALL_DIR%\%BAK_EXE%" (
    echo [信息] 检测到备份，正在还原原版...
    move /y "%INSTALL_DIR%\%BAK_EXE%" "%INSTALL_DIR%\%EXE_NAME%" >nul
    echo [完成] 已还原 %EXE_NAME%
    if exist "%INSTALL_DIR%\%BAK_DLL%" (
        move /y "%INSTALL_DIR%\%BAK_DLL%" "%INSTALL_DIR%\%DLL_NAME%" >nul
        echo [完成] 已还原 %DLL_NAME%
    )
) else (
    echo [信息] 未检测到备份，将直接删除 BFM-Zh 文件...
    del /f /q "%INSTALL_DIR%\%EXE_NAME%" >nul 2>&1
    echo [完成] 已删除 %EXE_NAME%
    del /f /q "%INSTALL_DIR%\%DLL_NAME%" >nul 2>&1
    if exist "%INSTALL_DIR%\%DLL_NAME%" (
        echo [完成] 已删除 %DLL_NAME%
    )
)
echo.

:: --- Step 3: 清理残留备份 ---
echo [3/4] 清理残留文件...
del /f /q "%INSTALL_DIR%\%BAK_EXE%" >nul 2>&1
del /f /q "%INSTALL_DIR%\%BAK_DLL%" >nul 2>&1
echo [完成] 已清理备份文件。
echo.

:: --- Step 4: 删除桌面快捷方式 ---
echo [4/4] 删除桌面快捷方式...
del /f /q "%USERPROFILE%\Desktop\BFM-Zh.lnk" >nul 2>&1
echo [完成] 已删除桌面快捷方式。
echo.

:: --- 完成 ---
echo ============================================
echo   卸载/还原完成！
echo   已恢复到安装前的状态。
echo ============================================
echo.
timeout /t 3 /nobreak >nul
exit /b 0
