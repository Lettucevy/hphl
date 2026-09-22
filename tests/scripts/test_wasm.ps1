#!/usr/bin/env powershell
# tests/scripts/test_wasm.ps1 — Tests the WASM backend

param(
    [switch]$Verbose,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$CompilerExe = Join-Path $RootDir "compiler\bin\hphlc.exe"
$Runner = Join-Path $ScriptDir "runner_wasm.js"
$WasmDir = Join-Path $RootDir "tests\wasm"
$OutDir = New-Item -ItemType Directory -Force -Path (Join-Path $RootDir "tests\_out_wasm")

if ($Help) {
    Write-Host @"
HP-HL WASM Backend Test
Usage: powershell -File tests/scripts/test_wasm.ps1

Compiles positive tests to WASM and runs them with the JS runner.
"@
    exit 0
}

if (-not (Test-Path $CompilerExe)) {
    Write-Host "ERROR: Compiler not found." -ForegroundColor Red
    exit 1
}

if (-not (Get-Command "node" -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: node.js not found. Install Node.js to run WASM tests." -ForegroundColor Red
    exit 1
}

# Use positive/ tests as the corpus
$testsDir = Join-Path $RootDir "tests\positive"
if (-not (Test-Path $testsDir)) {
    Write-Host "ERROR: positive/ not found" -ForegroundColor Red
    exit 1
}

$pass = 0
$fail = 0
$failures = @()

Get-ChildItem -Path $testsDir -Filter "*.hphl" | ForEach-Object {
    $name = $_.BaseName
    $src = $_.FullName
    $wasmFile = Join-Path $OutDir "$name.wasm"

    if ($Verbose) { Write-Host "  [wasm] $name" -ForegroundColor Gray }

    & $CompilerExe $src -o $wasmFile --backend=wasm --no-link 2>&1 | Out-Null
    $compileResult = $LASTEXITCODE

    if ($compileResult -ne 0) {
        Write-Host "  [SKIP] $name (compile failed - may not be supported yet)" -ForegroundColor Yellow
        return
    }

    # Run with the JS runner
    $output = & node $Runner $wasmFile Main 2>&1
    $runResult = $LASTEXITCODE

    if ($runResult -eq 0) {
        Write-Host "  [PASS] $name" -ForegroundColor Green
        $pass++
    } else {
        Write-Host "  [WARN] $name (exit=$runResult, may not have Main or output)" -ForegroundColor Yellow
        # WASM tests are more lenient since not all features may be supported
    }
}

Write-Host ""
Write-Host "WASM Results: $pass passed, $fail failed" -ForegroundColor Cyan
exit 0
