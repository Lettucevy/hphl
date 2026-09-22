# HP-HL v0.95.0-dev — STATUS

> **Snapshot do estado do projeto após Sprints 1–5 (M_RV1) + M31 (FFI v2, match, engine).**
> Atualizado em: 2026-09-11.
> Branch: `m-sprint3-v092`
> Último commit: `bdffcda` (selfhost, WASM target, 3-OS CI, release v0.94.0)

---

## TL;DR

| Métrica | Valor |
|---|---|
| Versão atual | v0.95.0-dev (fonte única: `compiler/src/version.h`, propagada para `--version` e LSP `serverInfo`) |
| Testes | **262/262 passando** (`tools/test.ps1 -Suite all`) + 7/7 package manager (`tools/test_packages.ps1`) |
| Selfhost | **6/6 compile + 5/5 E2E + bootstrap stage-2 OK** (`tests/scripts/test_selfhost.ps1` + `test_selfhost_e2e.ps1`) |
| Backends | **545/545** (100%) — todas as suites de backend (x64, llvm, ir, aarch64, wasm) passando, incl. `bit_types` e `lambda_basic` no LLVM (corrigidos: trunc, conversão float em call indireta/fcmp) |
| Bugs ativos | **0** na Seção 1 (todos corrigidos, incl. 1.3 regalloc) |
| Regalloc | **ON** (address-taken liveness) — `gc_stress_100k` retorna `cnt=100000` correto |
| IDE | **compila** (`dotnet build` em `ide/`) |
| LSP / DAP | **funcionam** (`hphlc --lsp` / `hphlc --dap` rodam); `serverInfo.version` = `0.94.0` |
| ABI runtime | **100** (v1.0.0) — `HPHL_RUNTIME_ABI_VERSION` estável |
| Release | `tools/release.ps1` produz `hphlc-{plat}-x64.{zip,tar.gz}` |
| CI 3-OS | `.github/workflows/ci.yml` v0.94.0 — usa `tools/build.sh` para Unix (auto-descobre TUs) |
| WASM | `--target wasm32-unknown-unknown` funcional E2E com Node.js |
| Tags | `v0.94.0`, `v1.0.0-rc1` |
| Documentação | Tutorial 30min, Language Reference, Migration, Cookbook, COMPAT — **todos presentes** |

---

## M31 — pós-v0.94.0: FFI v2, match, engine (2026-09-11)

> Validado pelo ProkionEngine (jogo FPS Vulkan real) + suite verde.

### Linguagem (compilador)
- **FFI v2**: tipo `ptr` opaco, `addr_of(x)`, 17 builtins `mem_*`, `extern "lib"` com `-L` (`HPHL_LIBDIR`, `$VULKAN_SDK/Lib`).
- **`match` com padrões string** (comparação por conteúdo) e **`char`** (literais + faixas); sujeito string/char exige `_`.
- **`ord(s[, i])` / `chr(c)`**, **`mem_zero(dst, n)`** (sem ambiguidade de offset).
- **`pool`/`base` contextuais** como identificadores (política `pool T x` e `base.M()` intactos; tipos chamados `pool`/`base` não suportados).
- **Divisão inteira estilo C**: peephole M13.3 fazia floor em divisores potência de 2 e truncava no resto (x64 discordava do LLVM); corrigido com bias branchless.
- `test_sat.hphl` movido para positivos (passava pelo motivo errado: usava `chr` inexistente).
- `linkObjects` (build `--project`) repassa `-lwsock32 -lbcrypt -lz` como o path single-file.

### Engine (ProkionEngine, validacao fim a fim)
- FPS jogável: menu/pause/game over/vitoria/restart, 5 waves + boss (15 HP), score, SFX assíncrono, input com debounce.
- Render: 6 pipelines (cena, HUD, chão xadrez, céu, paredes com textura RGBA via sampler, grid), depth LESS, materiais flat.
- `ProkionEngineFPS.hpproj` + `levels/*.lvl` (`Level.hphl` com fallback) + editor visual standalone (`tools/LevelEditor`, WinForms).
- Bugs achados no caminho: `DepthStencilCI` (`mem_fill` apagava os pokes), olho abaixo do chão, vértices arco-íris.

### IDE
- Ctrl+Enter compila `.hpproj` (procura na pasta/acima); árvore com data binding (`FileNode`); extensões `.lvl`/shaders; pasta via CLI.

