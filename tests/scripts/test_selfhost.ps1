#!/usr/bin/env powershell
# tests/scripts/test_selfhost.ps1 — Tests the selfhosting capability of HP-HL
# The compiler (hphlc) should be able to compile a subset of itself

param(
    [switch]$Verbose,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$CompilerExe = Join-Path $RootDir "compiler\bin\hphlc.exe"
$SelfhostDir = Join-Path $RootDir "selfhost"

if ($Help) {
    Write-Host @"
HP-HL Selfhost Test
Usage: powershell -File tests/scripts/test_selfhost.ps1 [options]

Verifies that the HP-HL compiler can build a subset of itself.
"@
    exit 0
}

if (-not (Test-Path $CompilerExe)) {
    Write-Host "ERROR: Compiler not found. Build first with: powershell -File tools/build.ps1" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $SelfhostDir)) {
    Write-Host "ERROR: selfhost/ directory not found at $SelfhostDir" -ForegroundColor Red
    exit 1
}

$pass = 0
$fail = 0
$failures = @()

$outDir = New-Item -ItemType Directory -Force -Path "$SelfhostDir\_out"

Write-Host "Selfhost Test Suite" -ForegroundColor Cyan
Write-Host "==================" -ForegroundColor Cyan
Write-Host ""

# Compile selfhost sources
Get-ChildItem -Path $SelfhostDir -Filter "*.hphl" | ForEach-Object {
    $name = $_.BaseName
    $exe = Join-Path $outDir "$name.exe"
    $src = $_.FullName

    # Skip library/module files that have no Main entry point
    $content = Get-Content $src -Raw
    if ($content -notmatch 'void\s+Main\s*\(') {
        if ($Verbose) { Write-Host "[skip]  $name (no Main)" -ForegroundColor DarkGray }
        return
    }

    if ($Verbose) { Write-Host "[compile] $name" -ForegroundColor Gray }

    & $CompilerExe $src -o $exe 2>&1 | Out-Null
    $compileResult = $LASTEXITCODE

    if ($compileResult -ne 0) {
        Write-Host "[FAIL] $name (compile error)" -ForegroundColor Red
        $failures += $name
        $fail++
        return
    }

    # Run the compiled program
    & $exe 2>&1 | Out-Null
    $runResult = $LASTEXITCODE

    if ($runResult -eq 0) {
        Write-Host "[PASS] $name" -ForegroundColor Green
        $pass++
    } else {
        Write-Host "[FAIL] $name (exit=$runResult)" -ForegroundColor Red
        $failures += $name
        $fail++
    }
}

Write-Host ""
Write-Host "Results: $pass passed, $fail failed" -ForegroundColor Cyan

if ($fail -gt 0) {
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

# Phase 2 of item 3 (true self-hosting): functional E2E — programs built by
# the selfhost must produce output identical to hphlc-built programs.
Write-Host ""
$E2EScript = Join-Path $ScriptDir "test_selfhost_e2e.ps1"
if (Test-Path $E2EScript) {
    $e2eArgs = @("-ExecutionPolicy", "Bypass", "-File", $E2EScript)
    if ($Verbose) { $e2eArgs += "-Verbose" }
    & powershell @e2eArgs
    if ($LASTEXITCODE -ne 0) { exit 1 }
} else {
    Write-Host "SKIP: test_selfhost_e2e.ps1 not found" -ForegroundColor Yellow
}

exit 0
