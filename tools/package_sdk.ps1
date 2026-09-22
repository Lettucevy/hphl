#!/usr/bin/env powershell
# ==============================================================================
# tools/package_sdk.ps1 — Empacotador Oficial do HP-HL SDK v1.0.0 (Windows x64)
# ==============================================================================
# Gera:
#   dist/hphl-sdk-v1.0.0-windows-x64/
#   dist/hphl-sdk-v1.0.0-windows-x64.zip
#
# Conteúdo:
#   bin/hphlc.exe
#   runtime/
#   stdlib/
#   examples/
#   docs/
#   editors/hphl-1.0.0.vsix (extensão oficial para VS Code / Cursor)
#   install.ps1 e install.bat (instaladores em 1 clique)
#   README.md e LICENSE
# ==============================================================================

param(
    [string]$Version = "1.0.0",
    [string]$OutputDir = "dist",
    [switch]$SkipZip = $false
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$DistDir = Join-Path $RootDir $OutputDir
$SdkName = "hphl-sdk-v$Version-windows-x64"
$StagingDir = Join-Path $DistDir $SdkName

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Empacotando HP-HL SDK v$Version para Windows x64" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Limpeza e preparação de diretórios
if (Test-Path $StagingDir) {
    Remove-Item -Recurse -Force $StagingDir
}
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

# 2. Copiar Compilador bin/hphlc.exe
Write-Host "--> Copiando bin/hphlc.exe..." -ForegroundColor Green
$binDir = Join-Path $StagingDir "bin"
New-Item -ItemType Directory -Path $binDir -Force | Out-Null
$compilerSrc = Join-Path $RootDir "compiler/bin/hphlc.exe"
if (-not (Test-Path $compilerSrc)) {
    $compilerSrc = Join-Path $RootDir "compiler/hphlc.exe"
}
if (-not (Test-Path $compilerSrc)) {
    throw "Compilador não encontrado em: $compilerSrc. Execute tools/build.ps1 primeiro."
}
Copy-Item $compilerSrc -Destination (Join-Path $binDir "hphlc.exe") -Force

# 3. Copiar Runtime
Write-Host "--> Copiando runtime..." -ForegroundColor Green
$runtimeDir = Join-Path $StagingDir "runtime"
New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null
Copy-Item (Join-Path $RootDir "runtime/VERSION") -Destination $runtimeDir -Force
if (Test-Path (Join-Path $RootDir "compiler/src/runtime")) {
    Copy-Item (Join-Path $RootDir "compiler/src/runtime/*") -Destination $runtimeDir -Recurse -Force
}
if (Test-Path (Join-Path $RootDir "runtime/include")) {
    Copy-Item (Join-Path $RootDir "runtime/include") -Destination $runtimeDir -Recurse -Force
}
if (Test-Path (Join-Path $RootDir "runtime/src")) {
    Copy-Item (Join-Path $RootDir "runtime/src") -Destination $runtimeDir -Recurse -Force
}

# 4. Copiar Biblioteca Padrão (stdlib)
Write-Host "--> Copiando stdlib..." -ForegroundColor Green
if (Test-Path (Join-Path $RootDir "stdlib")) {
    Copy-Item (Join-Path $RootDir "stdlib") -Destination $StagingDir -Recurse -Force
}

# 5. Copiar Exemplos
Write-Host "--> Copiando exemplos oficiais..." -ForegroundColor Green
$examplesSrc = Join-Path $RootDir "examples"
if (Test-Path $examplesSrc) {
    Copy-Item $examplesSrc -Destination $StagingDir -Recurse -Force
}


# 6. Copiar Documentação Limpa
Write-Host "--> Copiando documentação..." -ForegroundColor Green
$docsDir = Join-Path $StagingDir "docs"
New-Item -ItemType Directory -Path $docsDir -Force | Out-Null

$docFiles = @("index.md", "SUMMARY.md", "installation.md", "performance_guide.md", "vulkan_game_engines.md", "webassembly.md", "migration.md")
foreach ($df in $docFiles) {
    $srcFile = Join-Path $RootDir "docs/$df"
    if (Test-Path $srcFile) {
        Copy-Item $srcFile -Destination $docsDir -Force
    }
}
$docDirs = @("tutorial", "reference", "stdlib", "cookbook")
foreach ($dd in $docDirs) {
    $srcDir = Join-Path $RootDir "docs/$dd"
    if (Test-Path $srcDir) {
        Copy-Item $srcDir -Destination $docsDir -Recurse -Force
    }
}

# 7. Copiar Extensão do VS Code (.vsix) e Instaladores
Write-Host "--> Copiando extensão oficial do VS Code e instaladores..." -ForegroundColor Green
$editorsDir = Join-Path $StagingDir "editors"
New-Item -ItemType Directory -Path $editorsDir -Force | Out-Null
$vsixSrc = Join-Path $RootDir "editors/vscode/hphl-1.0.0.vsix"
if (Test-Path $vsixSrc) {
    Copy-Item $vsixSrc -Destination (Join-Path $editorsDir "hphl-1.0.0.vsix") -Force
}
$batSrc = Join-Path $RootDir "editors/vscode/install_extension.bat"
if (Test-Path $batSrc) {
    Copy-Item $batSrc -Destination (Join-Path $editorsDir "install_extension.bat") -Force
}
$ps1Src = Join-Path $RootDir "editors/vscode/install_extension.ps1"
if (Test-Path $ps1Src) {
    Copy-Item $ps1Src -Destination (Join-Path $editorsDir "install_extension.ps1") -Force
}

# 8. Criar Instalador PowerShell (install.ps1)
Write-Host "--> Gerando instalador automático install.ps1..." -ForegroundColor Green
$installPs1Content = @'
# ==============================================================================
# HP-HL SDK — Instalador Automático de Ambiente (Windows)
# ==============================================================================
$ErrorActionPreference = "Stop"
$SdkDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BinDir = Join-Path $SdkDir "bin"

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Instalando HP-HL SDK v1.0.0 no ambiente de usuario" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Diretorio do SDK: $SdkDir"

# 1. Definir HPHL_HOME
[Environment]::SetEnvironmentVariable("HPHL_HOME", $SdkDir, "User")
Write-Host "[OK] HPHL_HOME definido para: $SdkDir" -ForegroundColor Green

# 2. Adicionar %HPHL_HOME%\bin ao PATH
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$BinDir*") {
    $newPath = "$BinDir;$userPath"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "[OK] $BinDir adicionado ao PATH do usuario." -ForegroundColor Green
} else {
    Write-Host "[INFO] $BinDir ja estava no PATH." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "  Instalacao concluida com sucesso!" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Abra um novo terminal (PowerShell ou CMD) e execute:"
Write-Host "  hphlc --version" -ForegroundColor Yellow
Write-Host ""
Write-Host "Para instalar a extensao no VS Code:"
Write-Host "  code --install-extension editors/hphl-1.0.0.vsix" -ForegroundColor Yellow
Write-Host ""
'@
Set-Content -Path (Join-Path $StagingDir "install.ps1") -Value $installPs1Content -Encoding UTF8

# 9. Criar Instalador Batch de Duplo Clique (install.bat)
$installBatContent = @"
@echo off
title Instalador HP-HL SDK
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
pause
"@
Set-Content -Path (Join-Path $StagingDir "install.bat") -Value $installBatContent -Encoding ASCII

# 9.1 Copiar Instalador Linux/macOS (install.sh)
Write-Host "--> Copiando instalador Linux/macOS install.sh..." -ForegroundColor Green
$installShSrc = Join-Path $RootDir "tools/installers/install.sh"
if (Test-Path $installShSrc) {
    Copy-Item $installShSrc -Destination (Join-Path $StagingDir "install.sh") -Force
}

# 9.2 Copiar Instalador Gráfico (setup.bat e gui_installer.ps1)
Write-Host "--> Copiando instalador gráfico moderno (setup.bat)..." -ForegroundColor Green
$setupBatSrc = Join-Path $RootDir "tools/installers/setup.bat"
if (Test-Path $setupBatSrc) {
    Copy-Item $setupBatSrc -Destination (Join-Path $StagingDir "setup.bat") -Force
}
$guiPs1Src = Join-Path $RootDir "tools/installers/gui_installer.ps1"
if (Test-Path $guiPs1Src) {
    Copy-Item $guiPs1Src -Destination (Join-Path $StagingDir "gui_installer.ps1") -Force
}

# 9.3 Copiar Identidade Visual (assets/)
Write-Host "--> Copiando identidade visual oficial (assets)..." -ForegroundColor Green
$assetsSrc = Join-Path $RootDir "assets"
if (Test-Path $assetsSrc) {
    Copy-Item $assetsSrc -Destination $StagingDir -Recurse -Force
}

# 10. Criar README do SDK
$sdkReadmeContent = @"
# HP-HL Software Development Kit (SDK) v$Version

Bem-vindo ao SDK oficial da linguagem **HP-HL (High-Performance High-Level Language)**.

## 🚀 Instalação Rápida

### Opção 1: Interface Gráfica (Recomendado no Windows)
Dê um duplo-clique no arquivo `setup.bat` para abrir o instalador visual com seleção de opcionais!

### Opção 2: Linha de Comando / Silencioso
- **Windows:** Duplo-clique em `install.bat` ou execute `powershell -File install.ps1`.
- **Linux / macOS:** Execute `./install.sh`.

Após a instalação, abra um novo terminal e verifique:
```bash
hphlc --version
```

## 💻 Suporte ao VS Code & Cursor

O SDK inclui a extensão oficial com realce de sintaxe, LSP e depurador DAP.
Para instalar:
```bash
code --install-extension editors/hphl-1.0.0.vsix
```

## 📁 Conteúdo do SDK

- `bin/`: Compilador nativo `hphlc.exe`.
- `runtime/`: Cabeçalhos e runtime de alta performance com Immix GC.
- `stdlib/`: Biblioteca padrão em HP-HL.
- `examples/`: Exemplos funcionais de física, matemática, raytracer e hello world.
- `docs/`: Documentação offline completa.
- `editors/`: Pacote da extensão do VS Code / Cursor (.vsix).

Documentação online: https://hphl.dev
Repositório: https://github.com/Lettucevy/hphl
"@
Set-Content -Path (Join-Path $StagingDir "README.md") -Value $sdkReadmeContent -Encoding UTF8

# Copiar Licença
if (Test-Path (Join-Path $RootDir "LICENSE")) {
    Copy-Item (Join-Path $RootDir "LICENSE") -Destination $StagingDir -Force
}

# 11. Compactar em .zip se solicitado
if (-not $SkipZip) {
    $zipPath = Join-Path $DistDir "$SdkName.zip"
    Write-Host "--> Compactando $zipPath..." -ForegroundColor Green
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path "$StagingDir/*" -DestinationPath $zipPath -Force
    $zipSize = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
    Write-Host "--> [SUCESSO] Pacote gerado: $zipPath ($zipSize MB)" -ForegroundColor Green
}

# 12. Gerar Instalador Executável .exe (Inno Setup)
$isccPaths = @(
    "C:\Users\lettuce\AppData\Local\Programs\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)
$iscc = $isccPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    $cmd = Get-Command "iscc.exe" -ErrorAction SilentlyContinue
    if ($cmd) { $iscc = $cmd.Source }
}

if ($iscc) {
    Write-Host "--> Compilando instalador oficial Windows (.exe) com Inno Setup..." -ForegroundColor Green
    $issFile = Join-Path $RootDir "tools/installers/hphl_installer.iss"
    if (Test-Path $issFile) {
        & $iscc $issFile | Out-Null
        $exePath = Join-Path $DistDir "hphl-setup-v$Version-windows-x64.exe"
        if (Test-Path $exePath) {
            $exeSize = [math]::Round((Get-Item $exePath).Length / 1MB, 2)
            Write-Host "--> [SUCESSO] Instalador .EXE gerado: $exePath ($exeSize MB)" -ForegroundColor Green
        }
    }
}

# 13. Gerar Instalador .msi (WiX)
$wixCmd = Get-Command "wix.exe" -ErrorAction SilentlyContinue
if ($wixCmd) {
    Write-Host "--> Compilando instalador Windows Installer (.msi) com WiX..." -ForegroundColor Green
    $wxsFile = Join-Path $RootDir "tools/installers/hphl_installer.wxs"
    if (Test-Path $wxsFile) {
        $msiPath = Join-Path $DistDir "hphl-v$Version-windows-x64.msi"
        & wix build $wxsFile -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext -arch x64 -o $msiPath | Out-Null
        if (Test-Path $msiPath) {
            $msiSize = [math]::Round((Get-Item $msiPath).Length / 1MB, 2)
            Write-Host "--> [SUCESSO] Instalador .MSI gerado: $msiPath ($msiSize MB)" -ForegroundColor Green
        }
    }
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "  HP-HL SDK v$Version empacotado com sucesso!" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
