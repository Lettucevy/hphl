#!/usr/bin/env powershell
# tests/scripts/test_bootstrap.ps1 — Bootstrap stage-2 do selfhost.
#
# Stage-1 (e2e_main.exe, montado pelo hphlc) compila o proprio compilador
# selfhost (lexer+parser+semantic+codegen+main AMALGAMADOS, pois o driver
# ignora `import` por desenho) gerando o stage-2; o stage-2 compila demo e
# o stdout tem que bater com a referencia (ponto fixo).
#
# Uso: powershell -File tests/scripts/test_bootstrap.ps1 [-Verbose]

param([switch]$Verbose, [switch]$Help)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "../..")).Path
$CompilerExe = Join-Path $RootDir "compiler/bin/hphlc.exe"
$SelfhostDir = Join-Path $RootDir "selfhost"
$TargetFile = Join-Path $SelfhostDir "target.txt"
$OutDir = Join-Path $SelfhostDir "_out"

if ($Help) {
    Write-Host @"
HP-HL Bootstrap Test (stage-2 fixed point)
Usage: powershell -File tests/scripts/test_bootstrap.ps1 [-Verbose]
"@
    exit 0
}

function Fail($msg) {
    Write-Host "ERROR: $msg" -ForegroundColor Red
    exit 1
}

$Gcc = "C:\msys64\ucrt64\bin\gcc.exe"
if (-not (Test-Path $Gcc)) {
    $fromPath = (Get-Command gcc.exe -ErrorAction SilentlyContinue).Source
    if ($fromPath) { $Gcc = $fromPath }
}
if (-not (Test-Path $Gcc)) { Fail "gcc.exe nao encontrado" }

$RuntimeObj = Join-Path $RootDir "compiler/src/runtime/main.o"
$ShExe = Join-Path $OutDir "e2e_main.exe"
if (-not (Test-Path $ShExe)) { Fail "stage-1 ausente; rode test_selfhost_e2e.ps1 antes" }

function Run-Stage($exe, $target, $label) {
    Set-Content -Path $TargetFile -Value ($target -replace '\\','/') -NoNewline -Encoding ascii
    $prevCwd = Get-Location
    Set-Location $RootDir
    try {
        $env:HPHL_GC_ALLOC_THRESHOLD = "0"
        $log = (& $exe 2>&1 | Out-String)
        $rc = $LASTEXITCODE
    } finally {
        Set-Location $prevCwd
    }
    if ($Verbose) { Write-Host $log -ForegroundColor Gray }
    return $rc
}

function Build-Asm($asmPath, $exePath) {
    $obj = "$exePath.o"
    & $Gcc -c $asmPath -o $obj 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { return $false }
    & $Gcc "-Wl,--stack,33554432" -o $exePath $obj $RuntimeObj -lws2_32 -lwsock32 -lbcrypt -lz 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { return $false }
    Remove-Item $obj -ErrorAction SilentlyContinue
    return $true
}

Write-Host "Bootstrap stage-2 e stage-3 (fixed point)" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# 1. Amalgama (ordem de dependencia; tira module/import, BOM e CRLF)
$amal = Join-Path $OutDir "stage2_amalgam.hphl"
$parts = @(
    "src/ast/ast.hphl",
    "src/frontend/token.hphl",
    "src/frontend/lexer.hphl",
    "src/frontend/parser.hphl",
    "src/semantic/semantic.hphl",
    "src/codegen/codegen.hphl",
    "src/main.hphl"
)
$sb = New-Object System.Text.StringBuilder
$sb.AppendLine("// AMALGAMA gerado por test_bootstrap.ps1. Nao editar.") | Out-Null
foreach ($pn in $parts) {
    $fp = Join-Path $SelfhostDir $pn
    if (-not (Test-Path $fp)) { Fail "fonte ausente: $pn" }
    foreach ($ln in [IO.File]::ReadAllLines($fp)) {
        $t = $ln.Trim()
        if ($t.StartsWith("module ") -or $t.StartsWith("import ")) { continue }
        $sb.AppendLine($ln) | Out-Null
    }
}
[IO.File]::WriteAllText($amal, $sb.ToString())
$nlines = ([IO.File]::ReadAllLines($amal)).Count
Write-Host "[amalgam] $nlines linhas"

