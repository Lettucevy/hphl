# ==============================================================================
# package_vsix.ps1 — Empacota a extensão oficial do VS Code/Cursor (.vsix)
# ==============================================================================
param(
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "==> Compilando bundle da extensao HPHL..." -ForegroundColor Cyan
npm run build

Write-Host "==> Empacotando .vsix com @vscode/vsce..." -ForegroundColor Cyan
npx --yes @vscode/vsce package --no-git-tag-version --allow-missing-repository

$vsixFiles = Get-ChildItem -Path $ScriptDir -Filter "*.vsix"
if ($vsixFiles.Count -gt 0) {
    $vsix = $vsixFiles[0]
    Write-Host "==> Pacote VSIX gerado com sucesso: $($vsix.FullName)" -ForegroundColor Green
    if ($OutDir -and (Test-Path $OutDir)) {
        Copy-Item $vsix.FullName -Destination $OutDir -Force
        Write-Host "==> Copiado para: $OutDir" -ForegroundColor Green
    }
} else {
    Write-Error "Falha ao encontrar arquivo .vsix gerado."
}
