# Changelog

All notable changes to the HP-HL project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/) and the project adheres to [Semantic Versioning 2.0](COMPAT.md).

> **Tip:** the most relevant entries for the v0.93.0 release are at the top. Older entries are kept for history.

---

## [Unreleased] — 2026-09-08

### Added

- **Selfhost Modularization & Parity Milestones**:
  - Compilador selfhost modularizado sob `selfhost/src/` (`ast/ast.hphl`, `frontend/token.hphl`, `frontend/lexer.hphl`, `frontend/parser.hphl`, `semantic/semantic.hphl`, `codegen/codegen.hphl`, `main.hphl`).
  - Coleções dinâmicas no selfhost com 100% de paridade byte-a-byte contra `hphlc.exe`: `list<T>` (`add`, `len`, `cap`, `list[i] = v`, `foreach`) e `map<K, V>` nativo.
  - Genéricos avançados no selfhost com monomorfização: constraints `where T : supports +` (int, float, string concat), interfaces genéricas com vtable (`generic_interface_full`), herança genérica e variância.
  - Testes de bootstrap e E2E consolidados: `tests/scripts/test_bootstrap.ps1` e `tests/scripts/test_selfhost_e2e.ps1` (5/5 PASS).
- **FFI v2 (`ptr`, `addr_of`, `mem_*`)**:
  - Opaque pointer type `ptr` (void*) para interop C/Vulkan; `addr_of(x)` do lvalue; 17 builtins `mem_alloc/free/peek/poke/copy/fill/zero`.
  - `extern "lib"` repassa `-l<lib>` com `-L` de `HPHL_LIBDIR` e `$VULKAN_SDK/Lib` (CLI single-file, `--project` e DAP).
- **`match` com padrões string e char**:
  - Literais `"texto"` (comparação por conteúdo), literais `'c'` e faixas `'a'..'z'`; sujeito string/char exige `'_'` final; backends x64 e LLVM/HIR.
- **`ord(s[, i])` / `chr(c)`**, **`mem_zero(dst, n)`** (zera sem ambiguidade de offset).
- **`foreach (x in xs)` sem tipo** (infere do elemento, como `var`).
- **`tools/fuzz.ps1`**: fuzzer do frontend/backends (mutação do corpus, classifica OK/DIAG/CRASH/HANG; 800 casos limpos na estreia).
- **`pool`/`base` contextuais** como identificadores (política `pool T x` e chamada `base.M()` intactos).
- **IDE: Ctrl+Enter compila `.hpproj`** (procura na pasta/acima); árvore com data binding (`FileNode`); extensões `.lvl`/shaders; pasta via CLI.
- **ProkionEngine FPS jogável** validando o ecossistema (menu/waves/boss, 6 pipelines Vulkan, HUD bitmap, SFX, editor visual standalone, `levels/*.lvl`).

- **Bit-Parametrized Primitive Types (`int<N>`, `float<N>`, `char<N>`, `bool<N>`, `string<N>`)**:
  - Parametrization in bits (`<N>`): `int<1>` up to `int<1024>` (default: `int<32>` / 4 bytes), `float<16>` (half), `float<32>` (single), `float<64>` (double), `float<128>` (quad/fp128) up to `float<1024>`, `char<8>` (UTF-8), `char<16>` (UTF-16), `char<32>` (UTF-32), `bool<1>`, `bool<8>`, and `string<N>`.
  - Full end-to-end support in lexer, parser, type checker/coercion, LLVM IR emission, and LSP/IDE autocompletion.
  - Clarified that `uint` is not an HPHL keyword; unsigned types use `u8`, `u16`, `u32`, `u64`.
- **Dynamic JSON Localization Engine (i18n)**:
  - Replaced hardcoded string resources with dynamic JSON localization (`LocalizationManager.cs`).
  - Community translations: loads from `locales/`, `ide/locales/`, and user directory `~/.hphl/locales/` without recompilation.
  - Complete translations for English (`en`), Portuguese (`ptbr`), and Spanish (`es`) with 374 keys each.
  - Full localization across all IDE dialogs: Preferences, New Project, Package Manager, Global Search, Command Palette, Rename Symbol, and Memory Inspector.
- **Package Manager CLI & GUI**:
  - Package manager CLI commands `hphl fetch`, `hphl install`, `hphl build` with lockfile generation (`package.lock`).
  - Dedicated visual Package Manager dialog with search, install, and dependency inspection.
