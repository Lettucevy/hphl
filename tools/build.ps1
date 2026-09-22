#!/usr/bin/env powershell
# tools/build.ps1 - Builds the HP-HL compiler (hphlc.exe)
# Usage: powershell -File tools/build.ps1 [-Backend x64|llvm|ir] [-Debug] [-Clean] [-Jobs N] [-Verbose]
# Default: x64 backend, release build
#
# INCREMENTAL: cada TU gera um depfile (.d via -MMD -MP); só recompila o que
# mudou (.cpp ou qualquer header alcançável). Build sem mudanças = ~2s.
# PARALELO: compila até N TUs ao mesmo tempo (padrão = nº de CPUs).
#
# MSYS2 detection (priority order, see Resolve-MSYS2):
#   1. $env:MSYS2_ROOT (user override)
#   2. Common install paths: C:\msys64, D:\msys64, C:\msys2, D:\msys2,
#      C:\Program Files\msys64, C:\tools\msys64
#   3. $env:Path - where.exe g++.exe / gcc.exe
#   4. Error: clear message with install instructions

param(
    [ValidateSet("x64", "llvm", "ir", "aarch64", "wasm")]
    [string]$Backend = "x64",
    [switch]$Debug,
    [switch]$Clean,
    [int]$Jobs = 0,
    [switch]$Verbose,
    [switch]$Help
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CompilerDir = Join-Path $ScriptDir "..\compiler"
$CompilerDir = (Resolve-Path $CompilerDir).Path

if ($Help) {
    Write-Host @"
HP-HL Compiler Build Script
Usage: powershell -File tools/build.ps1 [options]

Options:
  -Backend <name>   Backend to build for: x64 (default), llvm, ir, aarch64, wasm
  -Debug            Build with debug symbols
  -Clean            Clean build artifacts before building
  -Jobs N           Parallel compile jobs (default: CPU count)
  -Verbose          Show per-file OK lines (default: failures + summary only)
  -Help             Show this help

Environment:
  MSYS2_ROOT        Override MSYS2 install path (e.g. D:\msys64)
  HPHL_GXX          Override g++ path (full path including filename)
  HPHL_GCC          Override gcc path
"@
    exit 0
}

# ---------------------------------------------------------------------------
# MSYS2 / g++ / gcc auto-detection
# ---------------------------------------------------------------------------
function Resolve-MSYS2 {
    # 1. Explicit override via $env:HPHL_GXX (full path to g++.exe)
    if ($env:HPHL_GXX -and (Test-Path $env:HPHL_GXX)) {
        Write-Host "[msys2] Using HPHL_GXX=$($env:HPHL_GXX)" -ForegroundColor DarkGray
        return (Split-Path -Parent $env:HPHL_GXX)
    }

    # 2. Override via $env:MSYS2_ROOT
    if ($env:MSYS2_ROOT -and (Test-Path $env:MSYS2_ROOT)) {
        $bin = Join-Path $env:MSYS2_ROOT "ucrt64\bin"
        if (Test-Path (Join-Path $bin "g++.exe")) {
            Write-Host "[msys2] Using MSYS2_ROOT=$env:MSYS2_ROOT" -ForegroundColor DarkGray
            return $bin
        }
    }

    # 3. Common install paths
    $candidates = @(
        "C:\msys64",
        "D:\msys64",
        "C:\msys2",
        "D:\msys2",
        "C:\Program Files\msys64",
        "C:\Program Files\msys2",
        "C:\tools\msys64",
        "C:\tools\msys2"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) {
            $bin = Join-Path $c "ucrt64\bin"
            if (Test-Path (Join-Path $bin "g++.exe")) {
                Write-Host "[msys2] Auto-detected $c" -ForegroundColor DarkGray
                return $bin
            }
        }
    }

    # 4. where.exe fallback
    $gxx = (Get-Command g++.exe -ErrorAction SilentlyContinue).Source
    if ($gxx) {
        Write-Host "[msys2] Using PATH g++.exe at $gxx" -ForegroundColor DarkGray
        return (Split-Path -Parent $gxx)
    }

    # 5. Error
    Write-Host ""
    Write-Host "ERROR: g++.exe not found. Tried:" -ForegroundColor Red
    Write-Host "  - HPHL_GXX env var" -ForegroundColor Red
    Write-Host "  - MSYS2_ROOT env var" -ForegroundColor Red
    Write-Host "  - Common paths: $($candidates -join ', ')" -ForegroundColor Red
    Write-Host "  - PATH (where.exe g++.exe)" -ForegroundColor Red
    Write-Host ""
    Write-Host "Install MSYS2 (https://www.msys2.org/) or set:" -ForegroundColor Yellow
    Write-Host '  $env:MSYS2_ROOT = "D:\msys64"' -ForegroundColor Yellow
    Write-Host ""
    exit 1
}

