#!/usr/bin/env bash
# ==============================================================================
# HP-HL SDK — Instalador Local para Linux e macOS
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"
COMPILER_BIN="$BIN_DIR/hphlc"

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${CYAN}==========================================================${NC}"
echo -e "${CYAN}  Instalando HP-HL SDK v1.0.0 (Linux / macOS)${NC}"
echo -e "${CYAN}==========================================================${NC}"
echo "Diretório do SDK: $SCRIPT_DIR"

# 1. Garantir permissões de execução no binário
if [ -f "$COMPILER_BIN" ]; then
    chmod +x "$COMPILER_BIN"
    echo -e "${GREEN}[OK] Permissões de execução aplicadas em bin/hphlc${NC}"
fi

# 2. Configurar variáveis de ambiente no shell do usuário
HPHL_ENV_LINE="export HPHL_HOME=\"$SCRIPT_DIR\""
PATH_ENV_LINE="export PATH=\"\$HPHL_HOME/bin:\$PATH\""

SHELL_PROFILES=()
[ -f "$HOME/.bashrc" ] && SHELL_PROFILES+=("$HOME/.bashrc")
[ -f "$HOME/.zshrc" ] && SHELL_PROFILES+=("$HOME/.zshrc")
[ -f "$HOME/.profile" ] && SHELL_PROFILES+=("$HOME/.profile")
[ -f "$HOME/.bash_profile" ] && SHELL_PROFILES+=("$HOME/.bash_profile")

CONFIGURED=0
for profile in "${SHELL_PROFILES[@]}"; do
    if ! grep -q "HPHL_HOME" "$profile" 2>/dev/null; then
        echo "" >> "$profile"
        echo "# HP-HL SDK Configuration" >> "$profile"
        echo "$HPHL_ENV_LINE" >> "$profile"
        echo "$PATH_ENV_LINE" >> "$profile"
        echo -e "${GREEN}[OK] Configurado em: $profile${NC}"
        CONFIGURED=1
    else
        echo -e "${YELLOW}[INFO] HP-HL já configurado em: $profile${NC}"
        CONFIGURED=1
    fi
done

if [ "$CONFIGURED" -eq 0 ]; then
    DEFAULT_PROFILE="$HOME/.profile"
    echo "" >> "$DEFAULT_PROFILE"
    echo "# HP-HL SDK Configuration" >> "$DEFAULT_PROFILE"
    echo "$HPHL_ENV_LINE" >> "$DEFAULT_PROFILE"
    echo "$PATH_ENV_LINE" >> "$DEFAULT_PROFILE"
    echo -e "${GREEN}[OK] Criada configuração em: $DEFAULT_PROFILE${NC}"
fi

# 3. Instalar extensão do VS Code / Cursor se disponível
VSIX_FILE="$SCRIPT_DIR/editors/hphl-1.0.0.vsix"
if [ -f "$VSIX_FILE" ]; then
    if command -v code >/dev/null 2>&1; then
        echo -e "${YELLOW}[*] Instalando extensão no Visual Studio Code...${NC}"
        code --install-extension "$VSIX_FILE" --force || true
        echo -e "${GREEN}[OK] Extensão instalada no VS Code!${NC}"
    fi

    if command -v cursor >/dev/null 2>&1; then
        echo -e "${YELLOW}[*] Instalando extensão no Cursor...${NC}"
        cursor --install-extension "$VSIX_FILE" --force || true
        echo -e "${GREEN}[OK] Extensão instalada no Cursor!${NC}"
    fi
fi

echo ""
echo -e "${GREEN}==========================================================${NC}"
echo -e "${GREEN}  Instalação concluída com sucesso!${NC}"
echo -e "${GREEN}==========================================================${NC}"
echo "Para ativar as alterações no terminal atual, execute:"
echo -e "  ${YELLOW}export HPHL_HOME=\"$SCRIPT_DIR\"${NC}"
echo -e "  ${YELLOW}export PATH=\"\$HPHL_HOME/bin:\$PATH\"${NC}"
echo ""
echo "Ou reinicie seu terminal e execute:"
echo -e "  ${YELLOW}hphlc --version${NC}"
echo ""
