# HP-HL Language Support (VS Code & Cursor)

Official extension for the **HP-HL (High-Performance High-Level Language)** programming language in Visual Studio Code and Cursor.

[English](#english) | [Português](#português)

---

## English

### Features

- 🎨 **TextMate Syntax Highlighting**: Full grammar support for `struct`, `class`, `extern`, `ptr`, `addr_of`, `spawn`, `channel`, SIMD vector types and string interpolation.
- ⚡ **Language Server Protocol (LSP)**:
  - Real-time compiler diagnostics as you type.
  - Contextual auto-completion for keywords, variables, methods, structs and types.
  - Go to Definition & Document Symbols.
  - Hover tooltips with signatures and type documentation.
- 🐞 **Debug Adapter Protocol (DAP)**:
  - Native debugging via `F5` in VS Code and Cursor.
  - Visual breakpoints, Step Over, Step Into, Step Out.
  - Variables inspection (locals, globals, memory heaps) and call stacks.
  - Multithreaded task inspection.
- 📝 **Productive Snippets**:
  - `main`: Canonical `void Main()` entry point.
  - `struct`: Flat zero-overhead stack structs (`Vec3`, `Mat4`).
  - `class`: Garbage-collected classes with constructors and interfaces.
  - `extern`: Native FFI v2 bindings to Win32, Vulkan, C DLLs.
  - `channel` & `spawn`: First-class concurrency constructs.

### Configuration

| Setting | Description | Default |
|---|---|---|
| `hphl.compilerPath` | Path to the `hphlc` compiler executable | Searches `bin/hphlc(.exe)` or system `PATH` |
| `hphl.backend` | Active compilation backend (`x64`, `llvm`, `ir`) | `x64` |
| `hphl.trace.server` | Verbosity of LSP protocol messages | `off` |

---

## Português

### Recursos

- 🎨 **Realce de Sintaxe TextMate**: Suporte completo a `struct`, `class`, `extern`, `ptr`, `addr_of`, `spawn`, `channel`, tipos vetoriais e strings.
- ⚡ **Language Server Protocol (LSP)**:
  - Diagnósticos em tempo real durante a digitação.
  - Autocompletação de palavras-chave, variáveis, métodos e tipos.
  - Navegação para definição (*Go to Definition*).
  - Assinatura e documentação ao passar o cursor (*Hover*).
- 🐞 **Debug Adapter Protocol (DAP)**:
  - Depuração nativa no VS Code e Cursor (F5).
  - Breakpoints visuais, execução passo a passo (*Step Over*, *Step In*, *Step Out*).
  - Inspeção de variáveis e pilha de chamadas (*Call Stack*).
- 📝 **Snippets Produtivos**:
  - `main`, `struct`, `class`, `extern`, `channel`, `spawn`.

### Instalação

1. No VS Code ou Cursor, pressione `Ctrl+Shift+X`.
2. Clique nos três pontinhos (`...`) no canto superior do painel e selecione **Install from VSIX...**
3. Escolha o arquivo `hphl-1.0.0.vsix` ou execute `install_extension.bat`.
