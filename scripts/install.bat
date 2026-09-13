@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul
title BFM-Zh Installer

echo ============================================
echo   BFM-Zh (Banner File Manager Zh) Install
echo ============================================
echo.

:: --- Step 1: Close running instance ---
echo [1/4] Closing running wfm.exe...
taskkill /f /im wfm.exe >nul 2>&1
timeout /t 1 /nobreak >nul

:: --- Step 2: Determine install dir ---
set "INSTALL_DIR=C:\windows"
echo [2/4] Install directory: %INSTALL_DIR%
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

:: --- Step 3: Backup old version ---
if exist "%INSTALL_DIR%\wfm.exe" (
    echo [3/4] Backing up old version to wfm.exe.bak...
    copy /y "%INSTALL_DIR%\wfm.exe" "%INSTALL_DIR%\wfm.exe.bak" >nul
    if exist "%INSTALL_DIR%\libcdio.dll" (
        copy /y "%INSTALL_DIR%\libcdio.dll" "%INSTALL_DIR%\libcdio.dll.bak" >nul
    )
) else (
    echo [3/4] No old version found, skipping backup.
)

:: --- Step 4: Copy files ---
echo [4/4] Copying files...
copy /y "%~dp0wfm.exe" "%INSTALL_DIR%\wfm.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy wfm.exe
    pause
    exit /b 1
)
if exist "%~dp0libcdio.dll" (
    copy /y "%~dp0libcdio.dll" "%INSTALL_DIR%\libcdio.dll" >nul
)

:: --- Create desktop shortcut via VBS (Wine-compatible) ---
set "VBSFILE=%TEMP%\bfm_shortcut.vbs"
echo Set WshShell = CreateObject("WScript.Shell") > "%VBSFILE%"
echo Set shortcut = WshShell.CreateShortcut("%USERPROFILE%\Desktop\BFM-Zh.lnk") >> "%VBSFILE%"
echo shortcut.TargetPath = "%INSTALL_DIR%\wfm.exe" >> "%VBSFILE%"
echo shortcut.WorkingDirectory = "%INSTALL_DIR%" >> "%VBSFILE%"
echo shortcut.Description = "Banner File Manager Zh" >> "%VBSFILE%"
echo shortcut.Save >> "%VBSFILE%"
cscript //nologo "%VBSFILE%" >nul 2>&1
del "%VBSFILE%" >nul 2>&1

echo.
echo ============================================
echo   Installation complete!
echo   Location: %INSTALL_DIR%\wfm.exe
echo   Desktop shortcut created.
echo ============================================
echo.
echo To uninstall, run uninstall.bat
pause
