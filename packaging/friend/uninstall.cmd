@echo off
setlocal
title Myanglish IME R1.17 Uninstaller
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1"
set "MYANGLISH_EXIT=%ERRORLEVEL%"
echo.
if not "%MYANGLISH_EXIT%"=="0" (
  echo Uninstall failed. Please send a screenshot of this window.
) else (
  echo Uninstall completed.
)
pause
exit /b %MYANGLISH_EXIT%
