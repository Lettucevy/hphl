#!/usr/bin/env powershell
# tools/fuzz.ps1 — Fuzzing do frontend (lexer/parser/semantic) do hphlc.
# Gera casos mutantes a partir do corpus (tests/positive + examples) e
# classifica: OK (0), DIAG (erro limpo, exit 1), CRASH (AV/stack), HANG.
# Achados vao para fuzz/finding-*.hphl com log. Roda com --no-link (rapido).
#
# Uso: powershell -File tools/fuzz.ps1 [-Cases 200] [-Seed 1234] [-TimeoutSec 10]

param(
    [int]$Cases = 200,
    [int]$Seed = 0,
    [int]$TimeoutSec = 10,
    [string]$CorpusDir = "fuzz",
    [string]$Backend = "x64"
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$CompilerExe = Join-Path $RootDir "compiler/bin/hphlc.exe"
if (-not (Test-Path $CompilerExe)) {
    $CompilerExe = Join-Path $RootDir "bin/hphlc.exe"
}
if (-not (Test-Path $CompilerExe)) {
    Write-Host "ERROR: hphlc nao encontrado; rode tools/build.ps1 antes." -ForegroundColor Red
    exit 1
}

if ($Seed -eq 0) { $Seed = [int](Get-Date -UFormat %s) % 100000 }
$rng = New-Object System.Random($Seed)
$WorkDir = Join-Path $RootDir $CorpusDir
New-Item -ItemType Directory -Force -Path (Join-Path $WorkDir "_tmp") | Out-Null

# Corpus: fontes .hphl pequenos e validos
$seeds = @()
$seeds += Get-ChildItem (Join-Path $RootDir "tests/positive/*.hphl") | Where-Object { $_.Length -lt 8000 } | Select-Object -ExpandProperty FullName
$seeds += Get-ChildItem (Join-Path $RootDir "examples/*/*.hphl") -ErrorAction SilentlyContinue | Where-Object { $_.Length -lt 8000 } | Select-Object -ExpandProperty FullName
$seeds += Get-ChildItem (Join-Path $RootDir "examples/*.hphl") -ErrorAction SilentlyContinue | Where-Object { $_.Length -lt 8000 } | Select-Object -ExpandProperty FullName
if ($seeds.Count -eq 0) { Write-Host "ERROR: corpus vazio." -ForegroundColor Red; exit 1 }
Write-Host "[fuzz] $($seeds.Count) seeds, $Cases casos, seed=$Seed"

$tokens = @(";", "{", "}", "(", ")", "[", "]", ",", ".", ":", "=", "==", "+", "-", "*", "/", "<", ">", "if", "else", "while", "for", "foreach", "in", "match", "_", "=>", "return", "int", "string", "bool", "float", "void", "class", "null", "true", "false", "new", "pool", "base", "this", "0", "1", '"s"', "'c'", "f", "x", "@", "..", "&&", "||", "!")

function Mutate([string]$src) {
    $op = $rng.Next(6)
    $chars = $src.ToCharArray()
    if ($chars.Length -eq 0) { return $src }
    switch ($op) {
        0 {
            # byte flip
            $n = 1 + $rng.Next(3)
            for ($k = 0; $k -lt $n; $k++) {
                $i = $rng.Next($chars.Length)
                $chars[$i] = [char]$rng.Next(32, 127)
            }
            return (-join $chars)
        }
        1 {
            # deleta chunk
            $i = $rng.Next($chars.Length)
            $len = 1 + $rng.Next([Math]::Min(40, $chars.Length - $i))
            return $src.Substring(0, $i) + $src.Substring($i + $len)
        }
        2 {
            # duplica chunk
            $i = $rng.Next($chars.Length)
            $len = 1 + $rng.Next([Math]::Min(30, $chars.Length - $i))
            return $src.Substring(0, $i) + $src.Substring($i, $len) + $src.Substring($i)
        }
        3 {
            # injeta token
            $i = $rng.Next($chars.Length)
            return $src.Substring(0, $i) + " " + $tokens[$rng.Next($tokens.Count)] + " " + $src.Substring($i)
        }
        4 {
            # trunca
            $i = $rng.Next($chars.Length)
            return $src.Substring(0, $i)
        }
        default {
            # troca dois blocos
            if ($chars.Length -lt 20) { return $src }
            $a = $rng.Next($chars.Length - 10)
            $b = $rng.Next($chars.Length - 10)
            $la = 1 + $rng.Next(10); $lb = 1 + $rng.Next(10)
            $arr = $chars.Clone()
            for ($k = 0; $k -lt [Math]::Min($la, $lb); $k++) {
                if ($a + $k -lt $arr.Length -and $b + $k -lt $arr.Length) {
                    $t = $arr[$a + $k]; $arr[$a + $k] = $arr[$b + $k]; $arr[$b + $k] = $t
                }
            }
            return (-join $arr)
        }
    }
    return $src
}

$ok = 0; $diag = 0; $crash = 0; $hang = 0; $findings = @()
for ($c = 0; $c -lt $Cases; $c++) {
    $seedFile = $seeds[$rng.Next($seeds.Count)]
    $src = [IO.File]::ReadAllText($seedFile)
    $n = 1 + $rng.Next(3)
    for ($k = 0; $k -lt $n; $k++) { $src = Mutate $src }
    $caseFile = Join-Path $WorkDir ("_tmp/fz_{0}_{1}.hphl" -f $Seed, $c)
    [IO.File]::WriteAllText($caseFile, $src)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $CompilerExe
    $psi.Arguments = "`"$caseFile`" --no-link -o `"$WorkDir/_tmp/fz_out.exe`""
    if ($Backend -eq "llvm") { $psi.Arguments += " --backend llvm" }
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.WorkingDirectory = $RootDir
    $p = [System.Diagnostics.Process]::Start($psi)
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        try { $p.Kill() } catch { }
        $hang++
        $dst = Join-Path $WorkDir ("finding-hang-{0}-{1}.hphl" -f $Seed, $c)
        Copy-Item $caseFile $dst -Force
        $findings += "HANG $dst (seed $(Split-Path $seedFile -Leaf))"
        Write-Host "[HANG] caso $c" -ForegroundColor Magenta
    } else {
        $rc = $p.ExitCode
        if ($rc -eq 0) { $ok++ }
        elseif ($rc -eq 1) { $diag++ }
        else {
            $crash++
            $dst = Join-Path $WorkDir ("finding-crash-{0}-{1}-rc{2}.hphl" -f $Seed, $c, $rc)
            Copy-Item $caseFile $dst -Force
            $findings += "CRASH(rc=$rc) $dst (seed $(Split-Path $seedFile -Leaf))"
            Write-Host "[CRASH rc=$rc] caso $c" -ForegroundColor Red
        }
    }
    Remove-Item $caseFile -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "========================================"
Write-Host "Fuzz: OK=$ok DIAG=$diag CRASH=$crash HANG=$hang / $Cases"
Write-Host "========================================"
if ($findings.Count -gt 0) {
    Write-Host "Achados:"
    $findings | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
}