# 2. Stage-1 compila o amalgama -> stage2.s
$rc = Run-Stage $ShExe "selfhost/_out/stage2_amalgam.hphl" "stage1"
$genAsm = Join-Path $RootDir "selfhost_demo.s"
if ($rc -ne 0 -or -not (Test-Path $genAsm)) { Fail "stage-1 nao compilou o amalgama (rc=$rc)" }
$stage2s = Join-Path $OutDir "stage2.s"
Move-Item $genAsm $stage2s -Force
$sz2 = (Get-Item $stage2s).Length
Write-Host "[stage-1] amalgama compilado ($sz2 bytes de asm -> stage2.s)"

# 3. Monta stage-2
$sh2 = Join-Path $OutDir "sh2.exe"
if (-not (Build-Asm $stage2s $sh2)) { Fail "stage-2 nao montou (assemble/link)" }
Write-Host "[stage-2] montado: sh2.exe"

# 4. Stage-2 compila o amalgama -> stage3.s (convergencia Stage-3)
$rc = Run-Stage $sh2 "selfhost/_out/stage2_amalgam.hphl" "stage2"
if ($rc -ne 0 -or -not (Test-Path $genAsm)) { Fail "stage-2 nao compilou o amalgama (rc=$rc)" }
$stage3s = Join-Path $OutDir "stage3.s"
Move-Item $genAsm $stage3s -Force
$sz3 = (Get-Item $stage3s).Length
Write-Host "[stage-2] amalgama compilado ($sz3 bytes de asm -> stage3.s)"

# 5. Verificacao bit-a-bit: stage2.s vs stage3.s
$b2 = [IO.File]::ReadAllBytes($stage2s)
$b3 = [IO.File]::ReadAllBytes($stage3s)
$match23 = ($b2.Length -eq $b3.Length)
if ($match23) {
    for ($i = 0; $i -lt $b2.Length; $i++) {
        if ($b2[$i] -ne $b3[$i]) { $match23 = $false; break }
    }
}
if (-not $match23) {
    Fail "divergencia bit-a-bit entre stage2.s e stage3.s"
}
Write-Host "[PASS] bit-for-bit parity: stage2.s == stage3.s" -ForegroundColor Green

# 6. Monta stage-3
$sh3 = Join-Path $OutDir "sh3.exe"
if (-not (Build-Asm $stage3s $sh3)) { Fail "stage-3 nao montou (assemble/link)" }
Write-Host "[stage-3] montado: sh3.exe"

# 7. Stage-3 compila demo; referencia via hphlc
$refExe = Join-Path $OutDir "demo.ref.exe"
& $CompilerExe "selfhost/demo.hphl" -o $refExe 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "hphlc nao compilou demo" }
$refOut = (& $refExe 2>&1 | Out-String)

$rc = Run-Stage $sh3 "selfhost/demo.hphl" "stage3"
if ($rc -ne 0 -or -not (Test-Path $genAsm)) { Fail "stage-3 nao compilou demo (rc=$rc)" }
$demo3s = Join-Path $OutDir "demo3.s"
Move-Item $genAsm $demo3s -Force
$demo3exe = Join-Path $OutDir "demo3.exe"
if (-not (Build-Asm $demo3s $demo3exe)) { Fail "demo do stage-3 nao montou" }
$demo3out = (& $demo3exe 2>&1 | Out-String)

# 8. Ponto fixo final de execucao
Set-Content -Path $TargetFile -Value "selfhost/demo.hphl" -NoNewline -Encoding ascii
Write-Host ""
Write-Host "referencia : $($refOut.Trim())"
Write-Host "stage-3    : $($demo3out.Trim())"
if ($demo3out -eq $refOut) {
    Write-Host "[PASS] bootstrap stage-3 completo (convergencia total + ponto fixo)" -ForegroundColor Green
    exit 0
} else {
    Write-Host "[FAIL] saidas de execucao divergem" -ForegroundColor Red
    exit 1
}