function Resolve-Tool {
    param([string]$ExeName, [string]$MSYS2Bin)
    $fullPath = Join-Path $MSYS2Bin $ExeName
    if (Test-Path $fullPath) { return $fullPath }
    # fallback to PATH
    $fromPath = (Get-Command $ExeName -ErrorAction SilentlyContinue).Source
    if ($fromPath) { return $fromPath }
    Write-Host "ERROR: $ExeName not found in $MSYS2Bin nor in PATH" -ForegroundColor Red
    exit 1
}

$MSYS2Bin = Resolve-MSYS2
$Gxx = Resolve-Tool -ExeName "g++.exe" -MSYS2Bin $MSYS2Bin
$Gcc = Resolve-Tool -ExeName "gcc.exe" -MSYS2Bin $MSYS2Bin
Write-Host "[build] g++ = $Gxx" -ForegroundColor DarkGray
Write-Host "[build] gcc = $Gcc" -ForegroundColor DarkGray

Set-Location $CompilerDir

if ($Clean) {
    Write-Host "[build] Cleaning..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue build, bin
}

New-Item -ItemType Directory -Force -Path build, bin | Out-Null

if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
if ($Jobs -lt 1) { $Jobs = 1 }
Write-Host "[build] jobs = $Jobs" -ForegroundColor DarkGray

$inc = @("-I.", "-Isrc", "-Isrc/runtime")
# M_RV1 I2: -Debug agora eh respeitado para TODOS os componentes.
# Antes: runtime C, SHA-512 e link usavam -O2 hardcoded (mesmo com -Debug).
# Separamos cxxflags (cpp), cflags (c) e ldflags (link); o script aplica
# o nivel de otimizacao certo a cada um, incluindo -g/-O0 quando -Debug.
$cxxflags = @("-std=c++17", "-O2")
$cflags    = @("-std=c11",  "-O2")
if ($Debug) {
    $cxxflags = @("-std=c++17", "-O0", "-g")
    $cflags    = @("-std=c11",  "-O0", "-g")
}

