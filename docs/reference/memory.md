# HP-HL Memory Architecture & Management

HP-HL combines **zero-overhead flat stack allocation** for value types with a **high-throughput, low-latency Immix Garbage Collector** for heap-allocated object graphs.

---

## Memory Layout Regions

Memory in an HP-HL executable is structured across three fundamental zones:

```text
┌─────────────────────────────────────────────────────────┐
│                       The Stack                         │
│  - Local variables, pointers, primitives (int, float)   │
│  - Structs (Vec3, Mat4, etc.) without object headers    │
│  - Instant deallocation via stack pointer (%rsp)        │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│               Managed Heap (Immix GC)                   │
│  - Class instances (new MyClass())                      │
│  - Dynamic lists (list<T>), hash maps (map<K, V>)       │
│  - Immutable strings and closures                       │
│  - Block-based mark-sweep collector in 32 KB chunks     │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│                 Raw / Unmanaged Memory                  │
│  - Buffers allocated via mem_alloc / malloc             │
│  - Texture memory mapped for Vulkan and shaders         │
│  - Explicit reclamation via mem_free                    │
└─────────────────────────────────────────────────────────┘
```

---

## Structs: Zero-Overhead Value Types

In HP-HL, `struct` defines a value type:

- **Zero Object Header:** Unlike managed runtimes where every object incurs a 16-24 byte header (type pointer, monitor lock), an HP-HL `struct` occupies **only its declared field bytes**.
- **Stack or Register Residency:** When passed to functions, small structs reside directly in CPU registers; larger structs remain in contiguous stack frames.
- **Value Semantics:** Mutating fields of a struct passed by value modifies only the local stack copy, unless an address is explicitly passed via `ptr` or `ref`.

---

## Immix Low-Latency Garbage Collector

For dynamic object graphs and class hierarchies, HP-HL utilizes an optimized **Immix** mark-sweep collector:

1. **Blocks and Lines Architecture:**
   - The heap is structured into 32 KB blocks.
   - Each block is divided into 128-byte lines.
2. **Line-Marking:**
   - During the sweep phase, the collector marks active lines. Unmarked lines are returned immediately to the free list without requiring global compaction pauses, eliminating Stop-The-World stalls.
3. **Bump Allocation:**
   - The allocator utilizes thread-local bump pointers across available lines, making `new` allocations as cheap as stack pointer increments.

---

## Deterministic Resource Management (RAII)

While heap memory is collected automatically, operating system resources (file descriptors, sockets, GPU handles) require deterministic lifetime management.

In HP-HL, the standard pattern utilizes `try` / `finally`:

```hphl
module app.resource;

void ProcessFile(string path) {
    ptr handle = OpenFile(path);
    try {
        // Read and write operations
    } finally {
        // Guaranteed to execute even if an exception occurs
        CloseFile(handle);
    }
}
```

Class finalizers `~ClassName()` are invoked when the GC reclaims an unreachable object, while `finally` blocks guarantee immediate deterministic cleanup of external native handles.
