# smoke test F2.4: protocolo debug do backend LLVM (dbg_teste5 --debug)
param([string]$Exe = "$PSScriptRoot\..\compiler\dbg_teste5.exe", [string]$Pipe = "hphl_dbg_llvm5")
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
Write-Host "== handshake"
RecvUntil "ready" | Out-Null
$meta = RecvUntil "meta"
Write-Host "   meta: $meta"
$nLoc = 0
while ($true) {
  $l = Recv
  if ($l -eq "metaend") { break }
  if ($l.StartsWith("loc ")) { $nLoc++; Write-Host "   loc: $l" }
}
Write-Host "== locais: $nLoc"
Write-Host "== bpx na linha 6 (print i) + continue"
Send "bpx 0 6 1"
Recv | Out-Null
Send "continue"
$paused = RecvUntil "paused"
Write-Host "   paused: $paused"
$frame = Recv
Write-Host "   frame: $frame"
$rbp = $frame.Split(" ")[3]
Send "read 0 $rbp i"
$rv = Recv
Write-Host "   read i => $rv"
Send "bpx 0 6 0"
Recv | Out-Null
Send "continue"
while ($true) { $l = Recv; if ($null -eq $l) { break }; if ($l -match "999") { Write-Host "   output: $l" } }
Send "quit"
$proc.WaitForExit(5000) | Out-Null
$server.Dispose()
Write-Host "== FIM"