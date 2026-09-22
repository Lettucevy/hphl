param([switch]$Verbose)

$ErrorActionPreference = 'Continue'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$CompilerExe = Join-Path $RootDir 'compiler\bin\hphlc.exe'

Write-Host '=== Test Suite: Package Manager (hphl fetch / install / manifests) ===' -ForegroundColor Cyan

$pass = 0
$fail = 0

function Assert-True($cond, $msg) {
    if ($cond) {
        Write-Host ('[PASS] ' + $msg) -ForegroundColor Green
        $global:pass++
    } else {
        Write-Host ('[FAIL] ' + $msg) -ForegroundColor Red
        $global:fail++
    }
}

# Test 1: package.hpkg fallback and backwards compatibility
Write-Host '--- Test 1: package.hpkg execution ---'
$out1 = & $CompilerExe --package examples/proj_package -o compiler/bin/test_hpkg.exe --run 2>&1
Assert-True ($LASTEXITCODE -eq 0 -and ($out1 -match 'Package Manager M14.2 OK')) 'examples/proj_package builds and executes'

# Test 2: hphl.pkg.toml with path: dependency
Write-Host '--- Test 2: hphl.pkg.toml and path: dependency ---'
$out2 = & $CompilerExe --package tests/package_demo/app -o compiler/bin/test_toml.exe --run 2>&1
Assert-True ($LASTEXITCODE -eq 0 -and ($out2 -match 'Resultado TOML: 42')) 'tests/package_demo/app builds and executes with TOML manifest'

# Test 3: hphlc install generates package.lock
Write-Host '--- Test 3: hphlc install lockfile generation ---'
Push-Location tests/package_demo/app
try {
    if (Test-Path package.lock) { Remove-Item package.lock }
    $out3 = & $CompilerExe install 2>&1
    Assert-True ($LASTEXITCODE -eq 0 -and (Test-Path package.lock)) 'hphlc install created package.lock'
    $lockJson = Get-Content package.lock -Raw | ConvertFrom-Json
    Assert-True ($lockJson.lockfileVersion -eq 1 -and $lockJson.packages.Count -ge 2) 'package.lock has valid JSON structure and packages'
    
    # Test 4: --frozen-lockfile with lockfile present
    Write-Host '--- Test 4: --frozen-lockfile success ---'
    $out4 = & $CompilerExe install --frozen-lockfile 2>&1
    Assert-True ($LASTEXITCODE -eq 0) 'install --frozen-lockfile passes when lockfile is in sync'

    # Test 5: --frozen-lockfile fails when lockfile missing
    Write-Host '--- Test 5: --frozen-lockfile failure when missing entry ---'
    Remove-Item package.lock
    $out5 = & $CompilerExe install --frozen-lockfile 2>&1
    Assert-True ($LASTEXITCODE -ne 0 -and ($out5 -match 'frozen-lockfile')) 'install --frozen-lockfile fails when dependency not in lockfile'
} finally {
    Pop-Location
}

# Test 6: Git package fetching
Write-Host '--- Test 6: Git package fetching ---'
$tempGit = Join-Path $env:TEMP ('hphl_git_mock_' + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tempGit | Out-Null
Push-Location $tempGit
try {
    & git init -q
    & git config user.name 'HPHL Tester'
    & git config user.email 'test@hphl.dev'
    Set-Content -Path 'hphl.pkg.toml' -Value ("name = `"mock_git`"`nversion = `"0.2.0`"`n")
    & git add hphl.pkg.toml
    & git commit -m 'Initial commit' -q
    & git tag v0.2.0
    $gitUrl = 'git+file:///' + ($tempGit -replace '\\', '/') + '#v0.2.0'
    $out6 = & $CompilerExe fetch $gitUrl 2>&1
    Assert-True ($LASTEXITCODE -eq 0 -and ($out6 -match 'fetch OK')) 'fetch git+file://...#tag succeeded'
} finally {
    Pop-Location
    Remove-Item -Recurse -Force $tempGit -ErrorAction SilentlyContinue
}

Write-Host ('`nTotal: ' + $pass + ' passed, ' + $fail + ' failed')
if ($fail -gt 0) { exit 1 }
