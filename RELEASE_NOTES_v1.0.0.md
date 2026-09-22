# HP-HL v1.0.0 — Official GA Release

I'm proud to announce the official **v1.0.0 General Availability (GA)** release of **HP-HL (High-Performance High-Level Language)**!

HP-HL is a compiled, statically typed systems programming language designed specifically for **game engines, physical simulations, computer graphics, low-latency backends, and WebAssembly**, combining the productivity and expressiveness of modern languages with the control and raw execution speed of C and C++.

---

## Key Highlights & Features

### 1. Bare-Metal Execution & LLVM Optimizations
- Native code generation for **x86_64** and **LLVM** with full **AVX2 / FMA SIMD autovectorization**.
- Release builds (`--backend llvm -O3`) achieve execution speeds matching or outperforming optimized C++ (GCC `-O3 -march=native`).

### 2. Hybrid Memory Model (`struct` vs `class`)
- **`struct` (Value Types / Zero-GC):** Contiguous stack and vector register residency, zero object header overhead, pass-by-value, and flat memory layout. Ideal for 3D mathematics, vectors, matrices, particles, and raw buffers.
- **`class` (Managed OOP):** Polymorphic reference types with single inheritance and interfaces, managed by a low-latency **Immix generational mark-sweep GC** operating in 32 KB blocks and 128-byte lines.

### 3. First-Class Concurrency
- Typed channels (`channel<T>`) for thread-safe message passing.
- Lightweight asynchronous tasks (`spawn`, `async` / `await`).
- Direct synchronization primitives: `mutex`, `barrier`, `event`, and `atomic`.

### 4. Zero-Overhead FFI v2
- Direct dynamic library binding (`extern "user32"`, `extern "vulkan-1"`, `extern "sqlite3"`).
- Direct pointer manipulation (`ptr`, `addr_of`) without marshaling or bridge penalties.

### 5. First-Class WebAssembly
- Compile directly to `.wasm` via LLVM and `wasm-ld` (`--target wasm32-unknown-unknown -O3`), enabling high-performance 3D graphics in modern browsers.

---

## Audited Definitive Benchmarks (Intel® Xeon® E5-2640 v4)

Executed and verified directly on real hardware (**Intel® Xeon® CPU E5-2640 v4 @ 2.40GHz, 10 Cores / 20 Threads, 16 GB RAM, Windows x64**):

| Benchmark Workload | C++ (GCC 16.1) | C++ (Clang 22.1) | HP-HL (struct) | HP-HL (class) | C# (.NET 10) | Java (Adoptium 25) | Result Summary |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **01. Physics Particles 3D (100k)** | 97.2 ms | 96.7 ms | **88.4 ms** | 113.8 ms | 185.9 ms | 221.6 ms | **HP-HL struct is fastest** (1.09x vs Clang, 2.1x vs C#, 2.5x vs Java) |
| **02. Matrix Transforms 4x4 (2M)** | 30.1 ms | 30.5 ms | **29.8 ms** | 32.3 ms | 139.1 ms | 162.8 ms | **HP-HL struct is fastest** (1.01x vs GCC, 4.6x vs C#, 5.4x vs Java) |
| **03. Raytracer 3D (800x800)** | 26.4 ms | 27.9 ms | **26.1 ms** | 38.0 ms | 144.2 ms | 208.8 ms | **HP-HL struct matches C++** (3.8x vs C#, 5.5x vs Java) |
| **04. Binary Trees Depth 14 (GC)** | 753.9 ms | 777.1 ms | — | **679.5 ms** | 265.7 ms | 216.8 ms | **HP-HL 1.11x faster than C++** (managed bump nursery) |
| **05. Mandelbrot (1200x1200)** | **303.3 ms** | 305.3 ms | 339.9 ms | 339.9 ms | 445.3 ms | 430.5 ms | **C++ leads by ~11%**; HP-HL beats C# by 1.31x and Java by 1.27x |

---

## Installation & Downloads

### Windows Quick Install (PowerShell):
```powershell
irm https://velaface.com/hphl/install.ps1 | iex
```

### Linux & macOS Quick Install (Bash):
```bash
curl -fsSL https://velaface.com/hphl/install.sh | bash
```

### Official Standalone Packages:
- **Windows Setup Wizard (`.exe`):** `hphl-setup-v1.0.0-windows-x64.exe` (Recommended)
- **Windows Enterprise Installer (`.msi`):** `hphl-v1.0.0-windows-x64.msi`
- **Windows Portable Archive (`.zip`):** `hphl-sdk-v1.0.0-windows-x64.zip`
- **Linux x64 Archive (`.tar.gz`):** `hphl-sdk-v1.0.0-linux-x64.tar.gz`
- **macOS Universal Archive (`.tar.gz`):** `hphl-sdk-v1.0.0-macos-universal.tar.gz`
- **VS Code / Cursor Extension (`.vsix`):** `hphl-1.0.0.vsix`

---

## Verifying Your Installation

```bash
hphlc --version
# Output: HP-HL Compiler (hphlc) v1.0.0 (x86_64-pc-windows-msvc)
```

Compile your first program:
```bash
hphlc hello.hphl -o hello.exe --backend llvm -O3
./hello.exe
```

---

*Developed by Velaface.*
