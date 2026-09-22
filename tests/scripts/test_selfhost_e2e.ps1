#!/usr/bin/env powershell
# tests/scripts/test_selfhost_e2e.ps1 — End-to-end functional validation of the
# HP-HL self-host compiler (item 3: true self-hosting).
#
# For each corpus target, it verifies that the program compiled by the SELFHOST
# (selfhost/main.hphl -> .s -> .exe) produces byte-identical stdout to the same
# program compiled by the reference compiler (hphlc).
#
# Pipeline per target T:
#   1. reference: hphlc T -o ref.exe ; run ; capture stdout
#   2. selfhost:  set selfhost/target.txt=T ; run selfhost main.exe (-> selfhost_demo.s)
#   3. assemble:  gcc -c selfhost_demo.s ; link with runtime main.o + syslibs -> sh.exe
#   4. run sh.exe ; capture stdout ; compare with reference (PASS/FAIL)
#
# Usage: powershell -File tests/scripts/test_selfhost_e2e.ps1 [-Verbose]
#
# NOTE: corpus is limited to what the selfhost codegen supports (int, string,
# class, control flow). Float targets need the Phase-2 float codegen fix.

param(
    [switch]$Verbose,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$CompilerExe = Join-Path $RootDir "compiler\bin\hphlc.exe"
$SelfhostDir = Join-Path $RootDir "selfhost"
$TargetFile = Join-Path $SelfhostDir "target.txt"

if ($Help) {
    Write-Host @"
HP-HL Selfhost E2E Test (functional output parity)
Usage: powershell -File tests/scripts/test_selfhost_e2e.ps1 [options]

Compares stdout of selfhost-built programs vs hphlc-built programs.
"@
    exit 0
}

function Fail($msg) {
    Write-Host "ERROR: $msg" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $CompilerExe)) {
    Fail "Compiler not found. Build first with: powershell -File tools/build.ps1"
}

# --- gcc (MSYS2) ---
$Gcc = $null
foreach ($c in @("C:\msys64\ucrt64\bin\gcc.exe", "D:\msys64\ucrt64\bin\gcc.exe")) {
    if (Test-Path $c) { $Gcc = $c; break }
}
if (-not $Gcc) {
    $fromPath = (Get-Command gcc.exe -ErrorAction SilentlyContinue).Source
    if ($fromPath) { $Gcc = $fromPath }
}
if (-not $Gcc) {
    Fail "gcc.exe not found (MSYS2 ucrt64 required for assembling/linking)"
}

# --- runtime object (same one hphlc links: compiler/src/runtime/main.c -> main.o) ---
$RuntimeSrc = Join-Path $RootDir "compiler\src\runtime\main.c"
$RuntimeObj = Join-Path $RootDir "compiler\src\runtime\main.o"
if (-not (Test-Path $RuntimeSrc)) {
    Fail "runtime source not found at $RuntimeSrc"
}
$rebuildRt = $true
if (Test-Path $RuntimeObj) {
    $oTime = (Get-Item $RuntimeObj).LastWriteTime
    $rebuildRt = $false
    foreach ($dep in @($RuntimeSrc,
            (Join-Path $RootDir "compiler\src\runtime\runtime.h"),
            (Join-Path $RootDir "compiler\src\runtime\runtime_api.h"))) {
        if ((Test-Path $dep) -and ((Get-Item $dep).LastWriteTime -gt $oTime)) {
            $rebuildRt = $true
            break
        }
    }
}
if ($rebuildRt) {
    Write-Host "[e2e] rebuilding runtime object..." -ForegroundColor Gray
    & $Gcc -O2 -c $RuntimeSrc -o $RuntimeObj 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $RuntimeObj)) {
        Fail "failed to compile runtime $RuntimeSrc"
    }
}

# --- selfhost driver (selfhost/main.hphl -> main.exe), rebuilt if stale ---
$ShSrc = Join-Path $SelfhostDir "main.hphl"
$ShOut = New-Item -ItemType Directory -Force -Path (Join-Path $SelfhostDir "_out") | Out-Null
$ShOut = Join-Path $SelfhostDir "_out"
$ShExe = Join-Path $ShOut "e2e_main.exe"
$rebuildSh = $true
if (Test-Path $ShExe) {
    $exeTime = (Get-Item $ShExe).LastWriteTime
    $rebuildSh = $false
    foreach ($dep in @(Get-ChildItem -Path $SelfhostDir -Filter "*.hphl" -Recurse)) {
        if ($dep.LastWriteTime -gt $exeTime) { $rebuildSh = $true; break }
    }
}
if ($rebuildSh) {
    Write-Host "[e2e] building selfhost driver..." -ForegroundColor Gray
    & $CompilerExe $ShSrc -o $ShExe 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Fail "failed to compile selfhost driver $ShSrc"
    }
}

