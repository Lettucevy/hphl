# run_bench.ps1 — ferramenta de benchmark do HP-HL (v0.22.9)
#
# Compila um ou mais .hphl e mede o tempo de execução com o Stopwatch do .NET
# (mesmo QueryPerformanceCounter do clock_ns() da língua — o `clock()` do C
# não chega perto dessa resolução).
#
# Uso:
#   .\run_bench.ps1                        # roda bench\bench_*.hphl
#   .\run_bench.ps1 -File bench\bench_fib.hphl
#   .\run_bench.ps1 -Runs 15 -Workers 4    # 15 execuções com pool de 4
#   .\run_bench.ps1 -Filter parallel       # só benchmarks com "parallel" no nome
#   .\run_bench.ps1 -CompileRuns 5         # mede o tempo de compilação (hot reload)
#   .\run_bench.ps1 -WorkersList 1,2,4,8   # matriz de escalabilidade (vs 1 worker)
#
# O relatório usa wall-clock por execução (o processo inteiro). Os próprios
# benchmarks podem usar clock_ns() dentro do código para isolar a seção
# medida; a saída do programa é conferida quanto à determinismo entre runs.

param(
    [string]$File = "",
    [string]$Filter = "",
    [int]$Runs = 7,
    [string]$Workers = "",
    [string]$WorkersList = "",
    [string]$Compiler = "",
    [int]$CompileRuns = 0,
    [switch]$Keep
)

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Compiler) { $Compiler = Join-Path $root "compiler\hphlc.exe" }
$env:HPHL_EXE_DIR = Join-Path $root "compiler"
if ($Workers) { $env:HPHL_WORKERS = $Workers } else { Remove-Item Env:HPHL_WORKERS -ErrorAction SilentlyContinue }

if ($File) {
    $files = @($File)
} else {
    $files = @(Get-ChildItem (Join-Path $root "bench") -Filter "bench_*.hphl" | Select-Object -ExpandProperty FullName)
    if ($Filter) { $files = $files | Where-Object { $_ -match $Filter } }
}
if ($files.Count -eq 0) {
    Write-Host "Nenhum benchmark encontrado ($Filter)" -ForegroundColor Red
    exit 1
}

$tmp = Join-Path $env:TEMP "hphl_bench"
New-Item -ItemType Directory -Path $tmp -Force | Out-Null

function Median($arr) {
    $s = @($arr | Sort-Object)
    $n = $s.Count
    if ($n % 2 -eq 1) { return $s[($n - 1) / 2] }
    return ($s[$n / 2 - 1] + $s[$n / 2]) / 2
}

$failed = 0

function Median($arr) {
    $s = @($arr | Sort-Object)
    $n = $s.Count
    if ($n % 2 -eq 1) { return $s[($n - 1) / 2] }
    return ($s[$n / 2 - 1] + $s[$n / 2]) / 2
}

function StripTimings($lines) {
    # linhas de medição ("N us ...", "N ms ...") variam a cada execução
    return @($lines | Where-Object { $_ -notmatch "\d+ (us|ms)" })
}