- **Side-by-Side Disassembly Viewer (`Ctrl+K, Ctrl+D`)**:
  - Automatically compiles HPHL code to native x64 assembly (or LLVM IR) and displays it in a side-by-side split editor.
  - Bidirectional line synchronization between HPHL source code and emitted assembly instructions using `# [line N]` metadata.
- **Code Actions & Quick Fixes (`Alt+Enter` / `Ctrl+.`)**:
  - Contextual smart suggestions menu for syntax and semantic diagnostics.
  - Automatically imports missing module dependencies (`import X;`).
  - Suggests explicit type casts for bit-width discrepancies (e.g. `(int<8>)`, `(float<32>)`).
  - Removes unused local variables and parameters.
- **Find All References (`Shift+F12`)**:
  - Integrated bottom panel tab (`TabItemReferences`) displaying all symbol definitions and usages across the workspace.
  - Powered by LSP `textDocument/references` with instant caret jump upon item selection.
- **Visual Profiling & Performance Metrics**:
  - Bottom panel profiling dashboard (`TabItemProfiler`) with metric cards: Total Execution Time, Young Gen GC, Old Gen GC, GC Pauses, Heap Memory / Peak, Allocation Rate.
  - Comprehensive per-function CPU load table with CSV export capabilities.
- **Code Minimap in Editor**:
  - High-performance custom canvas minimap (`MinimapControl.cs`) on the right side of each editor tab.
  - Displays code outline, breakpoint markers, compilation diagnostics, debugger stepping line, and an interactive draggable viewport slider.
  - Toggleable via `Exibir -> Minimapa` menu and saved across sessions in `ide_config.json`.
- **LSP Inlay Hints**:
  - Visual inline badges rendered within AvaloniaEdit displaying resolved types (`: int<32>`) and parameter names without modifying document character positions.
  - Automatically fetched and updated via LSP `textDocument/inlayHint`.
- **Real-Time Signature Help (`Ctrl+Shift+Space` / `(`, `,`)**:
  - Interactive parameter assistant popup showing function signatures and documentation.
  - Dynamically highlights active parameter in yellow as arguments are typed.
- **Profiler Flamegraph & Heap Memory Timeline**:
  - Hierarchical execution visualizer with proportional CPU load blocks and interactive tooltips.
  - Memory timeline chart tracking heap size over time with Young Gen and Old Gen GC event markers and pause durations.
- **Integrated Hex Viewer & Byte Decoder (`TabItemMemory`)**:
  - Bottom panel tab for inspecting arbitrary memory blocks, variable memory layouts, and GC heap dumps in 3 columns (Offset, Hex Bytes, ASCII).
  - Live multi-type byte inspector decoding selected offsets into Int8, Int16, Int32, Int64, Float32, Float64, and 8-bit binary.

### Fixed

- **Divisão inteira estilo C em todos os divisores**: peephole M13.3 (`sar`/`and` p/ potência de 2) fazia floor enquanto `idiv`/`sdiv` truncavam (x64 discordava do LLVM); bias branchless restaura truncamento com a otimização mantida.
- **`linkObjects` (build `--project`)** repassa `-lwsock32 -lbcrypt -lz` como o path single-file (link quebrava por falta de zlib).
- **`test_sat.hphl`** movido para positivos: passava pelo motivo errado (usava `chr` inexistente); `saturate` fixa sem erro por definição.

- **Autocomplete UX**: Fixed bug where typing additional characters during active completion reset the item selection back to index 0. Preserved open completion window and added immediate prefix selection via `cw.CompletionList.SelectItem(currentWord)`.
- **CLI & Loader Localization**: Localized module loader errors and CLI task logs in `loader.cpp` and `message_loader.cpp`.

---

## [v0.94.0] — 2026-09-05 — hotfix: bugs 1.1–1.10

