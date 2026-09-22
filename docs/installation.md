# HP-HL SDK Download & Installation

This page guides you through installing the **HP-HL Software Development Kit (SDK)** version **v1.0.0**.

---

## Official Downloads

The HP-HL SDK includes the native compiler, C runtime headers, the complete standard library, sample projects, and offline documentation.

| Platform | Package | Format | Status |
| :--- | :--- | :--- | :--- |
| **Windows x64 (Executable Installer)** | `hphl-setup-v1.0.0-windows-x64.exe` | Setup Wizard (.exe) | **Recommended (v1.0.0)** |
| **Windows x64 (Enterprise Installer)** | `hphl-v1.0.0-windows-x64.msi` | Windows Installer (.msi) | **Official (v1.0.0)** |
| **Windows x64 (Portable)** | `hphl-sdk-v1.0.0-windows-x64.zip` | ZIP | **Official (v1.0.0)** |
| **Linux x64** | `hphl-sdk-v1.0.0-linux-x64.tar.gz` | TAR.GZ | Source / script support |
| **macOS x64 / ARM64** | `hphl-sdk-v1.0.0-macos-universal.tar.gz`| TAR.GZ | Source / script support |

---

## System Requirements (Windows)

- **Operating System:** Windows 10 (64-bit) or Windows 11 (64-bit).
- **Processor:** x86_64 CPU with SSE4.2 / AVX2 instructions (recommended for autovectorization).
- **Host C/C++ Compiler (Optional):**
  - HP-HL includes direct x64 machine code generation and embedded LLVM backend.
  - To link external C dynamic libraries via FFI or rebuild custom runtimes, having **MinGW-w64 (GCC)** or **Clang** in your PATH is recommended.
- **Graphics (Optional):**
  - For Vulkan graphics projects, having the **Vulkan SDK** installed with updated GPU drivers is recommended.

---

## Multiplatform Installation

### Windows (10 / 11 x64)

#### Option 1: Official Setup Wizard `.exe` (Recommended)
1. Download `hphl-setup-v1.0.0-windows-x64.exe` from the official site or releases.
2. Double-click the installer to launch the modern setup wizard.
3. Select your destination directory and optional components (Core Compiler, Docs, Examples, VS Code Extension).
4. The wizard automatically configures `PATH`, `HPHL_HOME`, `.hphl` file associations, and registers the uninstaller in Windows Settings.

#### Option 2: Windows Installer `.msi` (Enterprise / GPO / Silent Deployments)
1. Download `hphl-v1.0.0-windows-x64.msi`.
2. For unattended or script-driven deployments:
   ```cmd
   msiexec /i hphl-v1.0.0-windows-x64.msi /quiet /qn
   ```

#### Option 3: Terminal Web Script (PowerShell)
Open **PowerShell** and run:
```powershell
irm https://velaface.com/hphl/install.ps1 | iex
```

#### Option 4: Portable ZIP Package
1. Download and extract `hphl-sdk-v1.0.0-windows-x64.zip`.
2. Double-click `install.bat` (or `setup.bat`).

---

### Linux (Ubuntu, Debian, Fedora, Arch)

#### Option 1: Terminal Web Script
```bash
curl -fsSL https://velaface.com/hphl/install.sh | bash
```

#### Option 2: Offline Archive
1. Extract the tarball:
   ```bash
   tar -xzf hphl-sdk-v1.0.0-linux-x64.tar.gz
   cd hphl-sdk-v1.0.0-linux-x64
   ```
2. Run the installer script:
   ```bash
   ./install.sh
   ```
3. The script sets execution permissions on `bin/hphlc`, exports `HPHL_HOME` and `PATH` into `~/.bashrc` / `~/.zshrc`, and installs the editor extension.

---

### macOS (Apple Silicon M-Series & Intel x64)

#### Option 1: Terminal Web Script
```bash
curl -fsSL https://velaface.com/hphl/install.sh | bash
```

