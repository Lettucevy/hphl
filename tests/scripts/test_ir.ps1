#!/usr/bin/env powershell
# tests/scripts/test_ir.ps1 — Tests the LLVM IR textual backend
# Usage: powershell -File tests/scripts/test_ir.ps1

param(
    [switch]$Verbose,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$CompilerExe = Join-Path $RootDir "compiler\bin\hphlc.exe"
$OutDir = New-Item -ItemType Directory -Force -Path (Join-Path $RootDir "tests\_out_ir")

if ($Help) {
    Write-Host @"
HP-HL IR Backend Test
Usage: powershell -File tests/scripts/test_ir.ps1

Compiles positive tests to LLVM IR textual format.
"@
    exit 0
}

if (-not (Test-Path $CompilerExe)) {
    Write-Host "ERROR: Compiler not found." -ForegroundColor Red
    exit 1
}

$testsDir = Join-Path $RootDir "tests\positive"

$pass = 0
$fail = 0
$failures = @()

Get-ChildItem -Path $testsDir -Filter "*.hphl" | ForEach-Object {
    $name = $_.BaseName
    $src = $_.FullName
    $irFile = Join-Path $OutDir "$name.ll"

    if ($Verbose) { Write-Host "  [ir] $name" -ForegroundColor Gray }

    & $CompilerExe $src -o $irFile --backend=ir --no-link 2>&1 | Out-Null
    $compileResult = $LASTEXITCODE

    if ($compileResult -ne 0) {
        Write-Host "  [FAIL] $name (compile error)" -ForegroundColor Red
        $failures += $name
        $fail++
        return
    }

    # Verify the .ll file is non-empty and has LLVM IR structure
    if ((Test-Path $irFile) -and ((Get-Item $irFile).Length -gt 0)) {
        $content = Get-Content $irFile -Raw
        if ($content -match "^\s*(define|declare)\s+") {
            Write-Host "  [PASS] $name" -ForegroundColor Green
            $pass++
        } else {
            Write-Host "  [FAIL] $name (no LLVM IR structure)" -ForegroundColor Red
            $failures += $name
            $fail++
        }
    } else {
        Write-Host "  [FAIL] $name (empty .ll file)" -ForegroundColor Red
        $failures += $name
        $fail++
    }
}

Write-Host ""
Write-Host "IR Results: $pass passed, $fail failed" -ForegroundColor Cyan

if ($fail -gt 0) {
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

exit 0