- **1.1 [P0]** `gc_pressure()` não causa mais ACCESS_VIOLATION: o conservative stack scan agora é limitado ao `StackBase` real da thread (`NT_TIB` no Windows).
- **1.2/1.10 [P0/P2]** GC não perde mais referências em linked lists grandes (`gc_stress_100k` → `cnt=100000 PASS`): (a) derefere slots de raiz (`*(void**)rv`), (b) registra descritores de classe no início de `main`, (c) adiciona objetos promovidos ao remembered set (old→young).
- **1.3 [P1]** adicionado `Codegen::slotRefMem()` (referência de memória) e aplicado nos 14 pontos de `leaq`. Reativação do regalloc **adiada** (requer análise de liveness própria; `cnt++` em loop `while` quebra).
- **1.4 [P1]** `new list<T>()` e `new map<K,V>()` suportados (parser + semantic + codegen HIR/AST).
- **1.5 [P1]** `message_loader` resolve `messages/ptbr.json` relativo ao executável (Windows/Linux/macOS).
- **1.6 [P1]** builtins que retornam inteiro (ex.: `str_len`, `parse_int`) agora são compatíveis com variáveis `int`.
- **1.7 [P2]** `new int[N]` (array heap) e `new int(...)`/primitivos na heap suportados.
- **1.8 [P2]** indexação de string `s[i]` retorna `char` (nova runtime `hphl_str_char_index`).
- **1.9 [P2]** operador `??` aceita referências nuláveis (classe/string/list/map), não só `Option<T>`.

---

## [v0.93.0] — 2026-09-04 — Sprint 4 (Docs + IDE + Compat)

**Theme:** Documentation, IDE polish, runtime ABI stability.

### Highlights

- **Runtime ABI v1.0** is now stable: `HPHL_RUNTIME_ABI_VERSION = 100` (`runtime/runtime.h`). Programs abort on mismatch.
- **Compatibility policy** documented (`docs/COMPAT.md`).
- **Language Reference** complete (`docs/reference/language.md`).
- **Standard Library Reference** auto-generated from `stdbuiltins.cpp` via `tools/gen_stdlib_doc.py`.
- **Tutorial** "HP-HL in 30 minutes" (`docs/tutorial/30min.md`).
- **Migration guide** for C++ and Rust programmers (`docs/migration.md`).
- **Cookbook** with 10 patterns + 5 runnable examples (`docs/cookbook/index.md`).
- **IDE user guide** (`docs/ide.md`).
- **ARCHITECTURE.md** rewritten for v0.93.0 with the 8-subsystem layout.

### Compiler

- COMPAT-2: emit `hphl_runtime_abi_version()` check at program startup; mismatch → abort.
- COMPAT-2: runtime now exports `hphl_runtime_abi_version()` returning `100`.
- COMPAT-2: string `runtime/VERSION` documents the ABI version.

### Documentation

- DOC-1: 5-lesson tutorial (Hello → Classes/Generics → Memory Policies → Concurrency → Project).
- DOC-2: full Language Reference with all keywords and types.
- DOC-3: auto-generated Stdlib Reference (88 builtins across 7 categories).
- DOC-4: migration guide with C++/Rust→HP-HL cheat sheet.
- DOC-5: 50+ examples curated, 5 cookbook examples added.
- DOC-6: IDE user guide with keybindings and screenshots.
- DOC-7: 10 cookbook patterns (RAII, Builder, Visitor, Observer, Iterator, Singleton, Producer/Consumer, Pipeline, Map-Reduce, State Machine).
- ARCH-1: ARCHITECTURE.md rewritten for 8 subsystems.
- ARCH-2: this CHANGELOG.md reorganized.

### Tests

- 210/210 tests passing.

---

## [v0.92.0] — 2026-09-03 — Sprint 3 (Frontend polish + Regalloc + MIR)

**Theme:** Last "deferred" items from M2-M7 closed. Coalescing peephole re-enabled. Async methods in classes work.

### Highlights

- **A2 — generic interfaces** accepted by parser, warning emitted for partial monomorphization.
- **A3 — async methods in classes** fully functional. `this` is loaded from env slot 0 in async methods.
- **C3 — coalescing peephole** re-enabled: `movq A,%rcx; movq %rcx,B` → `movq A,B` (when A is a register).
- Several other small bugfixes.

### Compiler

- A2: parser accepts `I<int>` in interface lists; semantic strips generic args when looking up the interface; warning for partial monomorphization.
- A3: parser propagates `isAsync` flag to `parseFunctionDecl` for methods; semantic marks the call as `isAsyncCall` and wraps return in `task<T>`; codegen loads `this` from env slot 0 and parameters from slot 1+ in async methods.
- C3: `dbg.cpp` coalescing re-enabled with AT&T-aware src/dst parsing.

### Tests

- 210/210 tests passing.

---

## [v0.91.0] — 2026-09-01 — Sprint 2 (Stdlib + Network + Datetime)

