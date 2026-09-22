#!/usr/bin/env powershell
# run_tests_lsp.ps1 — suíte do servidor LSP (hphlc --lsp)
# Conduz conversas JSON-RPC 2.0 pelo stdio e valida respostas/notificações.
# Uso: powershell -ExecutionPolicy Bypass -File tests\run_tests_lsp.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "compiler\hphlc.exe"
if (-not (Test-Path $exe)) {
    Write-Host "hphlc.exe não encontrado. Compile antes (compiler\)." -ForegroundColor Red
    exit 1
}

$tmp = Join-Path $env:TEMP ("hphl_lsp_tests_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmp | Out-Null

$pass = 0
$fail = 0

function Assert-True([bool]$cond, [string]$name, [string]$detail = "") {
    if ($cond) {
        $script:pass++
        Write-Host "  ok: $name" -ForegroundColor Green
    } else {
        $script:fail++
        $suffix = if ($detail) { " - $detail" } else { "" }
        Write-Host "  FALHA: $name$suffix" -ForegroundColor Red
    }
}

# ---------------------------------------------------------------------------
# Cliente JSON-RPC sobre stdio
# ---------------------------------------------------------------------------
function Send-Lsp([System.Diagnostics.Process]$p, [string]$json) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    $header = "Content-Length: $($bytes.Length)`r`n`r`n"
    $p.StandardInput.BaseStream.Write([Text.Encoding]::ASCII.GetBytes($header), 0, $header.Length)
    $p.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
    $p.StandardInput.BaseStream.Flush()
}

function Receive-Lsp([System.Diagnostics.Process]$p) {
    $stream = $p.StandardOutput.BaseStream
    $header = ""
    $buf = New-Object byte[] 1
    while ($true) {
        $n = $stream.Read($buf, 0, 1)
        if ($n -eq 0) { return $null }
        $header += [char]$buf[0]
        if ($header.Length -ge 4 -and $header.Substring($header.Length - 4) -eq "`r`n`r`n") { break }
    }
    $len = 0
    if ($header -match "Content-Length:\s*(\d+)") { $len = [int]$Matches[1] }
    $body = New-Object byte[] $len
    $off = 0
    while ($off -lt $len) {
        $n = $stream.Read($body, $off, $len - $off)
        if ($n -eq 0) { return $null }
        $off += $n
    }
    return [Text.Encoding]::UTF8.GetString($body)
}

function Read-Response([System.Diagnostics.Process]$p, [int]$id) {
    for ($i = 0; $i -lt 20; $i++) {
        $msg = Receive-Lsp $p
        if ($null -eq $msg) { return $null }
        $obj = $msg | ConvertFrom-Json
        if ($obj.id -eq $id) { return $obj }
    }
    return $null
}

function Read-Notification([System.Diagnostics.Process]$p, [string]$method) {
    for ($i = 0; $i -lt 20; $i++) {
        $msg = Receive-Lsp $p
        if ($null -eq $msg) { return $null }
        $obj = $msg | ConvertFrom-Json
        if ($obj.method -eq $method) { return $obj }
    }
    return $null
}

# ---------------------------------------------------------------------------
# Servidor
# ---------------------------------------------------------------------------
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = "--lsp"
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.CreateNoWindow = $true
$p = [System.Diagnostics.Process]::Start($psi)

function Stop-Server([System.Diagnostics.Process]$p) {
    if ($p -and -not $p.HasExited) {
        try {
            Send-Lsp $p '{"jsonrpc":"2.0","id":999,"method":"shutdown"}'
            Read-Response $p 999 | Out-Null
            Send-Lsp $p '{"jsonrpc":"2.0","method":"exit"}'
            $p.WaitForExit(5000) | Out-Null
        } catch { }
        if (-not $p.HasExited) { $p.Kill() }
    }
}

function UriOf([string]$path) {
    return "file:///" + ($path -replace "\\", "/")
}

function Esc-JsonString([string]$s) {
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $s.ToCharArray()) {
        $c = [int]$ch
        if ($ch -eq '"') { $sb.Append('\"') | Out-Null; continue }
        if ($ch -eq '\') { $sb.Append('\\') | Out-Null; continue }
        if ($ch -eq "`r") { $sb.Append('\r') | Out-Null; continue }
        if ($ch -eq "`n") { $sb.Append('\n') | Out-Null; continue }
        if ($ch -eq "`t") { $sb.Append('\t') | Out-Null; continue }
        if ($c -lt 0x20 -or $c -gt 0x7e) { $sb.Append("\u" + $c.ToString("x4")) | Out-Null }
        else { $sb.Append($ch) | Out-Null }
    }
    return '"' + $sb.ToString() + '"'
}

function Open-Doc([System.Diagnostics.Process]$p, [string]$uri, [string]$text) {
    $json = '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":' +
        (Esc-JsonString $uri) + ',"languageId":"hphl","version":1,"text":' +
        (Esc-JsonString $text) + '}}}'
    Send-Lsp $p $json
}

