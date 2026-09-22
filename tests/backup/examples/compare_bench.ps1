$ErrorActionPreference = 'Continue'
Set-Location C:\Projetos\HPHL\examples

function Measure-Peak {
    param([string]$exe, [string]$label)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $maxPeak = 0L
    while (!$p.HasExited) {
        try {
            $p.Refresh()
            if ($p.PeakWorkingSet64 -gt $maxPeak) { $maxPeak = $p.PeakWorkingSet64 }
        } catch {}
        Start-Sleep -Milliseconds 5
    }
    try {
        $p.Refresh()
        if ($p.PeakWorkingSet64 -gt $maxPeak) { $maxPeak = $p.PeakWorkingSet64 }
    } catch {}
    $peak = [math]::Round($maxPeak / 1MB, 1)
    Write-Host ("  $label peak WS = $peak MB (exit=$($p.ExitCode))")
    return $peak
}

function Measure-Time {
    param([string]$exe)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    while (!$p.HasExited) { Start-Sleep -Milliseconds 5 }
    $sw.Stop()
    return $sw.ElapsedMilliseconds
}

$bins = @(
    @{name="C++ (malloc + free por iter)"; exe=".\binary-trees-cpp.exe"},
    @{name="C++ (new sem free, GC no fim)"; exe=".\binary-trees-no-free.exe"},
    @{name="HP-HL (GC automatico no epilogo)"; exe=".\binary-trees-hphl.exe"},
    @{name="HP-HL (gc_pressure por profundidade)"; exe=".\binary-trees-gc.exe"}
)

Write-Host "=== BINARY-TREES BENCHMARK (M28) ==="
Write-Host "Algoritmo: arvores binarias balanceadas de profundidades 4..14."
Write-Host ""

Write-Host "=== Tempo (5 runs cada) ==="
foreach ($b in $bins) {
    $times = @()
    for ($i = 1; $i -le 5; $i++) {
        $times += Measure-Time $b.exe
    }
    $avg = ($times | Measure-Object -Average).Average
    $min = ($times | Measure-Object -Minimum).Minimum
    $max = ($times | Measure-Object -Maximum).Maximum
    Write-Host ("{0,-45}  avg={1,6:N0} ms  min={2,5:N0}  max={3,5:N0}" -f $b.name, $avg, $min, $max)
    $b | Add-Member -NotePropertyName time_avg -NotePropertyValue $avg
}

Write-Host ""
Write-Host "=== Memoria (peak working set, 5 runs) ==="
foreach ($b in $bins) {
    $mems = @()
    for ($i = 1; $i -le 5; $i++) {
        $mems += Measure-Peak $b.exe "$($b.name) run $i"
    }
    $avg_mem = ($mems | Measure-Object -Average).Average
    $b | Add-Member -NotePropertyName mem_avg -NotePropertyValue $avg_mem
    Write-Host ("{0,-45}  avg peak = {1,6:N1} MB" -f $b.name, $avg_mem)
}

Write-Host ""
Write-Host "=== Resumo (HP-HL gc_pressure vs outros) ==="
Write-Host ""
Write-Host "Razoes vs C++ (free, baseline real de uso de memoria):"
Write-Host ("  HP-HL (gc no epilogo):   tempo={0:N2}x  memoria={1:N2}x" -f ($bins[2].time_avg / $bins[0].time_avg), ($bins[2].mem_avg / $bins[0].mem_avg))
Write-Host ("  HP-HL (gc_pressure):      tempo={0:N2}x  memoria={1:N2}x" -f ($bins[3].time_avg / $bins[0].time_avg), ($bins[3].mem_avg / $bins[0].mem_avg))
Write-Host ""
Write-Host "Razoes vs C++ (no free, sem custo de gerenciamento):"
Write-Host ("  HP-HL (gc no epilogo):   tempo={0:N2}x  memoria={1:N2}x" -f ($bins[2].time_avg / $bins[1].time_avg), ($bins[2].mem_avg / $bins[1].mem_avg))
Write-Host ("  HP-HL (gc_pressure):      tempo={0:N2}x  memoria={1:N2}x" -f ($bins[3].time_avg / $bins[1].time_avg), ($bins[3].mem_avg / $bins[1].mem_avg))