---

## 1. Bugs ativos (conhecidos, com impacto real)

> **UPDATE 2026-09-05:** todos os bugs abaixo (1.1–1.10) foram **corrigidos**, incluindo
> o 1.3 (regalloc), resolvido via análise de address-taken (`hirMarkAddrTaken`).
> A lista é mantida para histórico/regressão. Nenhum bug ativo restante na
> Seção 1. Ver CHANGELOG.md para os detalhes.

### 1.1 BUG [P0] — `gc_pressure()` builtin causa ACCESS_VIOLATION ✅ CORRIGIDO
- **Arquivo:** `compiler/src/runtime/gc/gc.c` (built-in `hphl_gc_pressure`)
- **Sintoma:** chamada a `gc_pressure()` em qualquer programa crasha com exit code `-1073741819` (`0xC0000005`).
- **Reprodução:**
  ```hp-hl
  void Main() {
      gc_pressure();   // crash
  }
  ```
- **Impacto:** `examples/gc/gc_stress_1M.hphl` teve que remover a chamada para validar; a versão sem `gc_pressure()` passa com 1500 objetos.
- **Próximo passo:** investigar `hphl_gc_pressure` — provavelmente acessa root pointer liberado ou escreve fora do buffer.

### 1.2 BUG [P0] — Alocações grandes com linked list (>10k) crasham ✅ CORRIGIDO
- **Arquivo:** `compiler/src/runtime/gc/gc.c` (provavelmente triagem young/old)
- **Sintoma:** alocar mais de ~10k objetos interligados por referências `class` crasha. `examples/gc/gc_stress_100k.hphl` retorna `cnt=37` em vez de 100000 (perda silenciosa de refs).
- **Reprodução:** `examples/gc/gc_stress_100k.hphl`
- **Impacto:** `RC-4 (gc_stress_1M)` ficou em 1500 ao invés de 1M. Benchmarks com profundidade alta quebram.
- **Próximo passo:** investigar a triagem do GC geracional para linked lists grandes.

### 1.3 BUG [P1] — `if (false && ...)` para regalloc ON ✅ CORRIGIDO
- **Arquivos:**
  - `compiler/src/codegen/codegen_main.cpp` (parâmetros)
  - `compiler/src/codegen/expr_hir.cpp` (locais)
  - `compiler/src/codegen/emit_helpers.cpp` (`hirMarkAddrTaken` / `hirMarkAddrTakenExpr`)
  - `compiler/src/codegen/codegen.h` (`addrTakenVars_`)
- **Sintoma (antes):** regalloc desabilitado; habilitar quebrava com `operand type mismatch for lea` em 9 testes.
- **Causa raiz:** `slotRef()` retorna `%reg` quando slot é reg-backed, mas `leaq %reg, ...` é inválido. `++/--`/`ref`/`out`/`in` tomam o endereço via `genHirAddr` (que usa `slotRefMem`), lendo um slot de memória stale em vez do registrador.
- **Solução:** pré-passo de análise de address-taken (`hirMarkAddrTaken`) varre o HIR e marca variáveis cujo endereço é tomado (`++/--`, `ref`/`out`/`in`, ref em geral). `tryAllocReg` rejeita essas variáveis, garantindo consistência reg/memória.
- **Impacto:** RC-2/RC-3 desbloqueados (perf a ser medido).

### 1.4 BUG [P1] — `list<T>` não é tipo built-in reconhecível ✅ CORRIGIDO
- **Arquivo:** `compiler/src/frontend/parser.cpp` / `compiler/src/semantic/semantic.cpp`
- **Sintoma:** declarar `list<int> xs;` produz `erro semântico: tipo desconhecido 'list'`. O tipo existe (`stdbuiltins.cpp` retorna `SBType::List` em alguns casos) mas o parser/semantic não sabe dele.
- **Reprodução:** `examples/crypto/ed25519_stress.hphl` teve `list<Vetor>` rejeitado.
- **Impacto:** o cookbook `01_fizzbuzz.hphl` e a maioria dos exemplos não pode usar `list<T>` diretamente.
- **Próximo passo:** adicionar `list`, `map`, `set`, `option` como tipos built-in no parser.

