// Auto-generated from codegen.cpp
#include "codegen.h"
#include <cstdio>
#include <functional>
#include "stdlib/stdbuiltins.h"
#include <cstdint>
#include <cstring>

/* M27: hphl_set_class_desc â€” called at startup to register class bitmaps. */
extern "C" int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);


namespace hphl {
void Codegen::generateFunctions() {
  if (hasHir()) {
    generateFunctionsFromHir();
  } else {
    generateFunctionsFromAst();
  }
  genPendingTasks();
}

void Codegen::genFunction(FunctionDecl* fn) {
  if (!fn->body) return; // assinatura de interface (sem corpo)
  curFn_ = fn;
  scopes_.clear();
  heapSlots_.clear();
  listSlots_.clear();
  mapSlots_.clear();
  gcRefSlots_.clear();
  retSlot_ = -1;
  arenaHandleSlot_ = -1;
  retIsFloat_ = false;
  nextSlot_ = 0;
  regForSlot_.clear();
  nextCalleeIdx_ = 0;
  if (debug_) dbgStartFunction(fn);

  std::string label = fnLabel(fn);
  bool entry = fn->isEntryPoint;

  // ---- 1) gera o corpo em buffer temporÃ¡rio (para conhecer o frame) ----
  std::ostringstream bodyBuf;
  text_.swap(bodyBuf);

  scopes_.emplace_back();
  for (auto* g : sem_.globals()) {
    Local l;
    l.type = g->type;
    l.policy = g->storage;
    l.slot = -1;
    l.isGlobal = true;
    l.globalLabel = ".Lg_" + g->name;
    l.atomic = g->atomic;
    auto it = tlsOffsets_.find(g->name);
    if (it != tlsOffsets_.end()) l.tlsOffset = it->second;
    scopes_.back()[g->name] = l;
  }
  int thisSlot = -1;
  if (fn->isMethod && !fn->isStatic) {
    Local l;
    l.type = Type::makeClass(fn->ownerClass);
    l.slot = nextSlot_++;
    thisSlot = l.slot;
    scopes_.back()["this"] = l;
    // A3: async método → this vem do env (slot 0, antes dos parâmetros)
    if (fn->isAsync) {
      // taskEnvSlot_ é inicializado abaixo; usa rcx diretamente (env chega em rcx)
      emitText("movq %rcx, %rax");
      emitText("movq 0(%rax), %r10");
      emitText("movq %r10, " + slotRef(thisSlot));
    } else {
      // this chega em rcx
      emitText("movq %rcx, " + slotRef(thisSlot));
    }
  }
  // funÃ§Ã£o sintÃ©tica de tarefa (`spawn`/`parallel`) ou `async`: recebe
  // (env, res) em rcx/rdx. O env contÃ©m, na ordem das capturas (ou dos
  // parÃ¢metros, em async), os valores copiados no site do spawn; viram
  // locais prÃ³prios da tarefa (independentes do caller).
  taskEnvSlot_ = -1;
  taskResSlot_ = -1;
  if (fn->isTask || fn->isAsync) {
    taskEnvSlot_ = nextSlot_++;
    emitText("movq %rcx, " + slotRef(taskEnvSlot_));
    taskResSlot_ = nextSlot_++;
    emitText("movq %rdx, " + slotRef(taskResSlot_));
  }
  for (size_t i = 0; i < fn->params.size(); i++) {
    Local l;
    l.type = fn->params[i]->type;
    // A9 (async array): param array = ponteiro da base (ver genFunctionHir)
    if (l.type.kind == Type::Kind::Array) l.policy = StoragePolicy::Heap;
    l.slot = nextSlot_++;
    l.byRef = fn->params[i]->isByRef();
    if (!l.byRef) tryAllocReg(l.slot, l.type, StoragePolicy::Stack, false); // C1: regalloc ON
    scopes_.back()[fn->params[i]->name] = l;
    if (fn->isAsync) {
      // async: os parâmetros chegam no env. Para métodos, slot 0 = this;
      // os parâmetros vêm a partir do slot 1 (ou slot 0 se for função global)
      size_t envOffset = i + (thisSlot >= 0 ? 1 : 0);
      emitText("movq " + slotRef(taskEnvSlot_) + ", %rax");
      emitText("movq " + std::to_string(8 * envOffset) + "(%rax), %r10");
      emitText("movq %r10, " + slotRef(l.slot));
      continue;
    }
    int k = (int)i + (thisSlot >= 0 ? 1 : 0); // posiÃ§Ã£o ABI (this = posiÃ§Ã£o 0)
    if (l.byRef) {
      // ref/out/in: o valor recebido Ã© o ENDEREÃ‡O da variÃ¡vel do caller
      const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
      if (k < 4) {
        emitText("movq " + std::string(regs[k]) + ", " + slotRef(l.slot));
      } else {
        emitText("movq " + std::to_string(48 + 8 * (k - 4)) + "(%rbp), %rax");
        emitText("movq %rax, " + slotRef(l.slot));
      }
      continue;
    }
    if (k < 4) {
      if (isFloatType(l.type)) {
        emitText("movsd %xmm" + std::to_string(k) + ", " + slotRef(l.slot));
      } else {
        const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
        emitText("movq " + std::string(regs[k]) + ", " + slotRef(l.slot));
      }
    } else {
      // stack args: arg k (k>=4) em [rbp + 48 + 8*(k-4)]
      if (isFloatType(l.type)) {
        emitText("movsd " + std::to_string(48 + 8 * (k - 4)) + "(%rbp), %xmm0");
        emitText("movsd %xmm0, " + slotRef(l.slot));
      } else {
        emitText("movq " + std::to_string(48 + 8 * (k - 4)) + "(%rbp), %rax");
        emitText("movq %rax, " + slotRef(l.slot));
      }
    }
  }

  // M28 32.1: TCO body label (AST path) â€” apÃ³s salvar parÃ¢metros (que NÃƒO
  // devem executar na re-entrada via jmp .L_body). Ver TCO.txt Â§5, Â§21.
  emitText(".L_" + fnLabel(fn) + "_body:");

  // funÃ§Ã£o sintÃ©tica de tarefa: as capturas viram locais do prÃ³logo (o env
  // guarda os valores copiados no site do spawn â€” para async, os parÃ¢metros
  // jÃ¡ foram materializados acima, e nÃ£o hÃ¡ taskCaptures)
  if (fn->isTask) {
    for (auto& cap : fn->taskCaptures) {
      Local l;
      l.type = cap.second;
      l.slot = nextSlot_++;
      // captura de ARRAY (usada pelo `parallel foreach`): o env guarda a BASE
      // do array (ponteiro), nÃ£o o blob â€” polÃ­tica de ponteiro faz a carga da
      // base usar o valor do slot (emitArrayBaseLoad)
      if (cap.second.kind == Type::Kind::Array)
        l.policy = StoragePolicy::Heap;
      scopes_.back()[cap.first] = l;
      emitText("movq " + slotRef(taskEnvSlot_) + ", %rax");
      emitText("movq " + std::to_string(8 * ((size_t)(&cap - fn->taskCaptures.data()))) +
                   "(%rax), %r10");
      emitText("movq %r10, " + slotRef(l.slot));
    }
  }

  if (fn->body) {
    scopes_.emplace_back();
    genBlock(fn->body.get());
    scopes_.pop_back();
  }
  scopes_.pop_back();

  // programa com spawn/parallel: antes de sair de Main, aguarda as threads
  if (fn->isEntryPoint && programParallel_)
    emitRuntimeCall("hphl_join_tasks");

  std::string body = text_.str();
  text_.swap(bodyBuf); // restaura buffer principal

  // ---- 2) prÃ³logo com frame final ----
  int frame = ((nextSlot_ * 8) + 8 + 15) & ~15;  // reduce +32 -> +8
  // M14.4: alinhamento â€” nÂº Ã­mpar de pushes deixa rspâ‰¡8 (mod 16) nas calls
  {
    std::vector<std::string> seen;
    for (auto &kv : regForSlot_)
      if (std::find(seen.begin(), seen.end(), kv.second) == seen.end())
        seen.push_back(kv.second);
    if (seen.size() % 2 == 1) frame += 8;
  }
  emitText("\n# ===== funÃ§Ã£o " + fn->name + " =====");
  if (entry) emitText(".globl main");
  emitText(label + ":");
  emitText("pushq %rbp");
  emitText("movq %rsp, %rbp");
  if (frame > 0) emitText("subq $" + std::to_string(frame) + ", %rsp");
  /* M27: init runtime args for args() builtin */
  if (entry) {
    emitText("movq 24(%rbp), %rcx");
    emitText("leaq 32(%rbp), %rdx");
    emitRuntimeCall("hphl_init_args");
  }
  // COMPAT-2 (Sprint 4): verifica ABI do runtime. Aborta se mismatch.
  // %rax = hphl_runtime_abi_version(); cmp %rax, $HPHL_RUNTIME_ABI_VERSION; je ok
  if (entry) {
    std::string okLbl = newLabel("abi_ok");
    emitRuntimeCall("hphl_runtime_abi_version");
    emitText("cmpq $100, %rax");
    emitText("je " + okLbl);
    emitText("leaq .Lstr_abi_mismatch(%rip), %rcx");
    emitText("subq $32, %rsp");
    emitRuntimeCall("hphl_panic");
    emitText(okLbl + ":");
    if (!classInitText_.str().empty()) {
      emitText(classInitText_.str());
    }
  }
  // M17 Fase 1: registra heapSlots como roots para GC tracing
  // M29 FIX: batch via hphl_gc_register_slots (endereços via %rbp+off).
  {
    std::vector<int> slots;
    for (auto &hs : heapSlots_) {
      if (hs.policy == StoragePolicy::Shared || hs.policy == StoragePolicy::Heap) {
        slots.push_back(hs.slot);
      }
    }
    for (int s : gcRefSlots_) {
      slots.push_back(s);
    }
    if (!slots.empty()) {
      std::string offLbl = ".LgcOffs_" + label;
      // Root registration is a call emitted before the buffered body stores
      // incoming parameters. Preserve every Win64 argument register first.
      emitText("subq $64, %rsp");
      emitText("movq %rcx, 0(%rsp)");
      emitText("movq %rdx, 8(%rsp)");
      emitText("movq %r8, 16(%rsp)");
      emitText("movq %r9, 24(%rsp)");
      emitText("movsd %xmm0, 32(%rsp)");
      emitText("movsd %xmm1, 40(%rsp)");
      emitText("movsd %xmm2, 48(%rsp)");
      emitText("movsd %xmm3, 56(%rsp)");
      emitData(offLbl + ":");
      for (int s : slots) {
        std::string r = slotRef(s);
        std::string num = r.substr(0, r.find('('));
        emitData("  .quad " + num);
      }
      emitText("leaq " + offLbl + "(%rip), %rcx");
      emitText("movq %rbp, %rdx");
      emitText("movq $" + std::to_string(slots.size()) + ", %r8");
      emitRuntimeCall("hphl_gc_register_slots");
      emitText("movsd 32(%rsp), %xmm0");
      emitText("movsd 40(%rsp), %xmm1");
      emitText("movsd 48(%rsp), %xmm2");
      emitText("movsd 56(%rsp), %xmm3");
      emitText("movq 0(%rsp), %rcx");
      emitText("movq 8(%rsp), %rdx");
      emitText("movq 16(%rsp), %r8");
      emitText("movq 24(%rsp), %r9");
      emitText("addq $64, %rsp");
    }
  }
  // M13.3: salva callee-saved alocados (apÃ³s subq para nÃ£o deslocar slots)
  {
    std::vector<std::string> pushed;
    for (auto &kv : regForSlot_) {
      if (std::find(pushed.begin(), pushed.end(), kv.second) == pushed.end())
        pushed.push_back(kv.second);
    }
    for (auto &r : pushed) emitText("pushq " + r);
  }
  // M28 32.1: TCO body label (apÃ³s prologo, add_roots, push callee-saved).
  // Self-tail-calls re-entram aqui via `jmp .L_<fn>_body`, reusando o
  // frame atual sem re-executar add_roots nem push. Spec: TCO.txt Â§5, Â§21.
  // O label jÃ¡ foi emitido antes dos add_roots (perto dos parÃ¢metros).
  // slots de list (inclui o `..resto` dos patterns) comeÃ§am NULL: o epÃ­logo sÃ³
  // libera quando um objeto foi realmente criado (arms podem nÃ£o casar)
  for (int s : listSlots_) emitText("movq $0, " + slotRef(s));
  if (arenaHandleSlot_ >= 0) emitText("movq $0, " + slotRef(arenaHandleSlot_));
  if (debug_) emitDebugPrologue();

  // ---- 3) corpo ----
  if (fn->isEntryPoint && tlsSizeBytes_ > 0) {
    // runtime: bloco threadlocal de N bytes por thread (antes de qualquer
    // acesso — tarefas só rodam após o início de Main)
    emitText("movl $" + std::to_string(tlsSizeBytes_) + ", %ecx");
    emitRuntimeCall("hphl_tls_setup");
  }
  // M_RV1 A5: inicializa globais list/map no Main (aloca via runtime e
  // armazena o ponteiro no label .Lg_NAME em .data). Como `hphl_list_new`
  // e `hphl_map_new` alocam heap, isso garante que o global aponta para
  // um objeto válido antes de qualquer leitura.
  if (fn->isEntryPoint) {
    for (auto* g : sem_.globals()) {
      if (g->storage == StoragePolicy::ThreadLocal) continue;
      std::string lbl = ".Lg_" + g->name;
      if (g->type.kind == Type::Kind::List) {
        emitRuntimeCall("hphl_list_new");
        emitText("movq %rax, " + lbl + "(%rip)");
      } else if (g->type.kind == Type::Kind::Map) {
        int tag = 0;
        if (g->type.elem && g->type.elem->kind == Type::Kind::String) tag = 1;
        else if (g->type.elem && g->type.elem->kind == Type::Kind::Bool) tag = 2;
        else if (g->type.elem && g->type.elem->kind == Type::Kind::Char) tag = 3;
        emitText("movq $" + std::to_string(tag) + ", %rcx");
        emitRuntimeCall("hphl_map_new");
        emitText("movq %rax, " + lbl + "(%rip)");
      }
    }
    // M31: globals string (escalar + arrays) como roots permanentes do GC.
    emitGlobalStringRoots();
  }
  text_ << body;

  // ---- 4) retorno ----
  emitText("jmp .Lret_" + label);
  emitText(".Lret_" + label + ":");
  if (debug_) emitDebugEpilogue();
  // M10.2: liberaÃ§Ã£o por polÃ­tica (arena Ã© liberada em bloco no fim)
  // M17 Fase 1: remove roots antes de liberar (M29 FIX: batch via offsets)
  {
    std::vector<int> slots;
    for (auto &hs : heapSlots_) {
      if (hs.policy == StoragePolicy::Shared || hs.policy == StoragePolicy::Heap) {
        slots.push_back(hs.slot);
      }
    }
    for (int s : gcRefSlots_) {
      slots.push_back(s);
    }
    if (!slots.empty()) {
      std::string offLbl = ".LgcOffs_" + label + "_rm";
      emitData(offLbl + ":");
      for (int s : slots) {
        std::string r = slotRef(s);
        std::string num = r.substr(0, r.find('('));
        emitData("  .quad " + num);
      }
      emitText("movq $" + std::to_string(slots.size()) + ", %rcx");
      emitRuntimeCall("hphl_gc_pop_roots");
    }
  }
  for (auto it = heapSlots_.rbegin(); it != heapSlots_.rend(); ++it) {
    // M20.1.3 4.3: destrutor de classe libera list/map fields
    if (it->policy == StoragePolicy::Shared && !it->typeName.empty()) {
      emitText("movq " + slotRef(it->slot) + ", %r12");
      emitText("testq %r12, %r12");
      std::string skip = newLabel("class_dtor_null_skip");
      emitText("je " + skip);
      genClassDestructor(it->typeName);
      emitText(skip + ":");
    }
    emitText("movq " + slotRef(it->slot) + ", %rcx");
    switch (it->policy) {
      case StoragePolicy::Pool:
        emitText("movq $" + std::to_string(it->size) + ", %rdx");
        emitRuntimeCall("hphl_pool_free");
        break;
      case StoragePolicy::Shared:
        emitRuntimeCall("hphl_shared_release");
        break;
      case StoragePolicy::Arena:
        break; // hphl_arena_free_all abaixo
      default:
        emitRuntimeCall("free");
    }
  }
  if (arenaHandleSlot_ >= 0) {
    emitText("movq " + slotRef(arenaHandleSlot_) + ", %rcx");
    emitRuntimeCall("hphl_arena_free_all");
  }
  for (auto it = listSlots_.rbegin(); it != listSlots_.rend(); ++it) {
    // slots de `..resto` podem nÃ£o ter sido criados (padrÃ£o nÃ£o casou): NULL skip
    emitText("movq " + slotRef(*it) + ", %rcx");
    std::string skip = newLabel("list_free_skip");
    emitText("cmpq $0, %rcx");
    emitText("je " + skip);
    emitRuntimeCall("hphl_list_free");
    emitText(skip + ":");
  }
  for (auto it = mapSlots_.rbegin(); it != mapSlots_.rend(); ++it) {
    emitText("movq " + slotRef(*it) + ", %rcx");
    std::string skip = newLabel("map_free_skip");
    emitText("cmpq $0, %rcx");
    emitText("je " + skip);
    emitRuntimeCall("hphl_map_free");
    emitText(skip + ":");
  }
  if (retSlot_ >= 0) {
    if (retIsFloat_) emitText("movsd " + slotRef(retSlot_) + ", %xmm0");
    else emitText("movq " + slotRef(retSlot_) + ", %rax");
  }
  // M13.3: restaura callee-saved (UMA vez â€” bloco duplicado corrompia
  // rbx/r12-r15 do chamador com lixo e desalinhava a pilha)
  {
    std::vector<std::string> pushed;
    for (auto &kv : regForSlot_) {
      if (std::find(pushed.begin(), pushed.end(), kv.second) == pushed.end())
        pushed.push_back(kv.second);
    }
    for (auto it = pushed.rbegin(); it != pushed.rend(); ++it) emitText("popq " + *it);
  }
  if (entry) emitText("xorl %eax, %eax"); // main retorna 0
  emitText("leave");
  // M_RV1 A5: libera globais list/map no epílogo do Main (lê do label .Lg_NAME)
  if (fn->isEntryPoint) {
    for (auto* g : sem_.globals()) {
      if (g->storage == StoragePolicy::ThreadLocal) continue;
      std::string lbl = ".Lg_" + g->name;
      if (g->type.kind == Type::Kind::List) {
        emitText("movq " + lbl + "(%rip), %rcx");
        std::string skip = newLabel("global_list_free_skip");
        emitText("testq %rcx, %rcx");
        emitText("je " + skip);
        emitRuntimeCall("hphl_list_free");
        emitText(skip + ":");
      } else if (g->type.kind == Type::Kind::Map) {
        emitText("movq " + lbl + "(%rip), %rcx");
        std::string skip = newLabel("global_map_free_skip");
        emitText("testq %rcx, %rcx");
        emitText("je " + skip);
        emitRuntimeCall("hphl_map_free");
        emitText(skip + ":");
      }
    }
  }
  // M19 1.4 trigger condicional: só em Main chama GC
  if (fn->isEntryPoint) {
    emitRuntimeCall("hphl_gc");
  }
  // M31: Main sempre retorna 0 (ver paridade em expr_hir.cpp).
  if (fn->isEntryPoint) emitText("xorl %eax, %eax");
  emitText("ret");

  curFn_ = nullptr;
  if (debug_) dbgEndFunction();
}

void Codegen::genGlobal(VarDecl* v) {
  std::string lbl = ".Lg_" + v->name;
  if (v->storage == StoragePolicy::ThreadLocal) return; // sem slot em .data
  // M20-A 4.4: registrar global no debug metadata
  if (debug_) dbgAddGlobal(v->name, v->type, lbl);
  if (!v->init) {
    if (v->type.kind == Type::Kind::Array) {
      emitData(lbl + ": .zero " + std::to_string(typeSize(v->type)));
    } else {
      emitData(lbl + ": .quad 0");
    }
    return;
  }
  if (v->init->kind == ExprKind::ArrayLit) {
    std::vector<std::string> vals;
    collectArrayValues(v->init.get(), vals);
    std::string line = lbl + ": .quad";
    for (auto& val : vals) line += " " + val + ",";
    line.pop_back();
    emitData(line);
    return;
  }
  switch (v->init->kind) {
    case ExprKind::IntLit:
      emitData(lbl + ": .quad " + std::to_string(static_cast<IntLitExpr*>(v->init.get())->value));
      break;
    case ExprKind::CharLit:
      emitData(lbl + ": .quad " + std::to_string(static_cast<CharLitExpr*>(v->init.get())->value));
      break;
    case ExprKind::BoolLit:
      emitData(lbl + ": .quad " +
               std::string(static_cast<BoolLitExpr*>(v->init.get())->value ? "1" : "0"));
      break;
    case ExprKind::FloatLit: {
      double vd = static_cast<FloatLitExpr*>(v->init.get())->value;
      uint64_t bits;
      std::memcpy(&bits, &vd, 8);
      emitData(lbl + ": .quad " + std::to_string((long long)bits));
      break;
    }
    case ExprKind::StringLit: {
      std::string strLbl = internString(static_cast<StringLitExpr*>(v->init.get())->value);
      emitData(lbl + ": .quad " + strLbl);
      break;
    }
    case ExprKind::Member: {
      auto me = static_cast<MemberExpr*>(v->init.get());
      // enum const (Estado.Pausado) e arr.Length: valor constante resolvido
      if (me->isEnumConst || me->isArrayLength)
        emitData(lbl + ": .quad " + std::to_string(me->enumValue));
      else
        emitData(lbl + ": .quad 0");
      break;
    }
    case ExprKind::Unary: {
      auto ue = static_cast<UnaryExpr*>(v->init.get());
      if (ue->op == UnOp::Neg && ue->operand->kind == ExprKind::IntLit) {
        emitData(lbl + ": .quad " + std::to_string(-static_cast<IntLitExpr*>(ue->operand.get())->value));
      } else if (ue->op == UnOp::BitNot && ue->operand->kind == ExprKind::IntLit) {
        emitData(lbl + ": .quad " + std::to_string(~static_cast<IntLitExpr*>(ue->operand.get())->value));
      } else {
        emitData(lbl + ": .quad 0");
      }
      break;
    }
    default:
      emitData(lbl + ": .quad 0");
      break;
  }
}




} // namespace hphl