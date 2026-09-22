// test_browser_boot.mjs — smoke test headless do boot browser (v0.95).
// Importa web/game.js (módulo ES) com DOM/canvas stubados, executa boot()
// com fetch baseado em fs e valida que o jogo roda: exports presentes,
// SnakeInit/Step/Score/Len, HUD atualizado e frames desenhados.
//
// Uso: node web/test_browser_boot.mjs   (cwd = raiz do projeto)
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dir = path.dirname(fileURLToPath(import.meta.url));
const wasmPath = path.join(__dir, "snake.wasm");

// ---- stubs de DOM -------------------------------------------------------
function makeEl() {
  return {
    textContent: "",
    _handlers: {},
    classList: { add() {}, remove() {} },
    addEventListener(ev, fn) { this._handlers[ev] = fn; },
  };
}
const els = {
  board: null, score: makeEl(), len: makeEl(), speed: makeEl(),
  overlay: makeEl(), final: makeEl(), restart: makeEl(),
};
// canvas: getContext retorna Proxy que absorve qualquer chamada de desenho
const ctxStub = new Proxy({}, {
  get: (t, p) => (p === "canvas" ? {} : () => {}),
  set: () => true,
});
els.board = { ...makeEl(), width: 720, height: 480, getContext: () => ctxStub };

const keyHandlers = [];
globalThis.window = { addEventListener: (ev, fn) => keyHandlers.push([ev, fn]) };
globalThis.document = {
  getElementById: (id) => els[id] || makeEl(),
  body: {},
};
globalThis.performance = { now: () => simNow };
let simNow = 1000;
const rafQueue = [];
globalThis.requestAnimationFrame = (fn) => { rafQueue.push(fn); return rafQueue.length; };

// ---- importa o módulo do jogo (NÃO faz auto-boot: sem document de verdade
// na primeira importação... document existe (stub) então o auto-boot dispara
// com fetch relativo e falha silenciosamente no catch do módulo; o boot real
// é feito abaixo com fetch de fs) ------------------------------------------
const game = await import("./game.js");

// ---- boot com fetch de fs -------------------------------------------------
const fetchStub = () => Promise.resolve({
  arrayBuffer: () => Promise.resolve(fs.readFileSync(wasmPath)),
});
const E = await game.boot(fetchStub);

function assert(cond, msg) {
  if (!cond) { console.error("FALHOU:", msg); process.exitCode = 1; throw new Error(msg); }
}

// 1. exports presentes
for (const fn of ["SnakeInit", "SnakeStep", "SnakeScore", "SnakeLen",
                  "SnakeW", "SnakeH", "SnakeAt", "SnakeSetDir", "memory"]) {
  assert(E[fn] !== undefined, `export ausente: ${fn}`);
}
console.log("exports OK");

// 2. imports do módulo cobertos pelo shim do game.js
const mod = await WebAssembly.compile(fs.readFileSync(wasmPath));
const needed = WebAssembly.Module.imports(mod)
  .filter((i) => i.kind === "function").map((i) => i.name);
const { makeImports } = game;
const probeInst = await WebAssembly.instantiate(mod, makeImports());
// instanciação com o shim prova cobertura total (faltante = TypeError)
assert(probeInst !== undefined, "instanciação com shim falhou");
console.log(`imports OK (${needed.length} funções env.* cobertas)`);

// 3. jogo roda: reset + passos avançam estado e HUD
E.SnakeInit();
assert(Number(E.SnakeLen()) === 4, "len inicial = 4");
assert(Number(E.SnakeScore()) === 0, "score inicial = 0");
assert(Number(E.SnakeW()) === 24 && Number(E.SnakeH()) === 16, "grade 24x16");

// 4. frames: executa callbacks de rAF com tempo avançando
for (let f = 0; f < 5; f++) {
  simNow += 200;
  const q = rafQueue.splice(0);
  for (const fn of q) fn(simNow);
}
assert(els.score.textContent !== "", "HUD score atualizado");
assert(els.len.textContent !== "", "HUD len atualizado");
console.log(`frames OK (score=${els.score.textContent} len=${els.len.textContent})`);

// 5. input: despacha seta + R nos handlers capturados
const keydown = keyHandlers.find(([ev]) => ev === "keydown")?.[1];
assert(typeof keydown === "function", "handler keydown registrado");
keydown({ code: "ArrowUp", preventDefault() {} });
keydown({ code: "KeyR", preventDefault() {} }); // reset via teclado
assert(Number(E.SnakeScore()) === 0, "reset via R zera score");
console.log("input OK");

console.log("BROWSER BOOT OK");
