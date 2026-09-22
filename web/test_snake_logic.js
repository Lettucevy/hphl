// test_snake_logic.js — smoke test da lógica do Snake em wasm
const fs = require("fs");
(async () => {
  const mod = await WebAssembly.compile(fs.readFileSync("web/snake.wasm"));
  let mem = null;
  let heapCur = 0;
  function malloc(sz) {
    sz = Number(sz); if (sz <= 0) sz = 1;
    const p = heapCur; heapCur += (sz + 15) & ~15;
    while (mem.buffer.byteLength < heapCur) mem.grow(1);
    return p;
  }
  const inst = await WebAssembly.instantiate(mod, { env: {
    malloc: (s) => BigInt(malloc(s)),
    calloc: (n, s) => BigInt(malloc(Number(n) * Number(s))),
    free: () => {},
    hphl_print_int: () => {}, hphl_print_string: () => {},
    hphl_bounds_check: (i, n) => { if (i < 0n || i >= n) throw new Error("bounds"); },
  }});
  mem = inst.exports.memory; heapCur = mem.buffer.byteLength;
  const E = inst.exports;

  E.SnakeInit();
  console.log("W=" + E.SnakeW(), "H=" + E.SnakeH(), "len=" + E.SnakeLen(),
              "alive=" + E.SnakeAlive());

  // 30 passos para a direita → bate na parede leste
  let diedAt = -1;
  for (let i = 0; i < 30; i++)
    if (E.SnakeStep() == 1) { diedAt = i; break; }
  console.log("parede: morreu no passo", diedAt, "alive=" + E.SnakeAlive());
  if (E.SnakeAlive() != 0n) throw new Error("deveria ter morrido na parede");

  // reinicia e dirige para cima a partir do centro (H/2=8) → morre em ~9 passos
  E.SnakeInit();
  E.SnakeSetDir(0n, -1n);
  diedAt = -1;
  for (let i = 0; i < 30; i++)
    if (E.SnakeStep() == 1) { diedAt = i; break; }
  console.log("parede norte: morreu no passo", diedAt);
  if (diedAt < 5 || diedAt > 12) throw new Error("distância da parede norte inesperada");

  // reversão de 180° deve ser ignorada: indo para direita, SetDir(-1,0) não muda
  E.SnakeInit();
  E.SnakeSetDir(-1n, 0n);
  E.SnakeStep();
  // cabeça andou para +x? verifica via SnakeAt: procura cabeça
  let hx = -1;
  for (let x = 0; x < E.SnakeW(); x++)
    for (let y = 0; y < E.SnakeH(); y++)
      if (E.SnakeAt(BigInt(x), BigInt(y)) == 2) hx = x;
  console.log("apos SetDir reverso+step, cabeca em x=" + hx, "(inicio x=12)");
  if (hx !== 13) throw new Error("reversao de 180 nao foi bloqueada");

  // SnakeAt consistente: exatamente 1 cabeca
  let heads = 0, bodies = 0, foods = 0;
  E.SnakeInit();
  for (let x = 0; x < E.SnakeW(); x++)
    for (let y = 0; y < E.SnakeH(); y++) {
      const v = E.SnakeAt(BigInt(x), BigInt(y));
      if (v == 2) heads++; else if (v == 1) bodies++; else if (v == 3) foods++;
    }
  console.log("cabecas=" + heads, "corpo=" + bodies, "comida=" + foods,
              "len=" + E.SnakeLen());
  if (heads !== 1 || foods !== 1) throw new Error("estado inicial inconsistente");
  if (heads + bodies !== Number(E.SnakeLen())) throw new Error("tamanho da cobra diverge");

  console.log("LOGICA OK");
})().catch(e => { console.error("ERRO:", e.message); process.exit(1); });