# --- corpus (targets the selfhost codegen supports today) ---
# NOTE: hello.hphl needs Phase-2 float codegen (literal .double + print_float).
$Corpus = @(
    "selfhost/demo.hphl",
    "selfhost/tst_if.hphl",
    "selfhost/tst_int.hphl",
    "selfhost/tst_enum.hphl",
    "tests/positive/hello.hphl"
)

$E2EOut = New-Item -ItemType Directory -Force -Path (Join-Path $RootDir "tests\_out\selfhost_e2e")
$DefaultTarget = "selfhost/demo.hphl"
$pass = 0
$fail = 0
$failures = @()

Write-Host "Selfhost E2E (functional parity vs hphlc)" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host ""

try {
    foreach ($rel in $Corpus) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($rel)
        $src = Join-Path $RootDir $rel
        if (-not (Test-Path $src)) {
            Write-Host "[FAIL] $rel (corpus file missing)" -ForegroundColor Red
            $fail++; $failures += $rel
            continue
        }

        # 1. reference output via hphlc
        $refExe = Join-Path $E2EOut "$name.ref.exe"
        & $CompilerExe $src -o $refExe 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[FAIL] $rel (hphlc reference failed to compile)" -ForegroundColor Red
            $fail++; $failures += $rel
            continue
        }
        $refOut = (& $refExe 2>&1 | Out-String)

        # 2. selfhost compile -> selfhost_demo.s (CWD must be repo root)
        $prevCwd = Get-Location
        Set-Location $RootDir
        try {
            Set-Content -Path $TargetFile -Value ($rel -replace '\\','/') -NoNewline -Encoding ascii
            $shLog = (& $ShExe 2>&1 | Out-String)
            $shExit = $LASTEXITCODE
        } finally {
            Set-Location $prevCwd
        }
        $genAsm = Join-Path $RootDir "selfhost_demo.s"
        if ($shExit -ne 0 -or -not (Test-Path $genAsm)) {
            Write-Host "[FAIL] $rel (selfhost compile failed, exit=$shExit)" -ForegroundColor Red
            if ($Verbose) { Write-Host $shLog -ForegroundColor Gray }
            $fail++; $failures += $rel
            continue
        }

        # 3. assemble + link
        $shObj = Join-Path $E2EOut "$name.sh.o"
        $shExe2 = Join-Path $E2EOut "$name.sh.exe"
        & $Gcc -c $genAsm -o $shObj 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[FAIL] $rel (selfhost .s failed to assemble)" -ForegroundColor Red
            $fail++; $failures += $rel
            continue
        }
        & $Gcc -o $shExe2 $shObj $RuntimeObj -lws2_32 -lwsock32 -lbcrypt -lz 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[FAIL] $rel (selfhost .exe failed to link)" -ForegroundColor Red
            $fail++; $failures += $rel
            continue
        }

        # 4. run + compare
        $shOut = (& $shExe2 2>&1 | Out-String)
        if ($shOut -eq $refOut) {
            Write-Host "[PASS] $rel" -ForegroundColor Green
            $pass++
        } else {
            Write-Host "[FAIL] $rel (stdout mismatch)" -ForegroundColor Red
            if ($Verbose) {
                Write-Host "--- reference ---" -ForegroundColor Gray
                Write-Host $refOut -ForegroundColor Gray
                Write-Host "--- selfhost ---" -ForegroundColor Gray
                Write-Host $shOut -ForegroundColor Gray
            }
            $fail++; $failures += $rel
        }
    }
} finally {
    # restore default target + cleanup root scratch assembly
    Set-Content -Path $TargetFile -Value $DefaultTarget -NoNewline -Encoding ascii
    Remove-Item (Join-Path $RootDir "selfhost_demo.s") -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "Results: $pass passed, $fail failed" -ForegroundColor Cyan

if ($fail -gt 0) {
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

exit 0
