# smoke test F2.4: listas/enums no backend LLVM (dbg_teste3 --debug)
param([string]$Exe = "$PSScriptRoot\..\compiler\dbg_teste3.exe", [string]$Pipe = "hphl_dbg_llvm3")
$ErrorActionPreference = "Stop"
$server = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe
$proc = Start-Process -FilePath $Exe -PassThru -NoNewWindow
$server.WaitForConnection()
$reader = New-Object System.IO.StreamReader($server)
$writer = New-Object System.IO.StreamWriter($server)
$writer.NewLine = "`n"; $writer.AutoFlush = $true
function Send([string]$cmd) { $writer.WriteLine($cmd); $writer.Flush() }
function Recv() { $l = $reader.ReadLine(); Write-Host "   <- $l"; return $l }
function RecvUntil([string]$prefix) {
  while ($true) { $l = Recv; if ($null -eq $l) { return $null }; if ($l.StartsWith($prefix)) { return $l } }
}
RecvUntil "ready" | Out-Null
RecvUntil "meta" | Out-Null
while ($true) { $l = Recv; if ($l -eq "metaend") { break } }
Send "bpx 1 24 1"
Recv | Out-Null
Send "continue"
RecvUntil "paused" | Out-Null
$frame = Recv
$rbp = $frame.Split(" ")[3]
Send "read 1 $rbp nums"
$n1 = Recv
Send "read 1 $rbp nomes"
$n2 = Recv
Send "read 1 $rbp prods"
$n3 = Recv
Send "readarr 1 $rbp nums 0"
$n4 = Recv
Send "quit"
$proc.WaitForExit(5000) | Out-Null
$server.Dispose()
Write-Host "nums: $n1"
Write-Host "nomes: $n2"
Write-Host "prods: $n3"
Write-Host "nums[0]: $n4"