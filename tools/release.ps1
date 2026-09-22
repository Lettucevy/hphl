#!/usr/bin/env powershell
# tools/release.ps1 - Build distributable binaries for HP-HL
# Usage: powershell -File tools/release.ps1 [-OutputDir dist]
# Produz:
#   dist/hphlc-windows-x64.zip
#   dist/hphlc-linux-x64.tar.gz
#   dist/hphlc-macos-x64.tar.gz
# Cada arquivo contem:
#   bin/hphlc[.exe]        -- o compilador
#   runtime/VERSION         -- versao de ABI
#   runtime/README.md       -- como distribuir o runtime junto
#   examples/hello/         -- exemplo smoke test

param(
    [string]$OutputDir = "dist",
    [string]$Version = "v0.93.0",
    [switch]$SkipLinux = $false,
    [switch]$SkipMacos = $false,
    [switch]$SkipWindows = $false,
    [switch]$Help
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$ReleaseDir = Join-Path $RootDir $OutputDir

if ($Help) {
    Write-Host @"
HP-HL Release Script
Usage: powershell -File tools/release.ps1 [options]

Options:
  -OutputDir <dir>   Output directory (default: dist)
  -Version <ver>     Version string (default: v0.93.0)
  -SkipLinux         Skip Linux build
  -SkipMacos         Skip macOS build
  -SkipWindows       Skip Windows build
  -Help              Show this help

Builds distributable archives for the current platform (or all three
on a CI runner with cross-compilation).
"@
    exit 0
}

# Detecta plataforma atual
$Platform = "unknown"
if ($IsWindows -or $env:OS -eq "Windows_NT") { $Platform = "windows" }
elseif ($IsLinux) { $Platform = "linux" }
elseif ($IsMacOS) { $Platform = "macos" }

Write-Host "[release] HP-HL $Version" -ForegroundColor Green
Write-Host "[release] Platform: $Platform"
Write-Host "[release] Output: $ReleaseDir"

# Cria diretorio
if (-not (Test-Path $ReleaseDir)) {
    New-Item -ItemType Directory -Path $ReleaseDir | Out-Null
}

# Funcao: empacota um build num archive
function Package-Build {
    param(
        [string]$PlatformName,
        [string]$BinaryName,
        [string]$ArchiveExt
    )
    $staging = Join-Path $ReleaseDir "staging-$PlatformName"
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
    New-Item -ItemType Directory -Path $staging | Out-Null

    # Copia binario
    $srcBin = Join-Path $RootDir "compiler/bin/$BinaryName"
    if (-not (Test-Path $srcBin)) {
        Write-Host "  ERROR: binary not found: $srcBin" -ForegroundColor Red
        return $null
    }
    Copy-Item $srcBin -Destination (Join-Path $staging $BinaryName)
    # Copia VERSION
    Copy-Item (Join-Path $RootDir "runtime/VERSION") -Destination $staging
    # Copia exemplo hello
    $exHello = Join-Path $staging "examples/hello"
    New-Item -ItemType Directory -Path $exHello -Force | Out-Null
    Copy-Item (Join-Path $RootDir "tests/positive/hello.hphl") -Destination $exHello

    # Cria README
    $readme = @"
HP-HL $Version (platform: $PlatformName)
=============================================

The compiler and runtime are bundled together. To run a program:

  ./$BinaryName examples/hello/hello.hphl -o hello
  ./hello

ABI version: see runtime/VERSION
Source: https://github.com/Lettucevy/hphl
Docs:   https://hphl.dev/docs
"@
    Set-Content -Path (Join-Path $staging "README.md") -Value $readme

    # Cria archive
    $archiveName = "hphlc-$Version-$PlatformName-x64.$ArchiveExt"
    $archivePath = Join-Path $ReleaseDir $archiveName
    if ($ArchiveExt -eq "zip") {
        Compress-Archive -Path "$staging/*" -DestinationPath $archivePath -Force
    } else {
        # tar.gz via tar nativo
        $prevLoc = Get-Location
        Set-Location $ReleaseDir
        tar -czf $archiveName -C staging-$PlatformName .
        Set-Location $prevLoc
    }
    Remove-Item -Recurse -Force $staging
    Write-Host "  created: $archivePath" -ForegroundColor Green
    return $archivePath
}

# Constroi para a plataforma atual
Write-Host "[release] Building for $Platform..."
$buildScript = Join-Path $RootDir "tools/build.ps1"
& powershell -ExecutionPolicy Bypass -File $buildScript 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ERROR: build failed for $Platform" -ForegroundColor Red
    exit 1
}

# Empacota
switch ($Platform) {
    "windows" {
        if (-not $SkipWindows) {
            Package-Build "windows" "hphlc.exe" "zip" | Out-Null
        }
    }
    "linux" {
        if (-not $SkipLinux) {
            Package-Build "linux" "hphlc" "tar.gz" | Out-Null
        }
    }
    "macos" {
        if (-not $SkipMacos) {
            Package-Build "macos" "hphlc" "tar.gz" | Out-Null
        }
    }
    default {
        Write-Host "  WARNING: unknown platform, skipping packaging" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "[release] Done. Files in $ReleaseDir :" -ForegroundColor Green
Get-ChildItem $ReleaseDir -File | ForEach-Object {
    Write-Host "  $($_.Name) ($([math]::Round($_.Length / 1KB, 1)) KB)"
}
