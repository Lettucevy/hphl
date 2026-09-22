# Download & Instalação do SDK HP-HL

Esta página orienta na instalação do **HP-HL Software Development Kit (SDK)** versão **v1.0.0**.

---

## Downloads Oficiais

O SDK do HP-HL inclui o compilador nativo, cabeçalhos do runtime C, biblioteca padrão completa, projetos de exemplo e documentação offline.

| Plataforma | Pacote | Formato | Status |
| :--- | :--- | :--- | :--- |
| **Windows x64 (Instalador Executável)** | `hphl-setup-v1.0.0-windows-x64.exe` | Assistente (.exe) | **Recomendado (v1.0.0)** |
| **Windows x64 (Instalador Corporativo)** | `hphl-v1.0.0-windows-x64.msi` | Windows Installer (.msi) | **Oficial (v1.0.0)** |
| **Windows x64 (Portátil)** | `hphl-sdk-v1.0.0-windows-x64.zip` | ZIP | **Oficial (v1.0.0)** |
| **Linux x64** | `hphl-sdk-v1.0.0-linux-x64.tar.gz` | TAR.GZ | Suporte via script e código-fonte |
| **macOS x64 / ARM64** | `hphl-sdk-v1.0.0-macos-universal.tar.gz`| TAR.GZ | Suporte universal x64/arm64 |

---

## Requisitos de Sistema (Windows)

- **Sistema Operacional:** Windows 10 (64-bit) ou Windows 11 (64-bit).
- **Processador:** CPU x86_64 com instruções SSE4.2 / AVX2 (recomendado para autovetorização).
- **Compilador C/C++ Host (Opcional):**
  - O HP-HL inclui geração nativa de código x64 e backend LLVM embutido.
  - Para vincular bibliotecas externas C via FFI ou recompilar runtimes personalizados, recomenda-se ter o **MinGW-w64 (GCC)** ou **Clang** no seu PATH.
- **Gráficos (Opcional):**
  - Para projetos gráficos em Vulkan, recomenda-se ter o **Vulkan SDK** instalado com drivers de GPU atualizados.

---

## Instalação Multiplataforma

### Windows (10 / 11 x64)

#### Opção 1: Assistente de Instalação Oficial `.exe` (Recomendado)
1. Baixe `hphl-setup-v1.0.0-windows-x64.exe` do site oficial ou da central de downloads.
2. Dê duplo clique no arquivo para iniciar o instalador.
3. Selecione a pasta de destino e os componentes desejados (Compilador, Documentação, Exemplos, Extensão VS Code).
4. O assistente configura automaticamente o `PATH`, `HPHL_HOME`, associações de arquivos `.hphl` e registra o desinstalador no Windows.

#### Opção 2: Pacote MSI Corporativo `.msi` (Instalações Silenciosas / GPO)
1. Baixe `hphl-v1.0.0-windows-x64.msi`.
2. Para instalação silenciosa via linha de comando:
   ```cmd
   msiexec /i hphl-v1.0.0-windows-x64.msi /quiet /qn
   ```

#### Opção 3: Script Web via PowerShell
Abra o **PowerShell** e execute:
```powershell
irm https://velaface.com/hphl/install.ps1 | iex
```

#### Opção 4: Pacote Portátil em ZIP
1. Baixe e extraia `hphl-sdk-v1.0.0-windows-x64.zip`.
2. Dê duplo clique em `install.bat` (ou `setup.bat`).

---

### Linux (Ubuntu, Debian, Fedora, Arch)

#### Opção 1: Script de Terminal Web
```bash
curl -fsSL https://velaface.com/hphl/install.sh | bash
```

#### Opção 2: Arquivo Offline
1. Extraia o pacote:
   ```bash
   tar -xzf hphl-sdk-v1.0.0-linux-x64.tar.gz
   cd hphl-sdk-v1.0.0-linux-x64
   ```
2. Execute o instalador:
   ```bash
   ./install.sh
   ```
3. O script concede permissões de execução para `bin/hphlc`, exporta `HPHL_HOME` e `PATH` no `~/.bashrc` / `~/.zshrc` e configura a extensão para o editor.

---

### macOS (Apple Silicon M-Series e Intel x64)

#### Opção 1: Script de Terminal Web
```bash
curl -fsSL https://velaface.com/hphl/install.sh | bash
```

#### Opção 2: Arquivo Offline
1. Extraia e instale:
   ```bash
   tar -xzf hphl-sdk-v1.0.0-macos-universal.tar.gz
   cd hphl-sdk-v1.0.0-macos-universal
   ./install.sh
   ```

---

## Verificando a Instalação

Abra uma janela de terminal e execute:

```bash
hphlc --version
```

**Saída esperada:**
```text
HP-HL Compiler (hphlc) v1.0.0 (x86_64-pc-windows-msvc)
```

---

## Integração com Visual Studio Code & Cursor

O SDK inclui integração oficial para editores modernos:
- **Destaque de Sintaxe TextMate:** Suporte completo para `struct`, `class`, `extern`, `channel`, `ptr`, etc.
- **Protocolo de Servidor de Linguagem (LSP):** Autocompletar, documentação em hover e diagnósticos inline.
- **Protocolo de Depuração DAP:** Depuração passo a passo com pontos de parada (breakpoints), inspeção de variáveis e pilha de chamadas (F5).

### Instalação em 1 Clique:
Na pasta `editors/` do SDK:
- Dê duplo clique em `install_extension.bat` (ou execute `install_extension.ps1` no PowerShell).
- O instalador detecta automaticamente as instâncias do VS Code e Cursor e instala a extensão.

---

## Compilando Seu Primeiro Programa

Crie um arquivo chamado `hello.hphl`:

```hphl
module app.hello;

void Main() {
    print("SDK HP-HL v1.0.0 instalado com sucesso!\n");
}
```

Compile e execute:

```bash
hphlc hello.hphl -o hello.exe
./hello.exe
```

Com otimizações LLVM e autovetorização:

```bash
hphlc hello.hphl -o hello.exe --backend llvm -O3
./hello.exe
```

Seu ambiente HP-HL está pronto. Continue para o guia [HP-HL em 30 Minutos](tutorial/30min.md) para explorar a linguagem a fundo.
