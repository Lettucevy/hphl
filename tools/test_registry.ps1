# ==============================================================================
# test_registry.ps1 — Teste End-to-End do Registry HTTP e Package Manager
# ==============================================================================
param(
    [switch]$VerboseOutput
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$CompilerExe = Join-Path $RootDir "compiler\bin\hphlc.exe"

Write-Host "=== Test Suite: Package Manager HTTP Registry & Publish ===" -ForegroundColor Cyan

$pass = 0
$fail = 0

function Assert-True($cond, $msg) {
    if ($cond) {
        Write-Host "  [PASS] $msg" -ForegroundColor Green
        $script:pass++
    } else {
        Write-Host "  [FAIL] $msg" -ForegroundColor Red
        $script:fail++
    }
}

$TempDir = Join-Path $env:TEMP ("hphl_reg_test_" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

$PkgDir = Join-Path $TempDir "math_utils"
New-Item -ItemType Directory -Force -Path $PkgDir | Out-Null

$RegistryDir = Join-Path $TempDir "registry"
New-Item -ItemType Directory -Force -Path $RegistryDir | Out-Null

$ConsumerDir = Join-Path $TempDir "consumer_app"
New-Item -ItemType Directory -Force -Path $ConsumerDir | Out-Null

$listener = $null

try {
    # -------------------------------------------------------------------------
    # 1. Criar pacote math_utils v1.0.0
    # -------------------------------------------------------------------------
    Write-Host "`n--- Passo 1: Criar pacote math_utils v1.0.0 ---"
    $toml1 = @"
name = "math_utils"
version = "1.0.0"
entry = "lib.hphl"
sources = ["lib.hphl"]
"@
    Set-Content -Path (Join-Path $PkgDir "hphl.pkg.toml") -Value $toml1

    $lib1 = @"
module math_utils;

int Add(int a, int b) {
    return a + b;
}

int Multiply(int a, int b) {
    return a * b;
}
"@
    Set-Content -Path (Join-Path $PkgDir "lib.hphl") -Value $lib1

    # -------------------------------------------------------------------------
    # 2. Publicar math_utils v1.0.0 no registry
    # -------------------------------------------------------------------------
    Write-Host "--- Passo 2: Publicar math_utils v1.0.0 ---"
    $pubOut1 = & $CompilerExe publish $PkgDir --out $RegistryDir 2>&1
    if ($VerboseOutput) { Write-Host $pubOut1 }
    Assert-True ($LASTEXITCODE -eq 0) "hphlc publish executou com sucesso (exit 0)"

    $tarball1 = Join-Path $RegistryDir "math_utils\1.0.0.tar.gz"
    $shaFile1 = Join-Path $RegistryDir "math_utils\1.0.0.sha256"
    $indexFile = Join-Path $RegistryDir "math_utils\index.json"

    Assert-True (Test-Path $tarball1) "Tarball 1.0.0.tar.gz gerado"
    Assert-True (Test-Path $shaFile1) "Arquivo de checksum 1.0.0.sha256 gerado"
    Assert-True (Test-Path $indexFile) "Arquivo index.json gerado"

    $indexJson = Get-Content $indexFile -Raw | ConvertFrom-Json
    Assert-True ($indexJson.name -eq "math_utils" -and $indexJson.versions[0] -eq "1.0.0") "index.json contém name e versão 1.0.0"

    # -------------------------------------------------------------------------
    # 3. Publicar math_utils v1.1.0 e verificar atualização do index.json
    # -------------------------------------------------------------------------
    Write-Host "`n--- Passo 3: Publicar math_utils v1.1.0 (atualizacao) ---"
    $toml2 = @"
name = "math_utils"
version = "1.1.0"
entry = "lib.hphl"
sources = ["lib.hphl"]
"@
    Set-Content -Path (Join-Path $PkgDir "hphl.pkg.toml") -Value $toml2

    $lib2 = @"
module math_utils;

int Add(int a, int b) {
    return a + b;
}

int Multiply(int a, int b) {
    return a * b;
}

int Factorial(int n) {
    if (n <= 1) return 1;
    return n * Factorial(n - 1);
}
"@
    Set-Content -Path (Join-Path $PkgDir "lib.hphl") -Value $lib2

    $pubOut2 = & $CompilerExe publish $PkgDir --out $RegistryDir 2>&1
    Assert-True ($LASTEXITCODE -eq 0) "hphlc publish v1.1.0 executou com sucesso"

    $tarball2 = Join-Path $RegistryDir "math_utils\1.1.0.tar.gz"
    Assert-True (Test-Path $tarball2) "Tarball 1.1.0.tar.gz gerado"

    $indexJson2 = Get-Content $indexFile -Raw | ConvertFrom-Json
    Assert-True ($indexJson2.versions.Count -eq 2 -and ($indexJson2.versions -contains "1.0.0") -and ($indexJson2.versions -contains "1.1.0")) "index.json contém versoes 1.0.0 e 1.1.0 ordenadas"

    # -------------------------------------------------------------------------
    # 4. Iniciar Servidor Mock HTTP (Python)
    # -------------------------------------------------------------------------
    Write-Host "`n--- Passo 4: Iniciar Servidor HTTP Mock Local (Python) ---"
    $port = 18090
    $serverProc = Start-Process python -ArgumentList "-m", "http.server", "$port", "--directory", "$RegistryDir" -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 500
    Write-Host "  Servidor HTTP ouvindo em http://127.0.0.1:$port/ (PID $($serverProc.Id))" -ForegroundColor DarkGray

    # -------------------------------------------------------------------------
    # 5. Testar fetch remoto HTTP: hphlc fetch math_utils@1.0.0
    # -------------------------------------------------------------------------
    Write-Host "`n--- Passo 5: Testar hphlc fetch via HTTP remoto ---"
    $env:HPHL_REGISTRY = "http://127.0.0.1:$port"

    # Limpar cache do pacote math_utils se existir
    $cacheBase = if ($env:USERPROFILE) { $env:USERPROFILE } else { $env:HOME }
    $pkgCacheDir = Join-Path $cacheBase ".hphl\packages"
    if (Test-Path (Join-Path $pkgCacheDir "math_utils@1.0.0")) {
        Remove-Item -Recurse -Force (Join-Path $pkgCacheDir "math_utils@1.0.0")
    }
    if (Test-Path (Join-Path $pkgCacheDir "math_utils@1.1.0")) {
        Remove-Item -Recurse -Force (Join-Path $pkgCacheDir "math_utils@1.1.0")
    }

    $fetchOut = & $CompilerExe fetch "math_utils@1.0.0" 2>&1
    if ($VerboseOutput) { Write-Host $fetchOut }
    Assert-True ($LASTEXITCODE -eq 0 -and (Test-Path (Join-Path $pkgCacheDir "math_utils@1.0.0\lib.hphl"))) "hphlc fetch math_utils@1.0.0 baixou e extraiu via HTTP"

    # -------------------------------------------------------------------------
    # 6. Testar resolução SemVer via HTTP: hphlc fetch math_utils@^1.0.0 -> 1.1.0
    # -------------------------------------------------------------------------
    Write-Host "`n--- Passo 6: Testar resolução SemVer (^1.0.0) via HTTP ---"
    $fetchOut2 = & $CompilerExe fetch "math_utils@^1.0.0" 2>&1
    if ($VerboseOutput) { Write-Host $fetchOut2 }
    Assert-True ($LASTEXITCODE -eq 0 -and ($fetchOut2 -match "v1.1.0") -and (Test-Path (Join-Path $pkgCacheDir "math_utils@1.1.0\lib.hphl"))) "hphlc fetch math_utils@^1.0.0 resolveu para 1.1.0 via HTTP"

    # -------------------------------------------------------------------------
    # 7. Criar projeto consumidor e testar hphlc install + package.lock
    # -------------------------------------------------------------------------
    Write-Host "`n--- Passo 7: Testar hphlc install e package.lock ---"
    $consumerToml = @"
name = "consumer_app"
version = "0.1.0"
entry = "main.hphl"
sources = ["main.hphl"]

[dependencies]
math_utils = "^1.0.0"
"@
    Set-Content -Path (Join-Path $ConsumerDir "hphl.pkg.toml") -Value $consumerToml

    $consumerMain = @"
import math_utils;

void Main() {
    int sum = Add(15, 27);
    int fact = Factorial(5);
    println("Resultado Soma: " + sum);
    println("Resultado Fat: " + fact);
}
"@
    Set-Content -Path (Join-Path $ConsumerDir "main.hphl") -Value $consumerMain

    Push-Location $ConsumerDir
    try {
        $installOut = & $CompilerExe install 2>&1
        if ($VerboseOutput) { Write-Host $installOut }
        Assert-True ($LASTEXITCODE -eq 0 -and (Test-Path "package.lock")) "hphlc install criou package.lock com sucesso"

        $lockContent = Get-Content "package.lock" -Raw | ConvertFrom-Json
        $mathEntry = $lockContent.packages | Where-Object { $_.name -eq "math_utils" }
        Assert-True ($mathEntry -and $mathEntry.version -eq "1.1.0" -and $mathEntry.sha256.Length -eq 64) "package.lock tem math_utils v1.1.0 com hash SHA-256 valido"

        # ---------------------------------------------------------------------
        # 8. Compilar e executar projeto consumidor com pacote remoto
        # ---------------------------------------------------------------------
        Write-Host "`n--- Passo 8: Compilar e executar com pacote remoto instalado ---"
        $runOut = & $CompilerExe --package . -o test_app.exe --run 2>&1
        if ($VerboseOutput) { Write-Host $runOut }
        $matchOk = ($runOut -match "Resultado Soma: 42") -and ($runOut -match "Resultado Fat: 120")
        Assert-True ($LASTEXITCODE -eq 0 -and $matchOk) "Projeto consumidor compilou e executou usando o pacote remoto"
    } finally {
        Pop-Location
    }

} finally {
    if ($serverProc -and !$serverProc.HasExited) {
        Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -Recurse -Force $TempDir -ErrorAction SilentlyContinue
}

$color = if ($fail -eq 0) { "Green" } else { "Red" }
Write-Host "`n======================================================="
Write-Host "Total: $pass passaram, $fail falharam" -ForegroundColor $color
if ($fail -gt 0) {
    exit 1
}
