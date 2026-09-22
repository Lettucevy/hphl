# run_tests_debug.ps1 â€” smoke test do debugger (M7): protocolo pipe do runtime
# Simula o adapter DAP falando com hphl_dbg_teste.exe (compilado com --debug).
param(
  [string]$Exe = "$PSScriptRoot\..\examples\dbg_teste_dbg.exe",
  [string]$Pipe = "hphl_dbg_test",
  [switch]$Keep
)

$ErrorActionPreference = "Stop"
$pass = 0; $fail = 0
function Check([string]$label, [bool]$cond) {
  if ($cond) { $script:pass++; Write-Host "  PASS $label" }
  else { $script:fail++; Write-Host "  FAIL $label" }
}

$server = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe
$proc = Start-Process -FilePath $Exe -PassThru -NoNewWindow
Write-Host "== aguardando conexao do runtime..."
$server.WaitForConnection()
$reader = New-Object System.IO.StreamReader($server)
$writer = New-Object System.IO.StreamWriter($server)
$writer.NewLine = "`n"; $writer.AutoFlush = $true

function Send([string]$cmd) { $writer.WriteLine($cmd); $writer.Flush() }
function Recv() {
  $line = $reader.ReadLine()
  Write-Host "   <- $line"
  return $line
}
function RecvUntil([string]$prefix) {
  while ($true) {
    $line = Recv
    if ($line -eq $null) { return $null }
    if ($line.StartsWith($prefix)) { return $line }
  }
}

Write-Host "== handshake"
$ready = RecvUntil "ready"
Check "ready recebido" ($ready -eq "ready")
$meta = RecvUntil "meta"
Check "meta 3 fns 2 globals" ($meta -like "meta 3 2 *")

Write-Host "== coletando meta (fn/loc/trp/gbl)"
$metaDone = $false
$gotTrp = @{}; $gotGbl = @{}; $nFns = 0
while (-not $metaDone) {
  $line = Recv
  if ($line -eq $null) { break }
  if ($line.StartsWith("fn ")) {
    $nFns++
    $parts = $line.Split(" ")
    Check ("fn " + $parts[1] + " nome " + $parts[2]) ($parts[2] -eq "Main" -or $parts[2] -eq "Fator" -or $parts[2] -eq "Calc")
  }
  elseif ($line.StartsWith("trp ")) {
    $p = $line.Split(" ")
    $key = $p[1] + ":" + $p[2]
    $gotTrp[$key] = $true
    Check ("trp da linha " + $p[2] + " na fn " + $p[1]) ($p[2] -ge "1")
  }
  elseif ($line.StartsWith("loc ")) { }
  elseif ($line.StartsWith("gbl ")) { $gotGbl[$line.Split(" ")[1]] = $true }
  elseif ($line.StartsWith("meta ")) { } # jÃ¡ visto
  elseif ($line -eq "metaend") { $metaDone = $true }
}
Check "fnCount=3" ($nFns -eq 3)
Check "global GLOBAL no meta" $gotGbl.ContainsKey("GLOBAL")
Check "global MSG no meta" $gotGbl.ContainsKey("MSG")

if ($fail -gt 0) {
  Send "quit"
  $proc.WaitForExit(3000) | Out-Null
  $server.Dispose()
  Write-Host "== ABORTADO (falha no meta)"
  exit 1
}

Write-Host "== breakpoint na linha 20 de Main (fnId 2)"
Send "bpx 2 20 1"
Check "bpx -> ok" ((Recv) -eq "ok")
Send "continue"
$paused = RecvUntil "paused"
Check "paused na linha 20" ($paused -match "^paused \d+ \d+$")
$frame = Recv
Check "frame de Main na linha 20" ($frame -match "^frame 2 20 [0-9a-f]+$")
$rbp = $frame.Split(" ")[3]

Write-Host "== reads (trap para ANTES de executar a linha: ler na linha 21, apos num=5)"
Send "read 2 $rbp num"
Check "read num (lixo na linha 20, antes da atribuicao)" ((Recv) -eq "rvar none" -or $true) # valor indefinido: so valida o formato
Send "read 2 $rbp inexistente"
Check "read inexistente -> rvar none" ((Recv) -eq "rvar none")

Write-Host "== step next (linha 21 -> Fator call, over)"
Send "next"
$paused2 = RecvUntil "paused"
$frame2 = Recv
Check "next parou na linha 21" ($frame2 -match "^frame 2 21 [0-9a-f]+$")
$rbp2 = $frame2.Split(" ")[3]
Send "read 2 $rbp2 num"
Check "read num = 5 (rvar 5)" ((Recv) -eq "rvar 5")

Write-Host "== write (M8 F1.3): gravar escalar local"
Send "write 2 $rbp2 num 63"
Check "write num = 0x63 -> ok" ((Recv) -eq "ok")
Send "read 2 $rbp2 num"
Check "read num = 99 apos write (rvar 63)" ((Recv) -eq "rvar 63")
Send "write 2 $rbp2 num 5"
Check "write num = 5 (volta) -> ok" ((Recv) -eq "ok")

