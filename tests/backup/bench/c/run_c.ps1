# run_c.ps1 — benchmarks em C no mesmo formato do run_bench.ps1 (comparação
# justa: gcc -O2, Stopwatch .NET, N execuções, min/mediana/spread).
#
# Uso:
#   .\run_c.ps1                        # roda bench\c\c_*.c
#   .\run_c.ps1 -Runs 15
#   .\run_c.ps1 -File bench\c\c_fib.c

param(
    [string]$File = "",
    [int]$Runs = 7,
    [string]$Compiler = "C:\msys64\ucrt64\bin\gcc.exe",
    [switch]$Keep
)

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

if ($File) {
    $files = @($File)
} else {
    $files = @(Get-ChildItem (Join-Path $root "bench\c") -Filter "c_*.c" | Select-Object -ExpandProperty FullName)
}
if ($files.Count -eq 0) { Write-Host "Nenhum benchmark C encontrado" -ForegroundColor Red; exit 1 }

$tmp = Join-Path $env:TEMP "hphl_c_bench"
New-Item -ItemType Directory -Path $tmp -Force | Out-Null

function Median($arr) {
    $s = @($arr | Sort-Object)
    $n = $s.Count
    if ($n % 2 -eq 1) { return $s[($n - 1) / 2] }
    return ($s[$n / 2 - 1] + $s[$n / 2]) / 2
}

$failed = 0
foreach ($src in $files) {
    $name = "C_" + [System.IO.Path]::GetFileNameWithoutExtension($src).Substring(2)
    $exe = Join-Path $tmp ($name + ".exe")
    Write-Host ("=== {0} [gcc -O2] x{1} ===" -f $name, $Runs) -ForegroundColor Cyan

    cmd /c "`"$Compiler`" -O2 -o `"$exe`" `"$src`"" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  FALHA: compilação retornou $LASTEXITCODE" -ForegroundColor Red
        $failed++
        continue
    }

    $firstOut = (& $exe 2>&1)
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  FALHA: execução retornou $LASTEXITCODE" -ForegroundColor Red
        $failed++
        continue
    }

    $samples = @()
    $det = $true
    for ($r = 0; $r -lt $Runs; $r++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $out = (& $exe 2>&1)
        $sw.Stop()
        if ($LASTEXITCODE -ne 0) { $det = $false; break }
        if (($out -join "`n") -ne ($firstOut -join "`n")) { $det = $false }
        $samples += $sw.Elapsed.TotalMilliseconds
    }
    if ($samples.Count -eq 0) {
        Write-Host "  FALHA: execução retornou $LASTEXITCODE" -ForegroundColor Red
        $failed++
        continue
    }

    $min = ($samples | Measure-Object -Minimum).Minimum
    $avg = ($samples | Measure-Object -Average).Average
    $med = Median $samples
    $max = ($samples | Measure-Object -Maximum).Maximum
    $spread = [Math]::Round(($max - $min) / [Math]::Max($avg, 0.0001) * 100, 1)

    Write-Host ("  min={0,8:N2} ms  mediana={1,8:N2} ms  media={2,8:N2} ms  max={3,8:N2} ms  (spread {4}%)" -f $min, $med, $avg, $max, $spread) -ForegroundColor Green
    if (-not $det) { Write-Host "  AVISO: saída divergiu entre execuções" -ForegroundColor Yellow }
    Write-Host "  saida:"
    $firstOut | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }

    if (-not $Keep) { Remove-Item $exe -Force -ErrorAction SilentlyContinue }
}

if ($failed -eq 0) { Write-Host ""; Write-Host "BENCH OK" -ForegroundColor Green } else { Write-Host "BENCH COM FALHAS ($failed)" -ForegroundColor Red }
exit $failed