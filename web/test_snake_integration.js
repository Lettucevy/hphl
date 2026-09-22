// test_snake_integration.js — valida o fluxo igual ao da página:
// compila snake.wasm com os imports do game.js e joga com steering greedy
// atrás da comida (encontra a comida varrendo SnakeAt).
const fs = require("fs");
(async () => {
  const bytes = fs.readFileSync("web/snake.wasm");
  const mod = await WebAssembly.compile(bytes);

  let E = null;
  let heapCur = 0;
  function alloc(size) {
    size = Math.max(1, size);
    const p = heapCur;
    heapCur += (size + 15) & ~15;
    while (E.memory.buffer.byteLength < heapCur) E.memory.grow(1);
    return p;
  }
  const imports = { env: {
    malloc: (sz) => BigInt(alloc(Number(sz))),
    calloc: (n, sz) => BigInt(alloc(Number(n) * Number(sz))),
    free: () => {},
    hphl_print_int: () => {}, hphl_print_uint: () => {},
    hphl_print_float: () => {}, hphl_print_bool: () => {},
    hphl_print_char: () => {}, hphl_print_string: () => {},
    hphl_panic: () => { throw new Error("panic"); },
    hphl_bounds_check: (i) => { if (i < 0n) throw new Error("bounds"); },
    hphl_overflow_check: () => {},
  }};

  const inst = await WebAssembly.instantiate(mod, imports);
  E = inst.exports;
  heapCur = E.memory.buffer.byteLength;

  const W = Number(E.SnakeW());
  const H = Number(E.SnakeH());

  function findCell(v) {
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++)
        if (Number(E.SnakeAt(BigInt(x), BigInt(y))) === v) return [x, y];
    return null;
  }

  function headPos() { return findCell(2); }
  function foodPos() { return findCell(3); }

  // jogo completo: persegue a comida; desvio simples quando o próximo
  // passo livre ameaçar parede/corpo
  E.SnakeInit();
  let lastDir = [1, 0];
  let deaths = 0;
  let maxScore = 0;
  for (let i = 0; i < 3000 && Number(E.SnakeAlive()) === 1; i++) {
    const head = headPos();
    const food = foodPos();
    if (!head || !food) throw new Error("estado sem cabeça ou comida");

    // direção greedy em ordem: eixo maior primeiro, depois alternativa
    const cands = [];
    if (Math.abs(food[0] - head[0]) >= Math.abs(food[1] - head[1])) {
      cands.push([Math.sign(food[0] - head[0]), 0]);
      cands.push([0, Math.sign(food[1] - head[1])]);
    } else {
      cands.push([0, Math.sign(food[1] - head[1])]);
      cands.push([Math.sign(food[0] - head[0]), 0]);
    }
    cands.push([-lastDir[0], -lastDir[1]]); // nunca: reversão

    for (const d of cands) {
      if (d[0] === 0 && d[1] === 0) continue;
      if (d[0] === -lastDir[0] && d[1] === -lastDir[1]) continue;
      const nx = head[0] + d[0], ny = head[1] + d[1];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
      const cell = Number(E.SnakeAt(BigInt(nx), BigInt(ny)));
      if (cell === 1 || cell === 2) continue;
      E.SnakeSetDir(BigInt(d[0]), BigInt(d[1]));
      lastDir = d;
      break;
    }

    if (Number(E.SnakeStep()) === 1) deaths++;
    maxScore = Math.max(maxScore, Number(E.SnakeScore()));
  }

  console.log("deaths:", deaths, "| max score:", maxScore,
              "| len:", Number(E.SnakeLen()));
  if (maxScore <= 0)
    throw new Error("a cobra não comeu nenhuma comida em 3000 passos");

  // reinício limpo
  E.SnakeInit();
  if (Number(E.SnakeScore()) !== 0 || Number(E.SnakeAlive()) !== 1)
    throw new Error("reset não limpou o estado");
  console.log("INTEGRACAO OK");
})().catch(e => { console.error("ERRO:", e.message); process.exit(1); });