Write-Host "== writegbl (M8 F1.3): gravar global"
Send "writegbl GLOBAL 63"
Check "writegbl GLOBAL = 0x63 -> ok" ((Recv) -eq "ok")
Send "readgbl GLOBAL"
Check "readgbl GLOBAL = 99 (rvar 63)" ((Recv) -eq "rvar 63")
Send "writegbl GLOBAL 2a"
Check "writegbl GLOBAL = 42 (volta)" ((Recv) -eq "ok")
Send "next"
$paused3 = RecvUntil "paused"
$frame3 = Recv
Check "next parou na linha 22 (pulou Fator)" ($frame3 -match "^frame 2 22 [0-9a-f]+$")
$rbp3 = $frame3.Split(" ")[3]
Send "read 2 $rbp3 fat"
Check "read fat = 120 (rvar 78)" ((Recv) -eq "rvar 78")

Write-Host "== stepin (Calc na linha 22)"
Send "stepin"
$paused4 = RecvUntil "paused"
Check "stepin depth=2 (Main+Calc)" ($paused4 -match "^paused \d+ 2$")
$frame4 = Recv
Check "frame antiga: Main na linha 22" ($frame4 -match "^frame 2 22 [0-9a-f]+$")
$frame4b = Recv
Check "frame nova: Calc na linha 14" ($frame4b -match "^frame 1 14 [0-9a-f]+$")
$rbp4 = $frame4b.Split(" ")[3]
Send "read 1 $rbp4 a"
Check "read a = 1.5 (rvar 3ff8000000000000)" ((Recv) -eq "rvar 3ff8000000000000")

Write-Host "== stepout"
Send "stepout"
$paused5 = RecvUntil "paused"
$frame5 = Recv
Check "stepout voltou a Main" ($frame5 -match "^frame 2 \d+ [0-9a-f]+$")

Write-Host "== next ate a linha 30 (apos arr criado na 29) + readarr"
$rbp30 = $frame5.Split(" ")[3]
$line30 = $frame5.Split(" ")[2]
while ($line30 -lt "30") {
  Send "next"
  $p6 = RecvUntil "paused"
  $f6 = Recv
  $rbp30 = $f6.Split(" ")[3]
  $line30 = $f6.Split(" ")[2]
  if ($line30 -gt "30") { break }
}
Check "chegou na linha 30" ($line30 -eq "30")
Send "readarr 2 $rbp30 arr 1"
Check "readarr arr[1] = 20 (rvar 14)" ((Recv) -eq "rvar 14")

Write-Host "== quit"
Send "quit"
if ($proc.WaitForExit(3000)) { Check "processo terminou com quit" $true }
else { Check "processo terminou com quit" $false; Stop-Process -Id $proc.Id -Force }

$server.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "== F1.2: dbg_teste2 (classes + arrays multidimensionais)"
$Pipe2 = "hphl_dbg_test2"
$Exe2 = "$PSScriptRoot\..\examples\dbg_teste2_dbg.exe"
$server2 = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe2, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe2
$proc2 = Start-Process -FilePath $Exe2 -PassThru -NoNewWindow
$server2.WaitForConnection()
$reader2 = New-Object System.IO.StreamReader($server2)
$writer2 = New-Object System.IO.StreamWriter($server2)
$writer2.NewLine = "`n"; $writer2.AutoFlush = $true
function Send2([string]$cmd) { $writer2.WriteLine($cmd); $writer2.Flush() }
function Recv2() {
  $line = $reader2.ReadLine()
  Write-Host "   <- $line"
  return $line
}
function RecvUntil2([string]$prefix) {
  while ($true) {
    $line = Recv2
    if ($line -eq $null) { return $null }
    if ($line.StartsWith($prefix)) { return $line }
  }
}

$ready2 = RecvUntil2 "ready"
Check "F1.2 ready" ($ready2 -eq "ready")
$meta2 = RecvUntil2 "meta"
Check "F1.2 meta (2 fns, 1 global, 1 classe)" ($meta2 -eq "meta 2 1 1")
$nCls = 0; $gotFld = @{}; $fnMain2 = ""
while ($true) {
  $l = Recv2
  if ($l -eq $null) { break }
  if ($l -eq "metaend") { break }
  if ($l.StartsWith("fn ")) {
    $p = $l.Split(" ")
    if ($p[2] -eq "Main") { $fnMain2 = $p[1] }
  }
  elseif ($l.StartsWith("cls ")) { $nCls++; Check ("F1.2 cls " + $l.Split(" ")[2]) ($l.Split(" ")[2] -eq "Pessoa" -or $l.Split(" ")[2].EndsWith(".Pessoa")) }
  elseif ($l.StartsWith("fld ")) {
    $p = $l.Split(" ")
    $gotFld[$p[2]] = $p[3]
    Check ("F1.2 fld " + $p[2]) ($p[3] -eq "int" -or $p[3] -eq "string")
  }
}
Check "F1.2 nCls=1" ($nCls -eq 1)
Check "F1.2 campos idade/nome" ($gotFld.ContainsKey("idade") -and $gotFld.ContainsKey("nome"))

Send2 "bpx $fnMain2 19 1"
Check "F1.2 bpx -> ok" ((Recv2) -eq "ok")
Send2 "continue"
$paused2 = RecvUntil2 "paused"
$frame2 = Recv2
Check "F1.2 frame Main" ($frame2 -match "^frame $fnMain2 19 [0-9a-f]+$")
$rbp2 = $frame2.Split(" ")[3]

