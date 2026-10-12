@echo off
setlocal
title AI Sim - Build and Launch
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Development Tools\scripts\launch.ps1" -ProjectRoot "%~dp0."
if errorlevel 1 (
    echo.
    echo AI Sim could not start. See the error above.
    pause
    exit /b 1
)
endlocal