### 1.5 BUG [P1] — Mensagens `Could not open message file: messages/ptbr.json` ✅ CORRIGIDO
- **Sintoma:** toda compilação emite a mensagem duas vezes no stderr.
- **Causa:** o binário procura `messages/ptbr.json` relativo ao CWD, não ao caminho do executável.
- **Impacto:** cosmético; não afeta funcionalidade, mas polui o output.
- **Próximo passo:** alterar `message_loader.cpp` para procurar `$ORIGIN/messages/`.

### 1.6 BUG [P1] — `str_len()` em construtor de classe falha ✅ CORRIGIDO
- **Sintoma:** `len = str_len(s);` em construtor produz `erro semântico: atribuição de tipo incompatível`.
- **Reprodução:** `examples/showcase/json_parser.hphl` (versão original).
- **Causa:** o semantic trata `str_len` como built-in global, mas em construtor de classe o tipo `int` (do retorno) conflita com algo.
- **Próximo passo:** investigar o tratamento de built-ins em construtores.

### 1.7 BUG [P2] — `new int[...]`, `new Tipo(args)`, `new int(...)` não funcionam ✅ CORRIGIDO
- **Sintoma:** `new int[1000]`, `new int(0)`, `new int(...)` produzem `erro de sintaxe: esperava nome da classe (encontrado 'int')`.
- **Causa:** `new` só aceita `new NomeDaClasse` (não tipos primitivos nem arrays).
- **Impacto:** exemplos de GC stress tiveram que ser reescritos para usar `class Box { int V; Box(int v) {...} }` em vez de `int` direto.
- **Próximo passo:** estender o parser para suportar `new int[]` e similares.

### 1.8 BUG [P2] — `string` não suporta indexação por `s[i]` ✅ CORRIGIDO
- **Sintoma:** `Src[Pos]` em string produz `erro semântico: indexação exige um array ou list`.
- **Causa:** indexação `[i]` só funciona em `list<T>` e arrays `T[N]`, não em `string`.
- **Impacto:** tokenizadores de JSON e similares precisam usar `sub_str(s, i, 1)` em loops.
- **Próximo passo:** adicionar overload `string::operator[](int)` no semantic.

### 1.9 BUG [P2] — `??` exige Option<T> no lado esquerdo ✅ CORRIGIDO
- **Sintoma:** `(tail ?? firstNo).Next` produz `lado esquerdo do ?? deve ser Option<T>`.
- **Causa:** `??` só desambigua `Option<T>`, não trata nullable reference direto.
- **Próximo passo:** ampliar o tipo `null` para que `??` funcione com `T?` e `T` (não-Option).

### 1.10 BUG [P2] — `gc_stress_100k.hphl` retorna cnt=37 (perda silenciosa de refs) ✅ CORRIGIDO
- **Sintoma:** programa espera `cnt == 100000` mas recebe 37.
- **Causa:** parte do grafo de objetos é perdida após o GC (relacionado ao bug 1.2).
- **Próximo passo:** mesmo do bug 1.2.

---

## 2. Itens planejados mas não começados

### 2.1 [P0] RC-2 — Performance binary-trees-14 x64 ≤ 3s ✅ CONCLUÍDO
- **Desbloqueado por:** correção do bug 1.3 (regalloc ON, commit `29e8e72`).
- **Estado:** medido e validado com sucesso. O binário `binary-trees-14` gerado pelo backend nativo x64 executa em **~2.38s - 2.46s** (meta ≤ 3.0s superada com folga). Consumo de memória de pico: 2.37 MB.
- **Saída:** 100% idêntica e correta (~5.5M nós alocados/verificados).

### 2.2 [P0] RC-3 — Performance binary-trees-14 LLVM ≤ 0.8s ✅ CONCLUÍDO
- **Desbloqueado por:** RC-2 e otimizações LLVM (-O3).
- **Estado:** medido e validado com sucesso. O binário gerado via backend LLVM (-O3) executa em **~0.71s - 0.73s** (meta ≤ 0.8s superada), sendo mais rápido inclusive que o binário equivalente em C++ com `malloc/free` (813.3 ms). Consumo de memória de pico: 1.94 MB.
- **Saída:** 100% idêntica à referência.

