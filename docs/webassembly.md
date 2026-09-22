# WebAssembly (WASM), HTML5 & Web Applications

HP-HL supports first-class compilation to **WebAssembly (WASM)** via the LLVM backend and `wasm-ld` linker.

This enables running heavy physics simulations, WebGL/WebGPU graphics pipelines, and high-performance algorithms directly inside modern web browsers at near-native execution speed, styled with **HTML5** and **CSS3**.

---

## How HP-HL Targets WASM

1. HP-HL source code compiles directly to a standalone binary `.wasm` module.
2. WebAssembly linear memory (`(export "memory")`) is exposed directly to the browser runtime.
3. The host JavaScript environment instantiates the module via `WebAssembly.instantiateStreaming`, establishing a low-latency bidirectional bridge.

---

## Compiling for WebAssembly

To compile an HP-HL source file targeting WebAssembly:

```bash
hphlc app.hphl -o app.wasm --backend llvm --target wasm32-unknown-unknown -O3
```

- `--backend llvm`: Leverages the LLVM code generator to produce optimized IR.
- `--target wasm32-unknown-unknown`: Sets target architecture to 32-bit WebAssembly.
- `-O3`: Activates aggressive optimizations (vectorization, inlining, and DCE).

---

## Bridge Communication: HP-HL <-> JavaScript

HP-HL imports host browser functions using `extern "env"`:

```hphl
module web.app;

// Import utility functions provided by the host JavaScript runtime
extern "env" {
    void js_set_inner_text(int elementId, string text);
    void js_set_element_style(int elementId, string prop, string val);
    void js_canvas_draw_rect(int x, int y, int w, int h, int hexColor);
}

// Exported function called by host JavaScript on each animation frame
void UpdateSimulation(float deltaTime) {
    // Heavy physics, collision, and matrix updates
    js_canvas_draw_rect(100, 50, 200, 150, 16711680); // Red
    js_set_inner_text(1, "Simulation running at 60 FPS!");
}
```

---

## HTML5 & CSS3 Host Integration

A minimal `index.html` loads and drives the WebAssembly module:

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>HP-HL WebAssembly Demo</title>
  <style>
    body {
      margin: 0;
      background: #0f172a;
      color: #f8fafc;
      font-family: system-ui, sans-serif;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 2rem;
    }
    #game-canvas {
      border: 2px solid #38bdf8;
      border-radius: 8px;
      box-shadow: 0 10px 25px rgba(0,0,0,0.5);
    }
    .hud {
      margin-top: 1rem;
      padding: 0.75rem 1.5rem;
      background: #1e293b;
      border-radius: 6px;
      font-weight: bold;
    }
  </style>
</head>
<body>
  <h1>HP-HL WebAssembly Demo</h1>
  <canvas id="game-canvas" width="800" height="600"></canvas>
  <div id="status" class="hud">Loading WASM...</div>

  <script>
    const importObject = {
      env: {
        js_set_inner_text: (id, ptr) => {
          document.getElementById('status').innerText = "WASM Connected!";
        },
        js_canvas_draw_rect: (x, y, w, h, color) => {
          const ctx = document.getElementById('game-canvas').getContext('2d');
          ctx.fillStyle = `#${color.toString(16).padStart(6, '0')}`;
          ctx.fillRect(x, y, w, h);
        }
      }
    };

    WebAssembly.instantiateStreaming(fetch('app.wasm'), importObject).then(results => {
      console.log("HP-HL WASM Loaded Successfully!");
      results.instance.exports.UpdateSimulation(0.016);
    });
  </script>
</body>
</html>
```

---

## Key Advantages on the Web

1. **Full-Stack Code Reuse:** The same vector structs, physics engines, and game logic compile for desktop binaries and WebAssembly without code duplication.
2. **Predictable Low-Latency Execution:** Eliminates JavaScript V8 garbage collector frame drops during intensive animation and physics cycles.
3. **Compact Binaries:** Dead-code elimination strips unused runtime symbols, producing lightweight `.wasm` files for rapid client downloads.
