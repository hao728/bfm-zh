@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul
title BFM-Zh 安装程序

echo ============================================
echo   BFM-Zh 中文美化版 安装程序
echo   基于 Banner File Manager 修改
echo ============================================
echo.

:: --- 配置 ---
set "INSTALL_DIR=C:\windows"
set "EXE_NAME=wfm.exe"
set "DLL_NAME=libcdio.dll"
set "BAK_EXE=%EXE_NAME%.bak"
set "BAK_DLL=%DLL_NAME%.bak"

:: --- Step 1: 检查是否已安装 ---
echo [1/5] 检查安装状态...
if exist "%INSTALL_DIR%\%BAK_EXE%" (
    echo [警告] 检测到已安装版本的备份，说明之前已安装过。
    echo.
    choice /c YN /n /m "是否覆盖当前版本？(Y=覆盖安装, N=取消): "
    if errorlevel 2 (
        echo.
        echo 安装已取消。
        timeout /t 2 /nobreak >nul
        exit /b 0
    )
    echo [信息] 将覆盖当前版本，保留原有备份。
) else (
    echo [信息] 未检测到已安装版本，将备份当前文件。
)
echo.

:: --- Step 2: 关闭运行中的 WFM ---
echo [2/5] 关闭运行中的 WFM...
taskkill /f /im %EXE_NAME% >nul 2>&1
timeout /t 1 /nobreak >nul
echo [完成] 已关闭 WFM。
echo.

:: --- Step 3: 备份（仅首次安装时） ---
echo [3/5] 备份原文件...
if not exist "%INSTALL_DIR%\%BAK_EXE%" (
    if exist "%INSTALL_DIR%\%EXE_NAME%" (
        copy /y "%INSTALL_DIR%\%EXE_NAME%" "%INSTALL_DIR%\%BAK_EXE%" >nul
        echo [完成] 已备份 %EXE_NAME% -^> %BAK_EXE%
    ) else (
        echo [跳过] 未找到原 %EXE_NAME%，无需备份。
    )
    if exist "%INSTALL_DIR%\%DLL_NAME%" (
        copy /y "%INSTALL_DIR%\%DLL_NAME%" "%INSTALL_DIR%\%BAK_DLL%" >nul
        echo [完成] 已备份 %DLL_NAME% -^> %BAK_DLL%
    ) else (
        echo [跳过] 未找到原 %DLL_NAME%，无需备份。
    )
) else (
    echo [跳过] 已存在备份，不覆盖原有备份。
)
echo.

:: --- Step 4: 复制文件 ---
echo [4/5] 安装文件...
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
copy /y "%~dp0%EXE_NAME%" "%INSTALL_DIR%\%EXE_NAME%" >nul
if errorlevel 1 (
    echo [错误] 复制 %EXE_NAME% 失败！
    echo 请确认安装包中包含 %EXE_NAME%
    pause
    exit /b 1
)
echo [完成] 已复制 %EXE_NAME%

if exist "%~dp0%DLL_NAME%" (
    copy /y "%~dp0%DLL_NAME%" "%INSTALL_DIR%\%DLL_NAME%" >nul
    echo [完成] 已复制 %DLL_NAME%
) else (
    echo [跳过] 未找到 %DLL_NAME%
)
echo.

:: --- Step 5: 创建桌面快捷方式 ---
echo [5/5] 创建桌面快捷方式...
set "VBSFILE=%TEMP%\bfm_shortcut.vbs"
echo Set WshShell = CreateObject("WScript.Shell") > "%VBSFILE%"
echo Set shortcut = WshShell.CreateShortcut("%USERPROFILE%\Desktop\BFM-Zh.lnk") >> "%VBSFILE%"
echo shortcut.TargetPath = "%INSTALL_DIR%\%EXE_NAME%" >> "%VBSFILE%"
echo shortcut.WorkingDirectory = "%INSTALL_DIR%" >> "%VBSFILE%"
echo shortcut.Description = "BFM-Zh 中文美化版文件管理器" >> "%VBSFILE%"
echo shortcut.Save >> "%VBSFILE%"
cscript //nologo "%VBSFILE%" >nul 2>&1
del "%VBSFILE%" >nul 2>&1
echo [完成] 已创建桌面快捷方式。
echo.

:: --- 完成 ---
echo ============================================
echo   安装完成！
echo   位置: %INSTALL_DIR%\%EXE_NAME%
echo ============================================
echo.
echo 正在启动 WFM...
start "" "%INSTALL_DIR%\%EXE_NAME%"
timeout /t 2 /nobreak >nul
exit /b 0