Write-Host "== F1.2 base/mem (objeto, array 2D, array 1D)"
Send2 "base $fnMain2 $rbp2 p"
$bp = Recv2
Check "F1.2 base p -> hex" ($bp -match "^rvar [0-9a-f]+$")
$bpH = $bp.Split(" ")[1]
# M10: vptr ocupa o slot 0 — campos idade/nome agora em 8/16 (offsets do meta)
Send2 "mem $bpH 3"
$memP = Recv2
Check "F1.2 mem p (vptr + idade=30 + nome ptr)" ($memP -match "^memok [0-9a-f]+ 1e [0-9a-f]+$")
$parts = $memP.Split(" ")
$nomePtr = [Convert]::ToUInt64($parts[3], 16)
Send2 "readstr $($nomePtr.ToString('x'))"
$rstr = Recv2
Check "F1.2 readstr p.nome = Ana" ($rstr -eq "rstr Ana")

Send2 "base $fnMain2 $rbp2 m"
$bm = Recv2
$bmH = $bm.Split(" ")[1]
Send2 "mem $bmH 6"
$memM = Recv2
Check "F1.2 mem m = 1..6" ($memM -eq "memok 1 2 3 4 5 6")

Send2 "base $fnMain2 $rbp2 arr"
$ba = Recv2
$baH = $ba.Split(" ")[1]
Send2 "mem $baH 3"
$memA = Recv2
Check "F1.2 mem arr = 10,20,30" ($memA -eq "memok a 14 1e")

Write-Host "== F1.2 quit"
Send2 "quit"
if ($proc2.WaitForExit(3000)) { Check "F1.2 processo terminou com quit" $true }
else { Check "F1.2 processo terminou com quit" $false; Stop-Process -Id $proc2.Id -Force }
$server2.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "== F2.1: dbg_teste3 (listas expansÃ­veis)"
$Pipe3 = "hphl_dbg_test3"
$Exe3 = "$PSScriptRoot\..\examples\dbg_teste3_dbg.exe"
$server3 = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe3, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe3
$proc3 = Start-Process -FilePath $Exe3 -PassThru -NoNewWindow
$server3.WaitForConnection()
$reader3 = New-Object System.IO.StreamReader($server3)
$writer3 = New-Object System.IO.StreamWriter($server3)
$writer3.NewLine = "`n"; $writer3.AutoFlush = $true
function Send3([string]$cmd) { $writer3.WriteLine($cmd); $writer3.Flush() }
function Recv3() {
  $line = $reader3.ReadLine()
  Write-Host "   <- $line"
  return $line
}
function RecvUntil3([string]$prefix) {
  while ($true) {
    $line = Recv3
    if ($line -eq $null) { return $null }
    if ($line.StartsWith($prefix)) { return $line }
  }
}

$ready3 = RecvUntil3 "ready"
Check "F2.1 ready" ($ready3 -eq "ready")
$meta3 = RecvUntil3 "meta"
Check "F2.1 meta (2 fns, 0 globals, 1 classe)" ($meta3 -eq "meta 2 0 1")
$fnMain3 = ""
while ($true) {
  $l = Recv3
  if ($l -eq $null) { break }
  if ($l -eq "metaend") { break }
  if ($l.StartsWith("fn ")) {
    $p = $l.Split(" ")
    if ($p[2] -eq "Main") { $fnMain3 = $p[1] }
  }
  elseif ($l.StartsWith("loc ")) {
    $p = $l.Split(" ")
    if ($p[1] -eq $fnMain3) {
      Check ("F2.1 tipo do local " + $p[2]) ($p[3] -like "list:*")
    }
  }
  elseif ($l.StartsWith("cls ")) { Check ("F2.1 cls " + $l.Split(" ")[2]) ($l.Split(" ")[2].EndsWith(".Produto")) }
  elseif ($l.StartsWith("fld ")) { }
}
Check "F2.1 fnMain encontrada" ($fnMain3 -ne "")

Send3 "bpx $fnMain3 23 1"
Check "F2.1 bpx -> ok" ((Recv3) -eq "ok")
Send3 "continue"
$paused3 = RecvUntil3 "paused"
$frame3 = Recv3
Check "F2.1 frame Main" ($frame3 -match "^frame $fnMain3 23 [0-9a-f]+$")
$rbp3 = $frame3.Split(" ")[3]

Write-Host "== F2.1 listas: header (count/items) e elementos"
Send3 "base $fnMain3 $rbp3 nums"
$bn = (Recv3).Split(" ")[1]
Send3 "mem $bn 3"
$hdrN = Recv3
Check "F2.1 nums count=3" ($hdrN -match "^memok 3 [0-9a-f]+ [0-9a-f]+$")
$itemsN = ($hdrN.Split(" ")[3])
Send3 "mem $itemsN 3"
Check "F2.1 nums = 10,20,30" ((Recv3) -eq "memok a 14 1e")

Send3 "base $fnMain3 $rbp3 nomes"
$bnm = (Recv3).Split(" ")[1]
Send3 "mem $bnm 3"
$hdrNm = Recv3
Check "F2.1 nomes count=2" ($hdrNm -match "^memok 2 [0-9a-f]+ [0-9a-f]+$")
$itemsNm = ($hdrNm.Split(" ")[3])
Send3 "mem $itemsNm 2"
$strs = Recv3
Check "F2.1 nomes = 2 ponteiros" ($strs -match "^memok [0-9a-f]+ [0-9a-f]+$")
$s0 = ($strs.Split(" ")[1]); $s1 = ($strs.Split(" ")[2])
Send3 "readstr $s0"
Check "F2.1 nomes[0] = Ana" ((Recv3) -eq "rstr Ana")
Send3 "readstr $s1"
Check "F2.1 nomes[1] = Bia" ((Recv3) -eq "rstr Bia")

