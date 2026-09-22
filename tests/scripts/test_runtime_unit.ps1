#!/usr/bin/env powershell
# tests/scripts/test_runtime_unit.ps1 — Unit tests for the C runtime
# Compiles and runs the C unit test (_unbuf.c and others)

param(
    [switch]$Verbose,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$UnitDir = Join-Path $RootDir "tests\unit"
$OutDir = New-Item -ItemType Directory -Force -Path (Join-Path $RootDir "tests\_out_unit")

if ($Help) {
    Write-Host @"
HP-HL Runtime Unit Tests
Usage: powershell -File tests/scripts/test_runtime_unit.ps1
"@
    exit 0
}

if (-not (Test-Path $UnitDir)) {
    Write-Host "ERROR: tests/unit/ not found" -ForegroundColor Red
    exit 1
}

$pass = 0
$fail = 0
$failures = @()

$runtimeIncludes = Join-Path $RootDir "compiler\src\runtime"

Get-ChildItem -Path $UnitDir -Filter "*.c" | ForEach-Object {
    $name = $_.BaseName
    $src = $_.FullName
    $exe = Join-Path $OutDir "$name.exe"

    if ($Verbose) { Write-Host "  [unit] $name" -ForegroundColor Gray }

    # Compile the unit test with the runtime
    $gcc = "C:\msys64\ucrt64\bin\gcc.exe"
    $cmd = "& '$gcc' -std=c11 -O2 -I '$runtimeIncludes' '$src' '$runtimeIncludes\main.c' -o '$exe'"
    Invoke-Expression $cmd 2>&1 | Out-Null
    $compileResult = $LASTEXITCODE

    if ($compileResult -ne 0) {
        Write-Host "  [FAIL] $name (compile error)" -ForegroundColor Red
        $failures += $name
        $fail++
        return
    }

    # Run the test
    & $exe 2>&1 | Out-Null
    $runResult = $LASTEXITCODE

    if ($runResult -eq 0) {
        Write-Host "  [PASS] $name" -ForegroundColor Green
        $pass++
    } else {
        Write-Host "  [FAIL] $name (exit=$runResult)" -ForegroundColor Red
        $failures += $name
        $fail++
    }
}

Write-Host ""
Write-Host "Unit Test Results: $pass passed, $fail failed" -ForegroundColor Cyan

if ($fail -gt 0) {
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

exit 0
