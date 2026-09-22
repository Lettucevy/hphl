# bench_cmp.ps1 â€” comparativo HP-HL (--backend llvm) vs C++ (gcc -O2)
#
# Uso:  powershell -ExecutionPolicy Bypass -File bench_cmp.ps1 [-Runs N] [-Opts "0,2"]
#
# Compila os mesmos algoritmos em HP-HL (tests/benchmarks/hphl) e C++
# (tests/benchmarks/cpp), mede a mÃ©dia de N rodadas e imprime a tabela com o
# speedup de C++ sobre cada nÃ­vel de otimizaÃ§Ã£o HP-HL. As saÃ­das de cada par
# sÃ£o comparadas (mesmo resultado = benchmark vÃ¡lido).
#
# Requisitos: hphlc compilado (make em compiler/), gcc no PATH (MSYS2 ucrt64).

param(
    [int]$Runs = 10,
    [string]$Opts = "0,2"
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$root = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $root "compiler\hphlc.exe"
$srcHphl = Join-Path $PSScriptRoot "benchmarks\hphl"
$srcCpp = Join-Path $PSScriptRoot "benchmarks\cpp"
$out = Join-Path $PSScriptRoot "_out_benchcmp"

if (-not (Test-Path $compiler)) {
    Write-Host "ERRO: compilador nÃ£o encontrado em $compiler (rode 'make' em compiler/)" -ForegroundColor Red
    exit 1
}
$ucrt = "C:\msys64\ucrt64\bin"
if (Test-Path $ucrt) { $env:PATH = $ucrt + ";" + $env:PATH }

New-Item -ItemType Directory -Force -Path $out | Out-Null

$cases = @("collatz", "fib", "sieve", "matmul", "quicksort", "mandelbrot")

# 1) compila os .cpp uma vez (gcc -O2, alvo genÃ©rico â€” sem -march=native)
$cppExe = @{}
foreach ($name in $cases) {
    $cpp = Join-Path $srcCpp ("bench_" + $name + ".cpp")
    if (-not (Test-Path $cpp)) { continue }
    $exe = Join-Path $out ("cpp_" + $name + ".exe")
    & gcc -O2 -o $exe $cpp 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { $cppExe[$name] = $exe }
    else { Write-Host ("CPP {0}: COMPILE-FAIL" -f $name) -ForegroundColor Red }
}

$results = @{}
foreach ($opt in ($Opts -split "," | ForEach-Object { [int]$_ })) {
    $results[$opt] = @{}
    foreach ($name in $cases) {
        $src = Join-Path $srcHphl ("bench_" + $name + ".hphl")
        if (-not (Test-Path $src)) { continue }
        $exe = Join-Path $out ("hphl_" + $name + "_O" + $opt + ".exe")
        & $compiler $src --backend llvm "-O$opt" -o $exe 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host ("HPHL {0} -O{1}: COMPILE-FAIL" -f $name, $opt) -ForegroundColor Red
            continue
        }
        $times = @()
        for ($i = 0; $i -lt $Runs; $i++) {
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            cmd /c $exe *> $null
            $sw.Stop()
            $times += $sw.Elapsed.TotalMilliseconds; Write-Host ("  TRACE $exe [i=$i] $sw.Elapsed.TotalMilliseconds ms")
        }
        $avg = ($times | Measure-Object -Average).Average
        $results[$opt][$name] = $avg
    }
}

$cppTimes = @{}
foreach ($name in $cppExe.Keys) {
    $times = @()
    for ($i = 0; $i -lt $Runs; $i++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        cmd /c $cppExe[$name] *> $null
        $sw.Stop()
        $times += $sw.Elapsed.TotalMilliseconds; Write-Host ("  TRACE $exe [i=$i] $sw.Elapsed.TotalMilliseconds ms")
    }
    $cppTimes[$name] = ($times | Measure-Object -Average).Average
}

# 3) validaÃ§Ã£o: saÃ­da de cada par deve ser idÃªntica
Write-Host ""
Write-Host "validacao de saida (hphl -O2 vs cpp):" -ForegroundColor Cyan
foreach ($name in $cases) {
    if (-not $cppExe.ContainsKey($name)) { continue }
    $opt = ($Opts -split "," | ForEach-Object { [int]$_ } | Select-Object -Last 1)
    $h = Join-Path $out ("hphl_" + $name + "_O" + $opt + ".exe")
    $oh = (& cmd /c $h) -join "`n"
    $oc = (& cmd /c $cppExe[$name]) -join "`n"
    if ($oh -eq $oc) { Write-Host ("  {0,-11} OK" -f $name) }
    else { Write-Host ("  {0,-11} DIFERENTE! hphl: '{1}' cpp: '{2}'" -f $name, $oh, $oc) -ForegroundColor Red }
}

# 4) tabela
Write-Host ""
Write-Host ("comparativo (media de {0} rodadas, ms) - 'int' HP-HL = int32, 'double' = float64" -f $Runs) -ForegroundColor Cyan
Write-Host ("{0,-11} {1,10} {2,10} {3,10} {4,12} {5,12}" -f "algoritmo", "hphl-O0", "hphl-O2", "cpp-O2", "O2/cpp", "O0/cpp")
foreach ($name in $cases) {
    if (-not $cppTimes.ContainsKey($name)) { continue }
    $t0 = $results[0][$name]
    $t2 = 0.0
    foreach ($opt in $results.Keys) { if ($results[$opt].ContainsKey($name)) { $t2 = $results[$opt][$name] } }
    $tc = $cppTimes[$name]
    if ($tc -le 0) { continue }
    $r0 = $t0 / $tc
    $r2 = $t2 / $tc
    Write-Host ("{0,-11} {1,10:N2} {2,10:N2} {3,10:N2} {4,11:N1}x {5,11:N1}x" -f $name, $t0, $t2, $tc, $r2, $r0)
}
Write-Host ""
Write-Host "done"
