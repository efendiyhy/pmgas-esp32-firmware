@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash.ps1"
echo.
echo Tekan tombol untuk menutup terminal...
pause >nul