function Change-Doc([System.Diagnostics.Process]$p, [string]$uri, [string]$text, [int]$version) {
    $json = '{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":' +
        (Esc-JsonString $uri) + ',"version":' + $version + '},"contentChanges":[{"text":' +
        (Esc-JsonString $text) + '}]}}'
    Send-Lsp $p $json
}

Write-Host "=== LSP: inicialização ===" -ForegroundColor Cyan
Send-Lsp $p '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}'
$init = Read-Response $p 1
Assert-True ($null -ne $init) "initialize responde"
if ($init) {
    Assert-True ($init.result.capabilities.textDocumentSync -eq 1) "textDocumentSync = full (1)"
    Assert-True ($init.result.capabilities.hoverProvider -eq $true) "hoverProvider"
    Assert-True ($init.result.capabilities.definitionProvider -eq $true) "definitionProvider"
    Assert-True ($init.result.capabilities.documentSymbolProvider -eq $true) "documentSymbolProvider"
    Assert-True ($init.result.capabilities.completionProvider -ne $null) "completionProvider"
    Assert-True ($init.result.serverInfo.name -eq "hphl") "serverInfo.name"
}
Send-Lsp $p '{"jsonrpc":"2.0","method":"initialized","params":{}}'

Write-Host "=== LSP: diagnóstico válido ===" -ForegroundColor Cyan
$good = Join-Path $tmp "hello.hphl"
@"
module hello;

void Main() {
    print("ola");
}
"@ | Set-Content -Path $good -Encoding UTF8
$goodUri = UriOf $good
$goodText = [IO.File]::ReadAllText($good)
Open-Doc $p $goodUri $goodText
$diag = Read-Notification $p "textDocument/publishDiagnostics"
Assert-True ($null -ne $diag) "didOpen publica publishDiagnostics"
if ($diag) {
    Assert-True ($diag.params.uri -eq $goodUri) "diagnóstico no uri certo"
    Assert-True ($diag.params.diagnostics.Count -eq 0) "programa válido → 0 diagnósticos"
}

Write-Host "=== LSP: erro de sintaxe ===" -ForegroundColor Cyan
$bad = Join-Path $tmp "bad.hphl"
@"
module bad;

void Main() {
    int x = ;
}
"@ | Set-Content -Path $bad -Encoding UTF8
$badUri = UriOf $bad
$badText = [IO.File]::ReadAllText($bad)
Open-Doc $p $badUri $badText
$diag2 = Read-Notification $p "textDocument/publishDiagnostics"
Assert-True ($null -ne $diag2) "didOpen (inválido) publica diagnósticos"
if ($diag2) {
    Assert-True ($diag2.params.diagnostics.Count -eq 1) "1 diagnóstico para arquivo com erro"
    $d = $diag2.params.diagnostics[0]
    Assert-True ($d.severity -eq 1) "severity = Error"
    Assert-True ($d.source -eq "hphl") "source = hphl"
    Assert-True ($d.message -match "erro de sintaxe") "mensagem de sintaxe: '$($d.message)'"
    Assert-True ($d.range.start.line -eq 3) "linha do erro = 3 (0-based), veio $($d.range.start.line)"
    Assert-True ($d.range.start.character -ge 12 -and $d.range.start.character -le 13) "coluna do erro próxima de 12-13, veio $($d.range.start.character)"
}

Write-Host "=== LSP: erro semântico via didChange ===" -ForegroundColor Cyan
$sem = Join-Path $tmp "sem.hphl"
$semUri = UriOf $sem
$semGood = "module sem;`n`nvoid Main() {`n    int x = 1;`n}`n"
$semBad = "module sem;`n`nvoid Main() {`n    int x = ""texto"";`n}`n"
Open-Doc $p $semUri $semGood
Read-Notification $p "textDocument/publishDiagnostics" | Out-Null
Change-Doc $p $semUri $semBad 2
$diag3 = Read-Notification $p "textDocument/publishDiagnostics"
Assert-True ($null -ne $diag3) "didChange publica diagnósticos"
if ($diag3) {
    Assert-True ($diag3.params.diagnostics.Count -eq 1) "1 diagnóstico semântico"
    $d = $diag3.params.diagnostics[0]
    Assert-True ($d.message -match "tipo incompat") "mensagem semântica: '$($d.message)'"
    Assert-True ($d.range.start.line -eq 3) "linha 3 (0-based)"
}

Write-Host "=== LSP: coluna UTF-16 ===" -ForegroundColor Cyan
$utf = Join-Path $tmp "utf.hphl"
$utfText = 'module utf;' + "`n`n" + 'void Main() {' + "`n" + '    print("ol' + [char]0xE1 + '"); +' + "`n" + '}' + "`n"
$utfUri = UriOf $utf
Open-Doc $p $utfUri $utfText
$diag4 = Read-Notification $p "textDocument/publishDiagnostics"
Assert-True ($null -ne $diag4) "didOpen (utf) publica diagnósticos"
if ($diag4) {
    Assert-True ($diag4.params.diagnostics.Count -eq 1) "1 diagnóstico utf"
    $d = $diag4.params.diagnostics[0]
    Assert-True ($d.range.start.line -eq 3) "linha do '+' = 3"
    # '    print("olá"); ' = 18 unidades UTF-16 antes do '+' (á = 1 unidade; byte cru seria 19)
    Assert-True ($d.range.start.character -eq 18) "coluna UTF-16 do '+' = 18 (á=1 unidade), veio $($d.range.start.character)"
}

