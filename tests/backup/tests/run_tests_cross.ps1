# run_tests_cross.ps1 — suite de cross-compilation do backend LLVM (M10.5)
#
# Uso:  powershell -ExecutionPolicy Bypass -File run_tests_cross.ps1
#
# Requisitos:
#   - hphlc compilado (rode `make` em compiler/)
#   - wasm-ld.exe (LLVM/lld; procurado em "C:\Program Files\LLVM\bin")
#   - llvm-objdump.exe (MSYS2 ucrt64 ou LLVM)
#   - node.exe (execucao dos modulos WASM via tests/runner_wasm.js)
#
# Verificacoes:
#   1. WASM: compila exemplos com --target wasm32-unknown-unknown (-O0 e -O2),
#      linka com wasm-ld, executa no Node com o shim de runtime
#      (tests/runner_wasm.js) e compara a saida com o esperado da suíte
#      principal (run_tests.ps1 é a fonte única de verdade).
#   2. ARM64: compila objetos com --target aarch64-linux-gnu (ELF) e
#      aarch64-pc-windows-msvc (COFF); valida formato com llvm-objdump.
#
# Limitação conhecida (M10.5c): exemplos com dispatch por INTERFACE
# (ex.: structs.hphl) falham no wasm32 — os slots de vtable são i64 no IR,
# mas o elemento de `global [N x ptr]` tem 4 bytes no alvo; exige abstração
# de tamanho de ponteiro no irgen.

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$root = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $root "compiler\hphlc.exe"
$examples = Join-Path $root "examples"
$tmp = Join-Path $root "tests\_out_cross"

if (-not (Test-Path $compiler)) {
    Write-Host "ERRO: compilador não encontrado em $compiler" -ForegroundColor Red
    exit 1
}

$ucrt = "C:\msys64\ucrt64\bin"
if (Test-Path $ucrt) { $env:PATH = $ucrt + ";" + $env:PATH }

$wasmLd = @("C:\Program Files\LLVM\bin\wasm-ld.exe",
            "C:\msys64\ucrt64\bin\wasm-ld.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $wasmLd) {
    $cmd = Get-Command wasm-ld.exe -ErrorAction SilentlyContinue
    if ($cmd) { $wasmLd = $cmd.Source }
}
if (-not $wasmLd) {
    Write-Host "ERRO: wasm-ld.exe não encontrado (instale LLVM/lld)" -ForegroundColor Red
    exit 1
}

