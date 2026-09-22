@echo off
title HP-HL SDK Setup
powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0gui_installer.ps1"
if %errorlevel% neq 0 (
    echo Iniciando modo texto...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
    pause
)