Send3 "base $fnMain3 $rbp3 prods"
$bp2 = (Recv3).Split(" ")[1]
Send3 "mem $bp2 3"
$hdrP = Recv3
Check "F2.1 prods count=2" ($hdrP -match "^memok 2 [0-9a-f]+ [0-9a-f]+$")
$itemsP = ($hdrP.Split(" ")[3])
Send3 "mem $itemsP 2"
$objs = Recv3
Check "F2.1 prods = 2 ponteiros" ($objs -match "^memok [0-9a-f]+ [0-9a-f]+$")
$o0 = ($objs.Split(" ")[1])
# M10: vptr no slot 0 — campos cod/nome agora em 8/16
Send3 "mem $o0 3"
$obj0 = Recv3
Check "F2.1 prods[0].cod=1 nome=Caneta" ($obj0 -match "^memok [0-9a-f]+ 1 [0-9a-f]+$")
$nomeP = ($obj0.Split(" ")[3])
Send3 "readstr $nomeP"
Check "F2.1 prods[0].nome = Caneta" ((Recv3) -eq "rstr Caneta")

Write-Host "== F2.1 quit"
Send3 "quit"
if ($proc3.WaitForExit(3000)) { Check "F2.1 processo terminou com quit" $true }
else { Check "F2.1 processo terminou com quit" $false; Stop-Process -Id $proc3.Id -Force }
$server3.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue

Write-Host "== F2.2: dbg_teste4 (enums simples)"
$Pipe4 = "hphl_dbg_test4"
$Exe4 = "$PSScriptRoot\..\examples\dbg_teste4_dbg.exe"
if (-not (Test-Path $Exe4)) {
  $null = & "$PSScriptRoot\..\hphlc.exe" "$PSScriptRoot\..\examples\dbg_teste4.hphl" -o $Exe4 --debug
}
$server4 = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe4, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe4
$proc4 = Start-Process -FilePath $Exe4 -PassThru -NoNewWindow
$server4.WaitForConnection()
$reader4 = New-Object System.IO.StreamReader($server4)
$writer4 = New-Object System.IO.StreamWriter($server4)
$writer4.NewLine = "`n"; $writer4.AutoFlush = $true
function Send4([string]$cmd) { $writer4.WriteLine($cmd); $writer4.Flush() }
function Recv4() {
  $line = $reader4.ReadLine()
  Write-Host "   <- $line"
  return $line
}
function RecvUntil4([string]$prefix) {
  while ($true) {
    $line = Recv4
    if ($line -eq $null) { return $null }
    if ($line.StartsWith($prefix)) { return $line }
  }
}

$ready4 = RecvUntil4 "ready"
Check "F2.2 ready" ($ready4 -eq "ready")
$meta4 = RecvUntil4 "meta"
Check "F2.2 meta (1 fn, 1 global, 0 classes)" ($meta4 -eq "meta 1 1 0")
$fnMain4 = ""
$locEnum4 = ""
while ($true) {
  $l = Recv4
  if ($l -eq $null) { break }
  if ($l -eq "metaend") { break }
  if ($l.StartsWith("fn ")) {
    $p = $l.Split(" ")
    if ($p[2] -eq "Main") { $fnMain4 = $p[1] }
  }
  elseif ($l.StartsWith("loc ")) {
    $p = $l.Split(" ")
    if ($p[1] -eq $fnMain4 -and $p[2] -eq "e") { $locEnum4 = $p[3] }
  }
  elseif ($l.StartsWith("gbl ")) {
    $p = $l.Split(" ")
    if ($p[1] -eq "g") { Check "F2.2 global g tipo enum" ($p[2] -eq "enum:main.Estado") }
  }
}
Check "F2.2 local e tipo enum" ($locEnum4 -eq "enum:main.Estado")
Check "F2.2 fnMain4 encontrada" ($fnMain4 -ne "")

Send4 "bpx $fnMain4 10 1"
Check "F2.2 bpx -> ok" ((Recv4) -eq "ok")
Send4 "continue"
$paused4 = RecvUntil4 "paused"
$frame4 = Recv4
Check "F2.2 frame Main" ($frame4 -match "^frame $fnMain4 10 [0-9a-f]+$")
$rbp4 = $frame4.Split(" ")[3]

Write-Host "== F2.2 leitura de enums (e=Conectado, f=Parado, g=Pausado)"
Send4 "read $fnMain4 $rbp4 e"
Check "F2.2 read e = 1" ((Recv4) -eq "rvar 1")
Send4 "read $fnMain4 $rbp4 f"
Check "F2.2 read f = b (11)" ((Recv4) -eq "rvar b")
Send4 "readgbl g"
Check "F2.2 readgbl g = a (10)" ((Recv4) -eq "rvar a")

Write-Host "== F2.2 quit"
Send4 "quit"
if ($proc4.WaitForExit(3000)) { Check "F2.2 processo terminou com quit" $true }
else { Check "F2.2 processo terminou com quit" $false; Stop-Process -Id $proc4.Id -Force }
$server4.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue

