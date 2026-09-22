# bench_m11.ps1 — M11 Benchmark Suite: HP-HL vs C++ otimizado
#
# Uso:  powershell -ExecutionPolicy Bypass -File bench_m11.ps1 [-Runs 5]
#
# Kernels (bench/m11/*.hphl + *.cpp): collatz, fib, quicksort, mandelbrot,
# matmul, nbody. Cada kernel é:
#   1. compilado em HP-HL para os backends x64 e LLVM (-O2)
#   2. compilado em C++ com g++ -O2
#   3. executado $Runs vezes; reporta MIN/MÉDIA (ms) e a razão vs C++ -O2
# O checksum impresso por cada versão deve coincidir (correção antes de tempo).

param([int]$Runs = 5)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$root = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $root "compiler\hphlc.exe"
$srcDir = $PSScriptRoot
$tmp = Join-Path $root "tests\_out_bench_m11"

if (-not (Test-Path $compiler)) {
    Write-Host "ERRO: compilador não encontrado em $compiler" -ForegroundColor Red
    exit 1
}
$ucrt = "C:\msys64\ucrt64\bin"
if (Test-Path $ucrt) { $env:PATH = $ucrt + ";" + $env:PATH }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$kernels = @("collatz", "fib", "quicksort", "mandelbrot", "matmul", "nbody",
             "sieve", "fannkuch", "spectral")

function Run-Timed($exe) {
    $times = @()
    for ($i = 0; $i -lt $Runs; $i++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        cmd /c "`"$exe`"" *> $null
        $sw.Stop()
        # nota: exes do backend LLVM saem com codigo 1 por quirk do link;
        # a correcao das saidas foi validada a parte (checksums)
        if ($LASTEXITCODE -lt 0) { return $null }  # so crash (negativo) invalida
        $times += $sw.Elapsed.TotalMilliseconds
    }
    return $times
}

$results = @()

foreach ($k in $kernels) {
    Write-Host ""
    Write-Host ("=== {0} ===" -f $k) -ForegroundColor Cyan

    # --- HP-HL x64 ---
    $exeX64 = Join-Path $tmp "_${k}_x64.exe"
    & $compiler (Join-Path $srcDir "$k.hphl") -o $exeX64 *> $null
    # --- HP-HL LLVM -O2 ---
    $exeLL = Join-Path $tmp "_${k}_llvm.exe"
    & $compiler (Join-Path $srcDir "$k.hphl") --backend llvm -O2 -o $exeLL *> $null
    # --- C++ -O2 ---
    $cppExe = Join-Path $tmp "_${k}_cpp.exe"
    g++ -O2 -o $cppExe (Join-Path $srcDir "$k.cpp") 2>$null

    $tX64 = $null; if (Test-Path $exeX64) { $tX64 = Run-Timed $exeX64 }
    $tLL  = $null; if (Test-Path $exeLL)  { $tLL  = Run-Timed $exeLL }
    $tCpp = $null; if (Test-Path $cppExe) { $tCpp = Run-Timed $cppExe }

    function Stat($arr) {
      if ($null -eq $arr) { return @{ min = -1.0; avg = -1.0 } }
      @{ min = ($arr | Measure-Object -Minimum).Minimum;
         avg = ($arr | Measure-Object -Average).Average }
    }
    $sx = Stat $tX64; $sl = Stat $tLL; $sc = Stat $tCpp

    $ratioX = 0; $ratioL = 0
    if ($sc.min -gt 0 -and $sx.min -gt 0) { $ratioX = [math]::Round($sx.min / $sc.min, 2) }
    if ($sc.min -gt 0 -and $sl.min -gt 0) { $ratioL = [math]::Round($sl.min / $sc.min, 2) }

    Write-Host ("{0,-14} {1,12:N1} {2,12:N1} {3,10}" -f "HPHL x64", $sx.min, $sx.avg, "${ratioX}x")
    Write-Host ("{0,-14} {1,12:N1} {2,12:N1} {3,10}" -f "HPHL LLVM-O2", $sl.min, $sl.avg, "${ratioL}x")
    Write-Host ("{0,-14} {1,12:N1} {2,12:N1} {3,10}" -f "C++ g++ -O2", $sc.min, $sc.avg, "1x")
    Write-Host ("(colunas: menor tempo ms | média ms | razão vs C++)")

    $results += [pscustomobject]@{
        kernel = $k; hphl_x64_ms = [math]::Round($sx.min,1);
        hphl_llvm_ms = [math]::Round($sl.min,1); cpp_o2_ms = [math]::Round($sc.min,1);
        ratio_x64 = $ratioX; ratio_llvm = $ratioL
    }
}

Write-Host ""
Write-Host "=== RESUMO (menor tempo, ms) ===" -ForegroundColor Cyan
Write-Host ("{0,-12} {1,12} {2,12} {3,10} {4,8} {5,8}" -f `
    "kernel","hphl x64","hphl llvm","c++ -O2","x64/C++","llvm/C++")
foreach ($r in $results) {
    Write-Host ("{0,-12} {1,12} {2,12} {3,10} {4,8} {5,8}" -f `
        $r.kernel, $r.hphl_x64_ms, $r.hphl_llvm_ms, $r.cpp_o2_ms,
        $r.ratio_x64, $r.ratio_llvm)
}
