# WebAssembly (WASM) & Aplicações Web

O HP-HL oferece suporte de primeira classe à compilação com alvo em **WebAssembly (WASM)** através do backend LLVM e do vinculador `wasm-ld`.

Isso permite executar simulações físicas pesadas, pipelines gráficos WebGL/WebGPU e algoritmos de processamento de áudio diretamente no navegador com velocidade próxima à nativa.

---

## Compilando para WebAssembly

```bash
hphlc app.hphl -o app.wasm --backend llvm --target wasm32-unknown-unknown -O3
```

---

## Comunicação Bidirecional: HP-HL <-> JavaScript

O HP-HL importa funções utilitárias do ambiente host JavaScript com `extern "env"`:

```hphl
module web.app;

extern "env" {
    void js_set_status(string text);
    void js_draw_rect(int x, int y, int w, int h, int color);
}

void UpdateSimulation(float deltaTime) {
    js_draw_rect(100, 50, 200, 150, 16711680);
    js_set_status("Simulação executando a 60 FPS!");
}
```