$objdump = @("C:\Program Files\LLVM\bin\llvm-objdump.exe",
             "$ucrt\llvm-objdump.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not (Get-Command node.exe -ErrorAction SilentlyContinue)) {
    Write-Host "ERRO: node.exe não encontrado no PATH" -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# --- extrai expected (name -> texto) do run_tests.ps1 (fonte única) ---
# Excluídos no wasm32: clock_builtin (saída não-determinística), primitives
# (semáforo exige bloqueio real), cancel (loop infinito sob escalonamento
# cooperativo). trycatch/outdef ENTRAM no M14.3: EH por flag de propagação
# (arestas de CFG estáticas) substitui o modelo CONTEXT (returns_twice).
$wasmNames = @("hello", "classes", "policies", "specialchars", "arrays",
               "modules", "lists", "maps", "tuples", "generics_where",
               "valuegen", "visibility_members", "specialize", "compiletime",
               "reflect_meta", "policies_deep", "move", "poly", "overflow",
               "visibility", "alias_reexport", "match", "option", "structs",
               "patterns", "listpat", "cond", "generics", "refout",
               "properties", "match_expr", "concurrent", "nested", "batch",
               "async", "channel", "threadlocal", "awaitchan", "tasks",
               "threads", "parallel_cap", "parallel_foreach", "actor",
               "derives", "trycatch", "outdef")
$x64Only = @()
$expectedMap = @{}
$curName = $null
$curBlock = @()
$inBlock = $false
foreach ($line in (Get-Content (Join-Path $root "tests\run_tests.ps1"))) {
    if ($line -match '^\s*name = "([^"]+)"') {
        $curName = $Matches[1]
        $curBlock = @()
        $inBlock = $false
        continue
    }
    if ($line -match '^\s*expected = "([^"]*)"') {
        if ($curName) { $expectedMap[$curName] = $Matches[1] }
        continue
    }
    if ($line -match '^\s*expected = @"') { $inBlock = $true; continue }
    if ($inBlock) {
        if ($line -match '^"@') {
            if ($curName) { $expectedMap[$curName] = ($curBlock -join "`n") }
            $inBlock = $false
            continue
        }
        $curBlock += $line
    }
}

$pass = 0; $fail = 0

function Compile-Wasm($name, $opt) {
    $src = Join-Path $examples "$name.hphl"
    $outBase = Join-Path $tmp "$name$opt"
    $p = Start-Process -FilePath $compiler -ArgumentList "`"$src`"", "-o", "`"$outBase`"",
          "--backend", "llvm", "--target", "wasm32-unknown-unknown", "-O$opt" `
          -NoNewWindow -Wait -PassThru -RedirectStandardOutput (Join-Path $tmp "out.txt") -RedirectStandardError (Join-Path $tmp "err.txt")
    return @{ rc = $p.ExitCode; wasm = "$outBase.wasm" }
}

foreach ($name in $wasmNames) {
    foreach ($opt in @(0, 2)) {
        $label = "wasm32 $name -O$opt"
        $expected = $expectedMap[$name]
        if ($null -eq $expected) { continue }
        $r = Compile-Wasm $name $opt
        if ($r.rc -ne 0 -or -not (Test-Path $r.wasm)) {
            Write-Host "=== $label ==="; Write-Host "  FALHA (compilacao, rc=$($r.rc))"; Get-Content (Join-Path $tmp "err.txt") -ErrorAction SilentlyContinue | Select-Object -First 3
            $fail++; continue
        }
        $runOut = (& node (Join-Path $PSScriptRoot "runner_wasm.js") $r.wasm main 2>$null)
        if ($LASTEXITCODE -ne 0) {
            Write-Host "=== $label ==="; Write-Host "  FALHA (trap/erro no runner)"
            $fail++; continue
        }
        $norm = (($runOut -join "`n") -replace "`r","").TrimEnd("`n")
        $want = $expected.TrimEnd("`r","`n")
        if ($norm -eq $want -or (($norm -replace "`n","") -eq ($want -replace "`n",""))) {
            Write-Host "=== $label ==="; Write-Host "  OK"
            $pass++
        } else {
            Write-Host "=== $label ==="; Write-Host "  FALHA (saida divergente)"
            Write-Host "  esperado: [$($want -replace "`n","\n")]"
            Write-Host "  obtido  : [$($norm -replace "`n","\n")]"
            $fail++
        }
    }
}

# --- chamada direta de funcao exportada pelo host ---
$r = Compile-Wasm "hello" 1
if ($r.rc -eq 0 -and (Test-Path $r.wasm)) {
    $runOut = (& node (Join-Path $PSScriptRoot "runner_wasm.js") $r.wasm Fatorial 5 2>&1)
    if (($runOut -join "") -match "Fatorial\(\) = 120") {
        Write-Host "=== wasm32 Fatorial(5) via host ==="; Write-Host "  OK"
        $pass++
    } else {
        Write-Host "=== wasm32 Fatorial(5) via host ==="; Write-Host "  FALHA: [$($runOut -join ' ')]"
        $fail++
    }
}

# --- ARM64: ELF (linux) e COFF (windows) ---
$armTriples = @("aarch64-linux-gnu", "aarch64-pc-windows-msvc")
foreach ($triple in $armTriples) {
    $src = Join-Path $examples "hello.hphl"
    $outBase = Join-Path $tmp ("arm_" + ($triple -replace "-","_"))
    $p = Start-Process -FilePath $compiler -ArgumentList "`"$src`"", "-o", "`"$outBase`"",
          "--backend", "llvm", "--target", $triple, "-O2" `
          -NoNewWindow -Wait -PassThru -RedirectStandardOutput (Join-Path $tmp "out.txt") -RedirectStandardError (Join-Path $tmp "err.txt")
    $obj = "$outBase.cross.o"
    Write-Host "=== $triple -O2 ==="
    if ($p.ExitCode -ne 0 -or -not (Test-Path $obj)) {
        Write-Host "  FALHA (compilacao)"; $fail++; continue
    }
    $fmt = & $objdump -f $obj 2>&1 | Select-String -SimpleMatch "aarch64"
    if ($fmt) {
        Write-Host "  OK ($( ($fmt | Select-Object -First 2) -join '; '))"
        $pass++
    } else {
        Write-Host "  FALHA (formato nao-aarch64)"
        $fail++
    }
}

Write-Host ""
Write-Host "PASS=$pass FAIL=$fail"
if ($fail -eq 0) { Write-Host "TODOS OS TESTES CROSS PASSARAM" }
else { Write-Host "TESTES CROSS FALHARAM" -ForegroundColor Red; exit 1 }
