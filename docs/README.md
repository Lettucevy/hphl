# HP-HL Official Documentation

This directory contains the canonical documentation for the HP-HL programming language, compiler toolchain, and runtime ecosystem.

## Directory Structure

| Section | Path | Contents |
| :--- | :--- | :--- |
| **Overview** | [index.md](index.md) | High-level language introduction and features. |
| **Installation** | [installation.md](installation.md) | Official SDK download, installers (.exe, .msi, .zip), and setup instructions. |
| **Tutorial** | [tutorial/30min.md](tutorial/30min.md) | 30-minute introductory hands-on walkthrough. |
| **Performance** | [performance_guide.md](performance_guide.md) | Deep dive into `struct` vs `class`, memory layouts, and benchmarks vs C++. |
| **Game Engines** | [vulkan_game_engines.md](vulkan_game_engines.md) | Vulkan graphics engine development with FFI v2 and zero-overhead structs. |
| **WebAssembly** | [webassembly.md](webassembly.md) | Direct WebAssembly compilation, canvas integration, and web applications. |
| **Language Reference** | [reference/language.md](reference/language.md) | Canonical language grammar, type system, and keyword specification. |
| **Diagnostics** | [reference/diagnostics.md](reference/diagnostics.md) | Error code reference, compiler diagnostics, and i18n localization. |
| **FFI Reference** | [reference/ffi.md](reference/ffi.md) | Foreign Function Interface v2, pointer semantics, and C ABI compatibility. |
| **Memory Model** | [reference/memory.md](reference/memory.md) | Stack allocation, policies, and Immix mark-sweep Garbage Collector. |
| **Standard Library** | [stdlib/index.md](stdlib/index.md) | Modules for I/O, string manipulation, math, and system primitives. |
| **Cookbook** | [cookbook/index.md](cookbook/index.md) | Common programming idioms, recipes, and architectural patterns. |

## Building and Testing

For developers building the compiler or contributing to the toolchain:

```powershell
# Build compiler (Windows PowerShell)
powershell -File tools/build.ps1

# Run full test suite
powershell -File tools/test.ps1 -Suite all

# Package official SDK and installers (.exe, .msi, .zip)
powershell -File tools/package_sdk.ps1
```
