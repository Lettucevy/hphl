# test_all.ps1 — M13.5: suíte consolidada CI-friendly
# Roda todas as suítes existentes e reporta sumário único. Exit 0 só se tudo verde.

param([int]$BenchRuns = 2)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$root = Split-Path -Parent $PSScriptRoot
$testsDir = $PSScriptRoot
$failed = @()
$passed = @()

function Invoke-Suite($name, $script) {
  Write-Host "`n=== $name ===" -ForegroundColor Cyan
  $out = & powershell -ExecutionPolicy Bypass -File $script 2>&1 | Out-String -Width 500
  $ok = $false
  if ($out -match "TODOS OS TESTES PASSARAM") { $ok = $true }
  elseif ($out -match "PASS=(\d+)\s+FAIL=0") { $ok = $true }
  elseif ($out -match "PASS=\d+\s+FAIL=0") { $ok = $true }
  # run_tests_*.ps1 usam "TODOS OS TESTES PASSARAM" ou "PASS=... FAIL=0"
  # fallback: se não contém FALHA e contém OK, considera ok
  if (-not $ok) {
    if ($out -match "FALHA" -or $out -match "FAIL.*[1-9]") { $ok = $false }
    elseif ($out -match "PASS=") {
      # extrai FAIL
      if ($out -match "FAIL=(\d+)") { $ok = ([int]$Matches[1] -eq 0) }
    }
  }
  # imprime últimas linhas relevantes
  $lines = $out -split "`n" | Where-Object { $_ -match "PASS=|TODOS|FALHA|FAIL" } | Select-Object -Last 5
  foreach ($l in $lines) { Write-Host $l }
  if ($ok) { $passed += $name; Write-Host ("-> $name" + ": OK") -ForegroundColor Green }
  else { $failed += $name; Write-Host ("-> $name" + ": FALHA") -ForegroundColor Red; Write-Host ($out | Select-Object -Last 20) }
  return $ok
}

# 1. x64
Invoke-Suite "x64" (Join-Path $testsDir "run_tests.ps1")
# 2. IR
Invoke-Suite "IR" (Join-Path $testsDir "run_tests_ir.ps1")
# 3. LLVM API
Invoke-Suite "LLVM" (Join-Path $testsDir "run_tests_llvm.ps1")
# 4. CROSS (wasm/arm64)
Invoke-Suite "CROSS" (Join-Path $testsDir "run_tests_cross.ps1")
# 5. Debug (se existir)
if (Test-Path (Join-Path $testsDir "run_tests_debug.ps1")) {
  Invoke-Suite "DEBUG" (Join-Path $testsDir "run_tests_debug.ps1")
}
# 6. DAP (smoke)
if (Test-Path (Join-Path $testsDir "run_tests_dap.ps1")) {
  Invoke-Suite "DAP" (Join-Path $testsDir "run_tests_dap.ps1")
}

