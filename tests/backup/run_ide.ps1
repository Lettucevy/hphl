#!/usr/bin/env pwsh
# run_ide.ps1 — compila (se preciso) e abre a IDE do HP-HL
# Uso: powershell -ExecutionPolicy Bypass -File run_ide.ps1

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "ide\bin\Debug\net10.0\HPHLIde.exe"

if (-not (Test-Path $exe)) {
    Write-Host "Compilando a IDE (primeira vez pode demorar)..."
    dotnet build (Join-Path $root "ide\HPHLIde.csproj") | Out-Null
    if (-not (Test-Path $exe)) {
        Write-Host "Falha ao compilar a IDE." -ForegroundColor Red
        exit 1
    }
}

& $exe