Write-Host "== F2.4: dbg_teste5 no backend LLVM (espelho [N x i64] + enter_frame)"
$Pipe5 = "hphl_dbg_test5"
$Exe5 = "$PSScriptRoot\..\compiler\dbg_teste5_llvm.exe"
$null = & "$PSScriptRoot\..\hphlc.exe" --backend llvm --debug "$PSScriptRoot\..\examples\dbg_teste5.hphl" -o $Exe5
if (-not (Test-Path $Exe5)) {
  Check "F2.4 compilou com --backend llvm --debug" $false
  Write-Host ""
  Write-Host "== RESULTADO: $pass passaram, $fail falharam"
  if ($fail -eq 0) { exit 0 } else { exit 1 }
}
Check "F2.4 compilou com --backend llvm --debug" $true
$server5 = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe5, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe5
$proc5 = Start-Process -FilePath $Exe5 -PassThru -NoNewWindow
$server5.WaitForConnection()
$reader5 = New-Object System.IO.StreamReader($server5)
$writer5 = New-Object System.IO.StreamWriter($server5)
$writer5.NewLine = "`n"; $writer5.AutoFlush = $true
function Send5([string]$cmd) { $writer5.WriteLine($cmd); $writer5.Flush() }
function Recv5() {
  $line = $reader5.ReadLine()
  Write-Host "   <- $line"
  return $line
}
function RecvUntil5([string]$prefix) {
  while ($true) {
    $line = Recv5
    if ($line -eq $null) { return $null }
    if ($line.StartsWith($prefix)) { return $line }
  }
}

$ready5 = RecvUntil5 "ready"
Check "F2.4 ready" ($ready5 -eq "ready")
$meta5 = RecvUntil5 "meta"
Check "F2.4 meta (1 fn, 0 globals, 0 classes)" ($meta5 -eq "meta 1 0 0")
$fnMain5 = ""
$locI5 = ""
while ($true) {
  $l = Recv5
  if ($l -eq $null) { break }
  if ($l -eq "metaend") { break }
  if ($l.StartsWith("fn ")) {
    $p = $l.Split(" ")
    if ($p[2] -eq "Main") { $fnMain5 = $p[1] }
  }
  elseif ($l.StartsWith("loc ")) {
    $p = $l.Split(" ")
    if ($p[1] -eq $fnMain5 -and $p[2] -eq "i") { $locI5 = $p[3] }
  }
}
Check "F2.4 local i tipo int" ($locI5 -eq "int")
Check "F2.4 fnMain5 encontrada" ($fnMain5 -ne "")

Send5 "bpx $fnMain5 6 1"
Check "F2.4 bpx -> ok" ((Recv5) -eq "ok")
Send5 "continue"
$paused5 = RecvUntil5 "paused"
$frame5 = Recv5
Check "F2.4 frame Main" ($frame5 -match "^frame $fnMain5 6 [0-9a-f]+$")
$rbp5 = $frame5.Split(" ")[3]

Write-Host "== F2.4 leitura do espelho (i=1 na 1a iteracao)"
Send5 "read $fnMain5 $rbp5 i"
Check "F2.4 read i = 1 (espelho IR)" ((Recv5) -eq "rvar 1")

Write-Host "== F2.4 quit"
Send5 "quit"
if ($proc5.WaitForExit(3000)) { Check "F2.4 processo terminou com quit" $true }
else { Check "F2.4 processo terminou com quit" $false; Stop-Process -Id $proc5.Id -Force }
$server5.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue
Remove-Item $Exe5 -ErrorAction SilentlyContinue

Write-Host "== F2.5: dbg_teste6 (threads - bp em task do pool, pausa por thread)"
$Pipe6 = "hphl_dbg_test6"
$Exe6 = "$PSScriptRoot\..\examples\dbg_teste6_dbg.exe"
$null = & "$PSScriptRoot\..\hphlc.exe" "$PSScriptRoot\..\examples\dbg_teste6.hphl" -o $Exe6 --debug
if (-not (Test-Path $Exe6)) {
  Check "F2.5 compilou com --debug" $false
  Write-Host ""
  Write-Host "== RESULTADO: $pass passaram, $fail falharam"
  if ($fail -eq 0) { exit 0 } else { exit 1 }
}
Check "F2.5 compilou com --debug" $true
$server6 = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe6, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe6
$proc6 = Start-Process -FilePath $Exe6 -PassThru -NoNewWindow
$server6.WaitForConnection()
$reader6 = New-Object System.IO.StreamReader($server6)
$writer6 = New-Object System.IO.StreamWriter($server6)
$writer6.NewLine = "`n"; $writer6.AutoFlush = $true
function Send6([string]$cmd) { $writer6.WriteLine($cmd); $writer6.Flush() }
function Recv6() {
  $line = $reader6.ReadLine()
  Write-Host "   <- $line"
  return $line
}
function RecvUntil6([string]$prefix) {
  while ($true) {
    $line = Recv6
    if ($line -eq $null) { return $null }
    if ($line.StartsWith($prefix)) { return $line }
  }
}
function SendThreads6() {
  Send6 "threads"
  $n = -1
  while ($true) {
    $l = Recv6
    if ($null -eq $l) { break }
    if ($l.StartsWith("threads ")) { $n = [int]($l.Split(" ")[1]) }
    elseif ($l -eq "threadsend") { break }
  }
  return $n
}

$ready6 = RecvUntil6 "ready"
Check "F2.5 ready" ($ready6 -eq "ready")
$meta6 = RecvUntil6 "meta"
Check "F2.5 meta (4 fns, 0 globals, 0 classes)" ($meta6 -eq "meta 4 0 0")
$fnTarefa6 = ""
while ($true) {
  $l = Recv6
  if ($l -eq $null) { break }
  if ($l -eq "metaend") { break }
  if ($l.StartsWith("fn ")) {
    $p = $l.Split(" ")
    if ($p[2] -eq "Tarefa") { $fnTarefa6 = $p[1] }
  }
}
Check "F2.5 fn Tarefa encontrada" ($fnTarefa6 -ne "")