# roda um benchmark; retorna @{ med; min; avg; max; spread; det; firstOut }
function Invoke-Bench([string]$src, [string]$exe, [string]$cfg) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($src)

    # tempo de compilação: com -CompileRuns N, recompila N vezes e reporta
    # min/média (pensado no hot reload: é o custo do ciclo edit→rodar)
    $compileSamples = @()
    $nCompile = if ($CompileRuns -gt 0) { $CompileRuns } else { 1 }
    $compileFailed = $false
    for ($i = 0; $i -lt $nCompile; $i++) {
        $swC = [System.Diagnostics.Stopwatch]::StartNew()
        cmd /c "`"$Compiler`" `"$src`" -o `"$exe`"" | Out-Null
        $swC.Stop()
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  FALHA: compilação retornou $LASTEXITCODE" -ForegroundColor Red
            $compileFailed = $true
            break
        }
        $compileSamples += [double]$swC.Elapsed.TotalMilliseconds
    }
    if ($compileFailed) { return $null }
    if ($CompileRuns -gt 0) {
        $cMin = ($compileSamples | Measure-Object -Minimum).Minimum
        $cAvg = ($compileSamples | Measure-Object -Average).Average
        Write-Host ("  compile (x{0}): min={1:N2} ms  media={2:N2} ms" -f $nCompile, $cMin, $cAvg) -ForegroundColor Green
    }

    # 1a execução: saída para conferência (e conferir determinismo)
    $firstOut = (& $exe 2>&1)
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  FALHA: execução retornou $LASTEXITCODE" -ForegroundColor Red
        $firstOut | ForEach-Object { Write-Host "  | $_" -ForegroundColor Yellow }
        return $null
    }

    $firstStripped = StripTimings $firstOut
    $samples = @()
    $det = $true
    for ($r = 0; $r -lt $Runs; $r++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $out = (& $exe 2>&1)
        $sw.Stop()
        if ($LASTEXITCODE -ne 0) {
            $det = $false
            break
        }
        $stripped = StripTimings $out
        if (($stripped -join "`n") -ne ($firstStripped -join "`n")) { $det = $false }
        $samples += $sw.Elapsed.TotalMilliseconds
    }
    if ($samples.Count -eq 0) {
        Write-Host "  FALHA: execução retornou $LASTEXITCODE" -ForegroundColor Red
        return $null
    }

    $min = ($samples | Measure-Object -Minimum).Minimum
    $avg = ($samples | Measure-Object -Average).Average
    $med = Median $samples
    $max = ($samples | Measure-Object -Maximum).Maximum
    $spread = [Math]::Round(($max - $min) / [Math]::Max($avg, 0.0001) * 100, 1)

    Write-Host ("  min={0,8:N2} ms  mediana={1,8:N2} ms  media={2,8:N2} ms  max={3,8:N2} ms  (spread {4}%)" -f $min, $med, $avg, $max, $spread) -ForegroundColor Green
    if (-not $det) {
        Write-Host "  AVISO: saída divergiu entre execuções" -ForegroundColor Yellow
    }
    Write-Host "  saida:"
    $firstOut | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }

    return @{ med = $med; min = $min; avg = $avg; max = $max; spread = $spread; det = $det }
}

foreach ($src in $files) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $exe = Join-Path $tmp ($name + ".exe")

    if ($WorkersList) {
        # matriz de escalabilidade: roda com cada configuração e compara
        $rows = @()
        foreach ($w in @($WorkersList.Split(",") | ForEach-Object { $_.Trim() })) {
            if ($w -eq "auto") { Remove-Item Env:HPHL_WORKERS -ErrorAction SilentlyContinue; $cfg = "pool padrão" }
            else { $env:HPHL_WORKERS = $w; $cfg = "HPHL_WORKERS=$w" }
            Write-Host ("=== {0} [{1}] x{2} ===" -f $name, $cfg, $Runs) -ForegroundColor Cyan
            $res = Invoke-Bench $src $exe $cfg
            if ($null -eq $res) { $failed++; continue }
            $rows += @{ w = $cfg; med = $res.med }
            if (-not $Keep) { Remove-Item $exe -Force -ErrorAction SilentlyContinue }
        }
        if ($rows.Count -gt 1) {
            Write-Host ""
            Write-Host "--- escalabilidade: $name ---" -ForegroundColor Cyan
            $base = $rows[0].med
            Write-Host ("  {0,-18} {1,10} {2,10}" -f "workers", "mediana ms", "speedup") -ForegroundColor DarkGray
            foreach ($r in $rows) {
                $sp = if ($base -gt 0) { $base / $r.med } else { 0 }
                Write-Host ("  {0,-18} {1,10:N2} {2,10:N2}x" -f $r.w, $r.med, $sp)
            }
        }
        continue
    }

    $cfg = if ($Workers) { "HPHL_WORKERS=$Workers" } else { "pool padrão" }
    Write-Host ("=== {0} [{1}] x{2} ===" -f $name, $cfg, $Runs) -ForegroundColor Cyan
    $res = Invoke-Bench $src $exe $cfg
    if ($null -eq $res) { $failed++; continue }
    if (-not $Keep) { Remove-Item $exe -Force -ErrorAction SilentlyContinue }
}

if ($failed -eq 0) { Write-Host ""; Write-Host "BENCH OK" -ForegroundColor Green } else { Write-Host "BENCH COM FALHAS ($failed)" -ForegroundColor Red }
exit $failed