### 2.3 [P1] L3 — `__attribute__((naked))` cross-platform ✅ CONCLUÍDO
- **Arquivo:** `compiler/src/runtime/debug/debug.c:1085`
- **Estado:** implementado suporte multiplataforma para os wrappers `hphl_dbg_trap` e `hphl_dbg_enter`. Adicionados ramos `#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))` (Microsoft x64 ABI), `#elif defined(__x86_64__) || defined(__amd64__)` (System V AMD64 ABI para Linux/macOS x86_64 preservando `%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9`, `%rax` e `%xmm0-%xmm7`), `#elif defined(__aarch64__)` (AAPCS64 para Linux/macOS AArch64 preservando `x0-x7`, `q0-q7`, `x29` e `x30`), e `#else` (fallback genérico portátil via `__builtin_frame_address(0)`).
- **Verificação:** runtime reconstruído e validado em Windows x64 com compilação e execução `--debug`.

### 2.4 [P1] L4 — WASM imports cleanup ✅ CONCLUÍDO
- **Arquivo:** `compiler/src/main.cpp:1255`, `compiler/src/llvmapi.cpp:145`
- **Estado:** corrigida a emissão e declaração de imports/exports WebAssembly:
  - Removida a referência obsoleta a `env.__linear_memory` da mensagem de pós-link do WASM (o linear memory é exportado como `(export "memory")` pelo próprio módulo via `wasm-ld`).
  - Adicionada flag `--export-if-defined=main` ao linker `wasm-ld`.
  - Corrigido atributo `wasm-export-name` em `llvmapi.cpp` para evitar exportação duplicada da função `main`, permitindo que o `wasm-ld` gere e exporte a função `main` de forma única e compatível com as engines WebAssembly (Node.js e browsers).
- **Verificação:** execução end-to-end com `runner_wasm.js` no Node.js validada (`hello.wasm` e `simple_print.wasm`).

### 2.5 [P2] F14 — crash handler Linux/macOS ✅ CONCLUÍDO
- **Arquivos:** `compiler/src/runtime/runtime.h`, `compiler/src/runtime/runtime_api.h`, `compiler/src/runtime/core/core.c`, `compiler/src/runtime/concurrency/concurrency.c`
- **Estado:** implementado crash handler cross-platform: em Windows utiliza `SetUnhandledExceptionFilter` com extração detalhada de contexto (`EXCEPTION_POINTERS`); em POSIX (Linux/macOS) instala handlers `sigaction` para `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL` e `SIGABRT` com `SA_SIGINFO`, extraindo endereço da falha (`si_addr`) e registradores de CPU (`ucontext_t` rip/rsp/rbp em x86_64, pc/sp/fp em AArch64 para macOS e Linux), emitindo diagnóstico `[CRASH]` antes de restaurar `SIG_DFL` e propagar o sinal. Unificado sob `hphl_install_crash_handler()`, chamado no constructor de inicialização e em `hphl_ensure_pool()`.

### 2.6 [P2] C9 — `try/catch` `__asm__ volatile` cross-platform ✅ CONCLUÍDO
- **Arquivos:** `compiler/src/runtime/core/core.c`, `compiler/src/runtime/runtime_api.h`, `compiler/src/runtime/debug/debug.c`
- **Estado:** implementado manipulador de exceções cross-platform (`hphl_exc_new`, `hphl_exc_free`, `hphl_exc_push`, `hphl_exc_end`, `hphl_exc_payload`, `hphl_throw`, `hphl_exc_begin`, `hphl_exc_restore`). Em x86_64, utiliza assembly naked com convenções de chamada específicas por SO (`%rcx` em Windows x64 vs `%rdi` em System V AMD64 / POSIX); em arquiteturas não-x86 (AArch64, WASM), implementa fallback portável baseado em `jmp_buf` / `setjmp` / `longjmp`. Wrappers de trap e enter em `debug.c` também suportam Windows x64, System V e AAPCS64.
- **Testes:** `tests/positive/trycatch.hphl` e `tests/positive/outdef.hphl` passando nos backends x64 e LLVM API.

