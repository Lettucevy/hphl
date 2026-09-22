// runner_wasm.js — executa um módulo .wasm gerado pelo hphlc (M10.5)
//
// Uso: node tests/runner_wasm.js <modulo.wasm> [Funcao args...]
//
// Fornece um shim do runtime HPHL sobre o host WebAssembly:
//   - impressão (hphl_print_*) com os mesmos formatos do runtime.c
//   - alocador bump (malloc/calloc/realloc/free) via memory.grow — as páginas
//     novas do linear memory já nascem zeradas (comportamento de calloc)
//   - list/map/tuple/struct/string helpers espelhando runtime.c
// A memória linear é definida e exportada pelo próprio módulo (wasm-ld).
// Qualquer função env.* não implementada lança erro explícito na 1ª chamada.
"use strict";
const fs = require("fs");

(async () => {
  const [, , wasmPath, fn, ...callArgs] = process.argv;
  const mod = await WebAssembly.compile(fs.readFileSync(wasmPath));
  const out = [];

  // --- estado do heap bump ---
  let inst = null;
  let heapCur = 0; // próx. endereço livre
  const allocSizes = new Map(); // ptr -> bytes (p/ realloc)
  let tlsBlock = 0;
  const semaphores = new Map();
  const events = new Map();
  const barriers = new Map();
  const channels = new Map();

  const memory = () => inst.exports.memory || inst.exports.__linear_memory;
  const wasmTable = () =>
    inst.exports.__indirect_function_table ||
    fatal("módulo não exporta __indirect_function_table (use --export-table)");

  function growTo(need) {
    const mem = memory();
    while (mem.buffer.byteLength < need) mem.grow(1);
  }

  function malloc(size) {
    size = Number(size);
    if (size <= 0) size = 1;
    const aligned = (size + 15) & ~15;
    const ptr = heapCur;
    heapCur += aligned;
    growTo(heapCur);
    return ptr;
  }

  // --- helpers de string/memória ---
  function readStr(ptr) {
    const u8 = new Uint8Array(memory().buffer, Number(ptr));
    let end = 0;
    while (u8[end] !== 0) end++;
    return new TextDecoder("utf-8").decode(u8.subarray(0, end));
  }

  function writeStr(s) {
    const bytes = Buffer.from(s + "\0", "utf-8");
    const ptr = malloc(bytes.length);
    new Uint8Array(memory().buffer, ptr, bytes.length).set(bytes);
    return ptr;
  }

  function memcpy(dst, src, n) {
    if (n > 0)
      new Uint8Array(memory().buffer, dst, n).set(
        new Uint8Array(memory().buffer, src, n));
  }

  function fatal(msg) {
    process.stdout.write(out.join(""));
    process.stderr.write(`\nhphl(wasm): ${msg}\n`);
    process.exit(1);
  }

  // --- layouts (wasm32: i64=8B, ptr=4B) ---
  const LIST = { COUNT: 0, CAP: 8, ITEMS: 16 };
  const MAP = { COUNT: 0, CAP: 8, KEYS: 16, VALS: 20, OCC: 24, KIND: 28 };

  const dv = () => new DataView(memory().buffer);

  // espelha hphl_map_hash do runtime.c: djb2 p/ strings, k^(k>>33) p/ resto
  function mapHash(p, k) {
    const d = dv();
    const kind = d.getInt32(Number(p) + MAP.KIND, true);
    if (kind === 1) { // string: djb2 do conteúdo apontado pela chave
      const sPtr = Number(BigInt.asIntN(32, k));
      if (!sPtr) return 0n;
      const u8 = new Uint8Array(memory().buffer);
      let h = 5381n;
      for (let i = sPtr; u8[i] !== 0; i++)
        h = ((h << 5n) + h + BigInt(u8[i])) & 0xFFFFFFFFFFFFFFFFn;
      return h;
    }
    const u = BigInt.asUintN(64, k);
    return (u ^ (u >> 33n)) & 0xFFFFFFFFFFFFFFFFn;
  }

  // espelha hphl_map_eq: strings comparam conteúdo; resto, valor
  function mapKeyEq(p, a, b) {
    const kind = dv().getInt32(Number(p) + MAP.KIND, true);
    if (kind !== 1) return a === b;
    if (a === b) return true;
    return readStr(a) === readStr(b);
  }

  function mapFind(p, k) {
    const d = dv(); p = Number(p);
    const cap = Number(d.getBigInt64(p + MAP.CAP, true));
    const keys = d.getInt32(p + MAP.KEYS, true);
    const occBase = d.getInt32(p + MAP.OCC, true);
    const h = mapHash(p, k);
    for (let j = 0; j < cap; j++) {
      const slot = Number((h + BigInt(j)) % BigInt(cap));
      const occ = d.getUint8(occBase + slot);
      if (occ === 0) return -1;
      if (occ === 1 &&
          mapKeyEq(p, d.getBigInt64(keys + slot * 8, true), k))
        return slot;
    }
    return -1;
  }

  const env = {
    // --- alocação (tamanhos chegam como i64/BigInt; retornos ptr = Number) ---
    malloc: (sz) => malloc(sz),
    calloc: (n, sz) => malloc(Number(n) * Number(sz)),
    free: (_p) => {},

    // --- impressão (mesmos formatos do runtime.c) ---
    hphl_print_int: (v) => out.push(v.toString()),
    hphl_print_uint: (v) => out.push(v.toString()),
    hphl_print_float: (v) => out.push(fmtG(v)),
    hphl_print_bool: (b) => out.push(b ? "true" : "false"),
    hphl_print_char: (c) => out.push(String.fromCharCode(Number(c))),
    hphl_print_string: (ptr) => out.push(readStr(ptr)),

    // --- strings (retornam ptr = Number) ---
    hphl_str_concat: (a, b) => writeStr(readStr(a) + readStr(b)),
    hphl_str_eq: (a, b) => BigInt(readStr(a) === readStr(b) ? 1 : 0),
    hphl_str_cmp: (a, b) => {
      const x = readStr(a), y = readStr(b);
      return BigInt(x < y ? -1 : x > y ? 1 : 0);
    },
    hphl_str_from_int: (v) => writeStr(v.toString()),
    hphl_str_from_uint: (v) => writeStr(v.toString()),
    hphl_str_from_float: (v) => writeStr(fmtG(v)),
    hphl_str_from_bool: (v) => writeStr(v ? "true" : "false"),
    hphl_str_from_char: (v) => writeStr(String.fromCharCode(Number(v))),
    hphl_str_free: (_p) => {},

    // --- list<T> (ptr in/out = Number; count/idx = BigInt) ---
    hphl_list_new: () => {
      const p = malloc(20); // count+cap (16B) + items ptr (4B)
      const d = dv();
      d.setBigInt64(p + LIST.COUNT, 0n, true);
      d.setBigInt64(p + LIST.CAP, 0n, true);
      d.setInt32(p + LIST.ITEMS, 0, true);
      return p;
    },
    hphl_list_add_i: (p, v) => listPush(Number(p), v),
    hphl_list_add_f: (p, v) => listPushF(Number(p), v),
    hphl_list_len: (p) => dv().getBigInt64(Number(p) + LIST.COUNT, true),
    hphl_list_data: (p) => dv().getInt32(Number(p) + LIST.ITEMS, true),
    hphl_list_check: (idx, p) => {
      const d = dv();
      const n = d.getBigInt64(Number(p) + LIST.COUNT, true);
      if (idx < 0n || idx >= n)
        fatal(`bounds: índice ${idx} fora do tamanho ${n}`);
    },
    hphl_list_slice: (p, from, count, elemSize) => {
      // espelha runtime.c: retorna NOVO struct HphlList (não o dado cru)
      const items = dv().getInt32(Number(p) + LIST.ITEMS, true);
      const n = Number(count), es = Number(elemSize);
      const np = malloc(20);
      const d2 = dv();
      let data = 0;
      if (n > 0) {
        data = malloc(n * es);
        memcpy(data, items + Number(from) * es, n * es);
      }
      d2.setBigInt64(np + LIST.COUNT, BigInt(n), true);
      d2.setBigInt64(np + LIST.CAP, BigInt(n), true);
      d2.setInt32(np + LIST.ITEMS, data, true);
      return np;
    },
    hphl_list_free: (_p) => {},

    // --- map<K,V> (retorna ptr = Number; chaves/valores i64 = BigInt) ---
    // NOTA: KEYS/VALS/OCC são campos PONTEIRO para arrays separados
    hphl_map_new: (keyKind) => {
      const cap = 16;
      const p = malloc(32); // count/cap (16B) + 3 ptrs + keyKind (16B)
      const d = dv();
      d.setBigInt64(p + MAP.COUNT, 0n, true);
      d.setBigInt64(p + MAP.CAP, BigInt(cap), true);
      const keys = malloc(cap * 8), vals = malloc(cap * 8), occ = malloc(cap);
      d.setInt32(p + MAP.KEYS, keys, true);
      d.setInt32(p + MAP.VALS, vals, true);
      d.setInt32(p + MAP.OCC, occ, true);
      new Uint8Array(memory().buffer, occ, cap).fill(0);
      d.setInt32(p + MAP.KIND, Number(keyKind), true);
      return p;
    },
    hphl_map_put: (p, k, v) => {
      p = Number(p);
      const found = mapFind(p, k);
      const d = dv();
      const slot = found >= 0 ? found : mapFirstFree(p, k);
      const keys = d.getInt32(p + MAP.KEYS, true);
      const vals = d.getInt32(p + MAP.VALS, true);
      const occ = d.getInt32(p + MAP.OCC, true);
      d.setUint8(occ + slot, 1);
      d.setBigInt64(keys + slot * 8, k, true);
      d.setBigInt64(vals + slot * 8, v, true);
      d.setBigInt64(p + MAP.COUNT,
        d.getBigInt64(p + MAP.COUNT, true) + (found >= 0 ? 0n : 1n), true);
    },
    hphl_map_get: (p, k) => {
      p = Number(p);
      const slot = mapFind(p, k);
      if (slot < 0) fatal("map: chave não encontrada");
      const vals = dv().getInt32(p + MAP.VALS, true);
      return dv().getBigInt64(vals + slot * 8, true);
    },
    hphl_map_contains: (p, k) =>
      BigInt(mapFind(Number(p), k) >= 0 ? 1 : 0),
    hphl_map_remove: (p, k) => {
      p = Number(p);
      const slot = mapFind(p, k);
      if (slot < 0) return 0n;
      const d = dv();
      const occ = d.getInt32(p + MAP.OCC, true);
      d.setUint8(occ + slot, 2); // tombstone
      d.setBigInt64(p + MAP.COUNT, d.getBigInt64(p + MAP.COUNT, true) - 1n,
                    true);
      return 1n;
    },
    hphl_map_clear: (p) => {
      const d = dv(); p = Number(p);
      const cap = Number(d.getBigInt64(p + MAP.CAP, true));
      const occ = d.getInt32(p + MAP.OCC, true);
      new Uint8Array(memory().buffer, occ, cap).fill(0);
      d.setBigInt64(p + MAP.COUNT, 0n, true);
    },
    hphl_map_len: (p) => dv().getBigInt64(Number(p) + MAP.COUNT, true),
    hphl_map_free: (_p) => {},

    // --- tuple / struct (retornam ptr = Number) ---
    hphl_tuple_new: (n) => malloc(Number(n) * 8),
    hphl_tuple_clone: (src, n) => {
      if (!src) return 0;
      const p = malloc(Number(n) * 8);
      memcpy(p, Number(src), Number(n) * 8);
      return p;
    },
    hphl_struct_copy: (src, n) => {
      const p = malloc(Number(n));
      memcpy(p, Number(src), Number(n));
      return p;
    },

    // --- erros ---
    hphl_match_fail: () => fatal("match não correspondeu a nenhum braço"),
    hphl_panic: (msg) => fatal(`panic: ${readStr(msg)}`),
    hphl_bounds_check: (idx, size) => {
      if (idx < 0n || idx >= size)
        fatal(`bounds: índice ${idx} fora do tamanho ${size}`);
    },
    hphl_overflow_check: (v, min, max) => {
      if (v < min || v > max)
        fatal(`overflow: ${v} fora da faixa [${min}, ${max}]`);
    },
    hphl_assert: (cond, msg) => {
      if (!cond) fatal(`assert falhou: ${readStr(msg)}`);
    },

    // --- tempo ---
    hphl_clock_ns: () => BigInt(Date.now()) * 1000000n,

    // --- sincronização (single-thread: no-ops determinísticos; o spawn
    //     executa a task EAGERLY no ponto do spawn, então locks nunca
    //     disputam e canais podem ser filas ilimitadas) ---
    hphl_lock_begin: (_p) => {},
    hphl_lock_end: (_p) => {},
    hphl_mutex_new: () => malloc(8),
    hphl_mutex_lock: (_p) => {},
    hphl_mutex_unlock: (_p) => {},
    hphl_semaphore_new: (n) => {
      const p = malloc(8);
      semaphores.set(p, Number(n));
      return p;
    },
    hphl_semaphore_wait: (p) => {
      const s = semaphores.get(Number(p)) || 0;
      if (s <= 0) fatal("semaphore: sem permissão");
      semaphores.set(Number(p), s - 1);
    },
    hphl_semaphore_signal: (p) =>
      semaphores.set(Number(p), (semaphores.get(Number(p)) || 0) + 1),
    hphl_event_new: () => {
      const p = malloc(8);
      events.set(p, false);
      return p;
    },
    hphl_event_wait: (p) => {
      if (!events.get(Number(p))) fatal("event: não sinalizado");
    },
    hphl_event_set: (p) => events.set(Number(p), true),
    hphl_event_reset: (p) => events.set(Number(p), false),
    hphl_barrier_new: (_n) => malloc(8),
    hphl_barrier_wait: (_p) => {},
    hphl_region_begin: () => {},
    hphl_region_join: () => {},
    hphl_worker_count: () => 1n,

    // --- TLS (bloco único: tasks rodam eager/sequencial) ---
    hphl_tls_setup: (sz) => { tlsBlock = malloc(Number(sz)); },
    hphl_tls_block: () => tlsBlock,

    // --- canais: fila ilimitada (send/receive não bloqueiam de verdade
    //     porque as tasks já rodaram eageramente) ---
    hphl_channel_new: (_cap) => {
      const p = malloc(8);
      channels.set(p, []);
      return p;
    },
    hphl_channel_send: (p, v) => {
      const q = channels.get(Number(p));
      if (!q) fatal("channel: canal inválido");
      q.push(v);
    },
    hphl_channel_receive: (p) => {
      const q = channels.get(Number(p));
      if (!q || q.length === 0) fatal("channel: receive com fila vazia");
      return q.shift();
    },

    // --- tasks cooperativas: spawn executa a task NA HORA (work stealing
    //     levado ao extremo — determinístico por construção). A fn chega
    //     como índice na tabela exportada (__indirect_function_table).
    hphl_spawn_task_ex: (fnIdx, env) => {
      const t = malloc(32); // res(8) + done/cancel flags + paddings
      dv().setBigInt64(t, 0n, true);
      const fn = wasmTable().get(Number(fnIdx));
      if (!fn) fatal("spawn: entrada inválida na tabela de funções");
      const res = malloc(8);
      try {
        fn(Number(env), res);
      } catch (e) {
        fatal(`trap dentro de task: ${e.message}`);
      }
      dv().setBigInt64(t, dv().getBigInt64(res, true), true);
      return t;
    },
    hphl_wait_task: (t) => dv().getBigInt64(Number(t), true),
    hphl_join_tasks: () => {},
    hphl_cancel_task: (_t) => {},
    hphl_task_iscancelled: () => 0n,

    __linear_memory: undefined, // presente só se o módulo IMPORTAR memória
  };

  function mapFirstFree(p, k) {
    const d = dv(); p = Number(p);
    const cap = Number(d.getBigInt64(p + MAP.CAP, true));
    const occBase = d.getInt32(p + MAP.OCC, true);
    const h = mapHash(p, k);
    let tomb = -1;
    for (let j = 0; j < cap; j++) {
      const slot = Number((h + BigInt(j)) % BigInt(cap));
      const occ = d.getUint8(occBase + slot);
      if (occ === 0) return tomb >= 0 ? tomb : slot;
      if (occ === 2 && tomb < 0) tomb = slot;
    }
    fatal("map cheio");
    return -1;
  }

  function listPush(p, v) {
    const d = dv();
    let count = Number(d.getBigInt64(p + LIST.COUNT, true));
    let cap = Number(d.getBigInt64(p + LIST.CAP, true));
    let items = d.getInt32(p + LIST.ITEMS, true);
    if (count === cap) {
      const nc = cap ? cap * 2 : 8;
      const ni = malloc(nc * 8);
      if (items) memcpy(ni, items, count * 8);
      items = ni;
      d.setInt32(p + LIST.ITEMS, items, true);
      d.setBigInt64(p + LIST.CAP, BigInt(nc), true);
      cap = nc;
    }
    d.setBigInt64(items + count * 8, BigInt.asIntN(64, v), true);
    d.setBigInt64(p + LIST.COUNT, BigInt(count + 1), true);
  }

  function listPushF(p, v) {
    const d = dv();
    let count = Number(d.getBigInt64(p + LIST.COUNT, true));
    let cap = Number(d.getBigInt64(p + LIST.CAP, true));
    let items = d.getInt32(p + LIST.ITEMS, true);
    if (count === cap) {
      const nc = cap ? cap * 2 : 8;
      const ni = malloc(nc * 8);
      if (items) memcpy(ni, items, count * 8);
      items = ni;
      d.setInt32(p + LIST.ITEMS, items, true);
      d.setBigInt64(p + LIST.CAP, BigInt(nc), true);
    }
    d.setFloat64(items + count * 8, v, true);
    d.setBigInt64(p + LIST.COUNT, BigInt(count + 1), true);
  }

  // %g do C: 6 dígitos significativos, sem zeros à direita
  function fmtG(v) {
    if (!isFinite(v)) return String(v);
    return String(Number(v.toPrecision(6)));
  }

  // imports desconhecidos falham explicitamente ao ser chamados
  const known = new Set(Object.keys(env).filter((k) => env[k] !== undefined));
  const provided = {};
  for (const imp of WebAssembly.Module.imports(mod)) {
    if (imp.kind !== "function") continue; // memória global tratada abaixo
    const key = imp.name;
    if (known.has(key)) {
      provided[key] = env[key];
    } else {
      provided[key] = (...args) =>
        fatal(`import não implementado no runner: env.${key}(${args.join(",")})`);
    }
  }
  const imports = { env: provided };

  inst = await WebAssembly.instantiate(mod, imports);
  heapCur = memory().buffer.byteLength; // heap após dados/stack iniciais

  // Auto-discover entry point if none specified on command line
  const targetFn = fn || (inst.exports.main ? "main" : (inst.exports.Main ? "Main" : null));
  if (targetFn && inst.exports[targetFn]) {
    const args = callArgs.map((a) => (/^-?\d+$/.test(a) ? BigInt(a) : a));
    try {
      const r = inst.exports[targetFn](...args);
      console.error(`=> ${targetFn}() = ${r}`);
    } catch (e) {
      console.error(`trap em ${targetFn}: ${e.message}`);
      process.exitCode = 1;
    }
  }
  process.stdout.write(out.join(""));
})().catch((e) => {
  console.error(`runner_wasm: ${e.message}`);
  process.exit(1);
});
