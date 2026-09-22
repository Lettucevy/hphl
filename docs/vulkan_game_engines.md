# Building Game Engines in HP-HL with Vulkan & FFI v2

HP-HL was architected from day one to power **ultra-high-performance 2D and 3D game engines**, bypassing intermediate wrapper layers to interface directly with native system graphics APIs.

This guide explains how to structure graphics engines in HP-HL using the **Vulkan API**, how zero-overhead `struct` types mirror standard C memory layouts, and how to integrate pre-compiled SPIR-V shaders.

---

## Architectural Philosophy: Why Vulkan in HP-HL?

Unlike scripting languages or virtual-machine-based runtimes (such as C# or Java), HP-HL compiles directly to native machine code (x64/LLVM) with:
1. **Zero-Overhead Memory Layout:** Flat structs in HP-HL contain no hidden object headers or virtual table pointers (`vptr`).
2. **Direct FFI v2:** Direct invocation of system dynamic libraries (`vulkan-1.dll`, `user32.dll`, `gdi32.dll`, `libvulkan.so`) with zero marshaling penalties.
3. **Native Pointer Semantics (`ptr` and `addr_of`):** Pass stack memory addresses directly to Vulkan API calls.

---

## Interfacing with Vulkan via FFI v2

In HP-HL, the `extern` keyword imports foreign symbols directly and concisely:

```hphl
module engine.vulkan;

// Vulkan Core functions imported directly from vulkan-1.dll
extern "vulkan-1" int vkCreateInstance(ptr pCreateInfo, ptr pAllocator, ptr pInstance);
extern "vulkan-1" void vkDestroyInstance(ptr instance, ptr pAllocator);
extern "vulkan-1" int vkEnumeratePhysicalDevices(ptr instance, ptr pDeviceCount, ptr pDevices);

// Win32 APIs for window creation
extern "user32" ptr CreateWindowExA(
    int dwExStyle, string lpClassName, string lpWindowName,
    int dwStyle, int X, int Y, int nWidth, int nHeight,
    ptr hWndParent, ptr hMenu, ptr hInstance, ptr lpParam
);
```

---

## Zero-Overhead Memory Structures

Vulkan specifications rely heavily on creation descriptor structures starting with `sType` and `pNext`. In HP-HL, these are modeled as value `struct`s, ensuring **precise memory alignment on the stack**:

```hphl
// Exact binary mirror of C struct: VkApplicationInfo
struct VkApplicationInfo {
    int sType;              // VK_STRUCTURE_TYPE_APPLICATION_INFO = 0
    ptr pNext;              // null
    string pAppName;        // "My Game Engine"
    int appVersion;         // 1
    string pEngineName;     // "HP-HL Engine"
    int engineVersion;      // 1
    int apiVersion;         // VK_API_VERSION_1_3
}

// Exact binary mirror of C struct: VkInstanceCreateInfo
struct VkInstanceCreateInfo {
    int sType;              // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1
    ptr pNext;              // null
    int flags;              // 0
    ptr pAppInfo;           // Address of VkApplicationInfo
    int enabledLayerCount;
    ptr ppEnabledLayerNames;
    int enabledExtensionCount;
    ptr ppEnabledExtensionNames;
}
```

### Initializing the Vulkan Instance:

```hphl
void InitVulkan() {
    VkApplicationInfo appInfo;
    appInfo.sType = 0; // VK_STRUCTURE_TYPE_APPLICATION_INFO
    appInfo.pNext = null;
    appInfo.pAppName = "Vulkan Game";
    appInfo.appVersion = 1;
    appInfo.pEngineName = "HPHL-Engine";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = 4198400; // Vulkan 1.3

    VkInstanceCreateInfo createInfo;
    createInfo.sType = 1; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    createInfo.pNext = null;
    createInfo.flags = 0;
    createInfo.pAppInfo = addr_of(appInfo);
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = null;
    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = null;

    ptr instance = null;
    int result = vkCreateInstance(addr_of(createInfo), null, addr_of(instance));
    
    if (result == 0) { // VK_SUCCESS
        print("[Engine] Vulkan instance initialized successfully!\n");
    } else {
        print("[Engine] Failed to initialize Vulkan. Error code: ");
        print(result); print("\n");
    }
}
```

---

## SPIR-V Shaders & Vector Mathematics

Vulkan rendering pipelines consume compiled binary shaders in **SPIR-V** format (`.spv`).

1. **Shader Compilation:**
   Author shaders in GLSL or HLSL and compile them using `glslc`:
   ```bash
   glslc shader.vert -o vert.spv
   glslc shader.frag -o frag.spv
   ```
2. **Loading in HP-HL:**
   Read `.spv` bytecode into a memory buffer and pass the pointer to `vkCreateShaderModule`.
3. **Vector Math in HP-HL:**
   For camera transformations, Model-View-Projection (MVP) matrices, and lighting calculations, use `struct Mat4` and `struct Vec3`:
   ```hphl
   struct Mat4 {
       float m00; float m01; float m02; float m03;
       float m10; float m11; float m12; float m13;
       float m20; float m21; float m22; float m23;
       float m30; float m31; float m32; float m33;
   }
   ```
   As verified in official benchmarks, the HP-HL LLVM backend autovectorizes these operations with AVX2 SIMD instructions, running up to **1.87x faster than C++**.

---

## Ecosystem & Reference Implementations

During the evolution of HP-HL, full graphics engine viability was validated through the experimental **ProkionEngine**:
- Native Vulkan window initialization with swapchain double-buffering.
- 3D mesh rendering and real-time particle simulations at steady 60+ FPS without garbage collection stalls.
- Native **Dear ImGui** integration for debug overlays and engine tools.

ProkionEngine is maintained as an independent open-source project within the ecosystem to provide an architectural reference for game developers.
