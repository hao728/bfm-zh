@echo off
chcp 65001 >nul
title BFM 中文美化版 - 安装程序
setlocal enabledelayedexpansion

echo ============================================
echo   BFM 中文美化版 v1.2.1-zh.3 安装程序
echo ============================================
echo.

:: 获取脚本所在目录
set "SCRIPT_DIR=%~dp0"
:: WFM 正确部署位置：C:\windows\wfm.exe（上游 README 指定，保持 Bannerlator 启动路径不变）
set "TARGET_DIR=C:\windows"

:: 如果目标目录不存在，尝试查找WFM
if not exist "%TARGET_DIR%\wfm.exe" (
    echo [信息] 未在默认目录找到 WFM，正在搜索...
    for %%d in (C D E F G H) do (
        if exist "%%d:\windows\wfm.exe" (
            set "TARGET_DIR=%%d:\windows"
            goto :found
        )
    )
    echo [错误] 未找到 WFM 安装目录，请确认 Winlator 已安装
    echo        正确位置应为 C:\windows\wfm.exe
    pause
    exit /b 1
)
:found
echo [信息] 目标目录: %TARGET_DIR%
echo.

:: 检查是否已安装过本版本
if exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    echo [警告] 检测到已存在备份文件，说明之前已安装过汉化版
    echo        如需重新安装，请先运行"卸载还原.bat"
    echo.
    choice /c YN /m "是否继续覆盖安装？"
    if errorlevel 2 (
        echo 安装已取消
        pause
        exit /b 0
    )
)

:: 备份原版文件
echo [1/4] 备份原版文件...
if not exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    copy /y "%TARGET_DIR%\wfm.exe" "%TARGET_DIR%\wfm.exe.bak_zh" >nul
    echo       已备份 wfm.exe -^> wfm.exe.bak_zh
)
if exist "%TARGET_DIR%\libcdio.dll" (
    if not exist "%TARGET_DIR%\libcdio.dll.bak_zh" (
        copy /y "%TARGET_DIR%\libcdio.dll" "%TARGET_DIR%\libcdio.dll.bak_zh" >nul
        echo       已备份 libcdio.dll -^> libcdio.dll.bak_zh
    )
)
echo.

:: 部署 7-Zip 便携版
echo [2/4] 检查 7-Zip 环境...
set "SEVENZIP_DIR=Z:\opt\apps\7-Zip"
if exist "%SEVENZIP_DIR%\7z.exe" (
    echo       7-Zip 已存在: %SEVENZIP_DIR%
) else (
    if exist "%SCRIPT_DIR%7z\7z.exe" (
        if not exist "%SEVENZIP_DIR%" mkdir "%SEVENZIP_DIR%"
        copy /y "%SCRIPT_DIR%7z\7z.exe" "%SEVENZIP_DIR%\" >nul
        copy /y "%SCRIPT_DIR%7z\7z.dll" "%SEVENZIP_DIR%\" >nul
        echo       已部署 7-Zip 便携版到 %SEVENZIP_DIR%
    ) else if exist "%SCRIPT_DIR%7z*.exe" (
        echo       检测到 7-Zip 安装包，正在静默安装...
        for %%f in ("%SCRIPT_DIR%7z*.exe") do (
            "%%f" /S /D="%SEVENZIP_DIR%" >nul 2>&1
        )
        echo       7-Zip 已安装到 %SEVENZIP_DIR%
    ) else (
        echo       [提示] 未找到 7-Zip 文件，解压/压缩功能将不可用
        echo              可将 7z.exe + 7z.dll 放入 7z\ 目录后重新运行
    )
)
echo.

:: 复制汉化版文件
echo [3/4] 安装汉化版文件...
copy /y "%SCRIPT_DIR%wfm.exe" "%TARGET_DIR%\wfm.exe" >nul
if exist "%SCRIPT_DIR%libcdio.dll" (
    copy /y "%SCRIPT_DIR%libcdio.dll" "%TARGET_DIR%\libcdio.dll" >nul
)
echo       已复制 wfm.exe
if exist "%SCRIPT_DIR%libcdio.dll" echo       已复制 libcdio.dll
echo.

:: 重启 WFM
echo [4/4] 重启 WFM...
taskkill /f /im wfm.exe >nul 2>&1
timeout /t 1 /nobreak >nul
start "" "%TARGET_DIR%\wfm.exe"
echo.

echo ============================================
echo   安装完成！
echo ============================================
echo.
echo   版本: v1.2.1-zh.3
echo   基于: Banner File Manager v1.2.1
echo.
echo   主要功能:
echo   - 完整中文汉化
echo   - 文件类型智能识别图标
echo   - 右键解压/压缩/测试完整性
echo   - 双面板 + 拖拽
echo   - 预览窗格
echo   - 收藏夹
echo   - 加速启动
echo   - 启动参数预设
echo.
timeout /t 3 /nobreak >nul
exit /b 0
