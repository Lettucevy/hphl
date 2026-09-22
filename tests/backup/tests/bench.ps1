# bench.ps1 — benchmark do backend LLVM API (--backend llvm) do HP-HL
#
# Uso:  powershell -ExecutionPolicy Bypass -File bench.ps1 [-Runs N] [-Opts "0,2"]
#
# Compila os exemplos de CPU com -O0 e -O2 (pass manager do LLVM, M4) e mede o
# tempo médio de execução (ms) de N rodadas de cada um — comparação antes/depois
# da otimização.
#
# Requisitos: hphlc compilado (make em compiler/), gcc no PATH (MSYS2 ucrt64).

param(
    [int]$Runs = 20,
    [string]$Opts = "0,2"
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$root = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $root "compiler\hphlc.exe"
$examples = Join-Path $root "examples"
$tmp = Join-Path $root "tests\_out_bench"

if (-not (Test-Path $compiler)) {
    Write-Host "ERRO: compilador não encontrado em $compiler (rode 'make' em compiler/)" -ForegroundColor Red
    exit 1
}
$ucrt = "C:\msys64\ucrt64\bin"
if (Test-Path $ucrt) { $env:PATH = $ucrt + ";" + $env:PATH }

New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# exemplos com carga de CPU (loops, aritmética, listas/arrays)
$cases = @("bench_cpu", "hello", "classes", "arrays")

foreach ($opt in ($Opts -split "," | ForEach-Object { [int]$_ })) {
    Write-Host ""
    Write-Host "=== -O$opt ===" -ForegroundColor Cyan
    Write-Host ("{0,-12} {1,10} {2,10}" -f "exemplo", "media(ms)", "desvio")
    foreach ($name in $cases) {
        $src = Join-Path $examples ($name + ".hphl")
        if (-not (Test-Path $src)) { continue }
        $exe = Join-Path $tmp ("_" + $name + "_O" + $opt + ".exe")
        & $compiler $src --backend llvm "-O$opt" -o $exe 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host ("{0,-12} COMPILE-FAIL" -f $name) -ForegroundColor Red
            continue
        }
        $times = @()
        for ($i = 0; $i -lt $Runs; $i++) {
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            cmd /c $exe *> $null
            $sw.Stop()
            $times += $sw.Elapsed.TotalMilliseconds
        }
        $avg = ($times | Measure-Object -Average).Average
        $dev = 0.0
        if ($times.Count -gt 1) {
            $sumSq = 0.0
            foreach ($t in $times) { $d = $t - $avg; $sumSq += $d * $d }
            $dev = [Math]::Sqrt($sumSq / ($times.Count - 1))
        }
        Write-Host ("{0,-12} {1,10:N2} {2,10:N2}" -f $name, $avg, $dev)
    }
}
Write-Host ""
Write-Host "done"