Write-Host "=== LSP: hover ===" -ForegroundColor Cyan
$hover = @{ jsonrpc = "2.0"; id = 20; method = "textDocument/hover"; params = @{ textDocument = @{ uri = $goodUri }; position = @{ line = 3; character = 8 } } } | ConvertTo-Json -Depth 5 -Compress
Send-Lsp $p $hover
$h = Read-Response $p 20
Assert-True ($null -ne $h -and $null -ne $h.result) "hover de 'print' tem resultado"
if ($h -and $null -ne $h.result) {
    Assert-True ($h.result.contents.value -match "builtin") "hover de print = builtin"
    Assert-True ($h.result.range.start.character -eq 4) "range do hover começa no 'print' (col 4)"
}

Write-Host "=== LSP: definition ===" -ForegroundColor Cyan
$def = Join-Path $tmp "def.hphl"
@"
module def;

int Fator(int n) {
    return n;
}

void Main() {
    int r = Fator(3);
}
"@ | Set-Content -Path $def -Encoding UTF8
$defUri = UriOf $def
$defText = [IO.File]::ReadAllText($def)
Open-Doc $p $defUri $defText
Read-Notification $p "textDocument/publishDiagnostics" | Out-Null
$goto = @{ jsonrpc = "2.0"; id = 21; method = "textDocument/definition"; params = @{ textDocument = @{ uri = $defUri }; position = @{ line = 7; character = 12 } } } | ConvertTo-Json -Depth 5 -Compress
Send-Lsp $p $goto
$g = Read-Response $p 21
Assert-True ($null -ne $g -and $null -ne $g.result) "definition tem resultado"
if ($g -and $null -ne $g.result) {
    Assert-True ($g.result.uri -eq $defUri) "definition no mesmo arquivo"
    Assert-True ($g.result.range.start.line -eq 2) "definition vai para a linha da declaração (2), veio $($g.result.range.start.line)"
}

Write-Host "=== LSP: documentSymbol ===" -ForegroundColor Cyan
$sym = @{ jsonrpc = "2.0"; id = 22; method = "textDocument/documentSymbol"; params = @{ textDocument = @{ uri = $defUri } } } | ConvertTo-Json -Depth 5 -Compress
Send-Lsp $p $sym
$s = Read-Response $p 22
Assert-True ($null -ne $s -and $null -ne $s.result) "documentSymbol tem resultado"
if ($s -and $null -ne $s.result) {
    $names = @($s.result | ForEach-Object { $_.name })
    Assert-True ($names -contains "Fator") "documentSymbol contém 'Fator'"
    Assert-True ($names -contains "Main") "documentSymbol contém 'Main'"
    $mainSym = $s.result | Where-Object { $_.name -eq "Main" } | Select-Object -First 1
    Assert-True ($mainSym.kind -eq 12) "kind de Main = Function (12)"
}

Write-Host "=== LSP: completion ===" -ForegroundColor Cyan
$comp = @{ jsonrpc = "2.0"; id = 23; method = "textDocument/completion"; params = @{ textDocument = @{ uri = $defUri }; position = @{ line = 7; character = 5 } } } | ConvertTo-Json -Depth 5 -Compress
Send-Lsp $p $comp
$c = Read-Response $p 23
Assert-True ($null -ne $c -and $null -ne $c.result) "completion tem resultado"
if ($c -and $null -ne $c.result) {
    $labels = @($c.result.items | ForEach-Object { $_.label })
    Assert-True ($labels -contains "print") "completion contém 'print'"
    Assert-True ($labels -contains "int") "completion contém 'int'"
    Assert-True ($labels -contains "Fator") "completion contém 'Fator'"
    Assert-True ($labels -contains "if") "completion contém 'if'"
    $printItem = $c.result.items | Where-Object { $_.label -eq "print" } | Select-Object -First 1
    Assert-True ($printItem.kind -eq 3) "kind de print = Function (3)"
}

Write-Host "=== LSP: encerramento ===" -ForegroundColor Cyan
Send-Lsp $p '{"jsonrpc":"2.0","id":99,"method":"shutdown"}'
$sh = Read-Response $p 99
Assert-True ($null -ne $sh -and $null -eq $sh.result -and $null -eq $sh.error) "shutdown responde com result null"
Send-Lsp $p '{"jsonrpc":"2.0","method":"exit"}'
$exited = $p.WaitForExit(5000)
Assert-True ($exited) "servidor encerra após exit"
Stop-Server $p

Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "LSP: $pass passaram, $fail falharam" -ForegroundColor $(if ($fail -eq 0) { "Green" } else { "Red" })
exit $(if ($fail -eq 0) { 0 } else { 1 })