**Theme:** Complete the standard library. Add networking and datetime.

22/22 items closed. 203/203 tests passing.

### Compiler

- Stdlib complete: `split`, `trim`, `toUpper`, `toLower`, `format`, JSON get/set, socket/connect/listen/accept, datetime, mutex/condvar wrappers.
- Network: `std.net` module with `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`.
- Datetime: `std.datetime` with `now`, `now_ms`, `format`.
- Concurrency wrappers: `std.sync` with mutex, condvar, semaphore, event, barrier.

### Runtime

- New `runtime/net/` subsystem (BSD sockets wrapper).
- New `runtime/datetime.c` (QPC-based monotonic clock).
- New `runtime/concurrency/condition_variable.c`, `atomic.c` (M_RV1 F11).

### Tests

- 203/203 tests passing.

---

## [v0.90.0] — 2026-09-01 — Sprint 1 (Build system + Concurrency base)

**Theme:** `hphl.pkg.toml` + `hphl` CLI. Worker pool, async, GC improvements.

16/19 items closed.

### Compiler / Tools

- New `hphl.pkg.toml` manifest (TOML, M_RV1 H6): name, version, entry, sources, dependencies, [tasks].
- New `hphl` CLI subcommands: `new`, `install`, `build`, `run`, `test`, `fetch`.
- Semver 2.0 (`parseSemVer` + `compareSemVer` + `resolveVersion`).
- `tools/build.ps1` with auto-detect MSYS2, auto-discover `*.cpp`, `-Debug` flag.
- `tools/clean.ps1` with `-All` flag.

### Runtime

- Worker pool auto-detect (`GetSystemInfo` / `GetActiveProcessorCount`).
- `HPHL_WORKERS`, `HPHL_MAX_TASKS` env overrides + auto-grow.
- Striped locks (64) with `_mm_pause`.
- Hash-based `gc_map_*` O(1) for `unregister`.
- Inline write barrier `bt $32/$36` + fast-path `lock xaddq`.
- In-place promotion `hdr[1] |= HPHL_HDR_GEN_BIT`.

### Tests

- 209/209 tests passing.

---

## [v0.89.0] — 2026-09-01 — M30

**Theme:** GC O(1) finalization. TOML parser. Self-host extras.

- M30: GC O(1) unregister, barrier inline, promotion in-place.
- M30: `hphl.pkg.toml` TOML parser + semver 2.0.
- M30: `tools/build.ps1` without hardcoded MSYS2.
- M30: error caret `line/column + ^`.
- M30: i18n foundation (`en.json`, `ptbr.json`).

---

## [v0.86.x] — 2026-08-30 — M28 (TCO + GC)

- M28: TCO self-tail-call + frame size reduction + 8 MB stack.
- M28: GC precise roots (28.1) + safety suite (28.4).
- M28: GC r10 collapse **disabled** (regressions in `hello`/`batch`).

---

## [v0.55.x — v0.85.x] — M21-M27

- M27: complete stdlib (string, math, file, system, datetime).
- M26: benchmarks, incremental build, build system.
- M25: LSP advanced (completion, inlay hints).
- M24: AArch64 backend native (stub via LLVM).
- M23: self-host (monomorphization, E2E).
- M22: backend + tooling (LLVM SSA, Ed25519, WASM, CI).
- M21: runtime GC + self-host extras.

---

## [v0.20.x — v0.54.x] — M18-M20

- M20: GC final + parser + codegen + self-host + rebuild E2E.
- M19: GC definitive (arena física + tri-color incremental + trigger só Main).
- M18: GC generational complete (6 sub-fases).
- M17: package fetch remote real (WinHTTP + SHA-256 + lockfile).

---

## [v0.1.x — v0.17.x] — M1-M16

- M1-M16: lexer, parser, semantic, types, symbol, codegen, llvmapi, irgen, HIR optimizer.

---

[Unreleased]: https://github.com/hphl/hphl/compare/v0.93.0...HEAD
[v0.93.0]: https://github.com/hphl/hphl/compare/v0.92.0...v0.93.0
[v0.92.0]: https://github.com/hphl/hphl/compare/v0.91.0...v0.92.0
[v0.91.0]: https://github.com/hphl/hphl/compare/v0.90.0...v0.91.0
[v0.90.0]: https://github.com/hphl/hphl/compare/v0.89.0...v0.90.0
