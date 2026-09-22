# FFI v2: Foreign Function Interface & Native Interoperability

**FFI v2 (Foreign Function Interface)** in HP-HL enables direct, high-speed interoperability with C/C++ dynamic libraries (`.dll` on Windows, `.so` on Linux, `.dylib` on macOS) and native operating system APIs with zero marshaling overhead.

---

## Declaring External Functions (`extern`)

The `extern` keyword declares function signatures residing in external shared libraries.

### 1. Linking Specific Shared Libraries
```hphl
// Import from Win32 subsystem (user32.dll)
extern "user32" int MessageBoxA(ptr hwnd, string text, string caption, int type);
extern "user32" int GetAsyncKeyState(int vKey);

// Import from Vulkan Core (vulkan-1.dll)
extern "vulkan-1" int vkCreateInstance(ptr pCreateInfo, ptr pAllocator, ptr pInstance);

// Import from SQLite (sqlite3.dll)
extern "sqlite3" int sqlite3_open(string filename, ptr ppDb);
extern "sqlite3" int sqlite3_close(ptr db);
```

### 2. Standard C Runtime Functions (libc / msvcrt)
When the library specifier is omitted, the symbol is resolved directly from the standard C runtime:

```hphl
extern int puts(string text);
extern double sin(double v);
extern double cos(double v);
```

---

## Pointer Operations (`ptr` and `addr_of`)

HP-HL provides the native `ptr` type for raw memory addresses and the `addr_of(...)` unary operator to capture stack or heap variable addresses:

```hphl
struct Point {
    int x;
    int y;
}

void PointerExample() {
    Point pt;
    pt.x = 100;
    pt.y = 200;

    // Capture raw stack address of the struct
    ptr address = addr_of(pt);

    print("Memory address: ");
    print(address); print("\n");
}
```

---

## Native Memory Primitives (`mem_*`)

For low-level communication, network packet parsing, binary decoding, and advanced FFI interactions, the runtime provides intrinsic primitives:

| Function | Signature | Description |
| :--- | :--- | :--- |
| `mem_alloc` | `ptr mem_alloc(int sizeBytes)` | Allocates a contiguous memory block on the system heap. |
| `mem_free` | `void mem_free(ptr p)` | Frees the allocated memory block. |
| `mem_copy` | `void mem_copy(ptr dest, ptr src, int bytes)` | High-throughput memory copy (equivalent to `memcpy`). |
| `mem_set` | `void mem_set(ptr dest, int byteVal, int bytes)` | Memory fill (equivalent to `memset`). |
| `mem_read_i32` | `int mem_read_i32(ptr p, int offset)` | Reads a 32-bit signed integer from address + offset. |
| `mem_write_i32` | `void mem_write_i32(ptr p, int offset, int val)` | Writes a 32-bit signed integer at address + offset. |
| `mem_read_f32` | `float mem_read_f32(ptr p, int offset)` | Reads a 32-bit float from address + offset. |
| `mem_write_f32`| `void mem_write_f32(ptr p, int offset, float val)`| Writes a 32-bit float at address + offset. |

---

## Calling Conventions & ABI Compliance

The native x64 codegen strictly conforms to target platform calling conventions:
- **Windows x64 (Microsoft ABI):**
  - First 4 integer/pointer arguments in `%rcx`, `%rdx`, `%r8`, `%r9`.
  - Floating-point arguments in `%xmm0`, `%xmm1`, `%xmm2`, `%xmm3`.
  - Mandatory 32-byte Shadow Space allocated before every `call`.
  - Return values in `%rax` (integer/pointer) and `%xmm0` (float).
- **Linux/macOS x64 (System V AMD64 ABI):**
  - Arguments passed in `%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9` and `%xmm0`–`%xmm7`.

This ensures seamless binary compatibility with libraries compiled from C, C++, Rust, Zig, or Assembly without thunking layers.
