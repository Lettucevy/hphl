# ==============================================================================
# install_extension.ps1 — Instalador da extensão HP-HL para VS Code / Cursor
# ==============================================================================
$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$VsixPath = Join-Path $ScriptDir "hphl-1.0.0.vsix"

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Instalador da Extensão HP-HL para VS Code / Cursor" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

if (-not (Test-Path $VsixPath)) {
    Write-Host "[ERRO] Arquivo hphl-1.0.0.vsix não encontrado em $VsixPath" -ForegroundColor Red
    exit 1
}

$installed = $false

# 1. Tenta instalar no VS Code
if (Get-Command "code" -ErrorAction SilentlyContinue) {
    Write-Host "[*] Instalando no Visual Studio Code..." -ForegroundColor Yellow
    & code --install-extension $VsixPath --force
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[OK] Extensão instalada com sucesso no VS Code!" -ForegroundColor Green
        $installed = $true
    }
}

# 2. Tenta instalar no Cursor
if (Get-Command "cursor" -ErrorAction SilentlyContinue) {
    Write-Host "[*] Instalando no Cursor..." -ForegroundColor Yellow
    & cursor --install-extension $VsixPath --force
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[OK] Extensão instalada com sucesso no Cursor!" -ForegroundColor Green
        $installed = $true
    }
}

Write-Host ""
if ($installed) {
    Write-Host "==========================================================" -ForegroundColor Green
    Write-Host "  Extensão instalada com sucesso! Reinicie o editor." -ForegroundColor Green
    Write-Host "==========================================================" -ForegroundColor Green
} else {
    Write-Host "[INFO] Para instalar manualmente:" -ForegroundColor Yellow
    Write-Host "  1. Abra o VS Code e pressione Ctrl+Shift+X"
    Write-Host "  2. Clique nos 3 pontinhos (...) no topo do menu de extensões"
    Write-Host "  3. Escolha 'Install from VSIX...' e aponte para: $VsixPath"
}
Write-Host ""