#### Option 2: Offline Archive
1. Extract and install:
   ```bash
   tar -xzf hphl-sdk-v1.0.0-macos-universal.tar.gz
   cd hphl-sdk-v1.0.0-macos-universal
   ./install.sh
   ```

---

## Manual Environment Configuration

If you prefer to configure environment variables manually on Windows:

1. Extract the SDK to a permanent directory (e.g. `C:\hphl`).
2. Open Start Menu and search for **Environment Variables**.
3. Under **User Variables**, click **New**:
   - **Variable name:** `HPHL_HOME`
   - **Variable value:** `C:\hphl`
4. Locate the `Path` variable under **User Variables**, click **Edit**, then **New** and add:
   - `%HPHL_HOME%\bin`
5. Click **OK** on all dialogs to save changes.

---

## Verifying Your Installation

Open a new terminal window and run:

```bash
hphlc --version
```

**Expected output:**
```text
HP-HL Compiler (hphlc) v1.0.0 (x86_64-pc-windows-msvc)
```

---

## SDK Layout

The extracted SDK package has the following layout:

```text
hphl-sdk-v1.0.0-windows-x64/
├── bin/
│   └── hphlc.exe                 # Native compiler executable
├── runtime/                      # High-performance runtime and Immix GC
│   ├── include/
│   │   └── hphl_runtime.h        # C/C++ integration headers
│   └── src/
├── stdlib/                       # Standard library modules (.hphl)
├── examples/                     # Ready-to-compile sample programs
│   ├── hello/                    # Introductory Hello World
│   ├── matrix/                   # High-throughput matrix algebra
│   ├── physics/                  # Real-time particle simulation
│   └── raytracer/                # Multi-threaded raytracer
├── docs/                         # Offline documentation in Markdown
├── editors/                      # Official editor tooling
│   └── hphl-1.0.0.vsix           # Extension for VS Code and Cursor
├── install.ps1                   # Automated PowerShell installer
├── install.bat                   # Automated CMD installer
├── README.md                     # Quick start guide
└── LICENSE                       # License file
```

---

## Visual Studio Code & Cursor Integration

The SDK includes official editor integration:
- **TextMate Syntax Highlighting:** Comprehensive grammar for `struct`, `class`, `extern`, `channel`, `ptr`, etc.
- **Language Server Protocol (LSP):** Autocompletion, hover documentation, and inline diagnostics.
- **Debug Adapter Protocol (DAP):** Step-by-step debugging with breakpoints, variable inspection, and call stacks (F5).

### 1-Click Installation:
Inside the `editors/` directory of the SDK:
- Double-click `install_extension.bat` (or run `install_extension.ps1` in PowerShell).
- Automatically discovers installed VS Code and Cursor instances and installs the extension.

### Command Line Installation:
```bash
code --install-extension editors/hphl-1.0.0.vsix
```
Or for Cursor:
```bash
cursor --install-extension editors/hphl-1.0.0.vsix
```

> [!NOTE]
> **Notice for Visual Studio (Community / Professional) Users:**
> On Windows, double-clicking a `.vsix` file by default invokes the classic Visual Studio installer (`VSIXInstaller.exe`), which is incompatible with VS Code extensions. Always use `install_extension.bat` or use the VS Code UI (*Extensions* -> `...` -> *Install from VSIX...*).

---

## Compiling Your First Program

Create a file named `hello.hphl`:

```hphl
module app.hello;

void Main() {
    print("HP-HL SDK v1.0.0 installed successfully!\n");
}
```

Compile and run:

```bash
hphlc hello.hphl -o hello.exe
./hello.exe
```

With full LLVM optimizations and autovectorization:

```bash
hphlc hello.hphl -o hello.exe --backend llvm -O3
./hello.exe
```

Your HP-HL development environment is ready. Proceed to the [HP-HL in 30 Minutes](tutorial/30min.md) guide to explore language features in depth.
