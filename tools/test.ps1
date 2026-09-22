#!/usr/bin/env powershell
# tools/test.ps1 — Runs the HP-HL test suite
# Usage: powershell -File tools/test.ps1 [-Suite positive|negative|all|selfhost|x64|llvm|ir|aarch64|wasm]
# Default: all

param(
    [ValidateSet("positive", "negative", "all", "backends", "selfhost", "x64", "llvm", "ir", "aarch64", "wasm", "debug", "lsp", "dap")]
    [string]$Suite = "all",
    [switch]$Verbose,
    [switch]$Help
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$CompilerExe = Join-Path $RootDir "compiler\bin\hphlc.exe"
$TestsDir = Join-Path $RootDir "tests"

if ($Help) {
    Write-Host @"
HP-HL Test Runner
Usage: powershell -File tools/test.ps1 [options]

Options:
  -Suite <name>     Test suite to run: positive, negative, all, backends, selfhost, x64, llvm, ir, aarch64, wasm, debug, lsp, dap (default: all)
  -Verbose          Verbose output
  -Help             Show this help
"@
    exit 0
}

if (-not (Test-Path $CompilerExe)) {
    Write-Host "ERROR: Compiler not found at $CompilerExe" -ForegroundColor Red
    Write-Host "Run 'powershell -File tools/build.ps1' first." -ForegroundColor Yellow
    exit 1
}

$pass = 0
$fail = 0
$skipped = 0
$failures = @()

function Run-Test {
    param(
        [string]$File,
        [string]$Expected = "ok",
        [string]$Backend = "x64"
    )

    # Determine expected outcome based on test directory
    # Tests in negative/ should expect error, positive/ should expect ok
    $testDir = Split-Path (Split-Path $File -Parent) -Leaf
    if ($testDir -eq "negative") {
        $Expected = "error"
    }

    $name = [System.IO.Path]::GetFileNameWithoutExtension($File)
    $baseName = [System.IO.Path]::GetFileName($File)
    $exeBase = if ($name) { $name } else { [System.IO.Path]::GetFileNameWithoutExtension($baseName) }

    if ($Verbose) {
        Write-Host "[run] $baseName" -ForegroundColor Gray
    }

    # M30 v0.89.0: detecção de imports (single-file vs project).
    # Tests em tests/positive com `import "X.hphl"` ou `import Mod;` exigem
    # resolução de imports; single-file falha. Criamos um .hphproj
    # temporário no cwd (_out) para que paths relativos da ImportLoader
    # (cwd-relative) funcionem, e usamos paths relativos ao cwd do projeto.
    $src = Get-Content $File -Raw
    $needsProject = ($src -match '^\s*import\s+"') -or ($src -match '^\s*import\s+\w')

    $compilerArgs = @()
    $tempProj = $null
    $prevCwd = $null
    if ($needsProject) {
        # Garante _out e cd nele (cwd do projeto; sources relativos partem
        # do cwd, então "../positive/foo.hphl" funciona e a ImportLoader
        # encontra "ModCore.hphl" via tests/positive/ModCore.hphl relativo).
        $outDir = Join-Path $TestsDir "_out"
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        $tempProj = Join-Path $outDir ("$name.hpproj")
        # Path relativo ao projeto (cwd = outDir)
        $fileRel = "..\positive\$name.hphl" -replace '\\','/'
        # JSON sem BOM (PowerShell Set-Content -Encoding UTF8 adiciona BOM que
        # o parser do hphlc não aceita). Usamos File.WriteAllText com UTF8NoBom.
        $projJson = '{"entry_task":"main","output":"_out/' + $name + '.exe","tasks":{"main":{"sources":["' + $fileRel + '"]}}}'
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($tempProj, $projJson, $utf8NoBom)
        $prevCwd = Get-Location
        Set-Location $outDir
        $compilerArgs = @("--project", $name + ".hpproj")
    } else {
        $outExt = switch ($Backend) {
            "ir"      { ".ll" }
            "aarch64" { ".cross.o" }
            "wasm"    { ".wasm" }
            default   { ".exe" }
        }
        $compilerArgs = @($File, "-o", "$TestsDir\_out\$name$outExt")
    }
    switch ($Backend) {
        "llvm"    { $compilerArgs += @("--backend", "llvm") }
        "ir"      { $compilerArgs += @("--backend", "ir", "--no-link") }
        "aarch64" { $compilerArgs += @("--backend", "llvm", "--target", "aarch64-linux-gnu") }
        "wasm"    { $compilerArgs += @("--backend", "llvm", "--target", "wasm32-unknown-unknown") }
    }

    $output = & $CompilerExe @compilerArgs 2>&1
    $exitCode = $LASTEXITCODE

    if ($prevCwd) { Set-Location $prevCwd }

    if ($tempProj -and (Test-Path $tempProj)) { Remove-Item $tempProj -ErrorAction SilentlyContinue }

    # M30 v0.89.0: testes negativos que devem panic em runtime
    # (ex.: bounds check, assert, divisão por zero) saem com exit 1
    # APÓS executar. O compilador retorna 0, mas a execução falha.
    # Executamos o binário gerado para validar o exit code real.
    if ($Expected -eq "error" -and $exitCode -eq 0) {
        $exe = [System.IO.Path]::Combine($TestsDir, "_out", "$name.exe")
        if (Test-Path $exe) {
            & $exe 2>&1 | Out-Null
            $exitCode = $LASTEXITCODE
            Remove-Item $exe -ErrorAction SilentlyContinue
            Remove-Item ([System.IO.Path]::Combine($TestsDir, "_out", "$name.tmp.s")) -ErrorAction SilentlyContinue
            Remove-Item ([System.IO.Path]::Combine($TestsDir, "_out", "$name.tmp.o")) -ErrorAction SilentlyContinue
        }
    }

    $success = $false
    switch ($Expected) {
        "ok" { $success = ($exitCode -eq 0) }
        "error" { $success = ($exitCode -ne 0) }
    }

    # Clean up generated artifacts
    Remove-Item ([System.IO.Path]::Combine($TestsDir, "_out", "$name.exe")) -ErrorAction SilentlyContinue
    Remove-Item ([System.IO.Path]::Combine($TestsDir, "_out", "$name*.tmp.s")) -ErrorAction SilentlyContinue
    Remove-Item ([System.IO.Path]::Combine($TestsDir, "_out", "$name*.tmp.o")) -ErrorAction SilentlyContinue
    Remove-Item ([System.IO.Path]::Combine($TestsDir, "_out", "$name.ll")) -ErrorAction SilentlyContinue
    Remove-Item ([System.IO.Path]::Combine($TestsDir, "_out", "$name*.cross.o")) -ErrorAction SilentlyContinue
    Remove-Item ([System.IO.Path]::Combine($TestsDir, "_out", "$name.wasm")) -ErrorAction SilentlyContinue

    if ($success) {
        $script:pass++
        if ($Verbose) {
            Write-Host "[PASS] $baseName" -ForegroundColor Green
        }
    } else {
        $script:fail++
        $script:failures += $baseName
        Write-Host "[FAIL] $baseName (exit=$exitCode, expected=$Expected)" -ForegroundColor Red
    }
}

function Run-Positive {
    $dir = Join-Path $TestsDir "positive"
    if (-not (Test-Path $dir)) {
        Write-Host "SKIP: positive/ not found" -ForegroundColor Yellow
        return
    }
    Get-ChildItem -Path $dir -Filter "*.hphl" | Where-Object { $_.Name -notlike "Mod*.hphl" } | ForEach-Object {
        Run-Test -File $_.FullName -Expected "ok"
    }
}

function Run-Negative {
    $dir = Join-Path $TestsDir "negative"
    if (-not (Test-Path $dir)) {
        Write-Host "SKIP: negative/ not found" -ForegroundColor Yellow
        return
    }
    $files = Get-ChildItem -Path $dir -Filter "*.hphl"
    foreach ($file in $files) {
        $expected = "error"
        Run-Test -File $file.FullName -Expected $expected
    }
}

function Run-Selfhost {
    $dir = Join-Path $TestsDir "selfhost"
    if (-not (Test-Path $dir)) {
        Write-Host "SKIP: selfhost/ not found (no selfhost tests)" -ForegroundColor Yellow
        return
    }
    Get-ChildItem -Path $dir -Filter "*.hphl" | ForEach-Object {
        Run-Test -File $_.FullName -Expected "ok"
    }
}

function Run-Backend {
    param([string]$Name)
    $dir = Join-Path $TestsDir $Name
    $files = @()
    if (Test-Path $dir) {
        $files = @(Get-ChildItem -Path $dir -Filter "*.hphl")
    }
    if ($files.Count -eq 0) {
        $posDir = Join-Path $TestsDir "positive"
        if (Test-Path $posDir) {
            $files = @(Get-ChildItem -Path $posDir -Filter "*.hphl" | Where-Object { $_.Name -notlike "Mod*.hphl" })
        }
    }
    if ($files.Count -eq 0) {
        Write-Host "SKIP: No tests found for backend $Name" -ForegroundColor Yellow
        return
    }
    Write-Host "Running backend '$Name' across $($files.Count) tests..." -ForegroundColor Cyan
    foreach ($f in $files) {
        Run-Test -File $f.FullName -Expected "ok" -Backend $Name
    }
}

New-Item -ItemType Directory -Force -Path "$TestsDir\_out" | Out-Null

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "HP-HL Test Suite: $Suite" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

switch ($Suite) {
    "positive"  { Run-Positive }
    "negative"  { Run-Negative }
    "selfhost"  { Run-Selfhost }
    "x64"       { Run-Backend "x64" }
    "llvm"      { Run-Backend "llvm" }
    "ir"        { Run-Backend "ir" }
    "aarch64"   { Run-Backend "aarch64" }
    "wasm"      { Run-Backend "wasm" }
    "backends" {
        Run-Backend "x64"
        Run-Backend "llvm"
        Run-Backend "ir"
        Run-Backend "aarch64"
        Run-Backend "wasm"
    }
    "all" {
        Run-Positive
        Run-Negative
        Run-Selfhost
    }
    default { Write-Host "Unknown suite: $Suite" -ForegroundColor Red; exit 1 }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Results: $($pass) passed, $($fail) failed" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

if ($fail -gt 0) {
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

exit 0
