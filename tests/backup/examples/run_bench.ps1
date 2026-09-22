$ErrorActionPreference = 'Continue'
Set-Location C:\Projetos\HPHL\examples

Write-Host "===================================="
Write-Host "BINARY-TREES BENCHMARK (CLBG) - 5 runs"
Write-Host "===================================="
Write-Host ""

$hphl_runs = @()
$cpp_runs = @()

for ($i = 1; $i -le 5; $i++) {
    $hphl = (Measure-Command { & '.\binary-trees-hphl.exe' 2>$null }).TotalMilliseconds
    $cpp  = (Measure-Command { & '.\binary-trees-cpp.exe'  2>$null }).TotalMilliseconds
    $hphl_runs += $hphl
    $cpp_runs  += $cpp
    Write-Host ("Run {0}: HP-HL={1,5:N0} ms  C++={2,5:N0} ms  ratio={3,5:N2}x" -f $i, $hphl, $cpp, ($hphl / $cpp))
}

Write-Host ""
Write-Host "=== Medias (5 runs) ==="
$hphl_avg = ($hphl_runs | Measure-Object -Average).Average
$cpp_avg  = ($cpp_runs  | Measure-Object -Average).Average
Write-Host ("HP-HL: {0,6:N0} ms (min={1}, max={2})" -f $hphl_avg, ($hphl_runs | Measure-Object -Minimum).Minimum, ($hphl_runs | Measure-Object -Maximum).Maximum)
Write-Host ("C++:   {0,6:N0} ms (min={1}, max={2})" -f $cpp_avg,  ($cpp_runs  | Measure-Object -Minimum).Minimum, ($cpp_runs  | Measure-Object -Maximum).Maximum)
Write-Host ("Ratio HP-HL/C++: {0,5:N2}x" -f ($hphl_avg / $cpp_avg))
Write-Host ""

# Memory
Write-Host "=== Memória (peak working set) ==="

$hphl_proc = Start-Process -FilePath '.\binary-trees-hphl.exe' -PassThru -NoNewWindow -RedirectStandardOutput 'nul'
$hphl_proc.WaitForExit()
$hphl_mem = [math]::Round($hphl_proc.PeakWorkingSet64 / 1MB, 1)

$cpp_proc = Start-Process -FilePath '.\binary-trees-cpp.exe' -PassThru -NoNewWindow -RedirectStandardOutput 'nul'
$cpp_proc.WaitForExit()
$cpp_mem = [math]::Round($cpp_proc.PeakWorkingSet64 / 1MB, 1)

Write-Host ("HP-HL peak: {0} MB" -f $hphl_mem)
Write-Host ("C++   peak: {0} MB" -f $cpp_mem)