# 7. Self-host (M14.4): lexer + parser escritos em HP-HL
Write-Host "`n=== SELFHOST (M14.4) ===" -ForegroundColor Cyan
$selfhostOk = $false
try {
  $shOut = "$env:TEMP\selfhost_parser.exe"
  $c1 = & "$root\hphlc.exe" "$root\selfhost\parser.hphl" -o $shOut 2>&1 | Out-String
  if ($LASTEXITCODE -eq 0) {
    $run = ((& $shOut 2>&1 | Out-String) -replace "`r", "").Trim()
    $want = @"
(&& (== (+ 1 (/ (* 2 (- x 3)) (f a 10))) y) ok)
(block (var int x (+ 1 2)) (+= x (f x 3)) (if (&& (> x 2) ok) (block (print x)) (block (= x 0))) (while (< i n) (block (+= i 1))) (return x))
(prog
  (gvar int G 42)
  (gvar-array int buf 4)
  (fn int Soma (params (int a) (int b)) (block (var int r (+ a b)) (return r)))
  (fn void Main (params) (block (var int x (Soma G 8)) (print x))))
[sem] programa bom: OK
[sem] erro: variavel 'Y' nao declarada
[sem] erro: funcao 'Foo' nao definida
[sem] erro: funcao 'Soma' nao definida
[sem] programa ruim: 3 erro(s)
"@.Trim()
    if ($run -eq $want) { $selfhostOk = $true; Write-Host "lexer+parser+semantic OK" }
    else { Write-Host "saida divergiu: [$run]" }

  # M14.4f end-to-end: o compilador self-host gera .s do demo, gcc monta e executa
  if ($selfhostOk) {
    try {
      $shc = "$env:TEMP\selfhost_compiler.exe"
      & "$root\hphlc.exe" "$root\selfhost\main.hphl" -o $shc 2>&1 | Out-Null
      Push-Location $root
      & $shc 2>&1 | Out-Null
      Pop-Location
      $demoExe = "$env:TEMP\selfhost_demo_out.exe"
      & "$root\hphlc.exe" "$root\selfhost\demo.hphl" -o "$env:TEMP\demo_native.exe" 2>&1 | Out-Null
      $natOut = (& "$env:TEMP\demo_native.exe" 2>&1 | Out-String) -replace "`r", ""
      & gcc -O2 -o $demoExe "$root\selfhost_demo.s" "$root\compiler\src\runtime.c" 2>&1 | Out-Null
      if ($LASTEXITCODE -eq 0) {
        $genOut = (& $demoExe 2>&1 | Out-String) -replace "`r", ""
        if ($genOut.Trim() -eq $natOut.Trim() -and $genOut.Trim() -ne "") {
          Write-Host "-> SELF-COMPILADO EXECUTA IGUAL AO NATIVO: $($genOut.Trim() -replace '\n', ' ')" -ForegroundColor Green
        } else {
          $failed += "SELFHOST-E2E"; Write-Host "-> E2E divergiu: nativo=[$($natOut.Trim())] gerado=[$($genOut.Trim())]" -ForegroundColor Red
        }
      } else { $failed += "SELFHOST-E2E"; Write-Host "-> gcc falhou no .s self-host" -ForegroundColor Red }
    } catch { $failed += "SELFHOST-E2E"; Write-Host "-> E2E excecao" -ForegroundColor Red }
  }

  # M15.3 autohost do LEXER: self-host compila lexer.hphl -> .s -> host C tokeniza
  if ($selfhostOk) {
    try {
      $enc = New-Object System.Text.UTF8Encoding($false)
      [System.IO.File]::WriteAllText("$root\selfhost\target.txt", "selfhost/lexer.hphl", $enc)
      Push-Location $root
      & $shc 2>&1 | Out-Null
      Pop-Location
      & gcc -O2 -o "$env:TEMP\autohost_lexer.exe" "$root\selfhost_demo.s" "$root\selfhost\host_lexer.c" "$root\compiler\src\runtime.c" 2>&1 | Out-Null
      if ($LASTEXITCODE -eq 0) {
        $tokOut = (& "$env:TEMP\autohost_lexer.exe" 2>&1 | Out-String) -replace "`r", ""
        if ($tokOut -match "\[5\] int" -and $tokOut -match "\[1\] soma" -and $tokOut -match "\[2\] 42" -and $tokOut -match "\[0\]") {
          Write-Host "-> AUTOHOST LEXER TOKENIZA OK (v0.69)" -ForegroundColor Green
        } else {
          $failed += "SELFHOST-AUTOLEX"; Write-Host "-> autolex divergiu: [$($tokOut.Trim())]" -ForegroundColor Red
        }
      } else { $failed += "SELFHOST-AUTOLEX"; Write-Host "-> gcc falhou no .s do lexer" -ForegroundColor Red }
      Remove-Item "$root\selfhost\target.txt" -ErrorAction SilentlyContinue
    } catch { $failed += "SELFHOST-AUTOLEX"; Write-Host "-> autolex excecao" -ForegroundColor Red }
  }
  } else { Write-Host "compilacao falhou: $($c1.Substring(0, [Math]::Min(200, $c1.Length)))" }
} catch { $selfhostOk = $false }
if ($selfhostOk) { $passed += "SELFHOST"; Write-Host "-> SELFHOST: OK" -ForegroundColor Green }
else { $failed += "SELFHOST"; Write-Host "-> SELFHOST: FALHA" -ForegroundColor Red }

# 7. Projeto demo (M13.4)
Write-Host "`n=== PROJETO DEMO ===" -ForegroundColor Cyan
$projOk = $false
try {
  $outProj = & "$root\hphlc.exe" --project "$root\examples\proj_demo\app.hpproj" 2>&1 | Out-String
  $outRun = & "$root\examples\proj_demo\demo.exe" 2>&1 | Out-String
  if ($outRun -match "proj_demo ok") { $projOk = $true; Write-Host $outRun.Trim() }
} catch { $projOk = $false }
if ($projOk) { $passed += "PROJETO"; Write-Host "-> PROJETO: OK" -ForegroundColor Green } else { $failed += "PROJETO"; Write-Host "-> PROJETO: FALHA" -ForegroundColor Red }

# 8. Bench smoke (2 runs, rápido)
Write-Host "`n=== BENCH SMOKE (Runs=$BenchRuns) ===" -ForegroundColor Cyan
try {
  $benchOut = & powershell -ExecutionPolicy Bypass -File (Join-Path $testsDir "bench_m11.ps1") -Runs $BenchRuns 2>&1 | Out-String -Width 500
  if ($benchOut -match "RESUMO") { $passed += "BENCH"; Write-Host "-> BENCH: OK" -ForegroundColor Green; $benchOut -split "`n" | Where-Object { $_ -match "kernel|RESUMO" } | Select-Object -Last 5 | ForEach-Object { Write-Host $_ } }
  else { $failed += "BENCH"; Write-Host "-> BENCH: FALHA" -ForegroundColor Red }
} catch { $failed += "BENCH"; Write-Host "-> BENCH: FALHA" -ForegroundColor Red }

Write-Host "`n=== SUMÁRIO test_all ===" -ForegroundColor Cyan
Write-Host ("PASS: " + ($passed -join ", ")) -ForegroundColor Green
if ($failed.Count -gt 0) { Write-Host ("FAIL: " + ($failed -join ", ")) -ForegroundColor Red }
else { Write-Host "TODOS OS TESTES PASSARAM" -ForegroundColor Green }

if ($failed.Count -gt 0) { exit 1 } else { exit 0 }