### 2.7 [P2] I9 — `test_backends.ps1` dedup de `test.ps1` ✅ CONCLUÍDO
- **Arquivos:** `tests/scripts/test_backends.ps1`, `tools/test.ps1`
- **Estado:** deduplicação concluída: `tools/test.ps1` é a única fonte da verdade, suportando a suite `backends` e backends individuais (`x64`, `llvm`, `ir`, `aarch64`, `wasm`). `tests/scripts/test_backends.ps1` foi refatorado para ser um wrapper leve delegando diretamente para `tools/test.ps1`.
- **Correções associadas:** corrigido parsing de argumentos de backend (`--backend <name>` como tokens separados em vez de `--backend=...`), removida declaração duplicada de `hphl_str_len` no IRGen, corrigido cálculo de tamanho de arrays globais (`globalSizeBytes` alinhado com `typeSize`) e suporte a `isListBuffer` no index lowering do LLVM IRGen.

### 2.8 [P2] A2 — generic interface full monomorphization ✅ CONCLUÍDO
- **Arquivos:** `compiler/src/semantic/semantic.h`, `compiler/src/semantic/semantic.cpp`, `tests/positive/generic_interface_full.hphl`
- **Estado:** monomorfização completa de interfaces genéricas. Suporta declaração e implementação de interfaces genéricas por classes comuns e por classes genéricas (`class Box<T> : IProducer<T>, IConsumer<T>`). Argumentos de tipo são preservados como `TypeVar` no registro de templates e resolvidos em `resolveType`. `ClassInfo` armazena e instancia `interfaceTypeArgs` concretos na especialização. Em `layoutClass`, métodos de templates e interfaces registram seus slots virtuais em `vslots_` permitindo dispatch virtual (`call *slot*8(%r10)`) transparente através de referências de interface nos backends x64 e LLVM. Aviso obsoleto de monomorfização parcial removido.
- **Testes:** `tests/positive/generic_interface.hphl`, `tests/positive/interface_as_type.hphl`, `tests/positive/generic_interface_full.hphl`.

### 2.9 [P2] A9 — async array/move ✅ CONCLUÍDO
- **Estado:** implementado e corrigido no commit `3d8fe86`.

### 2.10 [P3] `examples/showcase/json_parser.hphl` skeleton vs real ✅ CONCLUÍDO
- **Estado:** parser JSON real completo com tokenizer, recursive descent, pretty-printer, escapes \uXXXX, números com expoente, deep nesting e asserts (commit `4aebfbe`).
- **Arquivo:** `examples/showcase/json_parser.hphl`.

### 2.11 [P3] `examples/showcase/http_server.hphl` skeleton vs real ✅ CONCLUÍDO
- **Estado:** servidor HTTP real com sockets Winsock nativos (WSAStartup, socket, bind, listen, accept, recv, send, closesocket), parse de requests HTTP (GET/POST, path, headers, body), roteamento dinâmico e responses (commit `057d678`).
- **Arquivo:** `examples/showcase/http_server.hphl`.

### 2.12 [P3] `examples/showcase/raytracer.hphl` skeleton vs real ✅ CONCLUÍDO
- **Estado:** raytracer real completo (commit `5814a14`, `f37ce58`, `1a0a6fc`, `c13f08c`) com esferas, reflexão, sombras, chão xadrez, luzes pontuais, geração de PNG (`img_png_save`).
- **Arquivo:** `examples/showcase/raytracer.hphl`.

### 2.13 [P3] A4 — pre/post conditions (`require`/`ensure`) ✅ CONCLUÍDO
- **Estado:** implementado no commit `a751c9b`.

### 2.14 [P3] Result<T, E> type ✅ CONCLUÍDO
- **Estado:** implementado: suporte de primeira classe a `Result<T, E>` / `result<T, E>` e `Option<T>` / `option<T>` com propriedades (`.IsOk`, `.IsError`, `.IsErr`, `.Value`, `.Error`, `.HasValue`, `.IsSome`, `.IsNone`), métodos (`.is_ok()`, `.is_err()`, `.unwrap()`, `.unwrap_err()`, `.unwrap_or()`, `.is_some()`, `.is_none()`), operador `??`, `Sendable` para concurrency (`channel` e `spawn`), com suporte em backends x64 e LLVM.
- **Testes:** `tests/positive/result_full.hphl`, `tests/negative/erro_result_member_unknown.hphl`.

### 2.15 [P3] `[[deprecated("...")]]` no parser ✅ CONCLUÍDO
- **Estado:** implementado (commit `c8ad87a`): `parseAttributes()` reconhece `deprecated`/
  `suppress_deprecation`/`inline`; `warn()` no semantic para função/método/classe/ctor.
- **Teste:** `tests/positive/attr_deprecated.hphl`.

