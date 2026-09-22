param([string]$List = "", [string]$Out = "", [int]$TimeoutSh = 90, [int]$TimeoutRef = 60)
$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$TDir = Join-Path $RootDir "selfhost\_out\parity_tmp"
$RefDir = Join-Path $TDir "ref"
New-Item -ItemType Directory -Force -Path $RefDir | Out-Null
$Hphlc = Join-Path $RootDir "compiler/bin/hphlc.exe"
$ShExe = Join-Path $RootDir "selfhost/_out/e2e_main.exe"
$Gcc = "C:\msys64\ucrt64\bin\gcc.exe"
$RuntimeObj = Join-Path $RootDir "compiler/src/runtime/main.o"

function Run-Bounded($exe, $arguments, $timeoutSec, $cwd, $stdoutFile) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = $arguments
    $psi.WorkingDirectory = $cwd
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $ok = $p.WaitForExit($timeoutSec * 1000)
    if (-not $ok) { try { $p.Kill() } catch { }; return @{ ok = $false; rc = "TIMEOUT"; out = "" } }
    $rc = $p.ExitCode
    if ($stdoutFile -ne "") {
        $ms = New-Object System.IO.MemoryStream
        $p.StandardOutput.BaseStream.CopyTo($ms)
        $p.Close()
        [System.IO.File]::WriteAllBytes($stdoutFile, $ms.ToArray())
        return @{ ok = $true; rc = $rc; out = "" }
    }
    $o = $p.StandardOutput.ReadToEnd() + $p.StandardError.ReadToEnd()
    $p.Close()
    return @{ ok = $true; rc = $rc; out = $o }
}

function Bytes-Equal($a, $b) {
    $x = [System.IO.File]::ReadAllBytes($a)
    $y = [System.IO.File]::ReadAllBytes($b)
    if ($x.Length -ne $y.Length) { return $false }
    for ($i = 0; $i -lt $x.Length; $i++) { if ($x[$i] -ne $y[$i]) { return $false } }
    return $true
}

if ($List -ne "") { $tests = Get-Content $List } else { $tests = Get-ChildItem (Join-Path $RootDir "tests/positive/*.hphl") | ForEach-Object { $_.Name } }
if ($Out -eq "") { $Out = Join-Path $RootDir "selfhost\_out\parity.csv" }
"test,stage,detail" | Set-Content -Path $Out -Encoding ascii

