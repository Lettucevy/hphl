#!/usr/bin/env bash
# ==============================================================================
# HP-HL Web Installer (Linux & macOS)
# Usage: curl -fsSL https://hphl.dev/install.sh | bash
# ==============================================================================
set -e

VERSION="1.0.0"
INSTALL_DIR="$HOME/.hphl"
REPO="Lettucevy/hphl"

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${CYAN}==========================================================${NC}"
echo -e "${CYAN}  Instalador Oficial do HP-HL (v${VERSION})${NC}"
echo -e "${CYAN}==========================================================${NC}"

# Detecta Sistema Operacional e Arquitetura
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Linux)
        OS_TAG="linux"
        ;;
    Darwin)
        OS_TAG="macos"
        ;;
    *)
        echo -e "${RED}[ERRO] Sistema operacional não suportado: $OS${NC}"
        exit 1
        ;;
esac

case "$ARCH" in
    x86_64|amd64)
        ARCH_TAG="x64"
        ;;
    arm64|aarch64)
        ARCH_TAG="arm64"
        ;;
    *)
        echo -e "${RED}[ERRO] Arquitetura não suportada: $ARCH${NC}"
        exit 1
        ;;
esac

PACKAGE_NAME="hphl-sdk-v${VERSION}-${OS_TAG}-${ARCH_TAG}.tar.gz"
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${PACKAGE_NAME}"

echo -e "Plataforma detectada: ${YELLOW}${OS_TAG}-${ARCH_TAG}${NC}"
echo -e "Diretório de instalação: ${YELLOW}${INSTALL_DIR}${NC}"

# Cria pasta temporária de download
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo -e "${YELLOW}[1/4] Baixando ${PACKAGE_NAME}...${NC}"
if command -v curl >/dev/null 2>&1; then
    curl -fSL "$DOWNLOAD_URL" -o "$TMP_DIR/$PACKAGE_NAME" || {
        echo -e "${YELLOW}[AVISO] Download do pacote pré-compilado indisponível. Clonando via fonte...${NC}"
        git clone --depth 1 https://github.com/${REPO}.git "$TMP_DIR/hphl-src"
        mkdir -p "$INSTALL_DIR"
        cp -r "$TMP_DIR/hphl-src/"* "$INSTALL_DIR/"
        cd "$INSTALL_DIR" && ./tools/build.sh
    }
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$TMP_DIR/$PACKAGE_NAME" "$DOWNLOAD_URL"
fi

if [ -f "$TMP_DIR/$PACKAGE_NAME" ]; then
    echo -e "${YELLOW}[2/4] Extraindo arquivos em ${INSTALL_DIR}...${NC}"
    mkdir -p "$INSTALL_DIR"
    tar -xzf "$TMP_DIR/$PACKAGE_NAME" -C "$INSTALL_DIR" --strip-components=1
fi

# Configura permissões
chmod +x "$INSTALL_DIR/bin/hphlc" 2>/dev/null || true

# Configura variáveis de ambiente
echo -e "${YELLOW}[3/4] Configurando variáveis de ambiente (PATH)...${NC}"
HPHL_ENV_LINE="export HPHL_HOME=\"$INSTALL_DIR\""
PATH_ENV_LINE="export PATH=\"\$HPHL_HOME/bin:\$PATH\""

SHELL_PROFILES=()
[ -f "$HOME/.bashrc" ] && SHELL_PROFILES+=("$HOME/.bashrc")
[ -f "$HOME/.zshrc" ] && SHELL_PROFILES+=("$HOME/.zshrc")
[ -f "$HOME/.profile" ] && SHELL_PROFILES+=("$HOME/.profile")

for profile in "${SHELL_PROFILES[@]}"; do
    if ! grep -q "HPHL_HOME" "$profile" 2>/dev/null; then
        echo "" >> "$profile"
        echo "# HP-HL Environment" >> "$profile"
        echo "$HPHL_ENV_LINE" >> "$profile"
        echo "$PATH_ENV_LINE" >> "$profile"
    fi
done

# Instala extensão do VS Code se disponível
echo -e "${YELLOW}[4/4] Verificando integração com editores...${NC}"
VSIX_PATH="$INSTALL_DIR/editors/hphl-1.0.0.vsix"
if [ -f "$VSIX_PATH" ]; then
    if command -v code >/dev/null 2>&1; then
        code --install-extension "$VSIX_PATH" --force 2>/dev/null || true
    fi
    if command -v cursor >/dev/null 2>&1; then
        cursor --install-extension "$VSIX_PATH" --force 2>/dev/null || true
    fi
fi

echo ""
echo -e "${GREEN}==========================================================${NC}"
echo -e "${GREEN}  HP-HL v${VERSION} instalado com sucesso em ${INSTALL_DIR}!${NC}"
echo -e "${GREEN}==========================================================${NC}"
echo ""
echo "Reinicie seu terminal ou execute:"
echo -e "  ${YELLOW}export HPHL_HOME=\"$INSTALL_DIR\"${NC}"
echo -e "  ${YELLOW}export PATH=\"\$HPHL_HOME/bin:\$PATH\"${NC}"
echo ""
echo "Teste a instalação com:"
echo -e "  ${YELLOW}hphlc --version${NC}"
echo ""