### 2.16 [P3] Operadores `?[]` (Safe Indexing) e `~` (bitwise NOT) ✅ CONCLUÍDO
- **Estado:** implementado e validado em todos os backends (x64 AST, HIR e LLVM IRGen).
  - `~`: operador unário bitwise NOT verificado com valores literais, identidades, expressões em tempo de compilação (`const int K = ~42;`) e rejeição de tipos não inteiros no semantic.
  - `?[]`: operador de indexação condicional/segura implementado no lexer (`TokenType::QuestionBracket`), parser (`OptIndexExpr`), semantic (`checkOptIndex`), lowering HIR (`lowerOptIndex`), e codegen AST (`genOptIndex`). Suporta `Array`, `List` e `String`, retornando `Option<T>` para tipos de valor e referência nullable para classes/strings, integrado com o operador `??`.
- **Testes:** `tests/positive/bitwise_not.hphl`, `tests/positive/opt_index.hphl`, `tests/negative/erro_bitwise_not_type.hphl`, `tests/negative/erro_opt_index_type.hphl`.

### 2.17 [P3] Module `std.collections` / `std.io` / `std.net` ✅ CONCLUÍDO
- **Estado:** implementado (commit `c8ad87a`): `isStdModule()` + sentinela `<std:*>` no loader;
  chamada qualificada `std.X.f()` resolve via `findStdBuiltin`; `expectIdentOrKeyword` para
  keywords em nomes de módulo.
- **Teste:** `tests/positive/std_modules_virtual.hphl`.

### 2.18 [P3] `tools/test_backends.ps1` workflow ✅ CONCLUÍDO
- **Estado:** script workflow `tools/test_backends.ps1` criado e 100% alinhado com `tools/test.ps1`. Suporta `-Backend x64|llvm|ir|aarch64|wasm|all`, `-Verbose` e delega a execução com saída limpa e códigos de saída consistentes.

### 2.19 [P3] Binary-trees benchmark com 14 profundidade em v0.93 ✅ CONCLUÍDO
- **Estado:** script de benchmark `bench/bench_binary_trees.ps1` atualizado para profundidade 14 e nova variante LLVM -O3 adicionada. Resultados salvos e validados em `examples/benchmarks/bench_results.txt`. Assembly x64 `examples/benchmarks/binary-trees-14.s` regenerado pelo compilador atualizado (`hphlc --keep-asm`).

### 2.20 [P3] Tag `v0.94.0` e `v1.0.0-rc1` ✅ CONCLUÍDO
- **Estado:** tags Git criadas: `v0.94.0` e `v1.0.0-rc1`.

### 2.21 [P3] 3 OS build matrix — verificação local ✅ CONCLUÍDO
- **Estado:** CI `.github/workflows/ci.yml` atualizado para `v0.94.0`. Build Unix agora usa `tools/build.sh` (auto-descobre TUs via `find`), eliminando lista hardcoded de arquivos. `tools/build.sh` corrigido para compilar `sha512.c` (necessário para linking no Linux/macOS).

### 2.22 [P3] Self-host (HP-HL escrito em HP-HL) ✅ CONCLUÍDO
- **Estado:** 6/6 programas selfhost passando (`bench_compile`, `demo`, `main`, `tst_enum`, `tst_if`, `tst_int`). Corrigido `tst_if.hphl` (`M()` → `Main()`). `tests/scripts/test_selfhost.ps1` agora filtra módulos de biblioteca (sem `Main`) automaticamente, skippando `lexer`, `parser`, `codegen`, `semantic`, `lexerparser`. Builtin `hphl_gas_str` exposto em `stdbuiltins.cpp`.
- **Item 3 (true self-hosting, pós-v0.94.0)** ✅ CONCLUÍDO:
  - Fase 1: `target.txt` corrigido (`selfhost/demo.hphl`) + `tests/scripts/test_selfhost_e2e.ps1` (paridade funcional vs `hphlc`: compilar→montar→linkar→rodar→comparar stdout; 5/5: demo/tst_if/tst_int/tst_enum/hello).
  - Fase 2: literais float no codegen selfhost (pool `.double` + `movq` de bits + `hphl_print_float` em `%xmm0`; globais com `.double`).
  - Fase 3: bootstrap completo — selfhost compila as 5210 linhas do próprio compilador (lexer+parser+semantic+codegen+main) em ~11s, e o binário resultante (stage-1) compila alvos funcionais; stage-2 (compilador compilado pelo compilador compilado) verificado com ponto fixo (`demo.hphl` → `7 12 fim`).
  - Bugs do selfhost corrigidos no caminho: rollback especulativo em `parseGenericArgs` (`i < 10` consumido como generics); corpo sem chaves no `for`; `VarDecl` guarda tipo declarado (`bool b=false`, `Pm p=null`); `ClsPut` vazio (`class` sem campos); tabela `fnN` 128→512; `discoverInStmt` linear (era exponencial); epílogo preserva `%rax`; spills de params antes de `gc_add_root`; arrays locais com base ascendente + `stosq` em qwords; chamada de ctor zero-arg no `new`.

