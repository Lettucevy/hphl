#!/bin/sh
# Executa a IDE do HP-HL (Linux/macOS/WSL).
# No Windows: .\run_ide.ps1
set -e
cd "$(dirname "$0")"

if ! command -v dotnet >/dev/null 2>&1; then
    echo "dotnet SDK (10+) nao encontrado no PATH." >&2
    exit 1
fi

dotnet run --project ide/HPHLIde.csproj
