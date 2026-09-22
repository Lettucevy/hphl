# run_tests_dap.ps1 â€” smoke test do servidor DAP (M7): hphlc --dap
# SessÃ£o completa: initialize â†’ launch â†’ breakpoint â†’ continue â†’ stopped â†’
# stackTrace â†’ scopes â†’ variables â†’ evaluate â†’ continue â†’ terminated.
param(
  [string]$Hphlc = "$PSScriptRoot\..\hphlc.exe",
  [string]$Program = "$PSScriptRoot\..\examples\dbg_teste.hphl"
)

$ErrorActionPreference = "Stop"
$pass = 0; $fail = 0
$programOut = ""   # acumula os eventos output (prints do programa depurado)
function Check([string]$label, [bool]$cond) {
  if ($cond) { $script:pass++; Write-Host "  PASS $label" }
  else { $script:fail++; Write-Host "  FAIL $label" }
}

$env:HPHL_EXE_DIR = "$PSScriptRoot\..\compiler"
$ucrt = "C:\msys64\ucrt64\bin"
if (Test-Path $ucrt) { $env:PATH = $ucrt + ";" + $env:PATH }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Hphlc
$psi.Arguments = "--dap"
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.CreateNoWindow = $true
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi
$null = $proc.Start()

$in = $proc.StandardInput