### 2.23 [P3] WASM target funcional ✅ CONCLUÍDO
- **Estado:** `--target wasm32-unknown-unknown` agora infere `--backend llvm` automaticamente (sem exigir flag explícita). `runner_wasm.js` auto-descobre `main`/`Main` quando nenhuma função é especificada. End-to-end validado com Node.js (`hello.wasm` → saída correta).

---

## 3. Items concluídos (Sprints 1-5)

### Sprint 1 (v0.90.0) — Build system + Concurrency base
- ✅ I1: `hphl.pkg.toml` (TOML manifest)
- ✅ I3: `hphl` CLI subcommands (`new`, `install`, `build`, `run`, `test`, `fetch`)
- ✅ A5, A11, F7-F11: worker pool, async, GC improvements
- ✅ Semver 2.0 + `parseSemVer` + `compareSemVer`
- ✅ `tools/build.ps1` com auto-detect MSYS2

### Sprint 2 (v0.91.0) — Stdlib + Network + Datetime
- ✅ Stdlib completa: split, trim, toUpper, toLower, format, JSON, socket, datetime, mutex/condvar
- ✅ 22/22 itens fechados, 203/203 testes

### Sprint 3 (v0.92.0) — Frontend polish + Regalloc + MIR
- ✅ A2: parser aceita `I<int>` com warning
- ✅ A3: async methods em classes (this no env slot 0)
- ✅ C3: coalescing peephole reativado
- ✅ 210/210 testes

### Sprint 4 (v0.93.0) — Docs + IDE + Compat
- ✅ COMPAT-1: `docs/COMPAT.md` (semver policy)
- ✅ COMPAT-2: `HPHL_RUNTIME_ABI_VERSION = 100` + check no startup
- ✅ DOC-1: `docs/tutorial/30min.md` (5 lições)
- ✅ DOC-2: `docs/reference/language.md`
- ✅ DOC-3: `docs/stdlib/index.md` auto-gerado (88 builtins)
- ✅ DOC-4: `docs/migration.md` (C++/Rust → HP-HL)
- ✅ DOC-5: `examples/cookbook/01..05.hphl` (FizzBuzz, JSON, HTTP, threadpool, eventloop)
- ✅ DOC-6: `docs/ide.md` (keybindings, screenshots ASCII)
- ✅ DOC-7: `docs/cookbook/index.md` (10 patterns)
- ✅ ARCH-1: `docs/ARCHITECTURE.md` reescrito (8 subsistemas)
- ✅ ARCH-2: `CHANGELOG.md` reorganizado
- ✅ IDE-1..5: `dotnet build` OK, LSP/DAP verificados
- ✅ K1+K2: `tools/clean.ps1 -All` agora limpa `examples/benchmarks/`

### Sprint 5 (v0.94.0-rc) — RC + cross-platform + showcases
- ✅ RC-4: `examples/gc/gc_stress_1M.hphl` (1500 validado)
- ✅ RC-5: `examples/crypto/ed25519_stress.hphl` (subset 10/10)
- ✅ RC-6: `.github/workflows/ci.yml` (3 OS matrix)
- ✅ RC-7: `tools/release.ps1` (cria `hphlc-{plat}-x64.{zip,tar.gz}`)
- ✅ RC-8: 3 showcases (json_parser, http_server, raytracer) — skeleton funcional
- ✅ L5: `findWasmLd` cross-platform (where/which + paths extras)
- ✅ F15: `HPHL_DBG_MAX_THREADS` env override
- ✅ I8: `tools/build.sh` reescrito (Linux/macOS)
- ⏸️ RC-9: GitHub release (deixado para futuro)
- ⏸️ RC-2/RC-3: Performance binary-trees (bloqueado por C1 regalloc)

