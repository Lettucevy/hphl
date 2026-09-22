# HP-HL Official Documentation

Welcome to the official documentation for **HP-HL (High-Performance High-Level Language)**.

HP-HL is a compiled, statically-typed systems programming language designed specifically for **game engines, physical simulations, computer graphics, low-latency backends, and WebAssembly**, combining the productivity and expressiveness of modern languages with the control and raw execution speed of C and C++.

---

## Key Features

- **Bare-Metal C++ Performance:** Native machine code generation for x64 and LLVM with AVX2/SIMD autovectorization. In numeric and matrix benchmarks, HP-HL matches or outperforms C++ (GCC `-O3`).
- **Hybrid Memory Model:**
  - `struct`: Value type layout on the stack and registers, 0 bytes object header overhead, pass-by-value, and contiguous vectorizable memory.
  - `class`: Polymorphism and heap allocation managed by a low-latency Garbage Collector (Immix mark-sweep).
- **First-Class Concurrency:** Thread-safe communication via typed channels (`channel<T>`), lightweight tasks (`spawn`, `async`/`await`), and low-level primitives (`mutex`, `atomic`).
- **Zero-Overhead FFI v2:** Direct foreign function binding to operating system and C dynamic libraries (`extern "user32"`, `extern "vulkan-1"`, `extern "sqlite3"`), manipulating raw pointers (`ptr`), buffers, and memory without conversion penalties.
- **First-Class WebAssembly:** Direct compilation to `.wasm` via LLVM and `wasm-ld`, enabling high-performance 3D graphics and WebGL/WebGPU computing in browsers.

---

## Navigation

| Section | Description |
| :--- | :--- |
| [Installation & SDK](installation.md) | How to download SDK v1.0.0, configure your environment, and use the compiler. |
| [HP-HL in 30 Minutes](tutorial/30min.md) | Practical walkthrough covering syntax, types, classes, concurrency, and builds. |
| [Performance Guide](performance_guide.md) | In-depth analysis of `struct` vs `class`, memory layouts, and benchmarks vs C++. |
| [Game Engines & Vulkan](vulkan_game_engines.md) | How to build graphics engines and simulations with Vulkan and FFI v2. |
| [WebAssembly & Web](webassembly.md) | Compilation targeting WebAssembly and browser integration. |
| [Language Reference](reference/language.md) | Complete grammar, syntax rules, keywords, and type system specification. |
| [FFI Reference](reference/ffi.md) | Comprehensive guide to linking native dynamic libraries and system APIs. |
| [Standard Library](stdlib/index.md) | Standard modules (`std.io`, `std.math`, `std.collections`, `std.sync`). |
| [Cookbook](cookbook/index.md) | Design patterns, idiomatic recipes, and practical examples. |

---

## First Program

```hphl
module main;

// Value struct: 0 bytes overhead, allocated on stack
struct Vec3 {
    float x;
    float y;
    float z;
}

Vec3 Add(Vec3 a, Vec3 b) {
    Vec3 r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    r.z = a.z + b.z;
    return r;
}

void Main() {
    Vec3 p1; p1.x = 1.0; p1.y = 2.0; p1.z = 3.0;
    Vec3 p2; p2.x = 4.0; p2.y = 5.0; p2.z = 6.0;

    Vec3 p3 = Add(p1, p2);

    print("Final position: (");
    print(p3.x); print(", ");
    print(p3.y); print(", ");
    print(p3.z); print(")\n");
}
```

To compile and execute:

```bash
hphlc main.hphl -o main.exe
./main.exe
```

Output:

```text
Final position: (5, 7, 9)
```

---

## SDK Download

Visit the [Installation & SDK Download](installation.md) page to download official packages for Windows, Linux, and macOS.