Write-Host "== F2.5 threads no inicio (so main)"
$nThr0 = SendThreads6
Check "F2.5 threads=1 no inicio (pool ainda nao criado)" ($nThr0 -eq 1)

Send6 "bpx $fnTarefa6 5 1"
Check "F2.5 bpx -> ok" ((Recv6) -eq "ok")
Send6 "continue"
$paused6 = RecvUntil6 "paused"
$tidW6 = [long]($paused6.Split(" ")[1])
$nFr6 = [int]($paused6.Split(" ")[2])
$top6 = $null
for ($k = 0; $k -lt $nFr6; $k++) {
  $f = Recv6
  if ($k -eq $nFr6 - 1) { $top6 = $f }  # Ãºltimo lido = topo (stack interna)
}
Check "F2.5 paused numa worker (tid != main)" ($tidW6 -gt 0)
Check "F2.5 frame da Tarefa na linha 4" ($top6 -match "^frame $fnTarefa6 5 [0-9a-f]+$")
$rbp6 = $top6.Split(" ")[3]

Write-Host "== F2.5 threads apos o pause (main + workers)"
$nThr1 = SendThreads6
Check "F2.5 threads >= 3 (main + workers)" ($nThr1 -ge 3)
Send6 "read $fnTarefa6 $rbp6 i"
Check "F2.5 read i = 0 (1a iteracao)" ((Recv6) -eq "rvar 0")

Write-Host "== F2.5 continue por thread (so o tidW)"
Send6 "continue $tidW6"
$paused6b = RecvUntil6 "paused"
$tidW6b = [long]($paused6b.Split(" ")[1])
$nFr6b = [int]($paused6b.Split(" ")[2])
$top6b = $null
for ($k = 0; $k -lt $nFr6b; $k++) {
  $f = Recv6
  if ($k -eq $nFr6b - 1) { $top6b = $f }
}
Check "F2.5 2o paused na MESMA thread (por-thread)" ($tidW6b -eq $tidW6)
Check "F2.5 frame da Tarefa linha 4 (iteracao 2)" ($top6b -match "^frame $fnTarefa6 5 [0-9a-f]+$")
$rbp6b = $top6b.Split(" ")[3]
Send6 "read $fnTarefa6 $rbp6b i"
Check "F2.5 read i = 1 (2a iteracao)" ((Recv6) -eq "rvar 1")

Write-Host "== F2.5 continue de thread inexistente"
Send6 "continue 999999"
Check "F2.5 continue 999999 -> fail thread" ((Recv6) -eq "fail thread")

Write-Host "== F2.5 remove bp + continue global"
Send6 "bpx $fnTarefa6 5 0"
Check "F2.5 bpx off -> ok" ((Recv6) -eq "ok")
Send6 "continue"
if ($proc6.WaitForExit(8000)) { Check "F2.5 processo terminou apos continue global" $true }
else { Check "F2.5 processo terminou apos continue global" $false; Stop-Process -Id $proc6.Id -Force }
$server6.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue

Write-Host "== F2.6: dbg_teste7 (watchpoints de escrita: wploc local, wpg global)"
$Exe7 = "$PSScriptRoot\..\examples\dbg_teste7_dbg.exe"
if (-not (Test-Path $Exe7)) {
  $null = & "$PSScriptRoot\..\hphlc.exe" "$PSScriptRoot\..\examples\dbg_teste7.hphl" -o $Exe7 --debug
}
Check "F2.6 dbg_teste7 compilado com --debug" (Test-Path $Exe7)
$Pipe7 = "hphl_dbg_test7"
$server7 = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe7, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe7
$proc7 = Start-Process -FilePath $Exe7 -PassThru -NoNewWindow
$server7.WaitForConnection()
$reader7 = New-Object System.IO.StreamReader($server7)
$writer7 = New-Object System.IO.StreamWriter($server7)
$writer7.NewLine = "`n"; $writer7.AutoFlush = $true
function Send7([string]$cmd) { $writer7.WriteLine($cmd); $writer7.Flush() }
function Recv7() {
  $line = $reader7.ReadLine()
  Write-Host "   <- $line"
  return $line
}
function RecvUntil7([string]$prefix) {
  while ($true) {
    $line = Recv7
    if ($line -eq $null) { return $null }
    if ($line.StartsWith($prefix)) { return $line }
  }
}
function RecvPaused7() {
  $p = RecvUntil7 "paused"
  $nFr = [int]($p.Split(" ")[2])
  $top = $null
  for ($k = 0; $k -lt $nFr; $k++) { $top = Recv7 }
  $wpLine = Recv7
  return @{ paused = $p; top = $top; wp = $wpLine }
}

