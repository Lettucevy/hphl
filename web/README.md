# HPHL Snake — demo do backend WebAssembly (M10.5)

Mini-jogo Snake cuja **lógica inteira roda em HP-HL compilado para
WebAssembly** (`snake.hphl` → `hphlc --backend llvm --target
wasm32-unknown-unknown` → `snake.wasm`). A página HTML/CSS/JS só desenha o
estado e captura o teclado.

## Arquivos

| Arquivo | Papel |
|---|---|
| `snake.hphl` | fonte HP-HL: grade, cobra, comida, colisões, placar, RNG xorshift |
| `snake.wasm` | artefato compilado (wasm32, funções `Snake*` exportadas) |
| `index.html` / `style.css` / `game.js` | shell visual (canvas + HUD); `game.js` é módulo ES (`<script type="module">`, boot exportado) |
| `test_snake_logic.js` | smoke test da lógica (parede, reversão, estado) |
| `test_snake_integration.js` | jogo completo com steering greedy (come/pontua/morre/reinicia) |
| `test_browser_boot.mjs` | boot browser headless: importa `game.js` com DOM stubado, valida exports/imports/HUD/input |

## Como rodar

O módulo é carregado via `fetch`, então precisa de um servidor HTTP local:

```
cd web
python -m http.server 8080
```

Abra `http://localhost:8080`. **Setas/WASD** movem, **R** reinicia; a
velocidade aumenta a cada 50 pontos.

## Reconstruir o .wasm

Na raiz do projeto:

```
hphlc web\snake.hphl -o web\snake --backend llvm --target wasm32-unknown-unknown -O2
```

## Testes headless (Node)

```
node web\test_snake_logic.js        # lógica pura
node web\test_snake_integration.js  # jogo completo com IA greedy
node web\test_browser_boot.mjs      # boot do módulo ES + HUD + input (DOM stubado)
```

## Notas de build

- O backend LLVM exporta funções com nome liso (`SnakeInit`, não
  `SnakeInit.0`): o sufixo de aridade é removido do `wasm-export-name` quando
  não há overloads ambíguos (`compiler/src/llvmapi.cpp`). Se dois overloads
  colidirem, o nome mangled é mantido.
- `snake.hphl` é lógica inteira pura (sem `print`/`malloc`), por isso o
  `snake.wasm` não declara imports: o shim `env.*` em `game.js` só é usado
  se o programa importar runtime.
