# run_tests_llvm.ps1 — suite do backend LLVM API real (--backend llvm) do
# compilador HP-HL (Milestone 3, etapa final; -O via pass manager no M4)
#
# Uso:  powershell -ExecutionPolicy Bypass -File run_tests_llvm.ps1 [-Opt N]
#       -Opt 0..3: nível de otimização repassado ao hphlc (padrão: 0)
#
# Requisitos:
#   - hphlc compilado (rode `make` em compiler/) — linkado contra libLLVM-22
#   - gcc.exe no PATH (MSYS2 ucrt64) — o próprio hphlc linka o objeto com gcc
#
# O script reusa os casos (name + expected) do run_tests.ps1 — os positivos são a
# FONTE ÚNICA DE VERDADE (expected em linha única `= "..."` ou bloco `= @"..."`):
# compila cada exemplo com `--backend llvm` (parse + verify + object file via
# C API do LLVM, link com gcc) e compara a saída com o esperado.

param([int]$Opt = 0)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$root = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $root "compiler\hphlc.exe"
$examples = Join-Path $root "examples"
$tmp = Join-Path $root "tests\_out_llvm"

if (-not (Test-Path $compiler)) {
    Write-Host "ERRO: compilador não encontrado em $compiler (rode 'make' em compiler/)" -ForegroundColor Red
    exit 1
}

# MSYS2 ucrt64 no PATH (se existir) — gcc para o link final
$ucrt = "C:\msys64\ucrt64\bin"
if (Test-Path $ucrt) { $env:PATH = $ucrt + ";" + $env:PATH }
if (-not (Get-Command gcc.exe -ErrorAction SilentlyContinue)) {
    Write-Host "ERRO: gcc.exe não encontrado no PATH (MSYS2 ucrt64)" -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# --- extrai os casos positivos (name + expected) do run_tests.ps1 ---
$tc = Get-Content (Join-Path $root "tests\run_tests.ps1")
# casos que dependem de dispatch virtual (ainda x64-only)
$names = @()
$x64Only = @()
$expectedMap = @{}
$curName = $null
$curBlock = $null
for ($i = 0; $i -lt $tc.Count; $i++) {
    $line = $tc[$i]
    if ($line -match '^\s*name = "([^"]+)"') {
        $curName = $Matches[1]
        if ($x64Only -notcontains $curName) { $names += $curName }
        $curBlock = @()
        $inBlock = $false
        continue
    }
    if ($line -match '^\s*expected = "([^"]*)"') {
        # forma de linha única: expected = "texto"
        $expectedMap[$curName] = $Matches[1]
        continue
    }
    if ($line -match '^\s*expected = @"') {
        $inBlock = $true
        continue
    }
    if ($inBlock) {
        if ($line -match '^"@') {
            $expectedMap[$curName] = ($curBlock -join "`n")
            $inBlock = $false
            continue
        }
        $curBlock += $line
    }
}

$passed = 0; $failed = 0
foreach ($name in $names) {
    $src = Join-Path $examples ($name + ".hphl")
    if (-not (Test-Path $src)) {
        Write-Host "=== $name ===  SKIP (sem fonte em examples)" -ForegroundColor Yellow
        continue
    }
    $exe = Join-Path $tmp ("_llvm_" + $name + ".exe")
    Write-Host "=== $name ===" -ForegroundColor Cyan

    & $compiler $src --backend llvm "-O$Opt" -o $exe 2>$null | Out-Null
    $crc = $LASTEXITCODE
    if ($crc -ne 0) {
        Write-Host "  FALHA: compilação --backend llvm retornou $crc" -ForegroundColor Red
        $failed++
        continue
    }
    $out = @(cmd /c $exe)
    $erc = $LASTEXITCODE
    if ($erc -lt 0 -or $erc -ge 100) {
        Write-Host "  FALHA: crash em runtime (exit $erc)" -ForegroundColor Red
        $failed++
        continue
    }
    $expected = $expectedMap[$name]
    if ($null -eq $expected) {
        Write-Host "  SEM BLOCO ESPERADO (nao conta como falha)" -ForegroundColor Yellow
        continue
    }
    $actual = ($out -join "`n").TrimEnd("`r", "`n") + "`n"
    $expected = $expected.TrimEnd("`r", "`n") + "`n"
    if ($actual -eq $expected) {
        Write-Host "  OK" -ForegroundColor Green
        $passed++
    } else {
        Write-Host "  FALHA: saída difere do esperado" -ForegroundColor Red
        Write-Host "--- esperado ---" -ForegroundColor Yellow
        Write-Host $expected
        Write-Host "--- obtido ---" -ForegroundColor Yellow
        Write-Host $actual
        $failed++
    }
}

Write-Host ""
Write-Host "PASS=$passed FAIL=$failed" -ForegroundColor Cyan
if ($failed -eq 0) {
    Write-Host "TODOS OS TESTES LLVM API PASSARAM" -ForegroundColor Green
} else {
    Write-Host "$failed teste(s) falharam" -ForegroundColor Red
    exit 1
}