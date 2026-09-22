# smoke test F2.5: threads no depurador (dbg_teste6 --debug, x64) - trace completo
param([string]$Exe = "$PSScriptRoot\..\examples\dbg_teste6_dbg.exe", [string]$Pipe = "hphl_dbg_test6")
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
function Send([string]$cmd) { Write-Host "=> $cmd"; $writer.WriteLine($cmd); $writer.Flush() }
function Recv() { $l = $reader.ReadLine(); Write-Host "   <- $l"; return $l }
function RecvUntil([string]$prefix) {
  while ($true) { $l = Recv; if ($null -eq $l) { return $null }; if ($l.StartsWith($prefix)) { return $l } }
}
function ReadPaused() {
  $p = RecvUntil "paused"
  $nFr = [int]($p.Split(" ")[2])
  $top = $null
  for ($k = 0; $k -lt $nFr; $k++) { $top = Recv }
  return @($p, $top)
}
RecvUntil "ready" | Out-Null
RecvUntil "meta" | Out-Null
$fnT = ""
while ($true) {
  $l = Recv
  if ($l -eq "metaend") { break }
  if ($l.StartsWith("fn ")) { $p = $l.Split(" "); if ($p[2] -eq "Tarefa") { $fnT = $p[1] } }
}
Send "bpx $fnT 5 1"
Recv | Out-Null
Send "continue"
$r1 = ReadPaused
$tidW = [long]($r1[0].Split(" ")[1])
Send "threads"
$tl = Recv
$nTh = [int]($tl.Split(" ")[1])
for ($k = 0; $k -lt $nTh; $k++) { Recv | Out-Null }
Recv | Out-Null
Send "read $fnT $($r1[1].Split(' ')[3]) i"
Recv | Out-Null
Send "continue $tidW"
$r2 = ReadPaused
Send "read $fnT $($r2[1].Split(' ')[3]) i"
Recv | Out-Null
Send "bpx $fnT 5 0"
$rok = Recv
if ($rok -ne "ok") { Write-Host "ERRO: bpx off -> $rok"; $proc.Kill(); exit 1 }
Send "continue"
$proc.WaitForExit(8000) | Out-Null
Write-Host "exit=$($proc.ExitCode) exited=$($proc.HasExited)"
if (-not $proc.HasExited) { $proc.Kill() }
$server.Dispose()