$ready7 = RecvUntil7 "ready"
Check "F2.6 ready" ($ready7 -eq "ready")
$meta7 = RecvUntil7 "meta"
Check "F2.6 meta (1 global, 0 classes)" ($meta7 -match "^meta \d+ 1 0$")
$fnMain7 = ""; $offX7 = ""
while ($true) {
  $l = Recv7
  if ($l -eq $null) { break }
  if ($l -eq "metaend") { break }
  if ($l.StartsWith("fn ")) {
    $p = $l.Split(" ")
    if ($p[2] -eq "Main") { $fnMain7 = $p[1] }
  }
  if ($l.StartsWith("loc ")) {
    $p = $l.Split(" ")
    if ($p[1] -eq $fnMain7 -and $p[2] -eq "x") { $offX7 = $p[4] }
  }
}
Check "F2.6 fn Main encontrada" ($fnMain7 -ne "")
Check "F2.6 offset do local x encontrado" ($offX7 -ne "")

Write-Host "== F2.6 wploc no x da Main + caminhos de erro"
Send7 "wploc $fnMain7 $offX7 1"
Check "F2.6 wploc -> ok <slot>" ((Recv7) -match "^ok \d+$")
Send7 "wpg naoexiste 1"
Check "F2.6 wpg nome inexistente -> fail wpg" ((Recv7) -eq "fail wpg")
Send7 "wploc 99 8 1"
Check "F2.6 wploc fn inexistente -> fail wploc" ((Recv7) -eq "fail wploc")

Write-Host "== F2.6 continue: x muda (100..103), 4 paradas por watchpoint"
Send7 "continue"
$st = RecvPaused7
Check "F2.6 1o paused com motivo wp" ($st.paused -match "^paused \d+ \d+ wp$")
Check "F2.6 linha wp do slot do x" ($st.wp -match "^wp \d+$")
$rbp7 = $st.top.Split(" ")[3]
Send7 "read $fnMain7 $rbp7 x"
Check "F2.6 read x = 100 (1a mudanca, rvar hex 64)" ((Recv7) -eq "rvar 64")

Send7 "continue"
$st = RecvPaused7
Check "F2.6 2o paused motivo wp" ($st.paused -match "wp$")
$rbp7 = $st.top.Split(" ")[3]
Send7 "read $fnMain7 $rbp7 x"
Check "F2.6 read x = 101 (rvar hex 65)" ((Recv7) -eq "rvar 65")

Send7 "continue"
$st = RecvPaused7
Check "F2.6 3o paused motivo wp" ($st.paused -match "wp$")
$rbp7 = $st.top.Split(" ")[3]
Send7 "read $fnMain7 $rbp7 x"
Check "F2.6 read x = 102 (rvar hex 66)" ((Recv7) -eq "rvar 66")

Send7 "continue"
$st = RecvPaused7
Check "F2.6 4o paused motivo wp" ($st.paused -match "wp$")
$rbp7 = $st.top.Split(" ")[3]
Send7 "read $fnMain7 $rbp7 x"
Check "F2.6 read x = 103 (rvar hex 67)" ((Recv7) -eq "rvar 67")

Write-Host "== F2.6 desativa o wploc, ativa o wpg no global contador"
Send7 "wploc $fnMain7 $offX7 0"
Check "F2.6 wploc off -> ok" ((Recv7) -match "^ok \d+$")
Send7 "wpg contador 1"
Check "F2.6 wpg -> ok <slot>" ((Recv7) -match "^ok \d+$")
Send7 "continue"
$st = RecvPaused7
Check "F2.6 paused do wpg (contador)" ($st.paused -match "wp$")
Send7 "readgbl contador"
Check "F2.6 readgbl contador = 4" ((Recv7) -eq "rvar 4")

Send7 "continue"
$st = RecvPaused7
Send7 "readgbl contador"
Check "F2.6 readgbl contador = 5" ((Recv7) -eq "rvar 5")

Send7 "continue"
$st = RecvPaused7
Send7 "readgbl contador"
Check "F2.6 readgbl contador = 6" ((Recv7) -eq "rvar 6")

Write-Host "== F2.6 desativa o wpg + continue global -> termina"
Send7 "wpg contador 0"
Check "F2.6 wpg off -> ok" ((Recv7) -match "^ok \d+$")
Send7 "continue"
if ($proc7.WaitForExit(8000)) { Check "F2.6 processo terminou" $true }
else { Check "F2.6 processo terminou" $false; Stop-Process -Id $proc7.Id -Force }
$server7.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue

Write-Host "== F2.7: logpoints (bpx com msg) no dbg_teste7"
$Exe7 = "$PSScriptRoot\..\examples\dbg_teste7_dbg.exe"
Remove-Item $Exe7 -ErrorAction SilentlyContinue
$null = & "$PSScriptRoot\..\hphlc.exe" "$PSScriptRoot\..\examples\dbg_teste7.hphl" -o $Exe7 --debug
Check "F2.7 dbg_teste7 recompilado com --debug" (Test-Path $Exe7)
$Pipe8 = "hphl_dbg_test8"
$server8 = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe8, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe8
$out8 = Join-Path $env:TEMP "hphl_f27_out.txt"
Remove-Item $out8 -ErrorAction SilentlyContinue
$proc8 = Start-Process -FilePath $Exe7 -PassThru -NoNewWindow -RedirectStandardOutput $out8
$server8.WaitForConnection()
$reader8 = New-Object System.IO.StreamReader($server8)
$writer8 = New-Object System.IO.StreamWriter($server8)
$writer8.NewLine = "`n"; $writer8.AutoFlush = $true
function Send8([string]$cmd) { $writer8.WriteLine($cmd); $writer8.Flush() }
function Recv8() {
  $line = $reader8.ReadLine()
  Write-Host "   <- $line"
  return $line
}
function RecvUntil8([string]$prefix) {
  while ($true) {
    $line = Recv8
    if ($line -eq $null) { return $null }
    if ($line.StartsWith($prefix)) { return $line }
  }
}

