# bench/bench_stress.ps1 - stress test binary-trees depth=8/10/12/14
$ErrorActionPreference = "Stop"
$benchDir = "C:\Projetos\HPHL\examples\benchmarks"
$outFile  = Join-Path $benchDir "stress_results.txt"

function Run-Variant {
    param([string]$Label, [string]$Exe, [string]$ExeArgs="", [int]$Runs=5, [int]$Depth=8, [hashtable]$Env=$null)
    $exePath = Join-Path $benchDir $Exe
    if (-not (Test-Path $exePath)) { return @() }
    $results = @()
    for ($i=1; $i -le $Runs; $i++) {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $exePath
        if ($ExeArgs) { $psi.Arguments = $ExeArgs }
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        if ($Env) { foreach ($k in $Env.Keys) { $psi.EnvironmentVariables[$k] = $Env[$k] } }
        $proc = New-Object System.Diagnostics.Process
        $proc.StartInfo = $psi
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        [void]$proc.Start()
        $peak = 0
        while (-not $proc.HasExited) {
            try { $cur = $proc.PeakWorkingSet64; if ($cur -gt $peak) { $peak = $cur } } catch {}
            Start-Sleep -Milliseconds 5
        }
        $proc.WaitForExit()
        $sw.Stop()
        $results += [pscustomobject]@{ Run=$i; TimeMs=$sw.Elapsed.TotalMilliseconds; PeakMB=[math]::Round($peak/1MB, 2) }
    }
    return [pscustomobject]@{
        Label   = $Label
        Runs    = $results
        AvgTime = [math]::Round(($results | Measure-Object TimeMs -Average).Average, 1)
        MinTime = ($results | Measure-Object TimeMs -Minimum).Minimum
        MaxTime = ($results | Measure-Object TimeMs -Maximum).Maximum
        PeakMB  = ($results | Measure-Object PeakMB -Maximum).Maximum
    }
}

"=== Binary-Trees stress test (M29 fixes applied) ===" | Out-File $outFile
"datetime: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $outFile -Append
"hostname: $env:COMPUTERNAME" | Out-File $outFile -Append
"" | Out-File $outFile -Append

foreach ($depth in @(8, 10, 12, 14)) {
    $exes = @{
        "C++ malloc/free"     = "binary-trees-${depth}-cpp.exe"
        "C++ no-free"         = "binary-trees-${depth}-nofree.exe"
        "HPHL GC"             = "binary-trees-${depth}-hphl.exe"
        "HPHL gc_pressure()"  = "binary-trees-${depth}-pressure-hphl.exe"
    }
    $pressureEnv = @{ "HPHL_GC_PRESSURE" = "1" }
    "[depth=$depth]" | Out-File $outFile -Append
    foreach ($kv in $exes.GetEnumerator()) {
        $r = Run-Variant -Label $kv.Key -Exe $kv.Value -Runs 3 -Depth $depth
        if ($r) {
            "  {0,-25} avg {1,8} ms  peak {2,6} MB  [{3}-{4} ms]" -f $r.Label, $r.AvgTime, $r.PeakMB, $r.MinTime, $r.MaxTime | Out-File $outFile -Append
            Write-Host ("  {0,-25} avg {1,8} ms  peak {2,6} MB" -f $r.Label, $r.AvgTime, $r.PeakMB)
        }
    }
    # HPHL with env
    $r = Run-Variant -Label "HPHL HPHL_GC_PRESSURE" -Exe "binary-trees-${depth}-hphl.exe" -Runs 3 -Depth $depth -Env $pressureEnv
    if ($r) {
        "  {0,-25} avg {1,8} ms  peak {2,6} MB  [{3}-{4} ms]" -f $r.Label, $r.AvgTime, $r.PeakMB, $r.MinTime, $r.MaxTime | Out-File $outFile -Append
        Write-Host ("  {0,-25} avg {1,8} ms  peak {2,6} MB" -f $r.Label, $r.AvgTime, $r.PeakMB)
    }
    "" | Out-File $outFile -Append
}

Write-Host ""
Write-Host "Resultados em: $outFile" -ForegroundColor Green
Get-Content $outFile