### Hotfixes pós-Sprint 5 (2026-09-05)
- ✅ Bugs 1.1–1.10 corrigidos (commit `ba67c7a`): GC/gc_pressure, string index, `??`,
  `new list<T>`/`int[N]`, `str_len` em ctor, mensagens ptbr.json, `slotRefMem`
- ✅ Item 2.15: `[[deprecated("...")]]` + warning no semantic (commit `c8ad87a`)
- ✅ Item 2.17: módulos std virtuais (`std.collections`/`std.io`/`std.net`) (commit `c8ad87a`)
- ✅ Bug 1.3: regalloc ON via address-taken liveness (commit `29e8e72`)
- ✅ `version.h` como fonte única de versão, propagada para `--version`/`-V` e LSP `serverInfo` (commit `b5516d6`)

### Sprint 6 (v0.95.0-dev) — Selfhost Modular & Paridade de Recursos (2026-09-19)
- ✅ **Modularização do Compilador Selfhost (`selfhost/src/`)**:
  - Arquitetura desacoplada em subsistemas: `ast/ast.hphl`, `frontend/token.hphl`, `frontend/lexer.hphl`, `frontend/parser.hphl`, `semantic/semantic.hphl`, `codegen/codegen.hphl`, `main.hphl`.
- ✅ **Coleções Dinâmicas no Selfhost (100% Paridade Byte-a-Byte)**:
  - `list<T>` (`lists`, `list_assign`, `list_assign2`, `list_cap`): métodos `.add()`, `.len()`, `.cap()`, atribuição em elemento e iteração `foreach`.
  - `map<K, V>` (`maps`): operações completas de dicionário nativo e indexação.
- ✅ **Genéricos Avançados & Monomorfização no Selfhost**:
  - Constraints `where T : supports +` com unificação para tipos numéricos e strings (`generics_where`).
  - Vtables completas para interfaces genéricas e herança genérica (`generic_interface_full`, `generic_inherit`, `generic_interface`, `generics_variance`, `generics_constraints_advanced`).
- ✅ **Suíte & E2E**:
  - `tools/test.ps1`: **263/263 PASS**.
  - `tests/scripts/test_selfhost_e2e.ps1`: **5/5 PASS**.

---

## 4. Recomendações de prioridade (pós v0.94.0)

> Todos os 23 itens da Seção 2 e todos os 10 bugs da Seção 1 estão **concluídos**.
> O foco agora é paridade total do selfhost e polish de release.

### Curto prazo (v0.95 — Paridade Selfhost)
1. **Lambdas e Closures no Selfhost** (`lambda_basic.hphl`, `lambda_capture.hphl`)
2. **Pattern Matching (`match`) no Selfhost** (`match.hphl`, `match_char.hphl`, `match_expr.hphl`, `patterns.hphl`, etc.)
3. **Concorrência & Async no Selfhost** (`async`, `channel`, `tasks`, `threads`)
4. **Tipos Algébricos no Selfhost** (`option`, `result_full`)

### Médio / Longo prazo (v1.0 — Release)
1. **CI real no GitHub Actions** — push e validar matrix nos runners
2. **RC-9: GitHub Release automation** — `gh release create` no CI para publicar binários em cada tag
3. **Fuzzing / property-based testing contínuo** (`tools/fuzz.ps1`)

---

## 5. Como reproduzir / testar

```bash
# Build do compilador de referência
powershell -File tools/build.ps1

# Test suite completa (263 testes)
powershell -File tools/test.ps1 -Suite all

# Teste E2E do compilador selfhost
powershell -File tests/scripts/test_selfhost_e2e.ps1

# Release (gera hphlc-windows-x64.zip)
powershell -File tools/release.ps1 -OutputDir dist

# Limpar artefatos
powershell -File tools/clean.ps1 -All

# IDE
cd ide && dotnet build

# Versão
hphlc --version   # hphlc 0.95.0-dev
```

---

**Última atualização:** 2026-09-19  
**Versão base:** v0.95.0-dev (263 testes, 5/5 selfhost E2E, paridade crescente no selfhost modular, WASM e2e OK)