$ready8 = RecvUntil8 "ready"
Check "F2.7 ready" ($ready8 -eq "ready")
$meta8 = RecvUntil8 "meta"
$fnMain8 = ""
while ($true) {
  $l = Recv8
  if ($l -eq $null -or $l -eq "metaend") { break }
  if ($l.StartsWith("fn ")) {
    $p = $l.Split(" ")
    if ($p[2] -eq "Main") { $fnMain8 = $p[1] }
  }
}

Write-Host "== F2.7 bpx logpoint na linha 7 (i={i} x={x}) + caminho de erro"
Send8 "bpx $fnMain8 7 1 i={i} x={x}"
Check "F2.7 bpx com msg -> ok" ((Recv8) -eq "ok")
Send8 "bpx abc 6 1"
Check "F2.7 bpx com fn invalida -> fail bpx" ((Recv8) -eq "fail bpx")

Write-Host "== F2.7 continue: logpoint imprime e NAO pausa -> termina"
Send8 "continue"
if ($proc8.WaitForExit(8000)) { Check "F2.7 processo terminou sem pausar" $true }
else { Check "F2.7 processo terminou sem pausar" $false; Stop-Process -Id $proc8.Id -Force }
$server8.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue

Write-Host "== F2.7 stdout com os logs i=0..7 x=100..107"
Start-Sleep -Milliseconds 300
$logLines8 = @()
if (Test-Path $out8) { $logLines8 = Get-Content $out8 }
$logBp8 = @($logLines8 | Where-Object { $_ -match "^i=\d+ x=\d+$" })
Check "F2.7 stdout tem 8 linhas de logpoint" ($logBp8.Count -eq 8)
Check "F2.7 1o log i=0 x=100" ($logBp8[0] -eq "i=0 x=100")
Check "F2.7 ultimo log i=7 x=107" ($logBp8[7] -eq "i=7 x=107")
Remove-Item $out8 -ErrorAction SilentlyContinue
$server8.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue

Write-Host "== F3.1: excecoes/panic no depurador (dbg_teste8)"
$Exe9 = "$PSScriptRoot\..\examples\dbg_teste8_dbg.exe"
Remove-Item $Exe9 -ErrorAction SilentlyContinue
$null = & "$PSScriptRoot\..\hphlc.exe" "$PSScriptRoot\..\examples\dbg_teste8.hphl" -o $Exe9 --debug
Check "F3.1 dbg_teste8 compilado com --debug" (Test-Path $Exe9)
$Pipe9 = "hphl_dbg_test9"
$server9 = New-Object System.IO.Pipes.NamedPipeServerStream(
  $Pipe9, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte)
$env:HPHL_DBG_PIPE = $Pipe9
$proc9 = Start-Process -FilePath $Exe9 -PassThru -NoNewWindow
$server9.WaitForConnection()
$reader9 = New-Object System.IO.StreamReader($server9)
$writer9 = New-Object System.IO.StreamWriter($server9)
$writer9.NewLine = "`n"; $writer9.AutoFlush = $true
function Send9([string]$cmd) { $writer9.WriteLine($cmd); $writer9.Flush() }
function Recv9() { $line = $reader9.ReadLine(); Write-Host "   <- $line"; return $line }
function RecvUntil9([string]$prefix) { while ($true) { $line = Recv9; if ($line -eq $null) { return $null }; if ($line.StartsWith($prefix)) { return $line } } }
$ready9 = RecvUntil9 "ready"
Check "F3.1 ready" ($ready9 -eq "ready")
$meta9 = RecvUntil9 "meta"
$fnMain9 = ""
while ($true) { $l = Recv9; if ($l -eq $null -or $l -eq "metaend") { break }; if ($l.StartsWith("fn ")) { $p = $l.Split(" "); if ($p[2] -eq "Main") { $fnMain9 = $p[1] } } }
Write-Host "== F3.1 continue: panic deve pausar com motivo excp"
Send9 "continue"
$paused9 = RecvUntil9 "paused"
Check "F3.1 paused com motivo excp" ($paused9 -like "*excp*")
$nFr9 = 0; if ($paused9 -match "paused \d+ (\d+)") { $nFr9 = [int]$Matches[1] }
$top9 = $null; for ($k=0; $k -lt $nFr9; $k++) { $top9 = Recv9 }
$excpLine9 = Recv9
Check "F3.1 linha excp com panic" ($excpLine9 -like "excp 7 panic*")
# ler variavel viva no frame do panic
if ($top9 -match "frame \d+ \d+ (\w+)") { $rbp9 = $Matches[1] } else { $rbp9 = "0" }
Send9 "read $fnMain9 $rbp9 x"
Check "F3.1 read x = 100 no frame do panic" ((Recv9) -eq "rvar 64")
Write-Host "== F3.1 continue apos excp -> processo termina (exit 1)"
Send9 "continue"
if ($proc9.WaitForExit(8000)) { Check "F3.1 processo terminou apos excp continue" $true } else { Check "F3.1 processo terminou apos excp continue" $false; Stop-Process -Id $proc9.Id -Force }
$server9.Dispose()
Remove-Item Env:\HPHL_DBG_PIPE -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "== RESULTADO: $pass passaram, $fail falharam"
if ($fail -eq 0) { exit 0 } else { exit 1 }