foreach ($t in $tests) {
    $t = $t.Trim()
    if ($t -eq "") { continue }
    $src = Join-Path $RootDir ("tests/positive/" + $t)
    $base = [System.IO.Path]::GetFileNameWithoutExtension($t)
    $srcContent = Get-Content $src -Raw
    $isModule = ($t -clike "Mod*.hphl") -and (-not ($srcContent -match '\bMain\s*\('))
    $compSrc = $src
    $cleanTmp = $null
    $targetPath = "tests/positive/" + $t

    try {
        if ($isModule) {
            # Mod*.hphl are library modules without Main(). To validate compilation and execution
            # parity, create a temporary harness in tests/positive so relative imports work.
            $tmpFile = Join-Path $RootDir ("tests/positive/_tmp_parity_" + $t)
            Set-Content $tmpFile ($srcContent + "`nvoid Main() { print(`"[$base ok]`"); }`n") -Encoding ascii
            $compSrc = $tmpFile
            $cleanTmp = $tmpFile
            $targetPath = "tests/positive/_tmp_parity_" + $t
        }

        # 1. referencia (cache, bytes crus)
        $refExe = Join-Path $RefDir ($base + ".ref.exe")
        $refOutF = Join-Path $RefDir ($base + ".ref.bin")
        $refRcF = Join-Path $RefDir ($base + ".ref.rc")
        $refRc = 0
        if (-not (Test-Path $refOutF) -or -not (Test-Path $refRcF)) {
            $r = Run-Bounded $Hphlc "`"$compSrc`" -o `"$refExe`"" $TimeoutRef $RootDir ""
            if (-not $r.ok -or $r.rc -ne 0 -or -not (Test-Path $refExe)) {
                $stage = "REF-FAIL"
                $detail = "$($r.rc) $($r.out)".Substring(0, [Math]::Min(80, "$($r.rc) $($r.out)".Length))
                throw "done"
            }
            $r2 = Run-Bounded $refExe "" $TimeoutRef $RootDir $refOutF
            if (-not $r2.ok -or -not (Test-Path $refOutF)) { $stage = "REF-RUN-FAIL"; $detail = "$($r2.rc)"; throw "done" }
            $refRc = $r2.rc
            Set-Content -Path $refRcF -Value "$refRc" -Encoding ascii
        } else {
            $refRc = [int](Get-Content $refRcF)
        }

        # 2. selfhost
        $targetFile = Join-Path $RootDir "selfhost/target.txt"
        for ($retry = 0; $retry -lt 10; $retry++) {
            try {
                [System.IO.File]::WriteAllText($targetFile, $targetPath)
                break
            } catch {
                Start-Sleep -Milliseconds 50
            }
        }
        Remove-Item (Join-Path $RootDir "selfhost_demo.s") -Force -ErrorAction SilentlyContinue
        $s = Run-Bounded $ShExe "" $TimeoutSh $RootDir ""
        $genAsm = Join-Path $RootDir "selfhost_demo.s"
        if (-not $s.ok) { $stage = "SH-HANG"; throw "done" }
        if ($s.rc -ne 0 -or -not (Test-Path $genAsm)) {
            if ($s.out -match "parse OK") { $stage = "SH-SEM-FAIL" } else { $stage = "SH-PARSE-FAIL" }
            $detail = (($s.out -split "`n" | Select-String -Pattern "panic|erro|esperado" | Select-Object -First 1) | Out-String).Trim()
            throw "done"
        }

        # 3. assemble+link+run (bytes crus)
        $obj = Join-Path $TDir ($base + ".o")
        $exe = Join-Path $TDir ($base + ".sh.exe")
        $shOutF = Join-Path $TDir ($base + ".sh.bin")
        Copy-Item $genAsm (Join-Path $TDir ($base + ".s")) -Force
        $a = Run-Bounded $Gcc "-c `"$(Join-Path $TDir ($base + '.s'))`" -o `"$obj`"" 60 $RootDir ""
        if (-not $a.ok -or $a.rc -ne 0) { $stage = "SH-ASM-FAIL"; $detail = ($a.out | Select-Object -First 2 | Out-String).Trim().Substring(0, [Math]::Min(80, ($a.out | Out-String).Length)); throw "done" }
        $l = Run-Bounded $Gcc "-o `"$exe`" `"$obj`" `"$RuntimeObj`" -lws2_32 -lwsock32 -lbcrypt -lz" 60 $RootDir ""
        if (-not $l.ok -or $l.rc -ne 0) { $stage = "SH-LINK-FAIL"; throw "done" }
        $x = Run-Bounded $exe "" 60 $RootDir $shOutF
        if (-not $x.ok) { $stage = "SH-RUN-HANG"; throw "done" }
        if (-not (Test-Path $shOutF)) { $stage = "SH-RUN-CRASH"; $detail = "$($x.rc)"; throw "done" }
        if ($x.rc -ne $refRc) {
            $stage = "SH-RC-MISMATCH"; $detail = "ref=$refRc sh=$($x.rc)"; throw "done"
        }
        if (Bytes-Equal $refOutF $shOutF) {
            $stage = "OK"
        } else {
            $stage = "SH-MISMATCH"; $detail = "bytes ref=$([System.IO.File]::ReadAllBytes($refOutF).Length) sh=$([System.IO.File]::ReadAllBytes($shOutF).Length)"
        }
    } catch {
        if (-not $stage) {
            $stage = "ERROR"
            $detail = ($_.Exception.Message).Substring(0, [Math]::Min(80, ($_.Exception.Message).Length))
        }
    }
    finally {
        if ($cleanTmp -and (Test-Path $cleanTmp)) {
            Remove-Item $cleanTmp -Force -ErrorAction SilentlyContinue
        }
    }
    "$t,$stage,$detail" | Add-Content -Path $Out -Encoding ascii
    Write-Host "$t => $stage $detail"
}
$targetFile = Join-Path $RootDir "selfhost/target.txt"
for ($retry = 0; $retry -lt 10; $retry++) {
    try {
        [System.IO.File]::WriteAllText($targetFile, "selfhost/demo.hphl")
        break
    } catch {
        Start-Sleep -Milliseconds 50
    }
}
Remove-Item (Join-Path $RootDir "selfhost_demo.s") -Force -ErrorAction SilentlyContinue
Write-Host "CSV: $Out"
