@echo off
setlocal
title Myanglish IME R1.17 Installer
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
set "MYANGLISH_EXIT=%ERRORLEVEL%"
echo.
if not "%MYANGLISH_EXIT%"=="0" (
  echo Installation failed. Please send a screenshot of this window.
) else (
  echo Installation completed.
)
pause
exit /b %MYANGLISH_EXIT%
