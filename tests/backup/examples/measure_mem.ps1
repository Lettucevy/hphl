$ErrorActionPreference = 'Continue'
Set-Location C:\Projetos\HPHL\examples

function Measure-Peak {
    param([string]$exe, [string]$label)
    Write-Host "Running $label ($exe)..."
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    # Poll PeakWorkingSet while running
    $maxPeak = 0L
    while (!$p.HasExited) {
        try {
            $p.Refresh()
            if ($p.PeakWorkingSet64 -gt $maxPeak) {
                $maxPeak = $p.PeakWorkingSet64
            }
        } catch {}
        Start-Sleep -Milliseconds 5
    }
    # Final read
    try {
        $p.Refresh()
        if ($p.PeakWorkingSet64 -gt $maxPeak) {
            $maxPeak = $p.PeakWorkingSet64
        }
    } catch {}
    $peak = [math]::Round($maxPeak / 1MB, 1)
    Write-Host "  $label peak WS = $peak MB (exit=$($p.ExitCode))"
    return $peak
}

Write-Host "=== Memory: 5 amostras (polling) ==="
$hphl_mems = @()
$cpp_mems = @()
for ($i = 1; $i -le 5; $i++) {
    $hphl_mems += Measure-Peak '.\binary-trees-hphl.exe' "HP-HL run $i"
    $cpp_mems  += Measure-Peak '.\binary-trees-cpp.exe'  "C++   run $i"
}

$hphl_avg = ($hphl_mems | Measure-Object -Average).Average
$cpp_avg  = ($cpp_mems  | Measure-Object -Average).Average
Write-Host ""
Write-Host "=== Medias ==="
Write-Host ("HP-HL peak avg: {0} MB" -f $hphl_avg)
Write-Host ("C++   peak avg: {0} MB" -f $cpp_avg)
Write-Host ("HP-HL/C++ ratio: {0:N2}x" -f ($hphl_avg / $cpp_avg))
