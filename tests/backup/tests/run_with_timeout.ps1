param([string]$Exe, [int]$Secs = 10)
$p = Start-Process -FilePath $Exe -NoNewWindow -PassThru -RedirectStandardOutput "$env:TEMP\run_out.txt" -RedirectStandardError "$env:TEMP\run_err.txt"
if (-not $p.WaitForExit($Secs * 1000)) {
    $p.Kill()
    Write-Host "[TIMEOUT apos ${Secs}s]"
}
Get-Content "$env:TEMP\run_out.txt" | Select-Object -First 20
$e = Get-Content "$env:Temp\run_err.txt" -ErrorAction SilentlyContinue | Select-Object -First 3
if ($e) { $e }
