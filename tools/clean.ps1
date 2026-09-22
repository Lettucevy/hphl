#!/usr/bin/env powershell
# tools/clean.ps1 — Cleans build artifacts and temporary files
# Usage: powershell -File tools/clean.ps1

param(
    [switch]$All,
    [switch]$Build,
    [switch]$Tests,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..")).Path

if ($Help) {
    Write-Host @"
HP-HL Cleanup Script
Usage: powershell -File tools/clean.ps1 [options]

Options:
  -All      Clean everything (build, tests, binaries)
  -Build    Clean build artifacts only
  -Tests    Clean test outputs only
  -Help     Show this help

Default: -Build
"@
    exit 0
}

if (-not ($All -or $Build -or $Tests)) {
    $Build = $true
}

$count = 0

if ($All -or $Build) {
    Write-Host "[clean] Build artifacts..." -ForegroundColor Yellow
    $dirs = @(
        "$RootDir\compiler\build",
        "$RootDir\compiler\bin",
        "$RootDir\build",
        "$RootDir\bin"
    )
    foreach ($d in $dirs) {
        if (Test-Path $d) {
            Remove-Item -Recurse -Force $d
            Write-Host "  removed: $d" -ForegroundColor Gray
            $count++
        }
    }
}

if ($All -or $Tests) {
    Write-Host "[clean] Test outputs..." -ForegroundColor Yellow
    $dirs = @(
        "$RootDir\tests\_out",
        "$RootDir\tests\positive\_out",
        "$RootDir\tests\negative\_out"
    )
    foreach ($d in $dirs) {
        if (Test-Path $d) {
            Remove-Item -Recurse -Force $d
            Write-Host "  removed: $d" -ForegroundColor Gray
            $count++
        }
    }
    # Remove .exe artifacts in tests
    Get-ChildItem -Path $RootDir\tests -Recurse -Filter "*.exe" -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -Force $_.FullName
        Write-Host "  removed: $($_.FullName)" -ForegroundColor Gray
        $count++
    }
    # Remove .o artifacts
    Get-ChildItem -Path $RootDir\tests -Recurse -Filter "*.o" -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -Force $_.FullName
        Write-Host "  removed: $($_.FullName)" -ForegroundColor Gray
        $count++
    }
    # Remove .ll/.s artifacts
    Get-ChildItem -Path $RootDir\tests -Recurse -Include "*.ll", "*.s" -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -Force $_.FullName
        Write-Host "  removed: $($_.FullName)" -ForegroundColor Gray
        $count++
    }
}

if ($All) {
    Write-Host "[clean] Loose binaries in root..." -ForegroundColor Yellow
    Get-ChildItem -Path $RootDir -File -ErrorAction SilentlyContinue | Where-Object {
        $_.Extension -in @(".exe", ".o", ".ll", ".s", ".bak")
    } | ForEach-Object {
        Remove-Item -Force $_.FullName
        Write-Host "  removed: $($_.FullName)" -ForegroundColor Gray
        $count++
    }
    # Sprint 4 K1+K2: also clean examples/benchmarks artifacts
    Write-Host "[clean] examples/benchmarks artifacts..." -ForegroundColor Yellow
    Get-ChildItem -Path $RootDir\examples\benchmarks -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
        $_.Extension -in @(".exe", ".o", ".ll", ".s", ".bak")
    } | ForEach-Object {
        Remove-Item -Force $_.FullName
        Write-Host "  removed: $($_.FullName)" -ForegroundColor Gray
        $count++
    }
}

Write-Host ""
Write-Host "Cleaned $count item(s)" -ForegroundColor Green