function Send([string]$body) {
  $in.Write("Content-Length: $($body.Length)`r`n`r`n")
  $in.Write($body)
  $in.Flush()
}
function SendReq([string]$method, $params, [string]$id) {
  $p = ""
  if ($null -ne $params) { $p = $params }
  Send ("{""jsonrpc"":""2.0"",""id"":" + $id + ",""method"":""" + $method + """,""params"":" + $p + "}")
}
function RecvFrame() {
  $line = $proc.StandardOutput.ReadLine()
  if ($null -eq $line) { throw "stdout fechado" }
  $parts = $line.Split(":")
  $len = [int]$parts[1].Trim()
  while ($true) {
    $h = $proc.StandardOutput.ReadLine()
    if ($h -eq "") { break }
  }
  $buf = New-Object char[] $len
  $n = 0
  while ($n -lt $len) {
    $r = $proc.StandardOutput.Read($buf, $n, $len - $n)
    if ($r -le 0) { throw "stdout fechado no corpo" }
    $n += $r
  }
  $msg = (-join $buf) | ConvertFrom-Json
  if ($msg.method -eq "output" -and $null -ne $msg.params.output) {
    $script:programOut += $msg.params.output
    if ($msg.params.output -match "\[dbg\]") { Write-Host "   [RUNTIME] $($msg.params.output.Trim())" }
  }
  return $msg
}
# espera um frame com id especÃ­fico, pulando eventos (output etc.)
function RecvId([string]$id, [string]$label) {
  while ($true) {
    $msg = RecvFrame
    Write-Host "   <- $label => $($msg.method) id=$($msg.id)"
    if ("$($msg.id)" -eq $id) { return $msg }
  }
}
# espera um evento com method especÃ­fico
function RecvMethod([string]$m, [string]$label) {
  while ($true) {
    $msg = RecvFrame
    Write-Host "   <- $label => $($msg.method) id=$($msg.id)"
    if ($msg.method -eq $m) { return $msg }
  }
}

Write-Host "== initialize"
SendReq "initialize" "{""clientID"":""hphl-test"",""adapterID"":""hphl""}" "1"
$r = RecvId "1" "initialize"
Check "initialize ok" ($null -ne $r.result)

Write-Host "== launch (compila com --debug e spawna)"
$prg = $Program.Replace('\','/')
SendReq "launch" "{""program"":""$prg""}" "2"
$r = RecvId "2" "launch"
Check "launch ok (sem error)" ($null -eq $r.error)
if ($null -ne $r.error) { throw "launch falhou: $($r.error.message)" }
$r = RecvMethod "initialized" "initialized"
Check "event initialized" ($r.method -eq "initialized")

Write-Host "== setBreakpoints (linha 20 -> 0-based 19)"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg""},""breakpoints"":[{""line"":19}]}" "3"
$r = RecvId "3" "setBreakpoints"
Check "bp linha 19 verificado" ($r.result.breakpoints[0].verified -eq $true)

SendReq "configurationDone" "{}" "4"
$null = RecvId "4" "configurationDone"

Write-Host "== continue ate o breakpoint"
SendReq "continue" "{}" "5"
$null = RecvId "5" "continue"
$r = RecvMethod "stopped" "stopped"
Check "stopped (breakpoint)" ($r.method -eq "stopped" -and $r.params.reason -eq "breakpoint")

Write-Host "== stackTrace"
SendReq "stackTrace" "{}" "6"
$r = RecvId "6" "stackTrace"
Check "1 frame no topo (Main)" ($r.result.frames.Count -eq 1 -and $r.result.frames[0].name -eq "Main")
Check "frame na linha 20 (0-based 19)" ($r.result.frames[0].line -eq 19)

Write-Host "== scopes + variables"
SendReq "scopes" "{""frameId"":0}" "7"
$r = RecvId "7" "scopes"
Check "2 scopes" ($r.result.scopes.Count -eq 2)
$locRef = $r.result.scopes[0].variablesReference
SendReq "variables" "{""variablesReference"":$locRef}" "8"
$r = RecvId "8" "variables"
$names = @($r.result.variables | ForEach-Object { $_.name })
Check "local num presente" ($names -contains "num")
$num = $r.result.variables | Where-Object { $_.name -eq "num" } | Select-Object -First 1
Check "num com variablesReference 0" ($num.variablesReference -eq 0)

Write-Host "== next (linha 21) + evaluate num+3"
SendReq "next" "{}" "9"
$null = RecvId "9" "next"
$r = RecvMethod "stopped" "stopped"
Check "stopped apos next" ($r.method -eq "stopped")
Check "reason step" ($r.params.reason -eq "step")
SendReq "stackTrace" "{}" "10"
$r = RecvId "10" "stackTrace"
Check "next: frame na linha 21 (0-based 20)" ($r.result.frames[0].line -eq 20)

Write-Host "== evaluate"
SendReq "evaluate" "{""expression"":""num + 3"",""context"":""repl""}" "11"
$r = RecvId "11" "evaluate"
if ($null -ne $r.result) { Write-Host "   evaluate num+3 => $($r.result.result)" }
else { Write-Host "   evaluate num+3 => ERROR: $($r.error.message)" }
Check "evaluate num+3" ($r.result.result -eq "8")
SendReq "evaluate" '{"expression":"\"ola\" + \" mundo\"","context":"repl"}' "12"
$r = RecvId "12" "evaluate"
if ($null -ne $r.result) { Write-Host "   evaluate concat => $($r.result.result)" }
else { Write-Host "   evaluate concat => ERROR: $($r.error.message)" }
Check "evaluate string concat" ($r.result.result -eq "`"ola mundo`"")

Write-Host "== next (linha 22) + leitura fat"
SendReq "next" "{}" "13"
$null = RecvId "13" "next"
$r = RecvMethod "stopped" "stopped"
Check "stopped apos next 2" ($r.method -eq "stopped")
SendReq "stackTrace" "{}" "14"
$r = RecvId "14" "stackTrace"
Check "next: frame na linha 22 (0-based 21)" ($r.result.frames[0].line -eq 21)
SendReq "scopes" "{""frameId"":0}" "15"
$r = RecvId "15" "scopes"
$locRef = $r.result.scopes[0].variablesReference
SendReq "variables" "{""variablesReference"":$locRef}" "16"
$r = RecvId "16" "variables"
$fat = $r.result.variables | Where-Object { $_.name -eq "fat" } | Select-Object -First 1
Write-Host "   fat value => $($fat.value)"
Check "fat = 120" ($fat.value -eq "120")

Write-Host "== setVariable (local escalar)"
SendReq "evaluate" "{""expression"":""fat + 1"",""context"":""repl""}" "159"
$r = RecvId "159" "evaluate"
Check "sanity evaluate fat+1" ($r.result.result -eq "121")
SendReq "setVariable" "{""variablesReference"":$locRef,""name"":""fat"",""value"":""200""}" "160"
$r = RecvId "160" "setVariable"
Check "setVariable fat = 200" ($r.result.value -eq "200")
SendReq "variables" "{""variablesReference"":$locRef}" "161"
$r = RecvId "161" "variables"
$fat2 = $r.result.variables | Where-Object { $_.name -eq "fat" } | Select-Object -First 1
Check "fat relido = 200" ($fat2.value -eq "200")
SendReq "setVariable" "{""variablesReference"":$locRef,""name"":""fat"",""value"":""120""}" "162"
$r = RecvId "162" "setVariable"
Check "setVariable fat = 120 (volta)" ($r.result.value -eq "120")

Write-Host "== scopes globais + setVariable (global escalar)"
SendReq "scopes" "{""frameId"":0}" "163"
$r = RecvId "163" "scopes"
$gblRef = $r.result.scopes[1].variablesReference
SendReq "variables" "{""variablesReference"":$gblRef}" "164"
$r = RecvId "164" "variables"
$g = $r.result.variables | Where-Object { $_.name -eq "GLOBAL" } | Select-Object -First 1
Check "GLOBAL = 42" ($g.value -eq "42")
SendReq "setVariable" "{""variablesReference"":$gblRef,""name"":""GLOBAL"",""value"":""100""}" "165"
$r = RecvId "165" "setVariable"
Check "setVariable GLOBAL = 100" ($r.result.value -eq "100")
SendReq "variables" "{""variablesReference"":$gblRef}" "166"
$r = RecvId "166" "variables"
$g = $r.result.variables | Where-Object { $_.name -eq "GLOBAL" } | Select-Object -First 1
Check "GLOBAL relido = 100" ($g.value -eq "100")
SendReq "setVariable" "{""variablesReference"":$gblRef,""name"":""GLOBAL"",""value"":""42""}" "167"
$r = RecvId "167" "setVariable"
Check "setVariable GLOBAL = 42 (volta)" ($r.result.value -eq "42")

Write-Host "== breakpoint condicional verdadeira (linha 24, cond num > 3)"
SendReq "setBreakpoints" "{""source"":{""path"":""$($Program.Replace('\','/'))""},""breakpoints"":[{""line"":23,""condition"":""num > 3""}]}" "168"
$null = RecvId "168" "setBreakpoints"
SendReq "continue" "{}" "169"
$null = RecvId "169" "continue"
$r = RecvMethod "stopped" "stopped"
Check "parou no bp condicional verdadeira" ($r.params.reason -eq "breakpoint" -and $r.params.description -match "condicional")
Write-Host "   descricao: $($r.params.description)"

Write-Host "== breakpoint condicional com erro (linha 25, cond 'num >')"
SendReq "setBreakpoints" "{""source"":{""path"":""$($Program.Replace('\','/'))""},""breakpoints"":[{""line"":24,""condition"":""num >""}]}" "170"
$null = RecvId "170" "setBreakpoints"
SendReq "continue" "{}" "171"
$null = RecvId "171" "continue"
$r = RecvMethod "stopped" "stopped"
Check "parou com erro de condicao descrito" ($r.params.description -match "erro")
Write-Host "   descricao: $($r.params.description)"

Write-Host "== breakpoint condicional falsa (linha 31, cond fat < 100) -> auto-continue ate o fim"
SendReq "setBreakpoints" "{""source"":{""path"":""$($Program.Replace('\','/'))""},""breakpoints"":[{""line"":30,""condition"":""fat < 100""}]}" "172"
$null = RecvId "172" "setBreakpoints"
SendReq "continue" "{}" "173"
$null = RecvId "173" "continue"
$r = RecvMethod "terminated" "terminated"
Check "terminated (condicao falsa nao parou)" ($r.method -eq "terminated")

Write-Host "== output do programa (prints do runtime via stdout pipe)"
Start-Sleep -Milliseconds 300
Check "print do programa chegou ao cliente (fat(5) = 120)" ($programOut -match "fat\(5\) = 120")
if (-not ($programOut -match "fat\(5\) = 120")) {
  Write-Host "   outputs recebidos: $($programOut -replace "`n","\n")"
}

SendReq "disconnect" "{}" "18"
$null = RecvId "18" "disconnect"
Start-Sleep -Milliseconds 300
if (-not $proc.HasExited) { $proc.Kill() }
$in.Dispose()

Write-Host ""
Write-Host "== F1.2: expansao de classes e arrays multidimensionais (dbg_teste2)"
if (-not $proc.HasExited) { $proc.Kill() }
$psi2 = New-Object System.Diagnostics.ProcessStartInfo
$psi2.FileName = $Hphlc
$psi2.Arguments = "--dap"
$psi2.UseShellExecute = $false
$psi2.RedirectStandardInput = $true
$psi2.RedirectStandardOutput = $true
$psi2.RedirectStandardError = $true
$psi2.CreateNoWindow = $true
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi2
$null = $proc.Start()
$in = $proc.StandardInput
$prg2 = "$PSScriptRoot\..\examples\dbg_teste2.hphl".Replace('\','/')

SendReq "initialize" "{""clientID"":""hphl-test"",""adapterID"":""hphl""}" "200"
$r = RecvId "200" "initialize F1.2"
Check "F1.2 initialize ok" ($null -ne $r.result)
SendReq "launch" "{""program"":""$prg2""}" "201"
$r = RecvId "201" "launch F1.2"
Check "F1.2 launch ok" ($null -eq $r.error)
if ($null -ne $r.error) { throw "launch F1.2 falhou: $($r.error.message)" }
$null = RecvMethod "initialized" "initialized F1.2"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg2""},""breakpoints"":[{""line"":19}]}" "202"
$r = RecvId "202" "setBreakpoints F1.2"
Check "F1.2 bp linha 19 verificado" ($r.result.breakpoints[0].verified -eq $true)
SendReq "configurationDone" "{}" "203"
$null = RecvId "203" "configurationDone F1.2"
SendReq "continue" "{}" "204"
$null = RecvId "204" "continue F1.2"
$r = RecvMethod "stopped" "stopped F1.2"
Check "F1.2 stopped (breakpoint)" ($r.params.reason -eq "breakpoint")
SendReq "stackTrace" "{}" "205"
$r = RecvId "205" "stackTrace F1.2"
Check "F1.2 frame Main na linha 19" ($r.result.frames[0].name -eq "Main" -and $r.result.frames[0].line -eq 19)

Write-Host "== F1.2 variables: p (classe), m (int[2][3]), arr (int[3])"
SendReq "scopes" "{""frameId"":0}" "206"
$r = RecvId "206" "scopes F1.2"
$locRef = $r.result.scopes[0].variablesReference
SendReq "variables" "{""variablesReference"":$locRef}" "207"
$r = RecvId "207" "variables F1.2 (locais)"
$v = $r.result.variables
$p = $v | Where-Object { $_.name -eq "p" } | Select-Object -First 1
$m = $v | Where-Object { $_.name -eq "m" } | Select-Object -First 1
$arr = $v | Where-Object { $_.name -eq "arr" } | Select-Object -First 1
Check "F1.2 p e nÃ³ expansÃ­vel" ($null -ne $p -and $p.variablesReference -gt 0)
Check "F1.2 m e nÃ³ expansÃ­vel" ($null -ne $m -and $m.variablesReference -gt 0)
Check "F1.2 arr e nÃ³ expansÃ­vel" ($null -ne $arr -and $arr.variablesReference -gt 0)

SendReq "variables" "{""variablesReference"":$($p.variablesReference)}" "208"
$r = RecvId "208" "variables p"
$pf = $r.result.variables
$idade = $pf | Where-Object { $_.name -eq "idade" } | Select-Object -First 1
$nome = $pf | Where-Object { $_.name -eq "nome" } | Select-Object -First 1
Check "F1.2 p.idade = 30" ($null -ne $idade -and $idade.value -eq "30")
Check "F1.2 p.nome = Ana" ($null -ne $nome -and $nome.value -eq "`"Ana`"")

SendReq "variables" "{""variablesReference"":$($m.variablesReference)}" "209"
$r = RecvId "209" "variables m"
$mv = $r.result.variables
Check "F1.2 m com 2 sub-arrays" ($mv.Count -eq 2)
$m0 = $mv | Where-Object { $_.name -eq "m[0]" } | Select-Object -First 1
$m1 = $mv | Where-Object { $_.name -eq "m[1]" } | Select-Object -First 1
Check "F1.2 m[0] expansÃ­vel" ($null -ne $m0 -and $m0.variablesReference -gt 0)
Check "F1.2 m[1] expansÃ­vel" ($null -ne $m1 -and $m1.variablesReference -gt 0)
SendReq "variables" "{""variablesReference"":$($m0.variablesReference)}" "210"
$r = RecvId "210" "variables m[0]"
$vals0 = @($r.result.variables | ForEach-Object { $_.value })
Check "F1.2 m[0] = 1,2,3" (($vals0 -join ",") -eq "1,2,3")
SendReq "variables" "{""variablesReference"":$($m1.variablesReference)}" "211"
$r = RecvId "211" "variables m[1]"
$vals1 = @($r.result.variables | ForEach-Object { $_.value })
Check "F1.2 m[1] = 4,5,6" (($vals1 -join ",") -eq "4,5,6")

SendReq "variables" "{""variablesReference"":$($arr.variablesReference)}" "212"
$r = RecvId "212" "variables arr"
$valsA = @($r.result.variables | ForEach-Object { $_.value })
Check "F1.2 arr = 10,20,30" (($valsA -join ",") -eq "10,20,30")

Write-Host "== F1.2 continue ate o fim"
SendReq "continue" "{}" "213"
$null = RecvId "213" "continue F1.2"
$r = RecvMethod "terminated" "terminated F1.2"
Check "F1.2 terminated" ($r.method -eq "terminated")
Start-Sleep -Milliseconds 300
Check "F1.2 prints do programa (nome = Ana)" ($programOut -match "nome = Ana")
if (-not $proc.HasExited) { $proc.Kill() }
$in.Dispose()

Write-Host "== F2.2: enums simples no watch e no painel (dbg_teste4)"
if (-not $proc.HasExited) { $proc.Kill() }
$psi4 = New-Object System.Diagnostics.ProcessStartInfo
$psi4.FileName = $Hphlc
$psi4.Arguments = "--dap"
$psi4.UseShellExecute = $false
$psi4.RedirectStandardInput = $true
$psi4.RedirectStandardOutput = $true
$psi4.RedirectStandardError = $true
$psi4.CreateNoWindow = $true
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi4
$null = $proc.Start()
$in = $proc.StandardInput
$prg4 = "$PSScriptRoot\..\examples\dbg_teste4.hphl".Replace('\','/')

SendReq "initialize" "{""clientID"":""hphl-test"",""adapterID"":""hphl""}" "220"
$r = RecvId "220" "initialize F2.2"
Check "F2.2 initialize ok" ($null -ne $r.result)
SendReq "launch" "{""program"":""$prg4""}" "221"
$r = RecvId "221" "launch F2.2"
Check "F2.2 launch ok" ($null -eq $r.error)
if ($null -ne $r.error) { throw "launch F2.2 falhou: $($r.error.message)" }
$null = RecvMethod "initialized" "initialized F2.2"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg4""},""breakpoints"":[{""line"":10}]}" "222"
$r = RecvId "222" "setBreakpoints F2.2"
Check "F2.2 bp linha 10 verificado" ($r.result.breakpoints[0].verified -eq $true)
SendReq "configurationDone" "{}" "223"
$null = RecvId "223" "configurationDone F2.2"
SendReq "continue" "{}" "224"
$null = RecvId "224" "continue F2.2"
$r = RecvMethod "stopped" "stopped F2.2"
Check "F2.2 stopped (breakpoint)" ($r.params.reason -eq "breakpoint")

Write-Host "== F2.2 variables: e/f (enum local) e g (enum global)"
SendReq "scopes" "{""frameId"":0}" "225"
$r = RecvId "225" "scopes F2.2"
$locRef4 = $r.result.scopes[0].variablesReference
$gblRef4 = $r.result.scopes[1].variablesReference
SendReq "variables" "{""variablesReference"":$locRef4}" "226"
$r = RecvId "226" "variables F2.2 (locais)"
$v4 = $r.result.variables
$e4 = $v4 | Where-Object { $_.name -eq "e" } | Select-Object -First 1
$f4 = $v4 | Where-Object { $_.name -eq "f" } | Select-Object -First 1
Check "F2.2 e = Conectado" ($null -ne $e4 -and $e4.value -eq "Conectado" -and $e4.type -eq "main.Estado")
Check "F2.2 f = Parado" ($null -ne $f4 -and $f4.value -eq "Parado")
SendReq "variables" "{""variablesReference"":$gblRef4}" "227"
$r = RecvId "227" "variables F2.2 (globais)"
$g4 = @($r.result.variables | Where-Object { $_.name -eq "g" } | Select-Object -First 1)
$gv = $g4[0]
Check "F2.2 g = Pausado" ($null -ne $gv -and $gv.value -eq "Pausado")

Write-Host "== F2.2 watch (evaluate)"
SendReq "evaluate" "{""expression"":""e"",""frameId"":0,""context"":""watch""}" "228"
$r = RecvId "228" "evaluate F2.2 e"
Check "F2.2 watch e = Conectado" ($null -ne $r.result -and $r.result.result -eq "Conectado")
SendReq "evaluate" "{""expression"":""g"",""frameId"":0,""context"":""watch""}" "229"
$r = RecvId "229" "evaluate F2.2 g"
Check "F2.2 watch g = Pausado" ($null -ne $r.result -and $r.result.result -eq "Pausado")
SendReq "evaluate" "{""expression"":""e == 1"",""frameId"":0,""context"":""watch""}" "230"
$r = RecvId "230" "evaluate F2.2 e == 1"
Check "F2.2 watch e == 1 = true" ($null -ne $r.result -and $r.result.result -eq "true")

Write-Host "== F2.2 continue ate o fim"
SendReq "continue" "{}" "231"
$null = RecvId "231" "continue F2.2"
$r = RecvMethod "terminated" "terminated F2.2"
Check "F2.2 terminated" ($r.method -eq "terminated")
Start-Sleep -Milliseconds 300
Check "F2.2 prints do programa (e = 1, g = 10)" ($programOut -match "e = 1" -and $programOut -match "g = 10")
if (-not $proc.HasExited) { $proc.Kill() }
$in.Dispose()

Write-Host ""
Write-Host "== F2.3: hit-count de breakpoints (dbg_teste5)"
if (-not $proc.HasExited) { $proc.Kill() }
$psi5 = New-Object System.Diagnostics.ProcessStartInfo
$psi5.FileName = $Hphlc
$psi5.Arguments = "--dap"
$psi5.UseShellExecute = $false
$psi5.RedirectStandardInput = $true
$psi5.RedirectStandardOutput = $true
$psi5.RedirectStandardError = $true
$psi5.CreateNoWindow = $true
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi5
$null = $proc.Start()
$in = $proc.StandardInput
$prg5 = "$PSScriptRoot\..\examples\dbg_teste5.hphl".Replace('\','/')

SendReq "initialize" "{""clientID"":""hphl-test"",""adapterID"":""hphl""}" "240"
$r = RecvId "240" "initialize F2.3"
Check "F2.3 initialize ok" ($null -ne $r.result)
SendReq "launch" "{""program"":""$prg5""}" "241"
$r = RecvId "241" "launch F2.3"
Check "F2.3 launch ok" ($null -eq $r.error)
if ($null -ne $r.error) { throw "launch F2.3 falhou: $($r.error.message)" }
$null = RecvMethod "initialized" "initialized F2.3"

Write-Host "== F2.3 hitCondition '3' (linha print(i), 0-based 5)"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg5""},""breakpoints"":[{""line"":5,""hitCondition"":""3""}]}" "242"
$r = RecvId "242" "setBreakpoints F2.3"
Check "F2.3 bp verificado" ($r.result.breakpoints[0].verified -eq $true)
SendReq "configurationDone" "{}" "243"
$null = RecvId "243" "configurationDone F2.3"
SendReq "continue" "{}" "244"
$null = RecvId "244" "continue F2.3"
$r = RecvMethod "stopped" "stopped F2.3"
Check "F2.3 stopped no 3o hit (breakpoint)" ($r.params.reason -eq "breakpoint" -and $r.params.description -match "hit-count '3'")
Write-Host "   descricao: $($r.params.description)"
SendReq "stackTrace" "{}" "245"
$r = RecvId "245" "stackTrace F2.3"
Check "F2.3 frame na linha 5 (0-based)" ($r.result.frames[0].line -eq 5)
SendReq "scopes" "{""frameId"":0}" "246"
$r = RecvId "246" "scopes F2.3"
$locRef5 = $r.result.scopes[0].variablesReference
SendReq "variables" "{""variablesReference"":$locRef5}" "247"
$r = RecvId "247" "variables F2.3"
$i5 = $r.result.variables | Where-Object { $_.name -eq "i" } | Select-Object -First 1
Check "F2.3 i = 3 no 3o hit" ($null -ne $i5 -and $i5.value -eq "3")

Write-Host "== F2.3 hitCondition '%2' (a cada 2 hits; reconfigurar zerou o contador -> 2o hit na 5a iteracao)"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg5""},""breakpoints"":[{""line"":5,""hitCondition"":""%2""}]}" "248"
$null = RecvId "248" "setBreakpoints F2.3 %2"
SendReq "continue" "{}" "249"
$null = RecvId "249" "continue F2.3 %2"
$r = RecvMethod "stopped" "stopped F2.3 %2"
Check "F2.3 stopped no 2o hit" ($r.params.reason -eq "breakpoint" -and $r.params.description -match "hit-count '%2'")
SendReq "scopes" "{""frameId"":0}" "250"
$r = RecvId "250" "scopes F2.3 %2"
$locRef5 = $r.result.scopes[0].variablesReference
SendReq "variables" "{""variablesReference"":$locRef5}" "251"
$r = RecvId "251" "variables F2.3 %2"
$i5 = $r.result.variables | Where-Object { $_.name -eq "i" } | Select-Object -First 1
Check "F2.3 i = 5 no 2o hit (contador zerou ao reconfigurar)" ($null -ne $i5 -and $i5.value -eq "5")

Write-Host "== F2.3 hitCondition '>5' (nunca satisfaz -> auto-continue ate o fim)"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg5""},""breakpoints"":[{""line"":5,""hitCondition"":"">5""}]}" "252"
$null = RecvId "252" "setBreakpoints F2.3 >5"
SendReq "continue" "{}" "253"
$null = RecvId "253" "continue F2.3 >5"
$r = RecvMethod "terminated" "terminated F2.3 >5"
Check "F2.3 terminated (hit-count nunca satisfez)" ($r.method -eq "terminated")
Start-Sleep -Milliseconds 300
Check "F2.3 prints completos (1..5 e 999)" ($programOut -match "999" -and $programOut -match "5")
if (-not $proc.HasExited) { $proc.Kill() }
$in.Dispose()

Write-Host "== F2.5: threads no depurador (dbg_teste6, pausa por thread)"
$psi6 = New-Object System.Diagnostics.ProcessStartInfo
$psi6.FileName = $Hphlc
$psi6.Arguments = "--dap"
$psi6.UseShellExecute = $false
$psi6.RedirectStandardInput = $true
$psi6.RedirectStandardOutput = $true
$psi6.RedirectStandardError = $true
$psi6.CreateNoWindow = $true
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi6
$null = $proc.Start()
$in = $proc.StandardInput
$prg6 = "$PSScriptRoot\..\examples\dbg_teste6.hphl".Replace('\','/')

SendReq "initialize" "{""clientID"":""hphl-test"",""adapterID"":""hphl""}" "260"
$r = RecvId "260" "initialize F2.5"
Check "F2.5 initialize ok" ($null -ne $r.result)
SendReq "launch" "{""program"":""$prg6""}" "261"
$r = RecvId "261" "launch F2.5"
Check "F2.5 launch ok" ($null -eq $r.error)
if ($null -ne $r.error) { throw "launch F2.5 falhou: $($r.error.message)" }
$null = RecvMethod "initialized" "initialized F2.5"

Write-Host "== F2.5 bp na linha 5 da Tarefa (0-based 4)"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg6""},""breakpoints"":[{""line"":4}]}" "262"
$r = RecvId "262" "setBreakpoints F2.5"
Check "F2.5 bp verificado" ($r.result.breakpoints[0].verified -eq $true)
SendReq "configurationDone" "{}" "263"
$null = RecvId "263" "configurationDone F2.5"
SendReq "continue" "{}" "264"
$null = RecvId "264" "continue F2.5"
$r = RecvMethod "stopped" "stopped F2.5"
Check "F2.5 stopped com threadId de worker" ($r.params.reason -eq "breakpoint" -and $r.params.threadId -gt 0)
$tidW5 = $r.params.threadId

SendReq "threads" "{}" "265"
$r = RecvId "265" "threads F2.5"
$nThr5 = @($r.result.threads).Count
Check "F2.5 threads >= 2 (main + workers)" ($nThr5 -ge 2)
Check "F2.5 threads inclui o tid parado" (@($r.result.threads | Where-Object { $_.id -eq $tidW5 }).Count -eq 1)

Write-Host "== F2.5 continue por thread (threadId = tid parado)"
SendReq "continue" "{""threadId"":$tidW5}" "266"
$null = RecvId "266" "continue por thread F2.5"
$r = RecvMethod "stopped" "stopped F2.5 por-thread"
Check "F2.5 2o stopped na MESMA thread" ($r.params.reason -eq "breakpoint" -and $r.params.threadId -eq $tidW5)

Write-Host "== F2.5 remove bp + continue -> terminated"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg6""},""breakpoints"":[]}" "267"
$null = RecvId "267" "setBreakpoints vazio F2.5"
SendReq "continue" "{}" "268"
$null = RecvId "268" "continue F2.5 final"
$r = RecvMethod "terminated" "terminated F2.5"
Check "F2.5 terminated" ($r.method -eq "terminated")
Start-Sleep -Milliseconds 300
Check "F2.5 prints completos (T1:0..2, T2:0..2, fim)" ($programOut -match "fim")
if (-not $proc.HasExited) { $proc.Kill() }
$in.Dispose()

Write-Host "== F2.6: watchpoints de escrita (dbg_teste7: dataBreakpointInfo/setDataBreakpoints)"
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi
$null = $proc.Start()
$in = $proc.StandardInput
$prg7 = "$PSScriptRoot\..\examples\dbg_teste7.hphl".Replace('\','/')
SendReq "launch" "{""program"":""$prg7""}" "270"
$r = RecvId "270" "launch F2.6"
Check "F2.6 launch ok" ($null -eq $r.error)
if ($null -ne $r.error) { throw "launch F2.6 falhou: $($r.error.message)" }
$null = RecvMethod "initialized" "initialized F2.6"

Write-Host "== F2.6 bp na linha 5 (0-based 4, o for) para parar no inicio do loop"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg7""},""breakpoints"":[{""line"":4}]}" "271"
$r = RecvId "271" "setBreakpoints F2.6"
Check "F2.6 bp verificado" ($r.result.breakpoints[0].verified -eq $true)
SendReq "configurationDone" "{}" "272"
$null = RecvId "272" "configurationDone F2.6"
SendReq "continue" "{}" "273"
$null = RecvId "273" "continue F2.6"
$r = RecvMethod "stopped" "stopped F2.6 (bp)"
Check "F2.6 stopped por breakpoint (para o dataBreakpointInfo)" ($r.params.reason -eq "breakpoint")

Write-Host "== F2.6 dataBreakpointInfo (locais do frame do topo + globais)"
SendReq "dataBreakpointInfo" "{""source"":{""path"":""$prg7""},""line"":4}" "274"
$r = RecvId "274" "dataBreakpointInfo F2.6"
$ids7 = @($r.result.dataBreakpoints | ForEach-Object { $_.dataId })
Check "F2.6 dataBreakpoints inclui loc 0 x" (@($ids7 | Where-Object { $_ -eq "loc 0 x" }).Count -eq 1)
Check "F2.6 dataBreakpoints inclui gbl contador" (@($ids7 | Where-Object { $_ -eq "gbl contador" }).Count -eq 1)

Write-Host "== F2.6 setDataBreakpoints no x, remove o bp, continue -> stopped data breakpoint"
SendReq "setDataBreakpoints" "{""breakpoints"":[{""dataId"":""loc 0 x""}]}" "275"
$r = RecvId "275" "setDataBreakpoints F2.6"
Check "F2.6 data breakpoint verificado" ($r.result.breakpoints[0].verified -eq $true)
SendReq "setBreakpoints" "{""source"":{""path"":""$prg7""},""breakpoints"":[]}" "276"
$r = RecvId "276" "setBreakpoints vazio F2.6"
Check "F2.6 removeu o bp" ($null -eq $r.error)
SendReq "continue" "{}" "277"
$null = RecvId "277" "continue F2.6"
$r = RecvMethod "stopped" "stopped F2.6 (data breakpoint)"
Check "F2.6 stopped reason data breakpoint" ($r.params.reason -eq "data breakpoint")

Write-Host "== F2.6 limpa data breakpoints + continue -> terminated"
SendReq "setDataBreakpoints" "{""breakpoints"":[]}" "278"
$r = RecvId "278" "setDataBreakpoints vazio F2.6"
Check "F2.6 limpeza ok" ($null -eq $r.error)
SendReq "continue" "{}" "279"
$null = RecvId "279" "continue F2.6 final"
$r = RecvMethod "terminated" "terminated F2.6"
Check "F2.6 terminated" ($r.method -eq "terminated")
Start-Sleep -Milliseconds 300
Check "F2.6 prints completos (contador 1..8)" ($programOut -match "8")
if (-not $proc.HasExited) { $proc.Kill() }
$in.Dispose()

Write-Host "== F2.7: logpoints (setBreakpoints com logMessage, dbg_teste7)"
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi
$null = $proc.Start()
$in = $proc.StandardInput
$prg8 = "$PSScriptRoot\..\examples\dbg_teste7.hphl".Replace('\','/')
SendReq "launch" "{""program"":""$prg8""}" "280"
$r = RecvId "280" "launch F2.7"
Check "F2.7 launch ok" ($null -eq $r.error)
if ($null -ne $r.error) { throw "launch F2.7 falhou: $($r.error.message)" }
$null = RecvMethod "initialized" "initialized F2.7"

Write-Host "== F2.7 logpoint na linha 7 (0-based 6): LOG i={i} x={x}"
SendReq "setBreakpoints" "{""source"":{""path"":""$prg8""},""breakpoints"":[{""line"":6,""logMessage"":""LOG i={i} x={x}""}]}" "281"
$r = RecvId "281" "setBreakpoints F2.7"
Check "F2.7 logpoint verificado" ($r.result.breakpoints[0].verified -eq $true)
SendReq "configurationDone" "{}" "282"
$null = RecvId "282" "configurationDone F2.7"
SendReq "continue" "{}" "283"
$null = RecvId "283" "continue F2.7"
$r = RecvMethod "terminated" "terminated F2.7"
Check "F2.7 terminated (logpoint nao pausa)" ($r.method -eq "terminated")
Start-Sleep -Milliseconds 300
Check "F2.7 output tem LOG i=0 x=100" ($programOut -match "LOG i=0 x=100")
Check "F2.7 output tem LOG i=7 x=107" ($programOut -match "LOG i=7 x=107")
if (-not $proc.HasExited) { $proc.Kill() }
$in.Dispose()

Write-Host "== F3.1: excecoes/panic no depurador (dbg_teste8)"
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi
$null = $proc.Start()
$in = $proc.StandardInput
$prg9 = "$PSScriptRoot\..\examples\dbg_teste8.hphl".Replace('\','/')
$programOut = ""
SendReq "launch" "{""program"":""$prg9""}" "290"
$r = RecvId "290" "launch F3.1"
Check "F3.1 launch ok" ($null -eq $r.error)
if ($null -ne $r.error) { throw "launch F3.1 falhou: $($r.error.message)" }
$null = RecvMethod "initialized" "initialized F3.1"
SendReq "configurationDone" "{}" "292"
$null = RecvId "292" "configurationDone F3.1"
SendReq "continue" "{}" "293"
$null = RecvId "293" "continue F3.1"
$r = RecvMethod "stopped" "stopped F3.1 (exception)"
Check "F3.1 stopped reason exception" ($r.params.reason -eq "exception")
Check "F3.1 descricao contem panic" ($r.params.description -like "*panic*boom*")
SendReq "stackTrace" "{}" "294"
$r = RecvId "294" "stackTrace F3.1"
Check "F3.1 stackTrace tem 1 frame" ($r.result.frames.Count -ge 1)
SendReq "scopes" "{""frameId"":0}" "295"
$r = RecvId "295" "scopes F3.1"
$locRef = ($r.result.scopes | Where-Object { $_.name -eq "Locais" }).variablesReference
SendReq "variables" "{""variablesReference"":$locRef}" "296"
$r = RecvId "296" "variables F3.1"
Check "F3.1 variavel x presente" ((@($r.result.variables | Where-Object { $_.name -eq "x" }).Count) -eq 1)
SendReq "exceptionInfo" "{""threadId"":1}" "298"
$r = RecvId "298" "exceptionInfo F3.1"
Check "F3.1 exceptionInfo id hphl:7" ($r.result.exceptionId -eq "hphl:7")
Check "F3.1 exceptionInfo contem boom" ($r.result.description -like "*boom*")
SendReq "continue" "{}" "299"
$null = RecvId "299" "continue F3.1 final"
$r = RecvMethod "terminated" "terminated F3.1"
Check "F3.1 terminated apos exception continue" ($r.method -eq "terminated")
if (-not $proc.HasExited) { $proc.Kill() }
$in.Dispose()

Write-Host "== F3.5: breakpoints de função (setFunctionBreakpoints)"
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi
$null = $proc.Start()
$in = $proc.StandardInput
$prgF = "$PSScriptRoot\..\examples\dbg_teste.hphl".Replace('\','/')
$programOut = ""
SendReq "launch" "{""program"":""$prgF""}" "310"
$r = RecvId "310" "launch F3.5"
Check "F3.5 launch ok" ($null -eq $r.error)
$null = RecvMethod "initialized" "initialized F3.5"
SendReq "setFunctionBreakpoints" "{""breakpoints"":[{""name"":""Calc""}]}" "311"
$r = RecvId "311" "setFunctionBreakpoints F3.5"
Check "F3.5 Calc verificado" ($r.result.breakpoints[0].verified -eq $true)
SendReq "setFunctionBreakpoints" "{""breakpoints"":[{""name"":""FuncaoInexistente""}]}" "312"
$r = RecvId "312" "setFunctionBreakpoints invalida F3.5"
Check "F3.5 funcao inexistente nao verificada" ($r.result.breakpoints[0].verified -eq $false)
# restaura o válido
SendReq "setFunctionBreakpoints" "{""breakpoints"":[{""name"":""Calc""}]}" "313"
$null = RecvId "313" "setFunctionBreakpoints F3.5 again"
SendReq "configurationDone" "{}" "314"
$null = RecvId "314" "configurationDone F3.5"
SendReq "continue" "{}" "315"
$null = RecvId "315" "continue F3.5"
$r = RecvMethod "stopped" "stopped F3.5"
Check "F3.5 stopped breakpoint (funcao Calc)" ($r.params.reason -eq "breakpoint")
SendReq "stackTrace" "{}" "316"
$r = RecvId "316" "stackTrace F3.5"
Check "F3.5 frame Calc" ($r.result.frames[0].name -eq "Calc")
SendReq "setFunctionBreakpoints" "{""breakpoints"":[]}" "317"
$null = RecvId "317" "setFunctionBreakpoints vazio F3.5"
SendReq "continue" "{}" "318"
$null = RecvId "318" "continue F3.5 final"
$r = RecvMethod "terminated" "terminated F3.5"
Check "F3.5 terminated" ($r.method -eq "terminated")
if (-not $proc.HasExited) { $proc.Kill() }
$in.Dispose()

Write-Host ""
Write-Host "== RESULTADO: $pass passaram, $fail falharam"
if ($fail -eq 0) { exit 0 } else { exit 1 }
