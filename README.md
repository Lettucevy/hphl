# HP-HL

<p align="center">
  <img src="assets/banner.png" alt="HP-HL Banner" width="100%">
</p>

<p align="center">
  <b>High Performance and High Level Programming Language</b><br>
  Compiled directly to native machine code with zero-cost abstractions, multi-paradigm memory policies, and native concurrency.
</p>

<p align="center">
  <a href="https://velaface.com/hphl"><img src="https://img.shields.io/badge/version-1.0.0-emerald.svg" alt="Version 1.0.0"></a>
  <a href="docs/installation.md"><img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue.svg" alt="Platforms"></a>
  <a href="docs/index.md"><img src="https://img.shields.io/badge/docs-official-cyan.svg" alt="Documentation"></a>
</p>

---

## Overview

**HP-HL** (*High Performance and High Level language*) is a compiled systems programming language designed for game engines, high-throughput backend services, real-time audio/graphics, and performance-critical software.

HP-HL compiles directly to native machine code (Ahead-Of-Time / AOT) via direct x64 machine code generation or LLVM backend. The standalone toolchain binary is **`hphlc`** (HP-HL Compiler), featuring an integrated command-line interface and Language Server Protocol (`hphlc --lsp`).

### Core Philosophy

- **High-Level Expressiveness:** Clean, expressive modern syntax without legacy boilerplate.
- **Bare-Metal Performance:** Execution speed and memory efficiency on par with C and C++.
- **Pragmatic Memory Safety:** Eliminates common memory hazards through clear memory policies without the friction of complex borrow checkers.
- **Zero-Cost Abstractions:** High-level constructs (generics, pattern matching, interfaces) compile away completely in final machine code.
- **Intent-Driven Architecture:** The programmer states operational intent; the compiler generates the most optimal machine representation.

---

## Language Highlights

| Feature | Syntax / Representation |
| :--- | :--- |
| **Parametric Types** | `int<32>`, `int<64>`, `float<32>`, `float<64>` |
| **Memory Policies** | `stack`, `heap`, `arena`, `pool`, `shared`, `threadlocal` |
| **Overflow Policies** | `int<32, wrap>`, `int<32, checked>`, `int<32, saturate>`, `int<32, promote>` |
| **Native Concurrency** | `parallel { }`, `parallel foreach (x in xs) { }`, `spawn { }`, `async` / `await` |
| **Fixed Arrays** | `int[4] v = {1, 2, 3};`, multidimensional `int[2][2]`, `v.Length`, `foreach (x in v)` |
| **Dynamic Lists** | `list<int> xs;`, `xs.Add(x)`, `xs.Length`, `xs[i]`, `foreach (x in xs)` |
| **Module System** | `module Math;`, `import Math;`, `import Math as M;`, `using X = Type;` |
| **Functions & Methods** | `public int Add(int a, int b) { return a + b; }` |
| **Object Model** | `class`, `struct`, `interface` |
| **Rich Enums** | `enum NetworkState { Disconnected, Connected(int ping, string host) }` |
| **Pattern Matching** | `match (x) { Connected(ping, host) when ping < 50 => { } _ => { } }` |
| **Explicit Errors** | `Option<T>`, `Result<T, E>`, `?` operator, `panic()`, `assert()` |
| **Generics** | `List<T>`, `where T : Comparable`, complete monomorphization |
| **Metaprogramming** | `compiletime`, `derive Serializable`, `reflect` |

---

## Compilation Pipeline

```
Source Code (.hphl)
  │
  ├──► Lexer & Parser ──► AST
  │
  ├──► Semantic Analysis & Type Checking
  │
  ├──► High-Level IR (HIR) ──► Dead Code / Constant Folding
  │
  ├──► Mid-Level IR (MIR) ──► Monomorphization & Memory Policies
  │
  ├──► Direct Code Generation (GAS x86_64) OR LLVM Backend (-O0 to -O3)
  │
  └──► Native Executable (.exe / ELF / Mach-O)
```

---

## Installation

### Windows (10 / 11 x64)

#### Option 1: Graphical Setup Wizard (.exe)
Download and run the official installer:
- **`dist/hphl-setup-v1.0.0-windows-x64.exe`**
- Includes component selection, PATH configuration, `HPHL_HOME`, and Windows Uninstaller registration.

#### Option 2: Enterprise / Automated MSI (.msi)
For IT deployment, GPO, or silent installation:
```cmd
msiexec /i hphl-v1.0.0-windows-x64.msi /quiet /qn
```

#### Option 3: Terminal One-Liner (PowerShell)
```powershell
irm https://velaface.com/hphl/install.ps1 | iex
```

#### Option 4: Portable SDK (.zip)
Download and extract **`hphl-sdk-v1.0.0-windows-x64.zip`**, then run `install.bat` or `setup.bat`.

---

### Linux & macOS

```bash
curl -fsSL https://velaface.com/hphl/install.sh | bash
```

For manual setup and offline archives, see the [Installation Guide](docs/installation.md).

---

## Quick Start

### 1. Write Code

Create a file named `hello.hphl`:

```hphl
module app.hello;

void Main() {
    print("Hello, HP-HL v1.0.0!\n");
}
```

### 2. Compile and Run

Compile directly to a native executable:

```bash
hphlc hello.hphl -o hello.exe
./hello.exe
```

Or compile and execute immediately in a single step:

```bash
hphlc hello.hphl --run
```

---

## Concurrency Example

HP-HL provides native concurrency constructs designed directly into the language syntax:

```hphl
module app.concurrency;

void Main() {
    atomic int total = 0;

    // Run parallel tasks across hardware threads with automatic barrier
    parallel {
        for (int i = 0; i < 1000; i++) total += 1;
        for (int i = 0; i < 1000; i++) total += 1;
        for (int i = 0; i < 1000; i++) total += 1;
    }

    print("Total accumulated: ");
    print(total); // Exactly 3000
    print("\n");
}
```

---

## Memory Policies

HP-HL lets you select memory allocation policies per type or variable:

```hphl
// Stack allocated (default for structs and primitives)
Vector3 pos;

// Bump-allocated frame arena (bulk freed on function exit)
arena Particle[1000] particles;

// Pool allocated with O(1) slot reuse
pool Bullet bullet = new Bullet();

// Reference-counted shared instance across threads
shared Config cfg = new Config();
```

---

## Tooling & Editor Support

Official extensions are provided for Visual Studio Code and Cursor:
- Syntax highlighting and semantic tokenization.
- Rich code snippets.
- Language Server Protocol integration (`hphlc --lsp`).
- One-click installer via `editors/vscode/install_extension.bat`.

---

## Documentation

- [Getting Started & Installation](docs/installation.md)
- [Language Overview](docs/reference/language.md)
- [Memory Management Reference](docs/reference/memory.md)
- [Foreign Function Interface (FFI)](docs/reference/ffi.md)
- [Diagnostics & Error Codes](docs/reference/diagnostics.md)
- [High-Performance Systems & Vulkan Engines](docs/vulkan_game_engines.md)
- [Performance Optimization Guide](docs/performance_guide.md)
- [WebAssembly Compilation](docs/webassembly.md)

---

## License

HP-HL is open source under the MIT License. See [LICENSE](LICENSE) for details.
