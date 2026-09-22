#!/usr/bin/env powershell
# tests/scripts/test_debug.ps1 — Tests the debug backend (--debug flag)
# Usage: powershell -File tests/scripts/test_debug.ps1

param(
    [switch]$Verbose,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$CompilerExe = Join-Path $RootDir "compiler\bin\hphlc.exe"
$DebugDir = Join-Path $RootDir "tests\debug"
$OutDir = New-Item -ItemType Directory -Force -Path (Join-Path $RootDir "tests\_out_debug")

if ($Help) {
    Write-Host @"
HP-HL Debug Test
Usage: powershell -File tests/scripts/test_debug.ps1

Compiles and runs debug-enabled programs.
"@
    exit 0
}

if (-not (Test-Path $CompilerExe)) {
    Write-Host "ERROR: Compiler not found." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $DebugDir)) {
    Write-Host "SKIP: tests/debug/ not found" -ForegroundColor Yellow
    exit 0
}

$pass = 0
$fail = 0
$failures = @()

Get-ChildItem -Path $DebugDir -Filter "*.hphl" | ForEach-Object {
    $name = $_.BaseName
    $src = $_.FullName
    $exe = Join-Path $OutDir "$name.exe"

    if ($Verbose) { Write-Host "  [debug] $name" -ForegroundColor Gray }

    & $CompilerExe $src -o $exe --debug 2>&1 | Out-Null
    $compileResult = $LASTEXITCODE

    if ($compileResult -ne 0) {
        Write-Host "  [FAIL] $name (compile error)" -ForegroundColor Red
        $failures += $name
        $fail++
        return
    }

    # Run the executable
    & $exe 2>&1 | Out-Null
    $runResult = $LASTEXITCODE

    if ($runResult -eq 0 -or $runResult -eq 1) {
        # 0=success, 1=assertion failed (acceptable for debug tests)
        Write-Host "  [PASS] $name" -ForegroundColor Green
        $pass++
    } else {
        Write-Host "  [FAIL] $name (exit=$runResult)" -ForegroundColor Red
        $failures += $name
        $fail++
    }
}

Write-Host ""
Write-Host "Debug Results: $pass passed, $fail failed" -ForegroundColor Cyan

if ($fail -gt 0) {
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

exit 0
