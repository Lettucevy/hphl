# bench/bench_binary_trees.ps1
$ErrorActionPreference = "Stop"
$benchDir = Join-Path $PSScriptRoot "..\examples\benchmarks"
$benchDir = (Resolve-Path $benchDir).Path
$outFile  = Join-Path $benchDir "bench_results.txt"

$v1 = @{ Name="C++ malloc/free";       Exe="binary-trees-14-cpp.exe";            Args=@() }
$v2 = @{ Name="C++ no-free";           Exe="binary-trees-14-nofree.exe";         Args=@() }
$v3 = @{ Name="HPHL x64 GC";           Exe="binary-trees-14-hphl.exe";           Args=@() }
$v4 = @{ Name="HPHL LLVM -O3";         Exe="binary-trees-14-llvm.exe";           Args=@() }
$v5 = @{ Name="HPHL gc_pressure()";    Exe="binary-trees-14-pressure-hphl.exe";  Args=@() }
$v6 = @{ Name="HPHL HPHL_GC_PRESSURE"; Exe="binary-trees-14-hphl.exe";           Args=@(); EnvK="HPHL_GC_PRESSURE"; EnvV="1" }
$variants = @($v1, $v2, $v3, $v4, $v5, $v6)

# Mede PeakWorkingSet usando [System.Diagnostics.Process] de outro processo
# Inicia o processo, monitora periodicamente, captura o pico, depois termina.
function Get-PeakWorkingSet {
    param([string]$ExePath, [string[]]$Args, [hashtable]$Env, [string]$Label, [int]$TimeoutSec=120)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    if ($Env) { foreach ($k in $Env.Keys) { $psi.EnvironmentVariables[$k] = $Env[$k] } }
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    [void]$proc.Start()
    $peak = 0
    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()
    while (-not $proc.HasExited) {
        try {
            $cur = $proc.PeakWorkingSet64
            if ($cur -gt $peak) { $peak = $cur }
        } catch {}
        Start-Sleep -Milliseconds 5
        if ($sw.Elapsed.TotalSeconds -gt $TimeoutSec) { $proc.Kill(); break }
    }
    $proc.WaitForExit()
    $sw.Stop()
    $ms = $sw.Elapsed.TotalMilliseconds
    $peakMB = [math]::Round($peak/1MB, 2)
    $exitCode = $proc.ExitCode
    $stdout = $outTask.Result
    $err = $errTask.Result
    return [pscustomobject]@{
        Variant = $Label
        TimeMs  = [math]::Round($ms, 1)
        PeakMB  = $peakMB
        Exit    = $exitCode
        Output  = ($stdout -split "`n")[0..2] -join "`n"
        Stderr  = $err
    }
}

$results = @()
foreach ($v in $variants) {
    $exePath = Join-Path $benchDir $v.Exe
    if (-not (Test-Path $exePath)) {
        Write-Host "[skip] $($v.Name): $($v.Exe) nao encontrado" -ForegroundColor Yellow
        continue
    }
    Write-Host "[run] $($v.Name) ..." -ForegroundColor Cyan
    $envBlock = $null
    if ($v.EnvK) { $envBlock = @{ $v.EnvK = $v.EnvV } }
    $r = Get-PeakWorkingSet -ExePath $exePath -Args $v.Args -Env $envBlock -Label $v.Name
    $results += $r
    Write-Host "       $($r.TimeMs) ms / peak $($r.PeakMB) MB / exit $($r.Exit)"
}

"=== Binary-Trees CLBG (maxDepth=14, stretch=15, ~5.5M nodes) ===" | Out-File $outFile -Encoding utf8
"datetime: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $outFile -Append -Encoding utf8
"hostname: $env:COMPUTERNAME" | Out-File $outFile -Append -Encoding utf8
"" | Out-File $outFile -Append -Encoding utf8
"{0,-30} {1,12} {2,12} {3,6}" -f "Variant","Time (ms)","Peak (MB)","Exit" | Out-File $outFile -Append -Encoding utf8
"-----------------------------------------------------------------------" | Out-File $outFile -Append -Encoding utf8
foreach ($r in $results) {
    "{0,-30} {1,12} {2,12} {3,6}" -f $r.Variant, $r.TimeMs, $r.PeakMB, $r.Exit | Out-File $outFile -Append -Encoding utf8
}
"" | Out-File $outFile -Append -Encoding utf8
"Output samples:" | Out-File $outFile -Append -Encoding utf8
foreach ($r in $results) {
    "--- $($r.Variant) ---" | Out-File $outFile -Append -Encoding utf8
    $r.Output | Out-File $outFile -Append -Encoding utf8
}

Write-Host ""
Write-Host "Resultados salvos em: $outFile" -ForegroundColor Green
Get-Content $outFile