// game.js — módulo ES: cola entre o WASM (lógica em HP-HL) e o canvas.
// A regra do jogo inteira vive no snake.wasm; aqui só há input, render e loop.
// Carregado via `<script type="module" src="game.js">` (defer implícito: o DOM
// já existe quando este módulo executa).

const CELL = 30; // px por célula (canvas 24*30 x 16*30)

const canvas = document.getElementById("board");
const ctx = canvas.getContext("2d");
const elScore = document.getElementById("score");
const elLen = document.getElementById("len");
const elSpeed = document.getElementById("speed");
const overlay = document.getElementById("overlay");
const elFinal = document.getElementById("final");
const btnRestart = document.getElementById("restart");

let E = null; // exports do módulo
let dir = { x: 1, y: 0 }; // direção pedida pelo jogador
let dead = false;
let stepMs = 140; // velocidade (acelera com o placar)
let lastStep = 0;

function fatal(msg) {
  console.error("hphl(wasm):", msg);
}

// ---- shim mínimo do runtime HPHL para este programa --------------------
export function makeImports() {
  return { env: {
    malloc: (sz) => BigInt(alloc(Number(sz))),
    calloc: (n, sz) => BigInt(alloc(Number(n) * Number(sz))),
    free: () => {},
    realloc: () => { throw new Error("realloc não usado"); },
    hphl_print_int: (v) => console.log(v.toString()),
    hphl_print_uint: (v) => console.log(v.toString()),
    hphl_print_float: (v) => console.log(v),
    hphl_print_bool: (b) => console.log(b ? "true" : "false"),
    hphl_print_char: () => {},
    hphl_print_string: () => {},
    hphl_panic: () => fatal("panic"),
    hphl_bounds_check: (i) => {
      if (i < 0n) throw new Error("bounds check falhou");
    },
    hphl_overflow_check: () => {},
  }};
}

// alocador bump sobre a memória exportada (só p/ o que o wasm pedir)
let heapCur = 0;
function alloc(size) {
  size = Math.max(1, size);
  const p = heapCur;
  heapCur += (size + 15) & ~15;
  while (E.memory.buffer.byteLength < heapCur) E.memory.grow(1);
  return p;
}

// ---- input --------------------------------------------------------------
const KEYMAP = {
  ArrowUp: [0, -1], KeyW: [0, -1],
  ArrowDown: [0, 1], KeyS: [0, 1],
  ArrowLeft: [-1, 0], KeyA: [-1, 0],
  ArrowRight: [1, 0], KeyD: [1, 0],
};
window.addEventListener("keydown", (ev) => {
  if (ev.code === "KeyR") { reset(); return; }
  const d = KEYMAP[ev.code];
  if (!d) return;
  ev.preventDefault();
  if (dead) return;
  dir = { x: d[0], y: d[1] };
  E.SnakeSetDir(BigInt(d[0]), BigInt(d[1]));
});
btnRestart.addEventListener("click", reset);

// ---- jogo ---------------------------------------------------------------
function reset() {
  E.SnakeInit();
  dir = { x: 1, y: 0 };
  dead = false;
  stepMs = 140;
  overlay.classList.add("hidden");
  lastStep = performance.now();
}

function tick() {
  if (!dead) {
    if (Number(E.SnakeStep()) === 1) {
      dead = true;
      elFinal.textContent = String(E.SnakeScore());
      overlay.classList.remove("hidden");
    }
  }
}

function hud() {
  const score = Number(E.SnakeScore());
  elScore.textContent = String(score);
  elLen.textContent = String(E.SnakeLen());
  const mult = stepMs <= 90 ? "3×" : stepMs <= 115 ? "2×" : "1×";
  elSpeed.textContent = mult;
  // acelera a cada 50 pontos
  stepMs = score >= 100 ? 85 : score >= 50 ? 110 : 140;
}

// ---- render -------------------------------------------------------------
function roundRect(x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}

function draw() {
  const W = Number(E.SnakeW());
  const H = Number(E.SnakeH());

  // fundo
  ctx.fillStyle = "#0a0e13";
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  // grade sutil
  ctx.strokeStyle = "#121a23";
  ctx.lineWidth = 1;
  for (let gx = 1; gx < W; gx++) {
    ctx.beginPath();
    ctx.moveTo(gx * CELL + .5, 0);
    ctx.lineTo(gx * CELL + .5, canvas.height);
    ctx.stroke();
  }
  for (let gy = 1; gy < H; gy++) {
    ctx.beginPath();
    ctx.moveTo(0, gy * CELL + .5);
    ctx.lineTo(canvas.width, gy * CELL + .5);
    ctx.stroke();
  }

  // comida pulsante
  const t = performance.now() / 300;
  const pulse = 3 + Math.sin(t) * 2;

  // células
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const v = Number(E.SnakeAt(BigInt(x), BigInt(y)));
      if (v === 0) continue;
      const px = x * CELL, py = y * CELL;
      if (v === 3) {
        ctx.save();
        ctx.shadowColor = "rgba(255,93,115,.8)";
        ctx.shadowBlur = 14 + pulse * 2;
        ctx.fillStyle = "#ff5d73";
        roundRect(px + 7 - pulse / 2, py + 7 - pulse / 2,
                  CELL - 14 + pulse, CELL - 14 + pulse, 6);
        ctx.fill();
        ctx.restore();
      } else if (v === 2) {
        ctx.save();
        ctx.shadowColor = "rgba(126,242,154,.65)";
        ctx.shadowBlur = 12;
        ctx.fillStyle = "#7ef29a";
        roundRect(px + 2, py + 2, CELL - 4, CELL - 4, 8);
        ctx.fill();
        ctx.restore();
      } else {
        ctx.fillStyle = "#3fae6d";
        roundRect(px + 3.5, py + 3.5, CELL - 7, CELL - 7, 7);
        ctx.fill();
      }
    }
  }

  // borda de perigo quando morto
  if (dead) {
    ctx.strokeStyle = "rgba(255,93,115,.55)";
    ctx.lineWidth = 3;
    ctx.strokeRect(1.5, 1.5, canvas.width - 3, canvas.height - 3);
  }
}

// ---- loop ---------------------------------------------------------------
function frame(now) {
  if (now - lastStep >= stepMs) {
    lastStep = now;
    tick();
    hud();
  }
  draw();
  requestAnimationFrame(frame);
}

export async function boot(fetchImpl = fetch) {
  const bytes = await (await fetchImpl("snake.wasm")).arrayBuffer();
  const mod = await WebAssembly.compile(bytes);
  const inst = await WebAssembly.instantiate(mod, makeImports());
  E = inst.exports;
  heapCur = E.memory.buffer.byteLength;

  reset();
  requestAnimationFrame(frame);
  return E;
}

export function getExports() { return E; }

// auto-boot ao carregar como <script type="module"> no navegador; o parâmetro
// injetável (fetchImpl) e o guarda de ambiente permitem teste headless via
// importação do módulo no Node.
if (typeof document !== "undefined" &&
    typeof requestAnimationFrame !== "undefined") {
  boot().catch((e) => {
    document.body.innerHTML =
      "<pre style='color:#ff5d73;padding:2em'>falha ao carregar snake.wasm: "
      + e.message + "</pre>";
  });
}
