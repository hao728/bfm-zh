@echo off
chcp 65001 >nul
title BFM-Zh Uninstaller

echo ============================================
echo   BFM-Zh Uninstall
echo ============================================
echo.

set "INSTALL_DIR=C:\windows"

echo [1/3] Closing running wfm.exe...
taskkill /f /im wfm.exe >nul 2>&1
timeout /t 1 /nobreak >nul

echo [2/3] Removing files...
del /f /q "%INSTALL_DIR%\wfm.exe" >nul 2>&1
del /f /q "%INSTALL_DIR%\libcdio.dll" >nul 2>&1
del /f /q "%INSTALL_DIR%\wfm.exe.bak" >nul 2>&1
del /f /q "%INSTALL_DIR%\libcdio.dll.bak" >nul 2>&1

echo [3/3] Removing desktop shortcut...
del /f /q "%USERPROFILE%\Desktop\BFM-Zh.lnk" >nul 2>&1

echo.
echo ============================================
echo   Uninstall complete.
echo ============================================
pause
