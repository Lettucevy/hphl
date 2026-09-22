#!/usr/bin/env powershell
# tests/scripts/run_all.ps1 — Runs all HP-HL test suites
# Usage: powershell -File tests/scripts/run_all.ps1

param(
    [switch]$SkipSelfhost,
    [switch]$SkipBackends,
    [switch]$SkipWasm,
    [switch]$SkipDebug,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$CompilerExe = Join-Path $RootDir "compiler\bin\hphlc.exe"

if ($Help) {
    Write-Host @"
HP-HL Master Test Runner
Usage: powershell -File tests/scripts/run_all.ps1 [options]

Options:
  -SkipSelfhost    Skip the selfhost tests
  -SkipBackends    Skip the backend tests (x64/llvm/ir)
  -SkipWasm        Skip the WASM tests
  -SkipDebug       Skip the debug tests
  -Help            Show this help
"@
    exit 0
}

# Build first
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "HP-HL Master Test Runner" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $CompilerExe)) {
    Write-Host "Building compiler first..." -ForegroundColor Yellow
    & powershell -File (Join-Path $RootDir "tools\build.ps1") 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        exit 1
    }
}

$suites = @(
    @{ Name = "Positive (x64)"; Script = "test.ps1"; Args = @("-Suite", "positive") }
    @{ Name = "Negative";       Script = "test.ps1"; Args = @("-Suite", "negative") }
)

if (-not $SkipBackends) {
    $suites += @(
        @{ Name = "Backend (x64)";    Script = "test_backends.ps1"; Args = @("-Backend", "x64") }
        @{ Name = "Backend (LLVM)";   Script = "test_backends.ps1"; Args = @("-Backend", "llvm") }
        @{ Name = "Backend (IR)";     Script = "test_backends.ps1"; Args = @("-Backend", "ir") }
    )
}

if (-not $SkipWasm) {
    $suites += @{ Name = "WASM"; Script = "test_wasm.ps1"; Args = @() }
}

if (-not $SkipDebug) {
    $suites += @{ Name = "Debug"; Script = "test_debug.ps1"; Args = @() }
}

if (-not $SkipSelfhost) {
    $suites += @{ Name = "Selfhost"; Script = "test_selfhost.ps1"; Args = @() }
}

$totalPass = 0
$totalFail = 0
$suiteResults = @()

foreach ($suite in $suites) {
    $scriptPath = Join-Path $ScriptDir $suite.Script
    Write-Host ""
    Write-Host "Running: $($suite.Name)" -ForegroundColor Cyan
    Write-Host ("-" * 50) -ForegroundColor Gray

    if (-not (Test-Path $scriptPath)) {
        Write-Host "  Script not found: $scriptPath" -ForegroundColor Yellow
        continue
    }

    & powershell -File $scriptPath @($suite.Args) 2>&1
    $result = $LASTEXITCODE
    $suiteResults += @{ Name = $suite.Name; Pass = ($result -eq 0) }
    if ($result -ne 0) {
        $totalFail++
    } else {
        $totalPass++
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Master Test Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
foreach ($r in $suiteResults) {
    $status = if ($r.Pass) { "[PASS]" } else { "[FAIL]" }
    $color = if ($r.Pass) { "Green" } else { "Red" }
    Write-Host "  $status $($r.Name)" -ForegroundColor $color
}
Write-Host ""
Write-Host "Total: $totalPass passed, $totalFail failed" -ForegroundColor $(if ($totalFail -eq 0) { "Green" } else { "Red" })

if ($totalFail -gt 0) {
    exit 1
}

exit 0
