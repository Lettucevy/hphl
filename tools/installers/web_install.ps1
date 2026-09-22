# ==============================================================================
# HP-HL Web Installer (Windows PowerShell)
# Usage: irm https://hphl.dev/install.ps1 | iex
# ==============================================================================
$ErrorActionPreference = "Stop"

$Version = "1.0.0"
$Repo = "Lettucevy/hphl"
$InstallDir = Join-Path $env:USERPROFILE ".hphl"
$ZipName = "hphl-sdk-v$Version-windows-x64.zip"
$DownloadUrl = "https://github.com/$Repo/releases/download/v$Version/$ZipName"

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Instalador Oficial do HP-HL para Windows (v$Version)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Destino da instalacao: $InstallDir"

# 1. Download do Pacote
$tempZip = Join-Path $env:TEMP $ZipName
Write-Host "[1/4] Baixando SDK oficial do HP-HL..." -ForegroundColor Yellow
try {
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $tempZip -UseBasicParsing
} catch {
    Write-Host "[AVISO] Falha no download remoto. Verifique sua conexao ou o link do release." -ForegroundColor Red
    throw $_
}

# 2. Extração
Write-Host "[2/4] Extraindo arquivos..." -ForegroundColor Yellow
if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
}
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null

$tempExtract = Join-Path $env:TEMP "hphl_extract"
if (Test-Path $tempExtract) { Remove-Item -Recurse -Force $tempExtract }
Expand-Archive -Path $tempZip -DestinationPath $tempExtract -Force

# Identifica se extraiu dentro de uma subpasta ou direto
$subFolder = Join-Path $tempExtract "hphl-sdk-v$Version-windows-x64"
if (Test-Path $subFolder) {
    Copy-Item "$subFolder/*" -Destination $InstallDir -Recurse -Force
} else {
    Copy-Item "$tempExtract/*" -Destination $InstallDir -Recurse -Force
}

Remove-Item -Recurse -Force $tempExtract
Remove-Item -Force $tempZip

# 3. Configuração de Variáveis de Ambiente
Write-Host "[3/4] Configurando variaveis de ambiente..." -ForegroundColor Yellow
[Environment]::SetEnvironmentVariable("HPHL_HOME", $InstallDir, "User")
$binPath = Join-Path $InstallDir "bin"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$binPath*") {
    $newPath = "$binPath;$userPath"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "[OK] $binPath adicionado ao PATH do usuario." -ForegroundColor Green
}

# 4. Instalação da Extensão do VS Code / Cursor
Write-Host "[4/4] Verificando integracao com editores..." -ForegroundColor Yellow
$vsixPath = Join-Path $InstallDir "editors/hphl-1.0.0.vsix"
if (Test-Path $vsixPath) {
    if (Get-Command "code" -ErrorAction SilentlyContinue) {
        Write-Host "  -> Instalando no Visual Studio Code..." -ForegroundColor Green
        & code --install-extension $vsixPath --force 2>$null
    }
    if (Get-Command "cursor" -ErrorAction SilentlyContinue) {
        Write-Host "  -> Instalando no Cursor..." -ForegroundColor Green
        & cursor --install-extension $vsixPath --force 2>$null
    }
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "  HP-HL SDK v$Version instalado com sucesso!" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Abra um novo terminal (PowerShell ou CMD) e execute:"
Write-Host "  hphlc --version" -ForegroundColor Yellow
Write-Host ""
