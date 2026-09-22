#!/usr/bin/env powershell
# tests/scripts/test_backends.ps1 — Delegates backend tests to tools/test.ps1
# Usage: powershell -File tests/scripts/test_backends.ps1 [-Backend <name>]

param(
    [ValidateSet("x64", "llvm", "ir", "aarch64", "wasm", "all")]
    [string]$Backend = "all",
    [switch]$Verbose,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$TestScript = Join-Path $RootDir "tools\test.ps1"

if ($Help) {
    Write-Host @"
HP-HL Backend Tests
Usage: powershell -File tests/scripts/test_backends.ps1 [-Backend <name>]

Backends: x64, llvm, ir, aarch64, wasm, all (default)
"@
    exit 0
}

$suite = if ($Backend -eq "all") { "backends" } else { $Backend }
$passThru = @("-ExecutionPolicy", "Bypass", "-File", $TestScript, "-Suite", $suite)
if ($Verbose) { $passThru += "-Verbose" }

& powershell @passThru
exit $LASTEXITCODE

