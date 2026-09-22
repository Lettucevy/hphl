@echo off
setlocal EnableDelayedExpansion
title Instalador da Extensao HP-HL
cd /d "%~dp0"

echo ==========================================================
echo   Instalando Extensao HP-HL no VS Code / Cursor
echo ==========================================================
echo.

set "VSIX_FILE=%~dp0hphl-1.0.0.vsix"

if not exist "!VSIX_FILE!" (
    echo [ERRO] Arquivo hphl-1.0.0.vsix nao encontrado!
    goto :end
)

set "DONE=0"

where code >nul 2>nul
if !errorlevel! equ 0 (
    echo [*] Instalando no Visual Studio Code...
    call code --install-extension "!VSIX_FILE!" --force
    if !errorlevel! equ 0 (
        echo [OK] Extensao instalada com sucesso no VS Code!
        set "DONE=1"
    )
)

where cursor >nul 2>nul
if !errorlevel! equ 0 (
    echo [*] Instalando no Cursor...
    call cursor --install-extension "!VSIX_FILE!" --force
    if !errorlevel! equ 0 (
        echo [OK] Extensao instalada com sucesso no Cursor!
        set "DONE=1"
    )
)

echo.
if "!DONE!"=="1" (
    echo [SUCESSO] Instalacao concluida! Reinicie o editor para ativar o suporte HP-HL.
) else (
    echo [INFO] Para instalar manualmente dentro do VS Code:
    echo 1. Abra o VS Code e tecle Ctrl+Shift+X
    echo 2. Clique nos tres pontinhos no topo do menu de extensoes
    echo 3. Escolha a opcao: Install from VSIX...
    echo 4. Selecione o arquivo: !VSIX_FILE!
)

:end
echo.
pause