# Stamp de config: trocar flags (-Debug etc.) invalida todos os .o.
# Sem isso, o incremental misturaria objetos -O2 com -O0/-g.
$cfgStamp = Join-Path $CompilerDir "build/.config"
$cfgNow = "cxxflags=$($cxxflags -join ' ')|cflags=$($cflags -join ' ')|backend=$Backend"
$cfgChanged = $true
if ((Test-Path $cfgStamp) -and ((Get-Content $cfgStamp -Raw) -eq $cfgNow)) {
    $cfgChanged = $false
}
if ($cfgChanged) {
    Write-Host "[build] config mudou (ou primeiro build) - rebuild total." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Incremental: depfiles (.d) via -MMD -MP. Um TU só recompila se o .o não
# existe, o .d não existe, a config mudou, ou algum pré-requisito
# (.cpp/header) é mais novo que o .o.
# ---------------------------------------------------------------------------
function Get-DepPrereqs {
    param([string]$DepFile)
    # Formato: "build/x.o: C:\...\x.cpp C:\...\y.h ..." (continuacoes " \" + NL;
    # MSYS2 emite '\' e o drive tem ':' - por isso o alvo eh casado por ".o:").
    # O -MP adiciona regras phony ("C:\...\y.h:") cujo ':' final eh removido.
    $raw = [System.IO.File]::ReadAllText($DepFile) -replace "\\\r?\n", " "
    $m = [regex]::Match($raw, '\.o\s*:')
    if (-not $m.Success) { return @() }
    $rest = $raw.Substring($m.Index + $m.Length)
    $out = @()
    foreach ($tok in ($rest -split '\s+')) {
        if ([string]::IsNullOrWhiteSpace($tok)) { continue }
        $p = $tok.TrimEnd(':')                 # regra phony do -MP
        $p = $p -replace '\\ ', ' '            # espaço escapado
        $p = $p -replace '/', '\'              # normaliza MSYS2
        if ([string]::IsNullOrWhiteSpace($p)) { continue }
        if (-not [System.IO.Path]::IsPathRooted($p)) {
            $p = Join-Path $CompilerDir $p
        }
        $out += $p
    }
    return $out
}

function Test-NeedsBuild {
    param([string]$Src, [string]$Obj, [string]$Dep)
    if (-not (Test-Path $Obj)) { return $true }
    if (-not (Test-Path $Dep)) { return $true }
    $objTime = (Get-Item $Obj).LastWriteTime
    foreach ($p in (Get-DepPrereqs $Dep)) {
        if (-not (Test-Path $p)) { return $true }  # header removido: rebuild
        if ((Get-Item $p).LastWriteTime -gt $objTime) { return $true }
    }
    return $false
}

# Auto-discover all .cpp files under src/ (M_RV1 I3).
# Excludes src/runtime/ which is built separately as a single TU
# (main.c includes all the other runtime .c files via #include).
$cppfiles = Get-ChildItem -Path src -Recurse -Filter *.cpp |
            Where-Object { $_.FullName -notmatch '[\\/]runtime[\\/]' } |
            Sort-Object FullName |
            ForEach-Object { $_.FullName }

# Monta a lista de TUs (src, obj, dep, flags, compilador)
$tus = @()
foreach ($f in $cppfiles) {
    $rel = $f -replace [regex]::Escape($CompilerDir + "\"), ""
    $base = $rel -replace '\.cpp$', '' -replace '\\', '_'
    $tus += [pscustomobject]@{
        Rel  = $rel
        Src  = $f
        Obj  = (Join-Path $CompilerDir "build/$base.o")
        Dep  = (Join-Path $CompilerDir "build/$base.d")
        Exe  = $Gxx
        Args = $cxxflags + @("-c", "-MMD", "-MP", "-MF", (Join-Path $CompilerDir "build/$base.d"), $f, "-o", (Join-Path $CompilerDir "build/$base.o")) + $inc
    }
}
$tus += [pscustomobject]@{
    Rel  = "src/runtime/main.c"
    Src  = (Join-Path $CompilerDir "src/runtime/main.c")
    Obj  = (Join-Path $CompilerDir "build/runtime_main.o")
    Dep  = (Join-Path $CompilerDir "build/runtime_main.d")
    Exe  = $Gcc
    Args = $cflags + @("-c", "-MMD", "-MP", "-MF", (Join-Path $CompilerDir "build/runtime_main.d"), (Join-Path $CompilerDir "src/runtime/main.c"), "-o", (Join-Path $CompilerDir "build/runtime_main.o"), "-Isrc/runtime") + $inc
}
$tus += [pscustomobject]@{
    Rel  = "src/sha512.c"
    Src  = (Join-Path $CompilerDir "src/sha512.c")
    Obj  = (Join-Path $CompilerDir "build/sha512.o")
    Dep  = (Join-Path $CompilerDir "build/sha512.d")
    Exe  = $Gcc
    Args = $cflags + @("-c", "-MMD", "-MP", "-MF", (Join-Path $CompilerDir "build/sha512.d"), (Join-Path $CompilerDir "src/sha512.c"), "-o", (Join-Path $CompilerDir "build/sha512.o"))
}

$queue = @()
foreach ($tu in $tus) {
    if ($cfgChanged -or (Test-NeedsBuild -Src $tu.Src -Obj $tu.Obj -Dep $tu.Dep)) {
        $queue += $tu
    }
}

$failures = 0
$failedNames = @()
$built = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()

function Quote-Arg {
    param([string]$a)
    if ($a -match '[\s"]') { return '"' + ($a -replace '"', '""') + '"' }
    return $a
}

if ($queue.Count -eq 0) {
    Write-Host "[build] tudo atualizado (incremental, 0 TUs)." -ForegroundColor Green
} else {
    Write-Host "[build] compilando $($queue.Count)/$($tus.Count) TUs..." -ForegroundColor Cyan
    # Paralelo por ondas: dispara até $Jobs processos, espera a onda, repete.
    # (PS 5.1 não tem ForEach-Object -Parallel.) Cada TU roda via
    # "cmd /c g++ ... > log 2>&1": redirect em arquivo (sem deadlock de pipe
    # e sem interleave) + exit code real via [Process]::Start. (Start-Process
    # -PassThru perde o ExitCode neste ambiente.)
    $idx = 0
    while ($idx -lt $queue.Count) {
        $wave = @()
        while ($idx -lt $queue.Count -and $wave.Count -lt $Jobs) {
            $tu = $queue[$idx]; $idx++
            $log = $tu.Obj + ".log"
            $cmdLine = (($tu.Args | ForEach-Object { Quote-Arg $_ }) -join ' ')
            # cmd /c remove a primeira e a ultima aspa da linha (quote-stripping):
            # exe vai SEM aspas (MSYS2 nunca tem espaco); se tiver, usa /s /c
            # com a linha toda entre aspas. Args/log com espaco vao quotados.
            if ($tu.Exe -match '\s') {
                $full = '/s /c """{0}"" {1} > "{2}" 2>&1"' -f $tu.Exe, $cmdLine, $log
            } else {
                $full = '/c {0} {1} > "{2}" 2>&1' -f $tu.Exe, $cmdLine, $log
            }
            $psi = New-Object System.Diagnostics.ProcessStartInfo("cmd.exe", $full)
            $psi.WorkingDirectory = $CompilerDir
            $psi.UseShellExecute = $false
            $psi.CreateNoWindow = $true
            $p = [System.Diagnostics.Process]::Start($psi)
            $wave += [pscustomobject]@{ TU = $tu; Proc = $p; Log = $log }
        }
        foreach ($w in $wave) {
            $w.Proc.WaitForExit()
            $code = $w.Proc.ExitCode
            if ($code -ne 0) {
                $failures++
                $failedNames += $w.TU.Rel
                Write-Host "[FAIL] $($w.TU.Rel) (exit $code)" -ForegroundColor Red
                if (Test-Path $w.Log) {
                    $errText = Get-Content $w.Log -Raw
                    if (-not [string]::IsNullOrWhiteSpace($errText)) { Write-Host $errText }
                }
            } else {
                $built++
                if ($Verbose) { Write-Host "[OK]   $($w.TU.Rel)" -ForegroundColor Gray }
                Remove-Item $w.Log -ErrorAction SilentlyContinue
            }
        }
        if ($failures -eq 0 -and $Verbose) {
            Write-Host "[build] ...$built/$($queue.Count)" -ForegroundColor DarkGray
        }
    }
}

$sw.Stop()

if ($failures -gt 0) {
    Write-Host ""
    Write-Host "BUILD FAILED: $failures error(s) em $($sw.Elapsed.TotalSeconds.ToString('N1'))s" -ForegroundColor Red
    exit 1
}

$exe = Join-Path $CompilerDir "bin/hphlc.exe"
if ($built -gt 0 -or -not (Test-Path $exe)) {
    Write-Host ""
    Write-Host "Linking..." -ForegroundColor Cyan
    $ldflags = @("-std=c++17", "-O2")
    if ($Debug) { $ldflags = @("-std=c++17", "-O0", "-g") }
    $objs = Get-ChildItem -Path (Join-Path $CompilerDir "build") -Filter *.o | ForEach-Object { $_.FullName }
    & $Gxx @ldflags -o $exe @objs -lLLVM-22 -lwinhttp -lbcrypt -lcrypt32 -lws2_32 -lwsock32 -ladvapi32 -luserenv -lz 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: link" -ForegroundColor Red
        exit 1
    }
    Write-Host "Built bin/hphlc.exe" -ForegroundColor Green
    $rootBin = Join-Path (Split-Path -Parent $CompilerDir) "bin"
    if (Test-Path $rootBin) {
        Copy-Item -Force $exe (Join-Path $rootBin "hphlc.exe")
    }
} else {
    Write-Host "Link skipped (nada mudou)." -ForegroundColor DarkGray
    $rootBin = Join-Path (Split-Path -Parent $CompilerDir) "bin"
    if (Test-Path $rootBin) {
        Copy-Item -Force $exe (Join-Path $rootBin "hphlc.exe")
    }
}

$msgSrc = Join-Path $CompilerDir "src/messages"
$msgDst = Join-Path $CompilerDir "bin/messages"
if (Test-Path $msgSrc) {
    New-Item -ItemType Directory -Force -Path $msgDst | Out-Null
    Copy-Item -Force (Join-Path $msgSrc "*.json") $msgDst
}
Set-Content -Path $cfgStamp -Value $cfgNow -NoNewline
Write-Host "[build] $($queue.Count) compilados, $($tus.Count - $queue.Count) reutilizados, $($sw.Elapsed.TotalSeconds.ToString('N1'))s" -ForegroundColor DarkGray
Write-Host "Backend: $Backend" -ForegroundColor Cyan
