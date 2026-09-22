// Auto-generated from codegen.cpp
#include "codegen.h"
#include <cstdio>
#include <functional>
#include "stdlib/stdbuiltins.h"
#include <cstdint>
#include <cstring>
static int wb_counter_hir = 0;

/* M27: hphl_set_class_desc — called at startup to register class bitmaps. */
extern "C" int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);

// Espelho exato da semântica do codegen AST sobre os nós desaçucarados.

namespace hphl {
void Codegen::genFunctionHir(FunctionDecl* fn, HirBlock* body) {
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
  addrTakenVars_.clear();
  if (debug_) dbgStartFunction(fn);

  std::string label = fnLabel(fn);
  bool entry = fn->isEntryPoint;

  // ---- 1) corpo em buffer temporário (para conhecer o frame) ----
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
      emitText("movq %rcx, " + slotRef(thisSlot));
    }
  }
  taskEnvSlot_ = -1;
  taskResSlot_ = -1;
  lambdaEnvSlot_ = -1;
  if (fn->isTask || fn->isAsync) {
    taskEnvSlot_ = nextSlot_++;
    emitText("movq %rcx, " + slotRef(taskEnvSlot_));
    taskResSlot_ = nextSlot_++;
    emitText("movq %rdx, " + slotRef(taskResSlot_));
  }
  // v0.95 (lambdas): env (capturas por valor) chega em rcx, como `this`;
  // os parâmetros de usuário deslocam +1 (rdx, r8, r9, ...)
  if (fn->isLambda) {
    lambdaEnvSlot_ = nextSlot_++;
    emitText("movq %rcx, " + slotRef(lambdaEnvSlot_));
  }
  for (size_t i = 0; i < fn->params.size(); i++) {
    Local l;
    l.type = fn->params[i]->type;
    // A9 (async array): param array guarda o PONTEIRO da base (slot único).
    // Sync recebe o endereço do caller; async recebe ponteiro p/ cópia no
    // env. Indexação via genHirAddr/emitArrayBaseLoad (policy-aware).
    if (l.type.kind == Type::Kind::Array) l.policy = StoragePolicy::Heap;
    l.slot = nextSlot_++;
    l.byRef = fn->params[i]->isByRef();
    if (!l.byRef) tryAllocReg(l.slot, l.type, StoragePolicy::Stack, false); // M14.4: regalloc ON
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
    int k = (int)i + (thisSlot >= 0 ? 1 : 0) + (lambdaEnvSlot_ >= 0 ? 1 : 0);
    if (l.byRef) {
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
      if (isFloatType(l.type)) {
        emitText("movsd " + std::to_string(48 + 8 * (k - 4)) + "(%rbp), %xmm0");
        emitText("movsd %xmm0, " + slotRef(l.slot));
      } else {
        emitText("movq " + std::to_string(48 + 8 * (k - 4)) + "(%rbp), %rax");
        emitText("movq %rax, " + slotRef(l.slot));
      }
    }
  }

  // M28 32.1: TCO body label (HIR path) — após salvar parâmetros.
  emitText(".L_" + fnLabel(fn) + "_body:");

  if (fn->isTask) {
    for (auto& cap : fn->taskCaptures) {
      Local l;
      l.type = cap.second;
      l.slot = nextSlot_++;
      if (cap.second.kind == Type::Kind::Array)
        l.policy = StoragePolicy::Heap;
      scopes_.back()[cap.first] = l;
      emitText("movq " + slotRef(taskEnvSlot_) + ", %rax");
      emitText("movq " + std::to_string(8 * ((size_t)(&cap - fn->taskCaptures.data()))) +
                   "(%rax), %r10");
      emitText("movq %r10, " + slotRef(l.slot));
    }
  }
  // v0.95 (lambdas): capturas por valor do env (bits; floats viajam como i64)
  if (fn->isLambda) {
    for (auto& cap : fn->taskCaptures) {
      Local l;
      l.type = cap.second;
      l.slot = nextSlot_++;
      scopes_.back()[cap.first] = l;
      emitText("movq " + slotRef(lambdaEnvSlot_) + ", %rax");
      emitText("movq " + std::to_string(8 * ((size_t)(&cap - fn->taskCaptures.data()))) +
                   "(%rax), %r10");
      emitText("movq %r10, " + slotRef(l.slot));
    }
  }

  if (body) {
    hirMarkAddrTaken(body);
    scopes_.emplace_back();
    for (auto& s : body->stmts) genHirStmt(s.get());
    scopes_.pop_back();
  }
  scopes_.pop_back();

  if (fn->isEntryPoint && programParallel_)
    emitRuntimeCall("hphl_join_tasks");

  std::string bodyAsm = text_.str();
  text_.swap(bodyBuf);

  // ---- 2) prólogo com frame final ----
  int frame = ((nextSlot_ * 8) + 8 + 15) & ~15;  // reduce +32 -> +8
  {
    // M14.4: alinhamento de pilha — nº ímpar de callee-saved deixaria todas
    // as calls com rsp≡8 (mod 16): crash movaps dentro do CRT
    int nregs = 0;
    for (auto &kv : regForSlot_) (void)kv, ++nregs;
    std::vector<std::string> seen;
    for (auto &kv : regForSlot_)
      if (std::find(seen.begin(), seen.end(), kv.second) == seen.end())
        seen.push_back(kv.second);
    if (seen.size() % 2 == 1) frame += 8;
    (void)nregs;
  }
  emitText("\n# ===== função " + fn->name + " =====");
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
  // M29 FIX: batch via hphl_gc_add_roots_batch.
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
      std::string lbl = ".LgcRoots_" + label;
      // Root registration runs before bodyAsm saves parameters; preserve
      // all Windows x64 integer and floating-point argument registers.
      emitText("subq $64, %rsp");
      emitText("movq %rcx, 0(%rsp)");
      emitText("movq %rdx, 8(%rsp)");
      emitText("movq %r8, 16(%rsp)");
      emitText("movq %r9, 24(%rsp)");
      emitText("movsd %xmm0, 32(%rsp)");
      emitText("movsd %xmm1, 40(%rsp)");
      emitText("movsd %xmm2, 48(%rsp)");
      emitText("movsd %xmm3, 56(%rsp)");
      // offsets em .data � os endere�os efetivos (%rbp+offset) s� existem em
      // runtime, ent�o vamos computar via lea%rip no prologo e armazenar no
      // array .data din�mico via hphl_gc_register_slots.
      //
      // Mais simples: emitimos uma chamada batch que recebe (base=rbp, offsets[])
      // e calcula os pointers via %rbp+off.
      std::string offLbl = ".LgcOffs_" + label;
      emitData(offLbl + ":");
      for (int s : slots) {
        // slotRef retorna "-N(%rbp)" � extra�mos o N
        std::string r = slotRef(s);
        // r is like "-24(%rbp)". parse.
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
      (void)lbl;  // unused now
    }
  }
  // M13.3: salva callee-saved alocados (após subq para não deslocar slots)
  {
    std::vector<std::string> pushed;
    for (auto &kv : regForSlot_) {
      if (std::find(pushed.begin(), pushed.end(), kv.second) == pushed.end())
        pushed.push_back(kv.second);
    }
    for (auto &r : pushed) emitText("pushq " + r);
  }
  // M28 32.1: body label já foi emitido antes dos add_roots.
  for (int s : listSlots_) emitText("movq $0, " + slotRef(s));
  if (arenaHandleSlot_ >= 0) emitText("movq $0, " + slotRef(arenaHandleSlot_));
  if (debug_) emitDebugPrologue();

  // ---- 3) corpo ----
  if (fn->isEntryPoint && tlsSizeBytes_ > 0) {
    emitText("movl $" + std::to_string(tlsSizeBytes_) + ", %ecx");
    emitRuntimeCall("hphl_tls_setup");
  }
  // M_RV1 A5: inicializa globais list/map no Main (caminho HIR).
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
  text_ << bodyAsm;

  // ---- 4) retorno ----
  emitText("jmp .Lret_" + label);
  emitText(".Lret_" + label + ":");
  if (debug_) emitDebugEpilogue();
  // M29 FIX: remove GC roots antes de liberar (batch via offsets).
  // Sem isso, gc_roots cresce monotonicamente a cada chamada recursiva,
  // // fazendo gc_root_count * sizeof(void*) explodir (5M nodes � 4 roots
  // // = 160MB s� de tabela de roots) e causar stack overflow por
  // // exaust�o de page file.
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
  // M10.2: liberação por política (arena é liberada em bloco no fim)
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
  // M13.3: restaura callee-saved
  {
    std::vector<std::string> pushed;
    for (auto &kv : regForSlot_) {
      if (std::find(pushed.begin(), pushed.end(), kv.second) == pushed.end())
        pushed.push_back(kv.second);
    }
    for (auto it = pushed.rbegin(); it != pushed.rend(); ++it) emitText("popq " + *it);
  }
if (entry) emitText("xorl %eax, %eax");
  // M19 1.4 trigger condicional HIR: só Main
  if (entry) {
    emitRuntimeCall("hphl_gc");
  }
  // M_RV1 A5: libera globais list/map no epílogo do Main (caminho HIR).
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
  // M31: Main sempre retorna 0 (o hphl_gc do epílogo devolve freed count em
  // %rax; sem zerar, o exit code do processo vira lixo).
  if (fn->isEntryPoint) emitText("xorl %eax, %eax");
  emitText("leave");
  emitText("ret");

  curFn_ = nullptr;
  if (debug_) dbgEndFunction();
}

void Codegen::genHirStmt(HirStmt* s) {
  // debugger (M7): trap por linha executável (line 0 = stmt sintético; Block
  // não tem execução própria — seus statements trapam)
  if (s->kind != HirStmtKind::Block && s->line > 0 &&
      s->line != lastTrapLine_) {
    emitText("# [line " + std::to_string(s->line) + "]");
    if (debug_) {
      emitDebugTrap(s->line);
    }
    lastTrapLine_ = s->line;
  }
  switch (s->kind) {
    case HirStmtKind::Block: {
      auto* b = static_cast<HirBlock*>(s);
      if (b->scope) scopes_.emplace_back();
      for (auto& inner : b->stmts) genHirStmt(inner.get());
      if (b->scope) scopes_.pop_back();
      break;
    }
    case HirStmtKind::If: {
      auto st = static_cast<HirIf*>(s);
      std::string elseL = newLabel("if_else");
      std::string endL = newLabel("if_end");
      genHirCond(st->cond.get(), elseL);
      if (st->thenBranch) genHirStmt(st->thenBranch.get());
      if (st->elseBranch) {
        emitText("jmp " + endL);
        emitText(elseL + ":");
        genHirStmt(st->elseBranch.get());
        emitText(endL + ":");
      } else {
        emitText(elseL + ":");
      }
      break;
    }
    case HirStmtKind::For: {
      auto st = static_cast<HirFor*>(s);
      std::string condL = newLabel("for_cond");
      std::string bodyL = newLabel("for_body");
      std::string stepL = newLabel("for_step");
      std::string endL = newLabel("for_end");
      loopStack_.push_back({endL, stepL});
      // M13.3 P8: extrai bound do loop para elidir bounds check + tracking de vars de indução
      std::string loopVar; long long loopBound = -1;
      bool hasLoopBound = extractLoopBound(st, loopVar, loopBound);
      bool pushedBound = false;
      long long prevBound = -1;
      std::string loopVarForSkip;
      if (st->init && st->init->kind == HirStmtKind::VarDecl) {
        loopVarForSkip = static_cast<HirVarDecl*>(st->init.get())->name;
      } else if (st->init && st->init->kind == HirStmtKind::ExprStmt) {
        auto* es = static_cast<HirExprStmt*>(st->init.get());
        if (es->expr && es->expr->kind == HirExprKind::Assign) {
          auto* a = static_cast<HirAssign*>(es->expr.get());
          if (a->target->kind == HirExprKind::Var) loopVarForSkip = static_cast<HirVar*>(a->target.get())->name;
        }
      } else if (hasLoopBound) {
        loopVarForSkip = loopVar;
      }
      bool pushedLoopVar = false;
      if (!loopVarForSkip.empty() && loopVarNames_.count(loopVarForSkip)==0) {
        loopVarNames_.insert(loopVarForSkip);
        pushedLoopVar = true;
      }
      if (hasLoopBound) {
        auto it = loopUpperBounds_.find(loopVar);
        if (it != loopUpperBounds_.end()) prevBound = it->second;
        loopUpperBounds_[loopVar] = loopBound;
        // loopVarNames already handled above, but ensure
        if (!loopVarForSkip.empty() && loopVarForSkip != loopVar) loopVarNames_.insert(loopVar);
        else if (loopVarForSkip.empty()) loopVarNames_.insert(loopVar);
        pushedBound = true;
      }
      scopes_.emplace_back();
      if (st->init) genHirStmt(st->init.get());
      emitText("jmp " + condL);
      emitText(bodyL + ":");
      scopes_.emplace_back();
      if (st->body) {
        for (auto& inner : st->body->stmts) genHirStmt(inner.get());
      }
      scopes_.pop_back();
      emitText(stepL + ":");
      if (st->step) genHirExpr(st->step.get());
      emitText(condL + ":");
      if (st->cond) {
        genHirCondTrue(st->cond.get(), bodyL);
      } else {
        emitText("jmp " + bodyL);
      }
      emitText(endL + ":");
      scopes_.pop_back();
      loopStack_.pop_back();
      if (pushedBound) {
        if (prevBound >= 0) loopUpperBounds_[loopVar] = prevBound;
        else loopUpperBounds_.erase(loopVar);
      }
      if (pushedLoopVar) loopVarNames_.erase(loopVarForSkip);
      else if (hasLoopBound && loopVarNames_.count(loopVar)) {
        // if hasLoopBound but loopVarForSkip was same, already handled
        // ensure we don't double erase if loopVarForSkip == loopVar
        if (loopVarForSkip != loopVar) loopVarNames_.erase(loopVar);
      }
      break;
    }
    case HirStmtKind::While: {
      auto st = static_cast<HirWhile*>(s);
      std::string condL = newLabel("while_cond");
      std::string bodyL = newLabel("while_body");
      std::string endL = newLabel("while_end");
      loopStack_.push_back({endL, condL});
      emitText("jmp " + condL);
      emitText(bodyL + ":");
      scopes_.emplace_back();
      if (st->body) {
        for (auto& inner : st->body->stmts) genHirStmt(inner.get());
      }
      scopes_.pop_back();
      emitText(condL + ":");
      genHirCondTrue(st->cond.get(), bodyL);
      emitText(endL + ":");
      loopStack_.pop_back();
      break;
    }
    case HirStmtKind::DoWhile: {
      auto st = static_cast<HirDoWhile*>(s);
      std::string bodyL = newLabel("dowhile_body");
      std::string condL = newLabel("dowhile_cond");
      std::string endL = newLabel("dowhile_end");
      loopStack_.push_back({endL, condL});
      emitText(bodyL + ":");
      scopes_.emplace_back();
      if (st->body) {
        for (auto& inner : st->body->stmts) genHirStmt(inner.get());
      }
      scopes_.pop_back();
      emitText(condL + ":");
      genHirCondTrue(st->cond.get(), bodyL);
      emitText(endL + ":");
      loopStack_.pop_back();
      break;
    }
    case HirStmtKind::Break: {
      if (!loopStack_.empty()) emitText("jmp " + loopStack_.back().breakLabel);
      break;
    }
    case HirStmtKind::Continue: {
      if (!loopStack_.empty()) emitText("jmp " + loopStack_.back().continueLabel);
      break;
    }
    case HirStmtKind::Return: {
      auto st = static_cast<HirReturn*>(s);
      if (curFn_->isTask || curFn_->isAsync) {
        if (st->value) {
          genHirExpr(st->value.get());
          emitText("movq " + slotRef(taskResSlot_) + ", %r10");
          if (isFloatType(st->value->type)) {
            emitText("movsd %xmm0, (%r10)");
          } else {
            emitText("movq %rax, (%r10)");
          }
        }
        emitText("jmp .Lret_" + fnLabel(curFn_));
        break;
      }
      // M28 32.1: TCO self-tail-call (caminho HIR)
      if (st->value && st->value->kind == HirExprKind::Call &&
          canSelfTailCallHir(static_cast<const HirCall*>(st->value.get()))) {
        emitSelfTailCallHir(static_cast<const HirCall*>(st->value.get()));
        break;
      }
      if (st->value) {
        genHirExpr(st->value.get());
        if (curFn_->hasReturnType) {
          if (isStructType(st->value->type))
            genStructCopy(st->value->type);
          if (st->value->kind == HirExprKind::Var &&
              !isStructType(st->value->type)) {
            auto* l = findLocal(static_cast<HirVar*>(st->value.get())->name);
            // M10.1b: tuple local retornado escapa como objeto/list
            bool isTuple = st->value->type.kind == Type::Kind::Tuple;
            if (l && !isTuple) releaseEscapeSlot(l->slot);
          }
          if (retSlot_ < 0) {
            retSlot_ = nextSlot_++;
            retIsFloat_ = isFloatType(st->value->type);
          }
          if (retIsFloat_) emitText("movsd %xmm0, " + slotRef(retSlot_));
          else emitText("movq %rax, " + slotRef(retSlot_));
        }
      }
      emitText("jmp .Lret_" + fnLabel(curFn_));
      break;
    }
    case HirStmtKind::Panic: {
      auto st = static_cast<HirPanic*>(s);
      genHirExpr(st->message.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_panic");
      break;
    }
    case HirStmtKind::Assert: {
      auto st = static_cast<HirAssert*>(s);
      std::string okL = newLabel("assert_ok");
      genHirCondTrue(st->cond.get(), okL);
      std::string msg = internString("assert falhou (linha " +
                                     std::to_string(st->line) + ")");
      emitText("xorl %ecx, %ecx");
      emitText("leaq " + msg + "(%rip), %rdx");
      emitRuntimeCall("hphl_assert");
      emitText(okL + ":");
      break;
    }
    case HirStmtKind::Throw: {
      auto st = static_cast<HirThrow*>(s);
      needExc_ = true;
      std::string okL = newLabel("throw_ok");
      genHirExpr(st->value.get());
      emitText("movq hphl_exc_hdl(%rip), %rcx");
      emitText("testq %rcx, %rcx");
      emitText("jne " + okL);
      std::string msg = internString("throw sem 'catch' ativo");
      emitText("leaq " + msg + "(%rip), %rcx");
      emitRuntimeCall("hphl_panic");
      emitText(okL + ":");
      if (isFloatType(st->value->type)) emitText("movq %xmm0, 32(%rcx)");
      else emitText("movq %rax, 32(%rcx)");
      emitText("movq 16(%rcx), %rbp");
      emitText("movq 8(%rcx), %rsp");
      emitText("jmpq *0(%rcx)");
      break;
    }
    case HirStmtKind::Try: {
      auto st = static_cast<HirTry*>(s);
      needExc_ = true;
      std::string hL = newLabel("try_h");
      std::string endL = newLabel("try_end");
      int base = nextSlot_;
      nextSlot_ += 5;
      std::string rec = "-" + std::to_string(8 * (base + 5)) + "(%rbp)";

      emitText("leaq " + rec + ", %rax");
      emitText("movq %rsp, 8(%rax)");
      emitText("movq %rbp, 16(%rax)");
      emitText("leaq " + hL + "(%rip), %rcx");
      emitText("movq %rcx, 0(%rax)");
      emitText("movq hphl_exc_hdl(%rip), %rcx");
      emitText("movq %rcx, 24(%rax)");
      emitText("movq %rax, hphl_exc_hdl(%rip)");

      for (auto& inner : st->body->stmts) genHirStmt(inner.get());

      emitText("movq hphl_exc_hdl(%rip), %rax");
      emitText("movq 24(%rax), %rcx");
      emitText("movq %rcx, hphl_exc_hdl(%rip)");
      emitText("jmp " + endL);

      emitText(hL + ":");
      emitText("movq hphl_exc_hdl(%rip), %rax");
      emitText("movq 24(%rax), %rcx");
      emitText("movq %rcx, hphl_exc_hdl(%rip)");
      if (st->hasCatch) {
        int slot = nextSlot_++;
        if (isFloatType(st->catchType)) {
          emitText("movq 32(%rax), %xmm0");
          emitText("movsd %xmm0, " + slotRef(slot));
        } else {
          emitText("movq 32(%rax), %rcx");
          emitText("movq %rcx, " + slotRef(slot));
        }
        scopes_.emplace_back();
        Local l;
        l.type = st->catchType;
        l.slot = slot;
        scopes_.back()[st->catchVar] = l;
        for (auto& inner : st->catchBody->stmts) genHirStmt(inner.get());
        scopes_.pop_back();
      }
      emitText(endL + ":");
      break;
    }
    case HirStmtKind::Lock: {
      auto st = static_cast<HirLock*>(s);
      genHirExpr(st->target.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_lock_begin");
      scopes_.emplace_back();
      for (auto& inner : st->body->stmts) genHirStmt(inner.get());
      scopes_.pop_back();
      genHirExpr(st->target.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_lock_end");
      break;
    }
    case HirStmtKind::Spawn: {
      auto st = static_cast<HirSpawn*>(s);
      programParallel_ = true;
      FunctionDecl* fn = makeHirTaskFunction(std::move(st->body), st->captures, "");
      emitSpawnSite(fn, st->captures);
      break;
    }
    case HirStmtKind::Parallel: {
      auto st = static_cast<HirParallel*>(s);
      programParallel_ = true;
      if (st->isDeterministic) {
        for (size_t i = 0; i < st->parts.size(); i++) {
          scopes_.emplace_back();
          for (auto& inner : st->parts[i]->stmts) genHirStmt(inner.get());
          scopes_.pop_back();
        }
        break;
      }
      bool nested = curFn_ && (curFn_->isTask || curFn_->isAsync);
      if (nested) emitRuntimeCall("hphl_region_begin");
      const std::vector<std::pair<std::string, Type>> semCaps;
      for (size_t i = 0; i < st->parts.size(); i++) {
        const auto& caps =
            i < st->partCaptures.size() ? st->partCaptures[i] : semCaps;
        FunctionDecl* fn = makeHirTaskFunction(std::move(st->parts[i]), caps, "");
        emitSpawnSite(fn, caps);
      }
      emitRuntimeCall(nested ? "hphl_region_join" : "hphl_join_tasks");
      break;
    }
    case HirStmtKind::ParallelForeach: {
      genHirParallelForeach(static_cast<HirParallelForeach*>(s));
      break;
    }
    case HirStmtKind::ExprStmt: {
      genHirExpr(static_cast<HirExprStmt*>(s)->expr.get());
      break;
    }
    case HirStmtKind::VarDecl: {
      genHirVarDecl(static_cast<HirVarDecl*>(s));
      break;
    }
  }
}

void Codegen::genHirVarDecl(HirVarDecl* v) {
  Local l;
  l.type = v->type;
  l.policy = v->storage;
  l.slot = nextSlot_++;
  l.atomic = v->atomic;
  // M14.4: regalloc de locais DESATIVADO — leaq de slot reg-backed (++/--,
  // ref/out, endereços) quebrava com `operand type mismatch for lea` em 9
  // testes da suíte x64. Infra (tryAllocReg/slotRef) mantida para retomada
  // com liveness próprio; variáveis voltam 100% para slots na frame.
  if (!v->atomic && !v->opaqueSlot && loopVarNames_.count(v->name)==0 &&
      addrTakenVars_.count(v->name)==0) {
    tryAllocReg(l.slot, l.type, l.policy, false);
  }

  if (l.type.kind == Type::Kind::Array && !isPointerPolicy(v->storage)) {
    int slots = (typeSize(l.type) + 7) / 8;
    nextSlot_ += slots - 1;
    l.slot += slots - 1;
  }
  scopes_.back()[v->name] = l;
  if (debug_ && !v->opaqueSlot)
    dbgAddLocal(v->name, l.type, l.slot, false, false,
                l.type.kind == Type::Kind::Array && !isPointerPolicy(v->storage));

  if (v->opaqueSlot) {
    // temp de lowering (foreach): o slot guarda o valor avaliado uma vez —
    // sem alocação e sem registro de free (itens de classe são empréstimos)
    if (v->init) {
      genHirExpr(v->init.get());
      if (isFloatType(v->type)) {
        if (!isFloatType(v->init->type))
          emitText("cvtsi2sdq %rax, %xmm0");
        emitText("movsd %xmm0, " + slotRef(l.slot));
      } else {
        emitText("movq %rax, " + slotRef(l.slot));
      }
    }
    return;
  }

  if (l.type.kind == Type::Kind::Array) {
    if (isPointerPolicy(v->storage)) {
      genPolicyAlloc(v->storage, typeSize(l.type)); // M10.2: por política
      emitText("movq %rax, " + slotRef(l.slot));
      heapSlots_.push_back({l.slot, v->storage, typeSize(l.type), l.type.kind==Type::Kind::Class?l.type.name:""});
      gcRefSlots_.push_back(l.slot);  // M28 28.1
    } else if (!v->init) {
      // v0.46: array stack nasce ZERADO (antes ficava com lixo da frame)
      long long zq = (typeSize(l.type) + 7) / 8;
      emitText("leaq " + slotRefMem(l.slot) + ", %rdi");
      emitText("xorl %eax, %eax");
      emitText("movq $" + std::to_string(zq) + ", %rcx");
      emitText("rep stosq");
    }
    if (v->init && v->init->kind == HirExprKind::ArrayLit) {
      if (l.type.kind == Type::Kind::List) {
        // B10: `list<T> xs = {e1, e2, ...}` — cria list e faz Add
        auto* al = static_cast<HirArrayLit*>(v->init.get());
        for (auto& el : al->elements) {
          if (el->kind == HirExprKind::ArrayLit && l.type.elem &&
              l.type.elem->kind == Type::Kind::List) {
            // aninhado: list<list<T>> = {{...}, {...}}
            auto* inner = static_cast<HirArrayLit*>(el.get());
            int subSlot = nextSlot_++;
            emitRuntimeCall("hphl_list_new");
            emitText("movq %rax, " + slotRef(subSlot));
            for (auto& e2 : inner->elements) {
              emitText("subq $16, %rsp");
              emitText("movq " + slotRef(subSlot) + ", (%rsp)");
              genHirExpr(e2.get());
              emitText("movq %rax, 8(%rsp)");
              emitText("movq (%rsp), %rcx");
              emitText("movq 8(%rsp), %rdx");
              emitRuntimeCall("hphl_list_add_i");
              emitText("addq $16, %rsp");
            }
            // adiciona sub-lista na lista externa
            emitText("subq $16, %rsp");
            emitText("movq " + slotRef(l.slot) + ", (%rsp)");
            emitText("movq " + slotRef(subSlot) + ", %rax");
            emitText("movq %rax, 8(%rsp)");
            emitText("movq (%rsp), %rcx");
            emitText("movq 8(%rsp), %rdx");
            emitRuntimeCall("hphl_list_add_i");
            emitText("addq $16, %rsp");
          } else {
            emitText("subq $16, %rsp");
            emitText("movq " + slotRef(l.slot) + ", (%rsp)");
            genHirExpr(el.get());
            if (isFloatType(el->type)) {
              emitText("movsd %xmm0, 8(%rsp)");
              emitText("movq (%rsp), %rcx");
              emitText("movsd 8(%rsp), %xmm1");
              emitRuntimeCall("hphl_list_add_f");
            } else {
              emitText("movq %rax, 8(%rsp)");
              emitText("movq (%rsp), %rcx");
              emitText("movq 8(%rsp), %rdx");
              emitRuntimeCall("hphl_list_add_i");
            }
            emitText("addq $16, %rsp");
          }
        }
      } else {
        genHirStoreArrayLit(l, l.type, static_cast<HirArrayLit*>(v->init.get()), 0);
      }
    } else if (v->init) {
      // cópia de bloco (temp de foreach etc.): origem em rax após genHirExpr
      genHirExpr(v->init.get());
      emitText("movq %rax, %rcx");
      if (isPointerPolicy(v->storage)) {
        emitText("movq " + slotRef(l.slot) + ", %rdx");
      } else {
        emitText("leaq " + slotRefMem(l.slot) + ", %rdx");
      }
      long long qwords = (typeSize(l.type) + 7) / 8;
      emitText("movq $" + std::to_string(qwords) + ", %r8");
      std::string cpL = newLabel("acp");
      emitText(cpL + ":");
      emitText("movq (%rcx), %r10");
      emitText("addq $8, %rcx");
      emitText("movq %r10, (%rdx)");
      emitText("addq $8, %rdx");
      emitText("decq %r8");
      emitText("jne " + cpL);
    }
    return;
  }

  if (l.type.kind == Type::Kind::List) {
    // M_RV1 IMG: init com valor (ex.: retorno de função) — avalia e guarda
    // o handle em vez de criar lista vazia (ArrayLit é rejeitado na semântica)
    if (v->init) {
      genHirExpr(v->init.get());
      emitText("movq %rax, " + slotRef(l.slot));
    } else {
      emitRuntimeCall("hphl_list_new");
      emitText("movq %rax, " + slotRef(l.slot));
    }
    listSlots_.push_back(l.slot);
    gcRefSlots_.push_back(l.slot);  // M28 28.1
    return;
  }

  if (l.type.kind == Type::Kind::Map) {
    int tag = 0;
    if (l.type.elem && l.type.elem->kind == Type::Kind::String) tag = 1;
    else if (l.type.elem && l.type.elem->kind == Type::Kind::Bool) tag = 2;
    else if (l.type.elem && l.type.elem->kind == Type::Kind::Char) tag = 3;
    emitText("movq $" + std::to_string(tag) + ", %rcx");
    emitRuntimeCall("hphl_map_new");
    emitText("movq %rax, " + slotRef(l.slot));
    mapSlots_.push_back(l.slot);
    gcRefSlots_.push_back(l.slot);  // M28 28.1 (paridade com stmt_ast.cpp:852)
    return;
  }

  if (l.type.kind == Type::Kind::Tuple) {
    // M10.1b: tuple local = handle de bloco heap; literal já aloca, origem
    // não-fresh clona (semântica de valor)
    int n = (int)l.type.tupleElems.size();
    if (v->init) {
      genHirExpr(v->init.get());
      if (v->init->kind != HirExprKind::TupleLit) {
        emitText("movq %rax, %rcx");
        emitText("movq $" + std::to_string(n) + ", %rdx");
        emitRuntimeCall("hphl_tuple_clone");
      }
    } else {
      emitText("movq $" + std::to_string(n) + ", %rcx");
      emitRuntimeCall("hphl_tuple_new");
    }
    emitText("movq %rax, " + slotRef(l.slot));
    heapSlots_.push_back({l.slot, StoragePolicy::Heap, 0, ""}); // tuple
    gcRefSlots_.push_back(l.slot);  // M28 28.1 (paridade com stmt_ast.cpp:932)
    return;
  }

  if (l.type.kind == Type::Kind::Mutex || l.type.kind == Type::Kind::Semaphore ||
      l.type.kind == Type::Kind::Event || l.type.kind == Type::Kind::Barrier) {
    if (l.type.kind == Type::Kind::Semaphore) {
      emitText("movq $" + std::to_string(v->primitiveInit) + ", %rcx");
      emitRuntimeCall("hphl_semaphore_new");
    } else if (l.type.kind == Type::Kind::Barrier) {
      emitText("movq $" + std::to_string(v->primitiveInit) + ", %rcx");
      emitRuntimeCall("hphl_barrier_new");
    } else if (l.type.kind == Type::Kind::Mutex) {
      emitRuntimeCall("hphl_mutex_new");
    } else {
      emitRuntimeCall("hphl_event_new");
    }
    emitText("movq %rax, " + slotRef(l.slot));
    return;
  }

  if (l.type.kind == Type::Kind::Class) {
    if (v->init) {
      genHirExpr(v->init.get());
      if (isStructType(l.type) && !hirIsFreshAlloc(v->init.get()))
        genStructCopy(l.type);
    } else if (isInterfaceType(l.type)) {
      // A2 (interface como tipo): sem init nasce null — interface nao tem
      // ctor nem objeto default; `.Lvt_<iface>` sequer existe
      emitText("xorl %eax, %eax");
    } else {
      genPolicyAlloc(v->storage, classSize(l.type.name)); // M10.2: por política
      auto cit = sem_.classes().find(l.type.name);
      if (cit != sem_.classes().end() && cit->second.hasVptr) {
        emitText("leaq .Lvt_" + l.type.name + "(%rip), %r10");
        emitText("movq %r10, (%rax)"); // M10: vptr
      }
      emitText("movq %rax, %r10"); // preserve object pointer
      genInitClassFields(l.type.name);
      emitText("movq %r10, %rax"); // restore object pointer
    }
    emitText("movq %rax, " + slotRef(l.slot));
    // M10: alias de outra variável classe N�O registra segunda liberação
    // M14.4: auto-free só para políticas com posse explícita (ver AST path)
    // M28 28.4: class-typed local SEMPRE entra em gcRefSlots_ para o GC
    // tracing, mesmo que o auto-free não se aplique (storage=Heap). Caso
    // contrário, o GC não vê o ponteiro e libera o objeto durante o sweep.
    gcRefSlots_.push_back(l.slot);
    if (v->storage == StoragePolicy::Arena || v->storage == StoragePolicy::Pool ||
        v->storage == StoragePolicy::Shared)
      heapSlots_.push_back({l.slot, v->storage, classSize(l.type.name), l.type.name});
    return;
  }

  if (isPointerPolicy(v->storage)) {
    genPolicyAlloc(v->storage, 8); // M10.2: por política
    emitText("movq %rax, " + slotRef(l.slot));
    heapSlots_.push_back({l.slot, v->storage, 8, ""});
    gcRefSlots_.push_back(l.slot);  // M28 28.1
    if (v->init) {
      genHirExpr(v->init.get());
      emitText("movq " + slotRef(l.slot) + ", %rcx");
      if (isFloatType(l.type)) {
        if (!isFloatType(v->init->type))
          emitText("cvtsi2sdq %rax, %xmm0");
        emitText("movsd %xmm0, (%rcx)");
      } else {
        emitText("movq %rax, (%rcx)");
      }
    }
    return;
  }

  if (v->init) {
    genHirExpr(v->init.get());
    if (isFloatType(l.type)) {
      // M11-bench: init inteiro em double — converte antes de gravar
      if (!isFloatType(v->init->type))
        emitText("cvtsi2sdq %rax, %xmm0");
      emitText("movsd %xmm0, " + slotRef(l.slot));
    } else {
      emitText("movq %rax, " + slotRef(l.slot));
    }
  }
  // M31: local string é root do GC (strings são GC-gerenciadas; sem isso o
  // sweep libera strings vivas referenciadas só por slots).
  if (l.type.kind == Type::Kind::String) {
    gcRefSlots_.push_back(l.slot);
  }
}

void Codegen::genHirExpr(HirExpr* e) {
  switch (e->kind) {
    case HirExprKind::IntLit: {
      auto ex = static_cast<HirIntLit*>(e);
      emitText("movq $" + std::to_string(ex->value) + ", %rax");
      break;
    }
    case HirExprKind::FloatLit: {
      auto ex = static_cast<HirFloatLit*>(e);
      double v = ex->value;
      uint64_t bits;
      std::memcpy(&bits, &v, 8);
      std::string lbl = newLabel("fld");
      emitRodata(".align 8");
      emitRodata(lbl + ": .quad " + std::to_string((long long)bits));
      emitText("movsd " + lbl + "(%rip), %xmm0");
      break;
    }
    case HirExprKind::StringLit: {
      auto ex = static_cast<HirStringLit*>(e);
      std::string lbl = internString(ex->value);
      emitText("leaq " + lbl + "(%rip), %rax");
      break;
    }
    case HirExprKind::CharLit: {
      auto ex = static_cast<HirCharLit*>(e);
      emitText("movq $" + std::to_string(ex->value) + ", %rax");
      break;
    }
    case HirExprKind::BoolLit: {
      auto ex = static_cast<HirBoolLit*>(e);
      emitText("movq $" + std::string(ex->value ? "1" : "0") + ", %rax");
      break;
    }
    case HirExprKind::NullLit: {
      emitText("xorl %eax, %eax");
      break;
    }
    case HirExprKind::This: {
      auto* l = findLocal("this");
      if (l) emitText("movq " + slotRef(l->slot) + ", %rax");
      else emitText("xorl %eax, %eax");
      break;
    }
    case HirExprKind::Var: {
      auto ex = static_cast<HirVar*>(e);
      // M10 (v0.45): parâmetro de valor da instância genérica → literal
      if (!curFn_->litParams.empty()) {
        auto lit = curFn_->litParams.find(ex->name);
        if (lit != curFn_->litParams.end()) {
          emitText("movq $" + std::to_string(lit->second) + ", %rax");
          break;
        }
      }
      auto* l = findLocal(ex->name);
      if (!l && ex->isProperty) {
        std::vector<HirExpr*> noargs;
        // M_RV1 A4: propriedade estatica nao recebe `this`
        if (ex->propGet->isStatic) {
          genHirCallInternal(ex->propGet, noargs, nullptr);
        } else {
          HirThis th;
          genHirCallInternal(ex->propGet, noargs, &th);
        }
        break;
      }
      if (!l && ex->isThisField) {
        auto* th = findLocal("this");
        int off = (int)ex->thisFieldOffset;
        if (off < 0) off = 0;
        if (th) {
          emitText("movq " + slotRef(th->slot) + ", %rcx");
          if (isFloatType(ex->type)) {
            if (off == 0) emitText("movsd (%rcx), %xmm0");
            else emitText("movsd " + std::to_string(off) + "(%rcx), %xmm0");
          } else {
            if (off == 0) emitText("movq (%rcx), %rax");
            else emitText("movq " + std::to_string(off) + "(%rcx), %rax");
          }
        } else {
          emitText("xorl %eax, %eax");
        }
        break;
      }
      if (ex->isConst) {
        emitText("movq $" + std::to_string(ex->constValue) + ", %rax");
        break;
      }
      if (!l) { emitText("xorl %eax, %eax"); break; }
      if (l->type.kind == Type::Kind::Array) {
        emitArrayBaseLoad(*l);
        break;
      }
      if (l->type.kind == Type::Kind::List) {
        if (l->isGlobal) {
          emitText("movq " + l->globalLabel + "(%rip), %rax");
        } else {
          emitText("movq " + slotRef(l->slot) + ", %rax");
        }
        break;
      }
      if (l->isGlobal) {
        if (l->tlsOffset >= 0) {
          emitRuntimeCall("hphl_tls_block");
          if (isFloatType(l->type)) {
            emitText("movsd " + std::to_string(l->tlsOffset) + "(%rax), %xmm0");
          } else {
            emitText("movq " + std::to_string(l->tlsOffset) + "(%rax), %rax");
          }
        } else if (isFloatType(l->type)) {
          emitText("movsd " + l->globalLabel + "(%rip), %xmm0");
        } else {
          emitText("movq " + l->globalLabel + "(%rip), %rax");
        }
      } else if (l->byRef) {
        emitText("movq " + slotRef(l->slot) + ", %rcx");
        if (isFloatType(l->type)) {
          emitText("movsd (%rcx), %xmm0");
        } else {
          emitText("movq (%rcx), %rax");
        }
      } else if (isPointerPolicy(l->policy)) {
        emitText("movq " + slotRef(l->slot) + ", %rcx");
        if (l->type.kind == Type::Kind::Class || l->type.kind == Type::Kind::List ||
            l->type.kind == Type::Kind::Map || l->type.kind == Type::Kind::Tuple ||
            l->type.kind == Type::Kind::Array) {
          // v0.47 FIX: sem deref extra (ver nota no genExpr Ident)
          emitText("movq %rcx, %rax");
        } else if (isFloatType(l->type)) {
          emitText("movsd (%rcx), %xmm0");
        } else {
          emitText("movq (%rcx), %rax");
        }
      } else {
        if (isFloatType(l->type)) {
          emitText("movsd " + slotRef(l->slot) + ", %xmm0");
        } else {
          emitText("movq " + slotRef(l->slot) + ", %rax");
        }
      }
      break;
    }
    case HirExprKind::Member: {
      auto ex = static_cast<HirMember*>(e);
      if (ex->isProperty) {
        std::vector<HirExpr*> noargs;
        // M_RV1 A4: propriedade estatica nao recebe `this`
        HirExpr* thisArg = ex->propGet->isStatic ? nullptr : ex->object.get();
        genHirCallInternal(ex->propGet, noargs, thisArg);
        break;
      }
      if (ex->isEnumCtor) {
        std::vector<HirExpr*> noargs;
        genHirEnumCtor(ex->enumCtorEnum, ex->enumCtorIndex, noargs);
        break;
      }
      if (ex->isEnumConst) {
        emitText("movq $" + std::to_string(ex->enumValue) + ", %rax");
        break;
      }
      if (ex->isModuleTypeRef) {
        emitText("xorl %eax, %eax");
        break;
      }
      if (ex->isGlobalRef) {
        auto tit = tlsOffsets_.find(ex->resolvedGlobal->name);
        if (tit != tlsOffsets_.end()) {
          emitRuntimeCall("hphl_tls_block");
          emitText("movq " + std::to_string(tit->second) + "(%rax), %rax");
        } else {
          emitText("movq .Lg_" + ex->resolvedGlobal->name + "(%rip), %rax");
        }
        break;
      }
      if (ex->isTaskCancelled) {
        emitRuntimeCall("hphl_task_iscancelled");
        break;
      }
      if (ex->isArrayLength) {
        emitText("movq $" + std::to_string(ex->arraySize) + ", %rax");
        break;
      }
      if (ex->isListLength) {
        genHirExpr(ex->object.get());
        emitText("movq %rax, %rcx");
        emitRuntimeCall("hphl_list_len");
        break;
      }
      if (ex->isMapLength) {
        genHirExpr(ex->object.get());
        emitText("movq %rax, %rcx");
        emitRuntimeCall("hphl_map_len");
        break;
      }
      if (ex->isResultIsOk) {
        genHirExpr(ex->object.get());
        emitText("cmpq $0, (%rax)");
        emitText("sete %al");
        emitText("movzbq %al, %rax");
        break;
      }
      if (ex->isResultIsError) {
        genHirExpr(ex->object.get());
        emitText("cmpq $1, (%rax)");
        emitText("sete %al");
        emitText("movzbq %al, %rax");
        break;
      }
      if (ex->isOptionHasValue) {
        genHirExpr(ex->object.get());
        emitText("cmpq $1, (%rax)");
        emitText("sete %al");
        emitText("movzbq %al, %rax");
        break;
      }
      if (ex->isOptionIsNone) {
        genHirExpr(ex->object.get());
        emitText("cmpq $0, (%rax)");
        emitText("sete %al");
        emitText("movzbq %al, %rax");
        break;
      }
      if (ex->isResultValue) {
        genHirExpr(ex->object.get());
        std::string okL = newLabel("res_val_ok");
        emitText("cmpq $0, (%rax)");
        emitText("je " + okL);
        std::string msg = internString("Result.Value called on Err");
        emitText("leaq " + msg + "(%rip), %rcx");
        emitRuntimeCall("hphl_panic");
        emitText(okL + ":");
        if (isFloatType(ex->type)) emitText("movsd 8(%rax), %xmm0");
        else emitText("movq 8(%rax), %rax");
        break;
      }
      if (ex->isResultError) {
        genHirExpr(ex->object.get());
        std::string okL = newLabel("res_err_ok");
        emitText("cmpq $1, (%rax)");
        emitText("je " + okL);
        std::string msg = internString("Result.Error called on Ok");
        emitText("leaq " + msg + "(%rip), %rcx");
        emitRuntimeCall("hphl_panic");
        emitText(okL + ":");
        if (isFloatType(ex->type)) emitText("movsd 8(%rax), %xmm0");
        else emitText("movq 8(%rax), %rax");
        break;
      }
      if (ex->isOptionValue) {
        genHirExpr(ex->object.get());
        std::string okL = newLabel("opt_val_ok");
        emitText("cmpq $1, (%rax)");
        emitText("je " + okL);
        std::string msg = internString("Option.Value called on None");
        emitText("leaq " + msg + "(%rip), %rcx");
        emitRuntimeCall("hphl_panic");
        emitText(okL + ":");
        if (isFloatType(ex->type)) emitText("movsd 8(%rax), %xmm0");
        else emitText("movq 8(%rax), %rax");
        break;
      }
      if (ex->type.kind == Type::Kind::Array) {
        genHirExpr(ex->object.get());
        emitText("movq %rax, %rdx");
        if (ex->fieldOffset != 0) {
          emitText("addq $" + std::to_string(ex->fieldOffset) + ", %rdx");
        }
        emitText("movq %rdx, %rax");
        break;
      }
      genHirExpr(ex->object.get());
      emitText("movq %rax, %rdx");
      if (isFloatType(ex->type)) {
        emitText("movsd " + std::to_string(ex->fieldOffset) + "(%rdx), %xmm0");
      } else {
        emitText("movq " + std::to_string(ex->fieldOffset) + "(%rdx), %rax");
      }
      break;
    }
    case HirExprKind::Call: {
      // v0.46: chamada `compiletime` dobrada na semântica
      if (static_cast<HirCall*>(e)->folded) {
        emitText("movq $" +
                 std::to_string(static_cast<HirCall*>(e)->foldValue) + ", %rax");
        break;
      }
      genHirCall(static_cast<HirCall*>(e));
      break;
    }
    case HirExprKind::Index: {
      auto ex = static_cast<HirIndex*>(e);
      if (ex->object->type.kind == Type::Kind::String) {
        // s[i] — retorna o char na posição i (bug 1.8). Spill do objeto:
        // a avaliação do índice pode usar rcx (ex.: campo do this) e
        // destruiria o ponteiro da string.
        emitText("subq $16, %rsp");
        genHirExpr(ex->object.get());
        emitText("movq %rax, (%rsp)");
        genHirExpr(ex->index.get());
        emitText("movq %rax, %rdx");
        emitText("movq (%rsp), %rcx");
        emitText("addq $16, %rsp");
        emitRuntimeCall("hphl_str_char_index");
        break;
      }
      if (ex->isListBuffer) {
        // foreach: o objeto já é a chamada hphl_list_data (buffer em rax);
        // o laço garante o limite, sem bounds check
        int stride = typeSize(ex->type);
        emitText("subq $16, %rsp");
        genHirExpr(ex->object.get());
        emitText("movq %rax, (%rsp)");
        genHirExpr(ex->index.get());
        emitText("movq %rax, %rcx");
        emitText("movq (%rsp), %rax");
        emitText("imulq $" + std::to_string(stride) + ", %rcx, %rcx");
        emitText("addq %rcx, %rax");
        emitText("addq $16, %rsp");
        if (ex->type.kind == Type::Kind::Array) break;
        if (isFloatType(ex->type)) {
          emitText("movsd (%rax), %xmm0");
        } else {
          emitText("movq (%rax), %rax");
        }
        break;
      }
      if (ex->object->type.kind == Type::Kind::List) {
        genHirListIndexAddr(ex->object.get(), ex->index.get());
        if (ex->type.kind == Type::Kind::Array) break;
        if (isFloatType(ex->type)) {
          emitText("movsd (%rax), %xmm0");
        } else {
          emitText("movq (%rax), %rax");
        }
        break;
      }
      int stride = typeSize(*ex->object->type.elem);
      genHirAddr(ex->object.get());
      emitText("subq $16, %rsp");
      emitText("movq %rax, (%rsp)");
      genHirExpr(ex->index.get());
      emitBoundsCheck(ex->object->type.arraySize);
      emitText("imulq $" + std::to_string(stride) + ", %rax, %rax");
      emitText("addq (%rsp), %rax");
      emitText("addq $16, %rsp");
      if (ex->type.kind == Type::Kind::Array) break;
      if (isFloatType(ex->type)) {
        emitText("movsd (%rax), %xmm0");
      } else {
        emitText("movq (%rax), %rax");
      }
      break;
    }
    case HirExprKind::ArrayLit:
      emitText("xorl %eax, %eax");
      break;
    case HirExprKind::TupleLit:
      genHirTupleLit(static_cast<HirTupleLit*>(e));
      break;
    case HirExprKind::Binary:
      genHirBinary(static_cast<HirBinary*>(e));
      break;
    case HirExprKind::Unary:
      genHirUnary(static_cast<HirUnary*>(e));
      break;
    case HirExprKind::Assign: {
      auto ex = static_cast<HirAssign*>(e);
      if (ex->target->kind == HirExprKind::Var &&
          static_cast<HirVar*>(ex->target.get())->isProperty) {
        auto id = static_cast<HirVar*>(ex->target.get());
        std::vector<HirExpr*> args;
        args.push_back(ex->value.get());
        // M_RV1 A4: propriedade estatica nao recebe `this`
        if (id->propSet->isStatic) {
          genHirCallInternal(id->propSet, args, nullptr);
        } else {
          HirThis th;
          genHirCallInternal(id->propSet, args, &th);
        }
        break;
      }
      if (ex->target->kind == HirExprKind::Member &&
          static_cast<HirMember*>(ex->target.get())->isProperty) {
        auto m = static_cast<HirMember*>(ex->target.get());
        std::vector<HirExpr*> args;
        args.push_back(ex->value.get());
        HirExpr* thisArg = m->propSet->isStatic ? nullptr : m->object.get();
        genHirCallInternal(m->propSet, args, thisArg);
        break;
      }
      bool tAtomic = false;
      if (ex->target->kind == HirExprKind::Var)
        tAtomic = static_cast<HirVar*>(ex->target.get())->atomic;
      else if (ex->target->kind == HirExprKind::Member)
        tAtomic = static_cast<HirMember*>(ex->target.get())->fieldAtomic;
      if (tAtomic) {
        if (ex->op == AssignOp::Plain) {
          int t = nextSlot_++;
          genHirExpr(ex->value.get());
          emitText("movq %rax, " + slotRef(t));
          genHirAddr(ex->target.get());
          emitText("movq " + slotRef(t) + ", %rdx");
          emitText("xchgq %rdx, (%rax)");
          emitText("movq %rdx, %rax");
        } else {
          genHirAddr(ex->target.get());
          emitText("subq $16, %rsp");
          emitText("movq %rax, 8(%rsp)");
          genHirExpr(ex->value.get());
          if (ex->op == AssignOp::Sub) emitText("negq %rax");
          emitText("movq %rax, %rcx");
          emitText("movq 8(%rsp), %r8");
          emitText("lock xaddq %rax, (%r8)");
          emitText("addq %rcx, %rax");
          emitText("addq $16, %rsp");
        }
        break;
      }
      // M10.4: MOVE entre referências politizadas (`b = a`) — copia o
      // PONTEIRO do slot da origem para o slot do destino e remove o slot
      // da origem da limpeza do epílogo (evita double free por aliasing);
      // uso posterior da origem é rejeitado na análise semântica
      if (ex->op == AssignOp::Plain && ex->target->kind == HirExprKind::Var &&
          ex->value->kind == HirExprKind::Var) {
        auto* tgtId = static_cast<HirVar*>(ex->target.get());
        auto* srcId = static_cast<HirVar*>(ex->value.get());
        Local* tl = findLocal(tgtId->name);
        Local* sl = findLocal(srcId->name);
        auto isRefPtr = [this](const Local* l) {
          if (!l) return false;
          // Class/List/Map SEMPRE alocam internamente no heap (slot guarda
          // ponteiro), mesmo quando a policy declarada é Stack.
          // Array só é "ref pointer" se a policy for pointer-based
          // (heap/arena/pool/shared) — caso contrário o slot contém os
          // elementos INLINE e a atribuição é cópia de bytes (B9).
          if (l->type.kind == Type::Kind::Class ||
              l->type.kind == Type::Kind::List ||
              l->type.kind == Type::Kind::Map) return true;
          if (l->type.kind == Type::Kind::Array) {
            return isPointerPolicy(l->policy);
          }
          return isPointerPolicy(l->policy);
        };
        if (tl && sl && isRefPtr(tl) && isRefPtr(sl) &&
            tl->slot != sl->slot) {
          genHirExpr(ex->value.get()); // rax = bloco da origem
          emitText("movq %rax, " + slotRef(tl->slot));
          releaseEscapeSlot(sl->slot);
          break;
        }
      }
      if (ex->op == AssignOp::Plain) {
        // M13.3: target em registrador — sem slot temporário
        if (ex->target->kind == HirExprKind::Var && !isFloatType(ex->type) && !isStructType(ex->type)) {
          auto* vt = static_cast<HirVar*>(ex->target.get());
          Local* tl2 = findLocal(vt->name);
          if (tl2 && isRegSlot(tl2->slot)) {
            genHirExpr(ex->value.get());
            if (isStructType(ex->type)) genStructCopy(ex->type);
            emitText("movq %rax, " + slotRef(tl2->slot));
            break;
          }
        }
        // M10.1b: atribuição a variável tuple clona (semântica de valor);
        // o bloco antigo é liberado na hora (o slot já está em heapSlots_)
        if (ex->type.kind == Type::Kind::Tuple &&
            ex->target->kind == HirExprKind::Var) {
          int n = (int)ex->type.tupleElems.size();
          genHirExpr(ex->value.get());
          emitText("movq %rax, %rcx");
          emitText("movq $" + std::to_string(n) + ", %rdx");
          emitRuntimeCall("hphl_tuple_clone");
          auto* lt = findLocal(static_cast<HirVar*>(ex->target.get())->name);
          if (lt) {
            int tOld = nextSlot_++;
            emitText("movq %rax, " + slotRef(tOld));
            emitText("movq " + slotRef(lt->slot) + ", %rcx");
            emitRuntimeCall("free");
            emitText("movq " + slotRef(tOld) + ", %rax");
            emitText("movq %rax, " + slotRef(lt->slot));
          }
          break;
        }
        // B10: atribuição list = {1, 2, 3} (array literal → list)
        if (ex->type.kind == Type::Kind::List &&
            ex->value->kind == HirExprKind::ArrayLit &&
            ex->target->kind == HirExprKind::Var) {
          auto* al = static_cast<HirArrayLit*>(ex->value.get());
          auto* ll = findLocal(static_cast<HirVar*>(ex->target.get())->name);
          if (ll) {
            int oldSlot = nextSlot_++;
            emitText("movq " + slotRef(ll->slot) + ", %rax");
            emitText("movq %rax, " + slotRef(oldSlot));
            emitRuntimeCall("hphl_list_new");
            int newSlot = nextSlot_++;
            emitText("movq %rax, " + slotRef(newSlot));
            for (auto& el : al->elements) {
              emitText("subq $16, %rsp");
              emitText("movq " + slotRef(newSlot) + ", (%rsp)");
              genHirExpr(el.get());
              if (isFloatType(el->type)) {
                emitText("movsd %xmm0, 8(%rsp)");
                emitText("movq (%rsp), %rcx");
                emitText("movsd 8(%rsp), %xmm1");
                emitRuntimeCall("hphl_list_add_f");
              } else {
                emitText("movq %rax, 8(%rsp)");
                emitText("movq (%rsp), %rcx");
                emitText("movq 8(%rsp), %rdx");
                emitRuntimeCall("hphl_list_add_i");
              }
              emitText("addq $16, %rsp");
            }
            emitText("movq " + slotRef(newSlot) + ", " + slotRef(ll->slot));
            emitText("movq " + slotRef(oldSlot) + ", %rcx");
            emitText("cmpq $0, %rcx");
            std::string skip = newLabel("list_free_skip");
            emitText("je " + skip);
            emitRuntimeCall("hphl_list_free");
            emitText(skip + ":");
          } else {
            genHirExpr(ex->value.get());
          }
          break;
        }
        // B9: cópia de array `a = b` — copia N qwords entre os dois
        if (ex->type.kind == Type::Kind::Array &&
            ex->target->kind == HirExprKind::Var) {
          auto* tl = findLocal(static_cast<HirVar*>(ex->target.get())->name);
          auto* sl = findLocal(static_cast<HirVar*>(ex->value.get())->name);
          if (tl && sl) {
            int nq = (typeSize(ex->type) + 7) / 8;
            int sBase = nextSlot_++;
            int dBase = nextSlot_++;
            // obtém endereço base do source (stack) e do target (stack)
            emitText("leaq " + slotRefMem(sl->slot) + ", %rax");
            emitText("movq %rax, " + slotRef(sBase));
            emitText("leaq " + slotRefMem(tl->slot) + ", %rax");
            emitText("movq %rax, " + slotRef(dBase));
            // rep movsq: copia nq qwords de sBase para dBase
            emitText("movq " + slotRef(sBase) + ", %rsi");
            emitText("movq " + slotRef(dBase) + ", %rdi");
            emitText("movq $" + std::to_string(nq) + ", %rcx");
            emitText("rep movsq");
            break;
          }
        }
        if (isFloatType(ex->type)) {
          genHirExpr(ex->value.get());
          // M11-bench: derrama o valor ANTES de genHirAddr — o cálculo do
          // endereço usa %rax e sobrescrevia o valor (gravava o ENDEREÇO do
          // array como double!)
          if (!isFloatType(ex->value->type))
            emitText("cvtsi2sdq %rax, %xmm0");
          int vt = nextSlot_++;
          emitText("movsd %xmm0, " + slotRef(vt));
          genHirAddr(ex->target.get());
          emitText("movsd " + slotRef(vt) + ", %xmm0");
          emitText("movsd %xmm0, (%rax)");
        } else {
          int t = nextSlot_++;
          genHirExpr(ex->value.get());
          if (isStructType(ex->type))
            genStructCopy(ex->type);
          emitText("movq %rax, " + slotRef(t));
          if (ex->target->kind == HirExprKind::Member) {
            auto* mem = static_cast<HirMember*>(ex->target.get());
            int tb = nextSlot_++;
            genHirExpr(mem->object.get());
            emitText("movq %rax, " + slotRef(tb));
            emitText("movq " + slotRef(tb) + ", %rax");
            if (mem->fieldOffset != 0) emitText("addq $" + std::to_string(mem->fieldOffset) + ", %rax");
            emitText("movq %rax, %r12");
            std::string skip = ".Lwb_skip_" + std::to_string(wb_counter_hir++);
            emitText("movq " + slotRef(tb) + ", %rax");
            emitText("movq -8(%rax), %rcx");
            emitText("bt $32, %rcx");
            emitText("jnc " + skip);
            emitText("bt $36, %rcx");
            emitText("jc " + skip);
            emitText("movq " + slotRef(tb) + ", %rcx");
            emitText("movq %r12, %rdx");
            emitRuntimeCall("hphl_write_barrier_slow");
            emitText(skip + ":");
            emitText("movq %r12, %rax");
          } else {
            genHirAddr(ex->target.get());
          }
          emitText("movq " + slotRef(t) + ", %rdx");
          emitText("movq %rdx, (%rax)");
          emitText("movq %rdx, %rax");
        }
      } else {
        // M13.3: compound em registrador — sem genHirAddr
        if (ex->target->kind == HirExprKind::Var && !isFloatType(ex->type) && !isStructType(ex->type)) {
          auto* vt = static_cast<HirVar*>(ex->target.get());
          Local* tl2 = findLocal(vt->name);
          if (tl2 && isRegSlot(tl2->slot)) {
            genHirExpr(ex->value.get());
            emitText("movq %rax, %rcx");
            emitText("movq " + slotRef(tl2->slot) + ", %rax");
            switch (ex->op) {
              case AssignOp::Add: emitText("addq %rcx, %rax"); break;
              case AssignOp::Sub: emitText("subq %rcx, %rax"); break;
              case AssignOp::Mul: emitText("imulq %rcx, %rax"); break;
              case AssignOp::Div: emitText("cqto"); emitText("idivq %rcx"); break;
              case AssignOp::Mod: emitText("cqto"); emitText("idivq %rcx"); emitText("movq %rdx, %rax"); break;
              default: break;
            }
            applyOverflowPolicy(ex->type);
            emitText("movq %rax, " + slotRef(tl2->slot));
            break;
          }
        }
        int t = nextSlot_++;
        if (isFloatType(ex->type)) {
          // M11-bench: valor primeiro em temp; genHirAddr usa %rax e
          // sobrescreveria o valor/acc (bug de store em double + TOCTOU de
          // scratch no composto)
          genHirExpr(ex->value.get());
          if (!isFloatType(ex->value->type))
            emitText("cvtsi2sdq %rax, %xmm0");
          emitText("movsd %xmm0, " + slotRef(t));
          genHirAddr(ex->target.get());
          emitText("movsd (%rax), %xmm1");
          emitText("movsd " + slotRef(t) + ", %xmm0");
          switch (ex->op) {
            case AssignOp::Add: emitText("addsd %xmm0, %xmm1"); break;
            case AssignOp::Sub: emitText("subsd %xmm0, %xmm1"); break;
            case AssignOp::Mul: emitText("mulsd %xmm0, %xmm1"); break;
            case AssignOp::Div: emitText("divsd %xmm0, %xmm1"); break;
            default: break;
          }
          emitText("movsd %xmm1, (%rax)");
          emitText("movsd %xmm1, %xmm0");
        } else {
          genHirExpr(ex->value.get());
          emitText("movq %rax, " + slotRef(t));
          if (ex->target->kind == HirExprKind::Member) {
            auto* mem = static_cast<HirMember*>(ex->target.get());
            int tb = nextSlot_++;
            genHirExpr(mem->object.get());
            emitText("movq %rax, " + slotRef(tb));
            emitText("movq " + slotRef(tb) + ", %rax");
            if (mem->fieldOffset != 0) emitText("addq $" + std::to_string(mem->fieldOffset) + ", %rax");
            emitText("movq %rax, %r12");
            std::string skip = ".Lwb_skip_" + std::to_string(wb_counter_hir++);
            emitText("movq " + slotRef(tb) + ", %rax");
            emitText("movq -8(%rax), %rcx");
            emitText("bt $32, %rcx");
            emitText("jnc " + skip);
            emitText("bt $36, %rcx");
            emitText("jc " + skip);
            emitText("movq " + slotRef(tb) + ", %rcx");
            emitText("movq %r12, %rdx");
            emitRuntimeCall("hphl_write_barrier_slow");
            emitText(skip + ":");
            emitText("movq %r12, %rax");
          } else {
            genHirAddr(ex->target.get());
          }
          emitText("movq %rax, %r10");           // endereço preservado
          emitText("movq (%r10), %rcx");         // antigo
          emitText("movq " + slotRef(t) + ", %rdx"); // valor
          emitText("movq %rcx, %rax");
          switch (ex->op) {
            case AssignOp::Add: emitText("addq %rdx, %rax"); break;
            case AssignOp::Sub: emitText("subq %rdx, %rax"); break;
            case AssignOp::Mul: emitText("imulq %rdx, %rax"); break;
            case AssignOp::Div: emitText("cqto"); emitText("idivq %rdx"); break;
            case AssignOp::Mod: emitText("cqto"); emitText("idivq %rdx"); emitText("movq %rdx, %rax"); break;
            default: break;
          }
          applyOverflowPolicy(ex->type);
          emitText("movq %rax, (%r10)");
        }
      }
      break;
    }
    case HirExprKind::Cast:
      genHirCast(static_cast<HirCast*>(e));
      break;
    case HirExprKind::New:
      genHirNew(static_cast<HirNew*>(e));
      break;
    case HirExprKind::CellTag: {
      auto ex = static_cast<HirCellTag*>(e);
      genHirExpr(ex->subject.get());
      emitText("movq (%rax), %rax");
      break;
    }
    case HirExprKind::LoadAt: {
      auto ex = static_cast<HirLoadAt*>(e);
      genHirExpr(ex->subject.get());
      if (isFloatType(ex->type)) {
        emitText("movsd " + std::to_string(ex->offset) + "(%rax), %xmm0");
      } else {
        emitText("movq " + std::to_string(ex->offset) + "(%rax), %rax");
      }
      break;
    }
    case HirExprKind::OptCtor:
      genHirOptCtor(static_cast<HirOptCtor*>(e));
      break;
    case HirExprKind::Spawn:
      genHirSpawn(static_cast<HirSpawnExpr*>(e));
      break;
    case HirExprKind::Await:
      genHirAwait(static_cast<HirAwaitExpr*>(e));
      break;
    case HirExprKind::Lambda:
      genHirLambda(static_cast<HirLambda*>(e));
      break;
  }
}

void Codegen::genHirCond(HirExpr* e, const std::string& falseLabel) {
  genHirExpr(e);
  emitText("testq %rax, %rax");
  emitText("je " + falseLabel);
}

void Codegen::genHirCondTrue(HirExpr* e, const std::string& trueLabel) {
  genHirExpr(e);
  emitText("testq %rax, %rax");
  emitText("jne " + trueLabel);
}

bool Codegen::hirIsAddressable(HirExpr* e) const {
  switch (e->kind) {
    case HirExprKind::Var:
      return !static_cast<HirVar*>(e)->isProperty;
    case HirExprKind::Member: {
      auto* m = static_cast<HirMember*>(e);
      return !m->isProperty && !m->isEnumConst && !m->isArrayLength &&
             !m->isListLength && !m->isModuleTypeRef && !m->isGlobalRef;
    }
    case HirExprKind::Index:
      return true;
    default:
      return false;
  }
}

bool Codegen::hirIsFreshAlloc(HirExpr* e) const {
  if (!e) return false;
  if (e->kind == HirExprKind::New) return true;
  if (e->kind == HirExprKind::Call) {
    auto* c = static_cast<HirCall*>(e);
    return c->resolved && c->resolved->isConstructor;
  }
  return false;
}

// endereço de um lvalue HIR em %rax
void Codegen::genHirAddr(HirExpr* e) {
  switch (e->kind) {
    case HirExprKind::Var: {
      auto ex = static_cast<HirVar*>(e);
      auto* l = findLocal(ex->name);
      if (l && l->byRef) {
        emitText("movq " + slotRef(l->slot) + ", %rax");
      } else if (l && l->isGlobal) {
        if (l->tlsOffset >= 0) {
          emitRuntimeCall("hphl_tls_block");
          if (l->tlsOffset != 0)
            emitText("addq $" + std::to_string(l->tlsOffset) + ", %rax");
        } else {
          emitText("leaq " + l->globalLabel + "(%rip), %rax");
        }
      } else if (l && l->type.kind == Type::Kind::List) {
        emitText("movq " + slotRef(l->slot) + ", %rax");
      } else if (l && isPointerPolicy(l->policy)) {
        emitText("movq " + slotRef(l->slot) + ", %rax");
      } else if (l) {
        emitText("leaq " + slotRefMem(l->slot) + ", %rax");
      } else if (ex->isThisField && findLocal("this")) {
        emitText("movq " + slotRef(findLocal("this")->slot) + ", %rax");
        if (ex->thisFieldOffset != 0)
          emitText("addq $" + std::to_string(ex->thisFieldOffset) + ", %rax");
      } else {
        emitText("xorl %eax, %eax");
      }
      return;
    }
    case HirExprKind::Member: {
      auto ex = static_cast<HirMember*>(e);
      if (ex->isGlobalRef) {
        auto tit = tlsOffsets_.find(ex->resolvedGlobal->name);
        if (tit != tlsOffsets_.end()) {
          emitRuntimeCall("hphl_tls_block");
          if (tit->second != 0)
            emitText("addq $" + std::to_string(tit->second) + ", %rax");
        } else {
          emitText("leaq .Lg_" + ex->resolvedGlobal->name + "(%rip), %rax");
        }
        return;
      }
      if (ex->isModuleTypeRef) {
        emitText("xorl %eax, %eax");
        return;
      }
      genHirExpr(ex->object.get());
      if (ex->fieldOffset != 0) {
        emitText("addq $" + std::to_string(ex->fieldOffset) + ", %rax");
      }
      return;
    }
    case HirExprKind::Index: {
      auto ex = static_cast<HirIndex*>(e);
      if (ex->isListBuffer) {
        int stride = typeSize(ex->type);
        emitText("subq $16, %rsp");
        genHirExpr(ex->object.get());
        emitText("movq %rax, (%rsp)");
        genHirExpr(ex->index.get());
        emitText("movq %rax, %rcx");
        emitText("movq (%rsp), %rax");
        emitText("imulq $" + std::to_string(stride) + ", %rcx, %rcx");
        emitText("addq %rcx, %rax");
        emitText("addq $16, %rsp");
        return;
      }
      if (ex->object->type.kind == Type::Kind::List) {
        genHirListIndexAddr(ex->object.get(), ex->index.get());
        return;
      }
      int stride = typeSize(*ex->object->type.elem);
      genHirAddr(ex->object.get());
      emitText("subq $16, %rsp");
      emitText("movq %rax, (%rsp)");
      genHirExpr(ex->index.get());
      if (!canElideHirBoundsCheck(ex->index.get(), ex->object->type.arraySize))
        emitBoundsCheck(ex->object->type.arraySize);
      emitText("imulq $" + std::to_string(stride) + ", %rax, %rax");
      emitText("addq (%rsp), %rax");
      emitText("addq $16, %rsp");
      return;
    }
    default:
      emitText("xorl %eax, %eax");
  }
}

void Codegen::genHirStoreArrayLit(const Local& l, const Type& arrayType,
                                  HirArrayLit* al, long long off) {
  int elSize = typeSize(*arrayType.elem);
  for (size_t i = 0; i < al->elements.size(); i++) {
    HirExpr* el = al->elements[i].get();
    long long elOff = off + (long long)elSize * (long long)i;
    if (el->kind == HirExprKind::ArrayLit) {
      genHirStoreArrayLit(l, *arrayType.elem, static_cast<HirArrayLit*>(el), elOff);
      continue;
    }
    if (isFloatType(el->type)) {
      genHirExpr(el);
      emitArrayBaseLoad(l);
      emitText("movq %rax, %r10");
      if (elOff) emitText("addq $" + std::to_string(elOff) + ", %r10");
      emitText("movsd %xmm0, (%r10)");
    } else {
      genHirExpr(el);
      emitText("movq %rax, %rdx");
      emitArrayBaseLoad(l);
      emitText("movq %rax, %r10");
      if (elOff) emitText("addq $" + std::to_string(elOff) + ", %r10");
      emitText("movq %rdx, (%r10)");
    }
  }
}

void Codegen::genHirListIndexAddr(HirExpr* object, HirExpr* index) {
  int stride = typeSize(*object->type.elem);
  emitText("subq $16, %rsp");
  genHirExpr(object);
  emitText("movq %rax, (%rsp)");
  genHirExpr(index);
  emitText("movq %rax, 8(%rsp)");
  emitText("movq %rax, %rcx");
  emitText("movq (%rsp), %rdx");
  emitRuntimeCall("hphl_list_check");
  emitText("movq (%rsp), %rcx");
  emitRuntimeCall("hphl_list_data");
  emitText("movq 8(%rsp), %rcx");
  emitText("imulq $" + std::to_string(stride) + ", %rcx, %rcx");
  emitText("addq %rcx, %rax");
  emitText("addq $16, %rsp");
}

void Codegen::genHirCall(HirCall* c) {
  switch (c->kind) {
    case HirCallKind::Print:
      genHirPrint(c);
      return;
    case HirCallKind::ClockNs:
      emitRuntimeCall("hphl_clock_ns");
      return;
    case HirCallKind::ListNew: {
      // B11: list<T>(n) — aloca list com capacidade inicial
      genHirExpr(c->args[0].get()); // capacidade em %rax
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_list_with_cap");
      return;
    }
    case HirCallKind::Sqrt:
      // M11-bench: sqrt(x) — sqrtsd (arg double em xmm0, resultado xmm0)
      genHirExpr(c->args[0].get());
      emitText("sqrtsd %xmm0, %xmm0");
      return;
    case HirCallKind::StrEq:
    case HirCallKind::StrCmp: {
      int h = nextSlot_++;
      genHirExpr(c->args[0].get());
      emitText("movq %rax, " + slotRef(h));
      genHirExpr(c->args[1].get());
      emitText("movq %rax, %rdx");
      emitText("movq " + slotRef(h) + ", %rcx");
      emitRuntimeCall(c->kind == HirCallKind::StrEq ? "hphl_str_eq" : "hphl_str_cmp");
      return;
    }
    case HirCallKind::ToStr: {
      genHirExpr(c->args[0].get());
      Type at = c->args[0]->type;
      if (!isFloatType(at)) {
        emitText("movq %rax, %rcx");
      }
      if (at.kind == Type::Kind::Bool) emitRuntimeCall("hphl_str_from_bool");
      else if (at.kind == Type::Kind::Char) emitRuntimeCall("hphl_str_from_char");
      else if (at.kind == Type::Kind::Float) emitRuntimeCall("hphl_str_from_float");
      else if (at.kind == Type::Kind::UInt) emitRuntimeCall("hphl_str_from_uint");
      else emitRuntimeCall("hphl_str_from_int");
      return;
    }
    case HirCallKind::AddrOf: {
      // FFI v2: addr_of(x) — endereço do lvalue em %rax (ponteiro opaco)
      genHirAddr(c->args[0].get());
      return;
    }
    case HirCallKind::ListAdd: {
      Type et = *c->args[0]->type.elem;
      emitText("subq $16, %rsp");
      genHirExpr(c->args[0].get());
      emitText("movq %rax, (%rsp)");
      genHirExpr(c->args[1].get());
      if (isFloatType(et)) {
        emitText("movsd %xmm0, 8(%rsp)");
        emitText("movq (%rsp), %rcx");
        emitText("movsd 8(%rsp), %xmm1");
        emitRuntimeCall("hphl_list_add_f");
      } else {
        emitText("movq %rax, 8(%rsp)");
        emitText("movq (%rsp), %rcx");
        emitText("movq 8(%rsp), %rdx");
        emitRuntimeCall("hphl_list_add_i");
      }
      emitText("addq $16, %rsp");
      return;
    }
    case HirCallKind::MapPut: {
      // args[0]=map, args[1]=key, args[2]=val
      emitText("subq $32, %rsp");
      genHirExpr(c->args[0].get()); emitText("movq %rax, (%rsp)");
      genHirExpr(c->args[1].get()); emitText("movq %rax, 8(%rsp)");
      genHirExpr(c->args[2].get());
      if (isFloatType(c->args[2]->type)) { emitText("movq %rax, 16(%rsp)"); emitText("movq (%rsp), %rcx"); emitText("movq 8(%rsp), %rdx"); emitText("movq 16(%rsp), %r8"); }
      else { emitText("movq %rax, 16(%rsp)"); emitText("movq (%rsp), %rcx"); emitText("movq 8(%rsp), %rdx"); emitText("movq 16(%rsp), %r8"); }
      emitRuntimeCall("hphl_map_put");
      emitText("addq $32, %rsp");
      return;
    }
    case HirCallKind::MapGet: {
      emitText("subq $16, %rsp");
      genHirExpr(c->args[0].get()); emitText("movq %rax, (%rsp)");
      genHirExpr(c->args[1].get()); emitText("movq %rax, 8(%rsp)");
      emitText("movq (%rsp), %rcx"); emitText("movq 8(%rsp), %rdx");
      emitRuntimeCall("hphl_map_get");
      if (isFloatType(c->type)) emitText("movq %rax, %xmm0");
      emitText("addq $16, %rsp");
      return;
    }
    case HirCallKind::MapContains: {
      emitText("subq $16, %rsp");
      genHirExpr(c->args[0].get()); emitText("movq %rax, (%rsp)");
      genHirExpr(c->args[1].get()); emitText("movq %rax, 8(%rsp)");
      emitText("movq (%rsp), %rcx"); emitText("movq 8(%rsp), %rdx");
      emitRuntimeCall("hphl_map_contains");
      emitText("addq $16, %rsp");
      return;
    }
    case HirCallKind::MapRemove: {
      emitText("subq $16, %rsp");
      genHirExpr(c->args[0].get()); emitText("movq %rax, (%rsp)");
      genHirExpr(c->args[1].get()); emitText("movq %rax, 8(%rsp)");
      emitText("movq (%rsp), %rcx"); emitText("movq 8(%rsp), %rdx");
      emitRuntimeCall("hphl_map_remove");
      emitText("addq $16, %rsp");
      return;
    }
    case HirCallKind::MapClear: {
      genHirExpr(c->args[0].get()); emitText("movq %rax, %rcx"); emitRuntimeCall("hphl_map_clear"); return;
    }
    case HirCallKind::MapLen: {
      genHirExpr(c->args[0].get()); emitText("movq %rax, %rcx"); emitRuntimeCall("hphl_map_len"); return;
    }
    case HirCallKind::ResultIsOk: {
      genHirExpr(c->args[0].get());
      emitText("cmpq $0, (%rax)");
      emitText("sete %al");
      emitText("movzbq %al, %rax");
      return;
    }
    case HirCallKind::ResultIsErr: {
      genHirExpr(c->args[0].get());
      emitText("cmpq $1, (%rax)");
      emitText("sete %al");
      emitText("movzbq %al, %rax");
      return;
    }
    case HirCallKind::OptionIsSome: {
      genHirExpr(c->args[0].get());
      emitText("cmpq $1, (%rax)");
      emitText("sete %al");
      emitText("movzbq %al, %rax");
      return;
    }
    case HirCallKind::OptionIsNone: {
      genHirExpr(c->args[0].get());
      emitText("cmpq $0, (%rax)");
      emitText("sete %al");
      emitText("movzbq %al, %rax");
      return;
    }
    case HirCallKind::ResultUnwrap: {
      genHirExpr(c->args[0].get());
      std::string okL = newLabel("res_unw_ok");
      emitText("cmpq $0, (%rax)");
      emitText("je " + okL);
      std::string msg = internString("unwrap called on Err");
      emitText("leaq " + msg + "(%rip), %rcx");
      emitRuntimeCall("hphl_panic");
      emitText(okL + ":");
      if (isFloatType(c->type)) emitText("movsd 8(%rax), %xmm0");
      else emitText("movq 8(%rax), %rax");
      return;
    }
    case HirCallKind::OptionUnwrap: {
      genHirExpr(c->args[0].get());
      std::string okL = newLabel("opt_unw_ok");
      emitText("cmpq $1, (%rax)");
      emitText("je " + okL);
      std::string msg = internString("unwrap called on None");
      emitText("leaq " + msg + "(%rip), %rcx");
      emitRuntimeCall("hphl_panic");
      emitText(okL + ":");
      if (isFloatType(c->type)) emitText("movsd 8(%rax), %xmm0");
      else emitText("movq 8(%rax), %rax");
      return;
    }
    case HirCallKind::ResultUnwrapErr: {
      genHirExpr(c->args[0].get());
      std::string okL = newLabel("res_unw_err_ok");
      emitText("cmpq $1, (%rax)");
      emitText("je " + okL);
      std::string msg = internString("unwrap_err called on Ok");
      emitText("leaq " + msg + "(%rip), %rcx");
      emitRuntimeCall("hphl_panic");
      emitText(okL + ":");
      if (isFloatType(c->type)) emitText("movsd 8(%rax), %xmm0");
      else emitText("movq 8(%rax), %rax");
      return;
    }
    case HirCallKind::ResultUnwrapOr: {
      int h = nextSlot_++;
      genHirExpr(c->args[0].get());
      emitText("movq %rax, " + slotRef(h));
      emitText("cmpq $0, (%rax)");
      std::string elseL = newLabel("res_unw_or_else");
      std::string endL = newLabel("res_unw_or_end");
      emitText("jne " + elseL);
      emitText("movq " + slotRef(h) + ", %rax");
      if (isFloatType(c->type)) emitText("movsd 8(%rax), %xmm0");
      else emitText("movq 8(%rax), %rax");
      emitText("jmp " + endL);
      emitText(elseL + ":");
      genHirExpr(c->args[1].get());
      emitText(endL + ":");
      return;
    }
    case HirCallKind::OptionUnwrapOr: {
      int h = nextSlot_++;
      genHirExpr(c->args[0].get());
      emitText("movq %rax, " + slotRef(h));
      emitText("cmpq $1, (%rax)");
      std::string elseL = newLabel("opt_unw_or_else");
      std::string endL = newLabel("opt_unw_or_end");
      emitText("jne " + elseL);
      emitText("movq " + slotRef(h) + ", %rax");
      if (isFloatType(c->type)) emitText("movsd 8(%rax), %xmm0");
      else emitText("movq 8(%rax), %rax");
      emitText("jmp " + endL);
      emitText(elseL + ":");
      genHirExpr(c->args[1].get());
      emitText(endL + ":");
      return;
    }
    case HirCallKind::Wait: {
      genHirExpr(c->args[0].get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_wait_task");
      if (isFloatType(c->type)) emitText("movq %rax, %xmm0");
      return;
    }
    case HirCallKind::TaskCancel: {
      genHirExpr(c->args[0].get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_cancel_task");
      return;
    }
    case HirCallKind::ChannelSend: {
      int h = nextSlot_++;
      genHirExpr(c->args[0].get());
      emitText("movq %rax, " + slotRef(h));
      genHirExpr(c->args[1].get());
      if (isFloatType(c->args[1]->type)) emitText("movq %xmm0, %rax");
      emitText("movq %rax, %rdx");
      emitText("movq " + slotRef(h) + ", %rcx");
      emitRuntimeCall("hphl_channel_send");
      return;
    }
    case HirCallKind::ChannelReceive: {
      genHirExpr(c->args[0].get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_channel_receive");
      if (isFloatType(c->type)) emitText("movq %rax, %xmm0");
      return;
    }
    case HirCallKind::PrimOp: {
      genHirExpr(c->args[0].get());
      emitText("movq %rax, %rcx");
      switch ((CallExpr::PrimOp)c->primOp) {
        case CallExpr::PrimOp::MutexLock: emitRuntimeCall("hphl_mutex_lock"); break;
        case CallExpr::PrimOp::MutexUnlock: emitRuntimeCall("hphl_mutex_unlock"); break;
        case CallExpr::PrimOp::SemaphoreWait: emitRuntimeCall("hphl_semaphore_wait"); break;
        case CallExpr::PrimOp::SemaphoreSignal: emitRuntimeCall("hphl_semaphore_signal"); break;
        case CallExpr::PrimOp::EventWait: emitRuntimeCall("hphl_event_wait"); break;
        case CallExpr::PrimOp::EventSet: emitRuntimeCall("hphl_event_set"); break;
        case CallExpr::PrimOp::EventReset: emitRuntimeCall("hphl_event_reset"); break;
        case CallExpr::PrimOp::BarrierWait: emitRuntimeCall("hphl_barrier_wait"); break;
        default: break;
      }
      return;
    }
    case HirCallKind::Async: {
      // F(...) async: env com os parâmetros (por valor) + spawn
      // A3: se for método, `this` é o primeiro arg (c->args[0]) e o slot 0 do env
      // A9 (async array): array é copiado (dados) p/ dentro do env — o slot do
      // param guarda o ponteiro da cópia (lifetime: env é free'd no fim da task)
      FunctionDecl* f = c->resolved;
      bool isMethod = f && f->isMethod;
      size_t envSize = c->args.size();
      if (isMethod && !f->isStatic) {
        // `this` é o primeiro arg; o env terá this + params
      } else {
        // função global: env só com os args
      }
      auto hirArrBytes = [&](const HirExpr* a) -> long long {
        if (!a || a->type.kind != Type::Kind::Array || !a->type.elem) return 0;
        if (a->type.arraySize <= 0) return 0; // dimensão simbólica: sem cópia
        return (long long)typeSize(a->type);
      };
      if (c->args.empty()) {
        emitText("xorq %rdx, %rdx");
      } else {
        int h = nextSlot_++; // slot com o ponteiro do env
        long long dataOff = (long long)c->args.size() * 8;
        long long total = dataOff;
        for (auto& a : c->args) total += hirArrBytes(a.get());
        genCallMalloc(total);
        emitText("movq %rax, " + slotRef(h));
        dataOff = (long long)c->args.size() * 8;
        for (size_t i = 0; i < c->args.size(); i++) {
          long long ab = hirArrBytes(c->args[i].get());
          if (ab > 0) {
            // copia N qwords p/ env+dataOff; env[i] = ponteiro da cópia
            long long nq = ab / 8;
            genHirExpr(c->args[i].get()); // rax = base origem
            emitText("pushq %rsi");
            emitText("pushq %rdi");
            emitText("movq %rax, %rsi");
            emitText("movq " + slotRef(h) + ", %rax");
            emitText("leaq " + std::to_string(dataOff) + "(%rax), %rdi");
            emitText("movq $" + std::to_string(nq) + ", %rcx");
            emitText("rep movsq");
            emitText("popq %rdi");
            emitText("popq %rsi");
            emitText("movq " + slotRef(h) + ", %rax");
            emitText("leaq " + std::to_string(dataOff) + "(%rax), %r10");
            emitText("movq %r10, " + std::to_string(8 * i) + "(%rax)");
            dataOff += ab;
          } else {
            genHirExpr(c->args[i].get());
            if (isFloatType(c->args[i]->type)) emitText("movq %xmm0, %rax");
            emitText("movq %rax, %r10");
            emitText("movq " + slotRef(h) + ", %rax");
            emitText("movq %r10, " + std::to_string(8 * i) + "(%rax)");
          }
        }
        emitText("movq " + slotRef(h) + ", %rdx");
      }
      emitText("leaq " + fnLabel(f) + "(%rip), %rcx");
      emitRuntimeCall("hphl_spawn_task_ex");
      return;
    }
    case HirCallKind::EnumCtor: {
      std::vector<HirExpr*> args;
      for (auto& a : c->args) args.push_back(a.get());
      genHirEnumCtor(c->enumCanon, c->index, args);
      return;
    }
    case HirCallKind::FromInt: {
      genHirExpr(c->args[0].get());
      int vSlot = nextSlot_++;
      emitText("movq %rax, " + slotRef(vSlot));
      genCallCalloc(16);
      int hSlot = nextSlot_++;
      emitText("movq %rax, " + slotRef(hSlot));
      std::string someL = newLabel("fromint_some");
      std::string endL = newLabel("fromint_end");
      emitText("movq " + slotRef(vSlot) + ", %rax");
      EnumDecl* en = sem_.enums().at(c->enumCanon);
      for (auto& e : en->entries) {
        emitText("cmpq $" + std::to_string(e.value) + ", %rax");
        emitText("je " + someL);
      }
      emitText("movq $0, %rax");
      emitText("movq " + slotRef(hSlot) + ", %rcx");
      emitText("movq %rax, (%rcx)");
      emitText("movq " + slotRef(vSlot) + ", %rax");
      emitText("movq %rax, 8(%rcx)");
      emitText("jmp " + endL);
      emitText(someL + ":");
      emitText("movq $1, %rax");
      emitText("movq " + slotRef(hSlot) + ", %rcx");
      emitText("movq %rax, (%rcx)");
      emitText("movq " + slotRef(vSlot) + ", %rax");
      emitText("movq %rax, 8(%rcx)");
      emitText(endL + ":");
      emitText("movq " + slotRef(hSlot) + ", %rax");
      return;
    }
    case HirCallKind::Serialize: {
      genHirExpr(c->args[0].get());
      if (sem_.isRichEnum(c->enumCanon)) emitText("movq (%rax), %rax");
      return;
    }
    case HirCallKind::ActorCall: {
      int h = nextSlot_++;
      genHirExpr(c->args[0].get());
      emitText("movq %rax, " + slotRef(h));
      emitText("movq " + slotRef(h) + ", %rcx");
      emitRuntimeCall("hphl_lock_begin");
      std::vector<HirExpr*> args;
      for (size_t i = 1; i < c->args.size(); i++) args.push_back(c->args[i].get());
      genHirCallInternal(c->resolved, args, c->args[0].get());
      bool rFloat = isFloatType(c->type);
      if (c->type.kind != Type::Kind::Void) {
        int rf = nextSlot_++;
        if (rFloat) emitText("movq %xmm0, " + slotRef(rf));
        else emitText("movq %rax, " + slotRef(rf));
        emitText("movq " + slotRef(h) + ", %rcx");
        emitRuntimeCall("hphl_lock_end");
        if (rFloat) emitText("movq " + slotRef(rf) + ", %xmm0");
        else emitText("movq " + slotRef(rf) + ", %rax");
      } else {
        emitText("movq " + slotRef(h) + ", %rcx");
        emitRuntimeCall("hphl_lock_end");
      }
      return;
    }
    case HirCallKind::Runtime:
      // M12.0: builtin da std? chamada tipada (doubles em xmm posicionais)
      if (!c->runtimeName.empty()) {
        for (auto& sb : stdBuiltinTable())
          if (c->runtimeName == sb.symbol &&
              c->args.size() == sb.params.size()) {
            genHirStdBuiltinCall(c, sb);
            return;
          }
      }
      genHirRuntimeCall(c);
      return;
    case HirCallKind::Indirect:
      genHirIndirectCall(c);
      return;
    case HirCallKind::Normal:
    case HirCallKind::ModuleCall: {
      if (!c->resolved) { emitText("xorl %eax, %eax"); return; }
      std::vector<HirExpr*> args;
      HirExpr* thisArg = nullptr;
      size_t start = (c->resolved->isMethod && !c->resolved->isStatic) ? 1 : 0;
      for (size_t i = start; i < c->args.size(); i++) args.push_back(c->args[i].get());
      if (start) thisArg = c->args[0].get();
      genHirCallInternal(c->resolved, args, thisArg, /*forceStaticHir=*/c->isBaseCall);
      return;
    }
  }
}

void Codegen::genHirRuntimeCall(HirCall* c) {
  // args inteiros (hphl_list_* / hphl_match_fail): spill em shadow/área
  // própria (tolerante a calls aninhadas — cada genHirExpr reserva sua área)
  int total = (int)c->args.size();
  int stackArgs = total > 4 ? total - 4 : 0;
  int bytes = (32 + stackArgs * 8 + 15) & ~15;
  if (total > 0) emitText("subq $" + std::to_string(bytes) + ", %rsp");
  for (size_t i = 0; i < c->args.size(); i++) {
    genHirExpr(c->args[i].get());
    int k = (int)i;
    if (k < 4) {
      emitText("movq %rax, " + std::to_string(8 * k) + "(%rsp)");
    } else {
      emitText("movq %rax, " + std::to_string(32 + 8 * (k - 4)) + "(%rsp)");
    }
  }
  const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
  for (size_t i = 0; i < c->args.size(); i++) {
    int k = (int)i;
    if (k >= 4) continue;
    emitText("movq " + std::to_string(8 * k) + "(%rsp), " + regs[k]);
  }
  emitText("call " + c->runtimeName);
  if (total > 0) emitText("addq $" + std::to_string(bytes) + ", %rsp");
}

// M12.0: chamada tipada de builtin da biblioteca padrão — doubles vão em
// xmm posicionais (Win64), resultado double volta em xmm0
void Codegen::genHirStdBuiltinCall(HirCall* c, const StdBuiltin& sb) {
  int total = (int)c->args.size();
  int stackArgs = total > 4 ? total - 4 : 0;
  int bytes = (32 + stackArgs * 8 + 15) & ~15;
  static const char* iregs[] = {"%rcx", "%rdx", "%r8", "%r9"};
  static const char* xregs[] = {"%xmm0", "%xmm1", "%xmm2", "%xmm3"};
  if (total > 0) emitText("subq $" + std::to_string(bytes) + ", %rsp");
  // 1) avalia cada argumento e derrama na sua posição. M_RV1 F11: argumentos
  // do tipo Ptr (mutex/condvar/atomic) precisam do ENDEREÇO do slot, não
  // do valor — genHirAddr em vez de genHirExpr.
  // FFI v2: se o argumento JÁ é `ptr` (ponteiro opaco) ou `string` (char*),
  // passa o VALOR (by value); só tira endereço de não-ponteiros (legado atomics).
  for (size_t i = 0; i < c->args.size(); i++) {
    int k = (int)i;
    std::string base =
        k < 4 ? std::to_string(8 * k) : std::to_string(32 + 8 * (k - 4));
    bool fp = sb.params[k] == SBType::Float;
    bool isPtr = sb.params[k] == SBType::Ptr;
    Type::Kind ak = c->args[i]->type.kind;
    bool argIsPtr = ak == Type::Kind::Ptr || ak == Type::Kind::String;
    if (isPtr && !argIsPtr) {
      genHirAddr(c->args[i].get());
    } else {
      genHirExpr(c->args[i].get());
      if (fp && !isFloatType(c->args[i]->type))
        emitText("cvtsi2sdq %rax, %xmm0"); // arg int para param double
    }
    emitText(fp ? "movsd %xmm0, " + base + "(%rsp)"
                : "movq %rax, " + base + "(%rsp)");
  }
  // 2) carrega registradores por posição (classes independentes no Win64)
  for (size_t i = 0; i < c->args.size(); i++) {
    int k = (int)i;
    if (k >= 4) continue;
    std::string base = std::to_string(8 * k);
    if (sb.params[k] == SBType::Float)
      emitText("movsd " + base + "(%rsp), " + xregs[k]);
    else
      emitText("movq " + base + "(%rsp), " + iregs[k]);
  }
  emitText("call " + std::string(sb.symbol));
  if (total > 0) emitText("addq $" + std::to_string(bytes) + ", %rsp");
}

void Codegen::genHirPrint(HirCall* c) {
  for (auto& a : c->args) {
    Type t = a->type;
    if (isFloatType(t)) {
      genHirExpr(a.get());
      emitRuntimeCall("hphl_print_float");
    } else if (t.kind == Type::Kind::String) {
      // M5 (v0.36.0): libera apenas strings na heap — concat (Binary) e
      // ToStr. Chamadas que retornam string podem devolver literal
      // estático; free nelas corrompe a heap.
      bool fresh =
          a->kind == HirExprKind::Binary ||
          (a->kind == HirExprKind::Call &&
           static_cast<HirCall*>(a.get())->kind == HirCallKind::ToStr);
      int h = -1;
      if (fresh) {
        h = nextSlot_++;
        genHirExpr(a.get());
        emitText("movq %rax, " + slotRef(h));
      } else {
        genHirExpr(a.get());
      }
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_string");
      if (h >= 0) {
        emitText("movq " + slotRef(h) + ", %rcx");
        emitRuntimeCall("hphl_str_free");
      }
    } else if (t.kind == Type::Kind::Bool) {
      genHirExpr(a.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_bool");
    } else if (t.kind == Type::Kind::Char) {
      genHirExpr(a.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_char");
    } else if (t.kind == Type::Kind::UInt) {
      genHirExpr(a.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_uint");
    } else {
      genHirExpr(a.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_int");
    }
  }
}

void Codegen::genHirCallInternal(FunctionDecl* fn, std::vector<HirExpr*>& args,
                                 HirExpr* thisArg, bool forceStaticHir) {
  std::string label = fnLabel(fn);
  int total = (thisArg ? 1 : 0) + (int)args.size();
  int stackArgs = total > 4 ? total - 4 : 0;
  int bytes = (32 + stackArgs * 8 + 15) & ~15;
  emitText("subq $" + std::to_string(bytes) + ", %rsp");

  if (thisArg) {
    genHirExpr(thisArg);
    emitText("movq %rax, 0(%rsp)");
  }

  for (size_t i = 0; i < args.size(); i++) {
    HirExpr* a = args[i];
    Param* p = (i < fn->params.size()) ? fn->params[i].get() : nullptr;
    bool pf = p && isFloatType(p->type);
    int k = (int)i + (thisArg ? 1 : 0);
    if (p && p->isByRef()) {
      if (p->byIn && !hirIsAddressable(a)) {
        int t = nextSlot_++;
        genHirExpr(a);
        emitText("movq %rax, " + slotRef(t));
        emitText("leaq " + slotRefMem(t) + ", %rax");
      } else {
        genHirAddr(a);
      }
      if (k < 4) {
        emitText("movq %rax, " + std::to_string(8 * k) + "(%rsp)");
      } else {
        emitText("movq %rax, " + std::to_string(32 + 8 * (k - 4)) + "(%rsp)");
      }
      continue;
    }
    genHirExpr(a);
    if (p && isStructType(p->type))
      genStructCopy(p->type);
    if (pf && !isFloatType(a->type))
      emitText("cvtsi2sdq %rax, %xmm0"); // arg int p/ param float (FFI v2 fix)
    if (pf) {
      if (k < 4) {
        emitText("movsd %xmm0, " + std::to_string(8 * k) + "(%rsp)");
      } else {
        emitText("movsd %xmm0, " + std::to_string(32 + 8 * (k - 4)) + "(%rsp)");
      }
    } else {
      if (k < 4) {
        emitText("movq %rax, " + std::to_string(8 * k) + "(%rsp)");
      } else {
        emitText("movq %rax, " + std::to_string(32 + 8 * (k - 4)) + "(%rsp)");
      }
    }
  }

  if (thisArg) emitText("movq 0(%rsp), %rcx");
  const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
  for (size_t i = 0; i < args.size(); i++) {
    int k = (int)i + (thisArg ? 1 : 0);
    if (k >= 4) continue;
    if (i >= fn->params.size()) break;
    if (fn->params[i]->isByRef()) {
      emitText("movq " + std::to_string(8 * k) + "(%rsp), " + regs[k]);
      continue;
    }
    if (isFloatType(fn->params[i]->type)) {
      emitText("movsd " + std::to_string(8 * k) + "(%rsp), %xmm" + std::to_string(k));
    } else {
      emitText("movq " + std::to_string(8 * k) + "(%rsp), " + regs[k]);
    }
  }

  // M10 (v0.44): dispatch virtual no caminho HIR
  // A2 (interface como tipo): receiver interface SEMPRE via vtable (ver calls.cpp)
  Type recvTy = thisArg ? thisArg->type : Type::makeVoid();
  bool recvIface = thisArg && isInterfaceType(recvTy);
  bool virt = thisArg && !forceStaticHir && fn->isMethod && !fn->isConstructor &&
              ((recvTy.kind == Type::Kind::Class && !isStructType(recvTy)) || recvIface);
  int slot = -1;
  if (virt) {
    slot = sem_.vslotOf(fn->name, fn->params.size());
    if (slot < 0 || (!recvIface && !sem_.slotOverridden(slot))) virt = false;
  }
  if (virt) {
    emitText("movq (%rcx), %r10");                    // vptr @0
    emitText("call *" + std::to_string(8 * slot) + "(%r10)");
  } else {
    emitText("call " + label);
  }
  emitText("addq $" + std::to_string(bytes) + ", %rsp");
}

bool canReloadHirExpr(HirExpr* e) {
  return e->kind == HirExprKind::Var || e->kind == HirExprKind::Member ||
         e->kind == HirExprKind::Index || e->kind == HirExprKind::IntLit;
}
void Codegen::genHirBinary(HirBinary* b) {
  bool flt = b->lhs->type.kind == Type::Kind::Float ||
             b->rhs->type.kind == Type::Kind::Float;

  if (b->op >= BinOp::Eq && b->op <= BinOp::Ge) {
    // M14.4: ==/!= entre strings compara CONTEÚDO (hphl_str_eq) — antes
    // comparava ponteiros (funcionava só com literais internados)
    if (b->strEqBin) {
      genHirExpr(b->lhs.get());
      int h = nextSlot_++;
      emitText("movq %rax, " + slotRef(h)); // lhs pode conter concat/call
      genHirExpr(b->rhs.get());
      emitText("movq %rax, %rdx");
      emitText("movq " + slotRef(h) + ", %rcx");
      emitRuntimeCall("hphl_str_eq");
      if (b->op == BinOp::Ne) emitText("xorq $1, %rax");
      return;
    }
    if (b->derivedEq) {
      std::vector<HirExpr*> args = {b->lhs.get(), b->rhs.get()};
      genHirCallInternal(b->derivedEq, args, nullptr);
      if (b->op == BinOp::Eq) {
        emitText("testq %rax, %rax");
        emitText("setne %al");
        emitText("movzbl %al, %eax");
      } else if (b->op == BinOp::Ne) {
        emitText("testq %rax, %rax");
        emitText("sete %al");
        emitText("movzbl %al, %eax");
      }
      return;
    }
    if (b->derivedCmp) {
      std::vector<HirExpr*> args = {b->lhs.get(), b->rhs.get()};
      genHirCallInternal(b->derivedCmp, args, nullptr);
      emitText("cmpq $0, %rax");
      const char* setcc = nullptr;
      switch (b->op) {
        case BinOp::Lt: setcc = "setl"; break;
        case BinOp::Le: setcc = "setle"; break;
        case BinOp::Gt: setcc = "setg"; break;
        case BinOp::Ge: setcc = "setge"; break;
        case BinOp::Eq: setcc = "sete"; break;
        case BinOp::Ne: setcc = "setne"; break;
        default: break;
      }
      emitText(std::string(setcc) + " %al");
      emitText("movzbl %al, %eax");
      return;
    }
    bool richEnum = ((b->lhs->type.kind == Type::Kind::Enum &&
                      b->rhs->type.kind == Type::Kind::Enum &&
                      sem_.isRichEnum(b->lhs->type.name)) ||
                     (b->lhs->type.isOption() && b->rhs->type.isOption()) ||
                     (b->lhs->type.isResult() && b->rhs->type.isResult()));
    if (richEnum) {
      genHirExpr(b->lhs.get());
      emitText("movq (%rax), %rdx");
      genHirExpr(b->rhs.get());
      emitText("movq (%rax), %rax");
      emitText("cmpq %rdx, %rax");
      emitText(b->op == BinOp::Eq ? "sete %al" : "setne %al");
      emitText("movzbl %al, %eax");
      return;
    }
    if (flt) {
      genHirExpr(b->lhs.get());
      if (!hirClobbersXmm1(b->rhs.get())) {
        emitText("movsd %xmm0, %xmm1");
        genHirExpr(b->rhs.get());
      } else {
        emitText("subq $16, %rsp");
        emitText("movsd %xmm0, (%rsp)");
        genHirExpr(b->rhs.get());
        emitText("movsd (%rsp), %xmm1");
        emitText("addq $16, %rsp");
      }
      emitText("comisd %xmm0, %xmm1");
      const char* setcc = nullptr;
      switch (b->op) {
        case BinOp::Eq: setcc = "sete"; break;
        case BinOp::Ne: setcc = "setne"; break;
        case BinOp::Lt: setcc = "setb"; break;
        case BinOp::Le: setcc = "setbe"; break;
        case BinOp::Gt: setcc = "seta"; break;
        case BinOp::Ge: setcc = "setae"; break;
        default: break;
      }
      emitText(std::string(setcc) + " %al");
      emitText("movzbl %al, %eax");
    } else {
      genHirExpr(b->lhs.get());
      if (b->rhs->kind == HirExprKind::IntLit && !rcxStaged_) {
        emitText("movq $" + std::to_string(static_cast<HirIntLit*>(b->rhs.get())->value) + ", %rdx");
      } else if (!hirHasCall(b->rhs.get()) && r10Live_ == 0) {
        // M14.4: r10 s� quando N�O aninhado � rhs com bin�rios internos usa o
        // MESMO padr�o de est�gio e corromperia o lhs aqui (bug do batch)
        emitText("movq %rax, %r10");
        r10Live_ = 1;
        genHirExpr(b->rhs.get());
        r10Live_ = 0;
        emitText("movq %rax, %rdx");
        emitText("movq %r10, %rax");
      } else if (b->lhs.get()->kind == HirExprKind::Var) {
        // P4: lhs Var � reload do slot ap�s rhs (call ou aninhamento r10)
        auto* vt = static_cast<HirVar*>(b->lhs.get());
        auto* l = findLocal(vt->name);
        std::string reloadSlot = l ? std::string(slotRef(l->slot)) : std::string("");
        emitText("subq $16, %rsp");
        emitText("movq %rax, 8(%rsp)");
        genHirExpr(b->rhs.get());
        emitText("movq %rax, %rdx");
        if (!reloadSlot.empty()) {
          emitText("movq " + reloadSlot + ", %rax");
        } else {
          emitText("movq 8(%rsp), %rax");
        }
        emitText("addq $16, %rsp");
      } else {
        emitText("subq $16, %rsp");
        emitText("movq %rax, 8(%rsp)");
        genHirExpr(b->rhs.get());
        emitText("movq %rax, %rdx");
        emitText("movq 8(%rsp), %rax");
        emitText("addq $16, %rsp");
      }
      emitText("cmpq %rdx, %rax");
      bool uns = b->lhs->type.kind == Type::Kind::UInt;
      const char* setcc = nullptr;
      switch (b->op) {
        case BinOp::Eq: setcc = "sete"; break;
        case BinOp::Ne: setcc = "setne"; break;
        case BinOp::Lt: setcc = uns ? "setb" : "setl"; break;
        case BinOp::Le: setcc = uns ? "setbe" : "setle"; break;
        case BinOp::Gt: setcc = uns ? "seta" : "setg"; break;
        case BinOp::Ge: setcc = uns ? "setae" : "setge"; break;
        default: break;
      }
      emitText(std::string(setcc) + " %al");
      emitText("movzbl %al, %eax");
    }
    return;
  }

  if (b->op == BinOp::And || b->op == BinOp::Or) {
    std::string endL = newLabel("logic_end");
    if (b->op == BinOp::And) {
      genHirExpr(b->lhs.get());
      emitText("testq %rax, %rax");
      std::string skipL = newLabel("logic_skip");
      emitText("je " + skipL);
      genHirExpr(b->rhs.get());
      emitText("testq %rax, %rax");
      emitText("je " + skipL);
      emitText("movq $1, %rax");
      emitText("jmp " + endL);
      emitText(skipL + ":");
      emitText("xorl %eax, %eax");
      emitText(endL + ":");
    } else {
      genHirExpr(b->lhs.get());
      emitText("testq %rax, %rax");
      std::string trueL = newLabel("logic_true");
      emitText("jne " + trueL);
      genHirExpr(b->rhs.get());
      emitText("testq %rax, %rax");
      std::string falseL = newLabel("logic_false");
      emitText("je " + falseL);
      emitText(trueL + ":");
      emitText("movq $1, %rax");
      emitText("jmp " + endL);
      emitText(falseL + ":");
      emitText("xorl %eax, %eax");
      emitText(endL + ":");
    }
    return;
  }

  if (flt) {
    genHirExpr(b->lhs.get());
    if (!isFloatType(b->lhs->type)) emitText("cvtsi2sdq %rax, %xmm0");
    if (!hirClobbersXmm1(b->rhs.get())) {
      emitText("movsd %xmm0, %xmm1");
      genHirExpr(b->rhs.get());
      if (!isFloatType(b->rhs->type)) emitText("cvtsi2sdq %rax, %xmm0");
    } else {
      emitText("subq $16, %rsp");
      emitText("movsd %xmm0, (%rsp)");
      genHirExpr(b->rhs.get());
      if (!isFloatType(b->rhs->type)) emitText("cvtsi2sdq %rax, %xmm0");
      emitText("movsd (%rsp), %xmm1");
      emitText("addq $16, %rsp");
    }
    switch (b->op) {
      case BinOp::Add: emitText("addsd %xmm0, %xmm1"); break;
      case BinOp::Sub: emitText("subsd %xmm0, %xmm1"); break;
      case BinOp::Mul: emitText("mulsd %xmm0, %xmm1"); break;
      case BinOp::Div: emitText("divsd %xmm0, %xmm1"); break;
      default: break;
    }
    emitText("movsd %xmm1, %xmm0");
    if (b->type.kind == Type::Kind::Float &&
        (b->type.policy == OverflowPolicy::Checked ||
         b->type.policy == OverflowPolicy::Saturate))
      applyFloatOverflowPolicy(b->type, b->type.bits == 0 ? 64 : b->type.bits);
    return;
  }

  if (b->op == BinOp::Add && b->lhs->type.kind == Type::Kind::String) {
    // M14.4: o lhs vai para SLOT (não %rcx) — avaliar o rhs pode envolver
    // calls (que clobberizam rcx) ou binários aninhados com fast path imediato
    genHirExpr(b->lhs.get());
    int h = nextSlot_++;
    emitText("movq %rax, " + slotRef(h));
    genHirExpr(b->rhs.get());
    emitText("movq %rax, %rdx");
    emitText("movq " + slotRef(h) + ", %rcx");
    emitRuntimeCall("hphl_str_concat");
    return;
  }

  genHirExpr(b->lhs.get());
  if (b->rhs->kind == HirExprKind::IntLit && !rcxStaged_) {
    emitText("movq $" + std::to_string(static_cast<HirIntLit*>(b->rhs.get())->value) + ", %rcx");
  } else if (!hirHasCall(b->rhs.get()) && r10Live_ == 0) {
    // M14.4: r10 s� no n�vel externo (aninhado corromperia o lhs estagiado)
    emitText("movq %rax, %r10");
    r10Live_ = 1;
    genHirExpr(b->rhs.get());
    r10Live_ = 0;
    emitText("movq %rax, %rcx");
    emitText("movq %r10, %rax");
  } else if (b->lhs.get()->kind == HirExprKind::Var) {
    // P4: coalesce shadow - se lhs é Var, não precisa de shadow no stack;
    // salva em rbx (callee-saved) e restaura. Mas para simplificar:
    // vamos apenas manter o shadow existente mas usar reload direto do slot.
    // Na verdade, para P4 simples: quando rhs tem call e lhs é Var,
    // recarrega lhs do slot após rhs em vez de usar shadow na stack.
    auto* vt = static_cast<HirVar*>(b->lhs.get());
    auto* l = findLocal(vt->name);
    std::string reloadSlot = l ? std::string(slotRef(l->slot)) : std::string("");
    emitText("subq $16, %rsp");
    emitText("movq %rax, 8(%rsp)");
    genHirExpr(b->rhs.get());
    emitText("movq %rax, %rcx");
    if (!reloadSlot.empty()) {
      emitText("movq " + reloadSlot + ", %rax");
    } else {
      emitText("movq 8(%rsp), %rax");
    }
    emitText("addq $16, %rsp");
  } else {
    emitText("subq $16, %rsp");
    emitText("movq %rax, 8(%rsp)");
    genHirExpr(b->rhs.get());
    emitText("movq %rax, %rcx");
    emitText("movq 8(%rsp), %rax");
    emitText("addq $16, %rsp");
  }
  switch (b->op) {
    case BinOp::Add: emitText("addq %rcx, %rax"); break;
    case BinOp::Sub: emitText("subq %rcx, %rax"); break;
    case BinOp::Mul: emitText("imulq %rcx, %rax"); break;
    case BinOp::Div: {
      bool uns = b->lhs->type.kind == Type::Kind::UInt;
      // M13.3: otimizacao idiv para divisores potencia de 2 (ex: n/2).
      // sar sozinho faz floor; com bias de (d-1) p/ negativos vira
      // truncamento estilo C (igual a idiv/sdiv dos outros backends).
      if (b->rhs->kind == HirExprKind::IntLit) {
        long long val = static_cast<HirIntLit*>(b->rhs.get())->value;
        if (val > 0 && (val & (val - 1)) == 0) {
          int shift = __builtin_ctzll(val);
          if (uns) emitText("shrq $" + std::to_string(shift) + ", %rax");
          else if (shift == 0) { /* x/1 = x */ }
          else {
            emitText("movq %rax, %rdx");
            emitText("sarq $63, %rdx");
            emitText("shrq $" + std::to_string(64 - shift) + ", %rdx");
            emitText("addq %rdx, %rax");
            emitText("sarq $" + std::to_string(shift) + ", %rax");
          }
          break;
        }
      }
      if (b->type.policy == OverflowPolicy::Checked) {
        emitText("testq %rcx, %rcx");
        std::string ok = newLabel("divz_ok");
        emitText("jne " + ok);
        emitRuntimeCall("hphl_divide_by_zero");
        emitText(ok + ":");
      }
      if (uns) {
        emitText("xorl %edx, %edx");
        emitText("divq %rcx");
      } else {
        emitText("cqto");
        emitText("idivq %rcx");
      }
      break;
    }
    case BinOp::Mod: {
      bool uns = b->lhs->type.kind == Type::Kind::UInt;
      // M13.3: otimizacao idiv para modulo potencia de 2 (ex: n%2).
      // `and` sozinho da resto estilo Python; via quociente truncado
      // (r = x - trunc(x/d)*d) vira resto estilo C como no idiv/srem.
      if (b->rhs->kind == HirExprKind::IntLit) {
        long long val = static_cast<HirIntLit*>(b->rhs.get())->value;
        if (val > 0 && (val & (val - 1)) == 0) {
          int shift = __builtin_ctzll(val);
          if (uns) emitText("andq $" + std::to_string(val - 1) + ", %rax");
          else if (shift == 0) emitText("xorl %eax, %eax");
          else {
            emitText("movq %rax, %r10");
            emitText("movq %rax, %rdx");
            emitText("sarq $63, %rdx");
            emitText("shrq $" + std::to_string(64 - shift) + ", %rdx");
            emitText("addq %rdx, %rax");
            emitText("sarq $" + std::to_string(shift) + ", %rax");
            emitText("shlq $" + std::to_string(shift) + ", %rax");
            emitText("subq %rax, %r10");
            emitText("movq %r10, %rax");
          }
          break;
        }
      }
      if (b->type.policy == OverflowPolicy::Checked) {
        emitText("testq %rcx, %rcx");
        std::string ok = newLabel("divz_ok");
        emitText("jne " + ok);
        emitRuntimeCall("hphl_divide_by_zero");
        emitText(ok + ":");
      }
      if (uns) {
        emitText("xorl %edx, %edx");
        emitText("divq %rcx");
      } else {
        emitText("cqto");
        emitText("idivq %rcx");
      }
      emitText("movq %rdx, %rax");
      break;
    }
    case BinOp::BitAnd: emitText("andq %rcx, %rax"); break;
    case BinOp::BitOr: emitText("orq %rcx, %rax"); break;
    case BinOp::BitXor: emitText("xorq %rcx, %rax"); break;
    case BinOp::Shl:
      emitText("movq %rcx, %rdx");
      emitText("shlq %cl, %rax");
      break;
    case BinOp::Shr: {
      bool uns = b->lhs->type.kind == Type::Kind::UInt;
      emitText("movq %rcx, %rdx");
      emitText(uns ? "shrq %cl, %rax" : "sarq %cl, %rax");
      break;
    }
    default: break;
  }
  if (b->op == BinOp::Add || b->op == BinOp::Sub || b->op == BinOp::Mul ||
      b->op == BinOp::Div || b->op == BinOp::Mod) {
    applyOverflowPolicy(b->type);
  }
}

void Codegen::genHirUnary(HirUnary* u) {
  switch (u->op) {
    case UnOp::Neg: {
      if (isFloatType(u->type)) {
        genHirExpr(u->operand.get());
        emitText("movabsq $-9223372036854775808, %rax");
        emitText("movq %rax, %xmm1");
        emitText("xorpd %xmm1, %xmm0");
      } else {
        genHirExpr(u->operand.get());
        emitText("negq %rax");
        applyOverflowPolicy(u->type);
      }
      break;
    }
    case UnOp::Not:
      genHirExpr(u->operand.get());
      emitText("testq %rax, %rax");
      emitText("sete %al");
      emitText("movzbl %al, %eax");
      break;
    case UnOp::BitNot:
      genHirExpr(u->operand.get());
      emitText("notq %rax");
      break;
    case UnOp::PreInc:
    case UnOp::PreDec: {
      bool uAtomic = false;
      if (u->operand->kind == HirExprKind::Var)
        uAtomic = static_cast<HirVar*>(u->operand.get())->atomic;
      else if (u->operand->kind == HirExprKind::Member)
        uAtomic = static_cast<HirMember*>(u->operand.get())->fieldAtomic;
      if (uAtomic) {
        genHirAddr(u->operand.get());
        emitText("subq $16, %rsp");
        emitText("movq %rax, 8(%rsp)");
        emitText(u->op == UnOp::PreInc ? "movq $1, %rax" : "movq $-1, %rax");
        emitText("movq 8(%rsp), %r8");
        emitText("lock xaddq %rax, (%r8)");
        emitText(u->op == UnOp::PreInc ? "addq $1, %rax" : "subq $1, %rax");
        emitText("addq $16, %rsp");
        break;
      }
      genHirAddr(u->operand.get());
      emitText("movq %rax, %r10");
      emitText("movq (%r10), %rdx");
      emitText(u->op == UnOp::PreInc ? "addq $1, %rdx" : "subq $1, %rdx");
      emitText("movq %rdx, %rax");
      applyOverflowPolicy(u->type);
      emitText("movq %rax, %rdx");
      emitText("movq %rdx, (%r10)");
      emitText("movq %rdx, %rax");
      break;
    }
    case UnOp::PostInc:
    case UnOp::PostDec: {
      bool uAtomic = false;
      if (u->operand->kind == HirExprKind::Var)
        uAtomic = static_cast<HirVar*>(u->operand.get())->atomic;
      else if (u->operand->kind == HirExprKind::Member)
        uAtomic = static_cast<HirMember*>(u->operand.get())->fieldAtomic;
      if (uAtomic) {
        genHirAddr(u->operand.get());
        emitText("subq $16, %rsp");
        emitText("movq %rax, 8(%rsp)");
        emitText(u->op == UnOp::PostInc ? "movq $1, %rax" : "movq $-1, %rax");
        emitText("movq 8(%rsp), %r8");
        emitText("lock xaddq %rax, (%r8)");
        emitText("addq $16, %rsp");
        break;
      }
      int rt = nextSlot_++;
      genHirAddr(u->operand.get());
      emitText("movq %rax, %r10");
      emitText("movq (%r10), %rdx");
      emitText("movq %rdx, " + slotRef(rt));
      emitText(u->op == UnOp::PostInc ? "addq $1, %rdx" : "subq $1, %rdx");
      emitText("movq %rdx, %rax");
      applyOverflowPolicy(u->type);
      emitText("movq %rax, %rdx");
      emitText("movq %rdx, (%r10)");
      emitText("movq " + slotRef(rt) + ", %rax");
      break;
    }
  }
}

void Codegen::genHirCast(HirCast* c) {
  genHirExpr(c->operand.get());
  Type from = c->operand->type;
  Type to = c->target;
  if (from.isInteger() && to.kind == Type::Kind::Float) {
    emitText("cvtsi2sdq %rax, %xmm0");
  } else if (from.kind == Type::Kind::Float && to.isInteger()) {
    emitText("cvttsd2siq %xmm0, %rax");
  }
}

void Codegen::genHirNew(HirNew* n) {
  if (n->isChannel) {
    emitText("movq $" + std::to_string(n->channelCapacity) + ", %rcx");
    emitRuntimeCall("hphl_channel_new");
    return;
  }
  if (n->isList) {
    if (n->args.empty()) {
      emitRuntimeCall("hphl_list_new");
    } else {
      genHirExpr(n->args[0].get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_list_with_cap");
    }
    return;
  }
  if (n->isArrayNew) {
    // `new int[N]` — array heap (bug 1.7)
    Type et = n->type.elem ? *n->type.elem : Type::makeInt(32);
    long long elemSz = typeSize(et);
    emitText("movq $" + std::to_string(n->arraySize * elemSz) + ", %rcx");
    emitRuntimeCall("hphl_shared_alloc");
    return;
  }
  // `new int(0)` — primitivo na heap: retorna o valor (bug 1.7)
  {
    bool prim = n->type.kind == Type::Kind::Int || n->type.kind == Type::Kind::UInt ||
                n->type.kind == Type::Kind::Bool || n->type.kind == Type::Kind::Char;
    if (prim && sem_.classes().count(n->className) == 0) {
      if (!n->args.empty()) genHirExpr(n->args[0].get());
      return;
    }
  }
  std::vector<HirExpr*> args;
  for (auto& a : n->args) args.push_back(a.get());
  int stackArgs = (int)args.size() > 3 ? (int)args.size() - 3 : 0;
  int bytes = 16 + ((32 + stackArgs * 8 + 15) & ~15);
  emitText("subq $" + std::to_string(bytes) + ", %rsp");
  /* M28 28.3: typed allocation if class has descriptor (bitmap-based GC) �
   * antes era sempre calloc, que ignora o GC e causa leak de memoria
   * (binary-trees-gc mantinha 295 MB mesmo com gc_pressure). */
  auto it = classIdMap_.find(n->className);
  if (it != classIdMap_.end() && it->second > 0) {
    int cid = it->second;
    emitText("movq $" + std::to_string(classSize(n->className)) + ", %rcx\n");
    emitText("movq $" + std::to_string(cid) + ", %rdx\n");
    emitRuntimeCall("hphl_shared_alloc_typed");
  } else {
    genCallCalloc(classSize(n->className));
  }
  {
    auto cit = sem_.classes().find(n->className);
    if (cit != sem_.classes().end() && cit->second.hasVptr)
      emitText("leaq .Lvt_" + n->className + "(%rip), %r10");
      emitText("movq %r10, (%rax)"); // M10: vptr
  }
  emitText("movq %rax, 0(%rsp)");

  for (size_t i = 0; i < args.size(); i++) {
    HirExpr* a = args[i];
    Param* p = !n->hasNamedArgs && n->ctor ? n->ctor->params[i].get() : nullptr;
    bool byRef = p && p->isByRef();
    bool pf = p ? isFloatType(p->type) : isFloatType(a->type);
    int pos = (int)i + 1;
    if (byRef) {
      if (p->byIn && !hirIsAddressable(a)) {
        int t = nextSlot_++;
        genHirExpr(a);
        emitText("movq %rax, " + slotRef(t));
        emitText("leaq " + slotRefMem(t) + ", %rax");
      } else {
        genHirAddr(a);
      }
      emitText("movq %rax, " + std::to_string(pos < 4 ? 8 * pos : 32 + 8 * (pos - 4)) + "(%rsp)");
    } else {
      genHirExpr(a);
      if (p && isStructType(p->type))
        genStructCopy(p->type);
      if (pf && p && !isFloatType(a->type))
        emitText("cvtsi2sdq %rax, %xmm0"); // arg int p/ param float do ctor (FFI v2 fix)
      if (pf) {
        emitText("movsd %xmm0, " + std::to_string(pos < 4 ? 8 * pos : 32 + 8 * (pos - 4)) + "(%rsp)");
      } else {
        emitText("movq %rax, " + std::to_string(pos < 4 ? 8 * pos : 32 + 8 * (pos - 4)) + "(%rsp)");
      }
    }
  }

  emitText("movq 0(%rsp), %rcx");
  const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
  for (size_t i = 0; i < args.size(); i++) {
    int pos = (int)i + 1;
    if (pos >= 4) continue;
    Param* p = !n->hasNamedArgs && n->ctor ? n->ctor->params[i].get() : nullptr;
    if (p && p->isByRef()) {
      emitText("movq " + std::to_string(8 * pos) + "(%rsp), " + regs[pos]);
      continue;
    }
    if (p ? isFloatType(p->type) : isFloatType(args[i]->type)) {
      emitText("movsd " + std::to_string(8 * pos) + "(%rsp), %xmm" + std::to_string(pos));
    } else {
      emitText("movq " + std::to_string(8 * pos) + "(%rsp), " + regs[pos]);
    }
  }
  if (n->ctor) {
    emitText("call " + fnLabel(n->ctor));
  }
  emitText("movq 0(%rsp), %r10"); // object in %r10 for field init
  genInitClassFields(n->className);
  emitText("movq %r10, %rax"); // restore object pointer
  emitText("addq $" + std::to_string(bytes) + ", %rsp");
}

void Codegen::genHirEnumCtor(const std::string& enumCanon, int entryIndex,
                             const std::vector<HirExpr*>& args) {
  long long tag = 0;
  const auto& en = *sem_.enums().at(enumCanon);
  if (entryIndex >= 0 && entryIndex < (int)en.entries.size())
    tag = en.entries[entryIndex].value;
  long long bytes = 8 + 8 * (long long)args.size();
  int h = nextSlot_++;
  genCallCalloc(bytes);
  emitText("movq %rax, " + slotRef(h));
  emitText("movq $" + std::to_string(tag) + ", %rax");
  emitText("movq " + slotRef(h) + ", %rcx");
  emitText("movq %rax, (%rcx)");
  for (size_t i = 0; i < args.size(); i++) {
    genHirExpr(args[i]);
    emitText("movq " + slotRef(h) + ", %rcx");
    if (isFloatType(args[i]->type)) {
      emitText("movsd %xmm0, " + std::to_string(8 * (i + 1)) + "(%rcx)");
    } else {
      emitText("movq %rax, " + std::to_string(8 * (i + 1)) + "(%rcx)");
    }
  }
  emitText("movq " + slotRef(h) + ", %rax");
}

void Codegen::genHirOptCtor(HirOptCtor* o) {
  long long tag = (o->variant == "Some" || o->variant == "Err") ? 1 : 0;
  bool hasArg = o->arg != nullptr;
  genCallCalloc(16);
  int h = nextSlot_++;
  emitText("movq %rax, " + slotRef(h));
  emitText("movq $" + std::to_string(tag) + ", %rax");
  emitText("movq " + slotRef(h) + ", %rcx");
  emitText("movq %rax, (%rcx)");
  if (hasArg) {
    genHirExpr(o->arg.get());
    emitText("movq " + slotRef(h) + ", %rcx");
    if (isFloatType(o->arg->type)) emitText("movsd %xmm0, 8(%rcx)");
    else emitText("movq %rax, 8(%rcx)");
  }
  emitText("movq " + slotRef(h) + ", %rax");
}

void Codegen::genHirSpawn(HirSpawnExpr* sp) {
  programParallel_ = true;
  FunctionDecl* fn = makeHirTaskFunction(std::move(sp->body), sp->captures,
                                         sp->type.elem ? "task<T>" : "task<void>");
  emitSpawnSite(fn, sp->captures);
}

void Codegen::genHirAwait(HirAwaitExpr* a) {
  // `await E`: espera a task E e devolve o payload; `await ch.Receive()` é
  // açúcar — o Receive já bloqueia (nó ChannelReceive no HIR)
  if (a->operand->kind == HirExprKind::Call &&
      static_cast<HirCall*>(a->operand.get())->kind == HirCallKind::ChannelReceive) {
    genHirExpr(a->operand.get());
    return;
  }
  genHirExpr(a->operand.get());
  emitText("movq %rax, %rcx");
  emitRuntimeCall("hphl_wait_task");
  if (isFloatType(a->type)) emitText("movq %rax, %xmm0");
}

FunctionDecl* Codegen::makeHirTaskFunction(
    std::unique_ptr<HirBlock> body,
    const std::vector<std::pair<std::string, Type>>& captures,
    const std::string& payloadTypeName) {
  auto fn = std::make_unique<FunctionDecl>();
  fn->name = "__task_" + std::to_string(taskCounter_++);
  fn->moduleName = "spawn";
  fn->hasReturnType = true;
  fn->returnType = Type::makeVoid();
  fn->isTask = true;
  fn->taskPayloadTypeName = payloadTypeName;
  fn->taskCaptures = captures;
  FunctionDecl* raw = fn.get();
  taskDecls_.push_back(std::move(fn));
  pendingHirTasks_.push_back({raw, std::move(body)});
  return raw;
}

void Codegen::genPendingHirTasks() {
  for (size_t i = 0; i < pendingHirTasks_.size(); i++) {
    genFunctionHir(pendingHirTasks_[i].fn, pendingHirTasks_[i].body.get());
  }
  pendingHirTasks_.clear();
}

// v0.95 (lambdas): função sintética `__lambda_N(env, p...)` — env em rcx
// (como `this`), capturas por valor, corpo adiado como nas tasks.
FunctionDecl* Codegen::makeHirLambdaFunction(
    std::unique_ptr<HirBlock> body,
    const std::vector<std::pair<std::string, Type>>& params, const Type& ret,
    const std::vector<std::pair<std::string, Type>>& captures) {
  auto fn = std::make_unique<FunctionDecl>();
  fn->name = "__lambda_" + std::to_string(lambdaCounter_++);
  fn->moduleName = "lambda";
  fn->hasReturnType = true;
  fn->returnType = ret;
  fn->isLambda = true;
  fn->isSynthetic = true;
  for (auto& p : params) {
    auto pm = std::make_unique<Param>();
    pm->name = p.first;
    pm->type = p.second;
    fn->params.push_back(std::move(pm));
  }
  fn->taskCaptures = captures;
  FunctionDecl* raw = fn.get();
  taskDecls_.push_back(std::move(fn));
  pendingHirTasks_.push_back({raw, std::move(body)});
  return raw;
}

void Codegen::genHirLambda(HirLambda* l) {
  FunctionDecl* fn = makeHirLambdaFunction(std::move(l->body), l->params,
                                           l->retType, l->captures);
  // 1. env box (capturas por valor, 8 bytes cada; vazio → NULL)
  int envSlot = nextSlot_++;
  if (!l->captures.empty()) {
    genCallMalloc((long long)l->captures.size() * 8); // rax = env
    emitText("movq %rax, " + slotRef(envSlot));
    for (size_t i = 0; i < l->captures.size(); i++) {
      auto* lc = findLocal(l->captures[i].first);
      if (!lc) continue; // inalcançável (validação semântica)
      emitText("movq " + slotRef(lc->slot) + ", %r10");
      emitText("movq " + slotRef(envSlot) + ", %rax");
      emitText("movq %r10, " + std::to_string(i * 8) + "(%rax)");
    }
  } else {
    int z = nextSlot_++;
    emitText("movq $0, %rax");
    emitText("movq %rax, " + slotRef(z));
    envSlot = z;
  }
  // 2. closure box {code, env} (16 bytes); valor em rax
  int boxSlot = nextSlot_++;
  genCallMalloc(16); // rax = box
  emitText("movq %rax, " + slotRef(boxSlot));
  emitText("leaq " + fnLabel(fn) + "(%rip), %r10");
  emitText("movq " + slotRef(boxSlot) + ", %rax");
  emitText("movq %r10, (%rax)");
  emitText("movq " + slotRef(envSlot) + ", %r10");
  emitText("movq " + slotRef(boxSlot) + ", %rax");
  emitText("movq %r10, 8(%rax)");
  // 3. auto-referência: grava o próprio box no slot do env (ponto fixo)
  if (!l->selfName.empty()) {
    for (size_t i = 0; i < l->captures.size(); i++) {
      if (l->captures[i].first == l->selfName) {
        emitText("movq " + slotRef(boxSlot) + ", %rax");
        emitText("movq " + slotRef(envSlot) + ", %r10");
        emitText("movq %rax, " + std::to_string(i * 8) + "(%r10)");
        break;
      }
    }
  }
  emitText("movq " + slotRef(boxSlot) + ", %rax");
}

void Codegen::genHirIndirectCall(HirCall* c) {
  // v0.95: `f(args)` com f valor `func` — box {code, env} em rax;
  // env vai em rcx (1º arg oculto), args em rdx/r8/r9/pilha (tipos da assinatura).
  const Type& ft = c->funcValue->type;
  size_t nargs = c->args.size();
  int total = 1 + (int)nargs;
  int stackArgs = total > 4 ? total - 4 : 0;
  int bytes = (32 + stackArgs * 8 + 15) & ~15;
  emitText("subq $" + std::to_string(bytes) + ", %rsp");
  int boxSlot = nextSlot_++;
  genHirExpr(c->funcValue.get());
  emitText("movq %rax, " + slotRef(boxSlot));
  for (size_t i = 0; i < nargs; i++) {
    int k = (int)i + 1;
    bool fp = i < ft.genericArgs.size() && isFloatType(ft.genericArgs[i]);
    genHirExpr(c->args[i].get());
    if (fp) {
      if (k < 4) emitText("movsd %xmm0, " + std::to_string(8 * k) + "(%rsp)");
      else emitText("movsd %xmm0, " + std::to_string(32 + 8 * (k - 4)) + "(%rsp)");
    } else {
      if (k < 4) emitText("movq %rax, " + std::to_string(8 * k) + "(%rsp)");
      else emitText("movq %rax, " + std::to_string(32 + 8 * (k - 4)) + "(%rsp)");
    }
  }
  // env = box[1] → slot 0; code = box[0] → r10
  emitText("movq " + slotRef(boxSlot) + ", %rax");
  emitText("movq 8(%rax), %rax");
  emitText("movq %rax, 0(%rsp)");
  emitText("movq " + slotRef(boxSlot) + ", %r10");
  emitText("movq (%r10), %r10");
  const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
  emitText("movq 0(%rsp), %rcx");
  for (size_t i = 0; i < nargs; i++) {
    int k = (int)i + 1;
    if (k >= 4) continue;
    bool fp = i < ft.genericArgs.size() && isFloatType(ft.genericArgs[i]);
    if (fp) {
      emitText("movsd " + std::to_string(8 * k) + "(%rsp), %xmm" + std::to_string(k));
    } else {
      emitText("movq " + std::to_string(8 * k) + "(%rsp), " + regs[k]);
    }
  }
  emitText("call *%r10");
  emitText("addq $" + std::to_string(bytes) + ", %rsp");
}

// `parallel foreach (x in coll)` com corpo HIR: mesma estratégia do codegen
// AST — partes strided/batch com task sintética por env.
void Codegen::genHirParallelForeach(HirParallelForeach* pf) {
  programParallel_ = true;
  bool isList = pf->collection->type.kind == Type::Kind::List;
  long long n = isList ? 0 : pf->collection->type.arraySize;
  int B = pf->batchSize;
  bool isBatch = B > 0;
  bool nested = curFn_ && (curFn_->isTask || curFn_->isAsync);
  if (nested) emitRuntimeCall("hphl_region_begin");
  int ns = nextSlot_++;
  int ps = nextSlot_++;
  if (isList) {
    genHirExpr(pf->collection.get());
    emitText("movq %rax, %rcx");
    emitRuntimeCall("hphl_list_len");
  } else {
    emitText("movq $" + std::to_string(n) + ", %rax");
  }
  emitText("movq %rax, " + slotRef(ns));
  if (isBatch) {
    emitText("addq $" + std::to_string(B - 1) + ", %rax");
    emitText("movq $" + std::to_string(B) + ", %rcx");
    emitText("cqto");
    emitText("idivq %rcx");
  } else {
    emitRuntimeCall("hphl_worker_count");
    emitText("movq " + slotRef(ns) + ", %rcx");
    emitText("cmpq %rcx, %rax");
    emitText("cmovg %rcx, %rax");
    emitText("testq %rax, %rax");
    std::string p1 = newLabel("pf_gt0");
    emitText("jne " + p1);
    emitText("movq $1, %rax");
    emitText(p1 + ":");
  }
  emitText("movq %rax, " + slotRef(ps));

  // corpo sintético da task (mesmo desenho do makeParallelForeachBody da AST)
  auto body = std::make_unique<HirBlock>();
  Type tInt = Type::makeInt(64);
  auto mkVar = [](const std::string& name, const Type& t) {
    auto v = std::make_unique<HirVar>();
    v->name = name;
    v->type = t;
    return v;
  };
  auto mkDecl = [](const std::string& name, const Type& t,
                   std::unique_ptr<HirExpr> init) {
    auto d = std::make_unique<HirVarDecl>();
    d->name = name;
    d->type = t;
    d->init = std::move(init);
    return d;
  };
  if (isList && !isBatch) {
    auto len = std::make_unique<HirMember>();
    len->object = mkVar("__coll", pf->collection->type);
    len->member = "Length";
    len->isListLength = true;
    len->type = tInt;
    body->stmts.push_back(mkDecl("__n", tInt, std::move(len)));
  }
  auto loop = std::make_unique<HirFor>();
  loop->init = mkDecl("__j", tInt, mkVar(isBatch ? "__start" : "__p", tInt));
  auto lt = std::make_unique<HirBinary>();
  lt->op = BinOp::Lt;
  lt->lhs = mkVar("__j", tInt);
  lt->type = Type::makeBool();
  if (isBatch) {
    auto up = std::make_unique<HirBinary>();
    up->op = BinOp::Add;
    up->lhs = mkVar("__start", tInt);
    up->rhs = mkVar("__count", tInt);
    up->type = tInt;
    lt->rhs = std::move(up);
  } else if (isList) {
    lt->rhs = mkVar("__n", tInt);
  } else {
    auto nn = std::make_unique<HirIntLit>();
    nn->value = n;
    nn->type = tInt;
    lt->rhs = std::move(nn);
  }
  loop->cond = std::move(lt);
  if (isBatch) {
    auto inc = std::make_unique<HirUnary>();
    inc->op = UnOp::PostInc;
    inc->operand = mkVar("__j", tInt);
    inc->type = tInt;
    loop->step = std::move(inc);
  } else {
    auto assign = std::make_unique<HirAssign>();
    assign->op = AssignOp::Plain;
    assign->target = mkVar("__j", tInt);
    assign->type = tInt;
    auto add = std::make_unique<HirBinary>();
    add->op = BinOp::Add;
    add->lhs = mkVar("__j", tInt);
    add->rhs = mkVar("__step", tInt);
    add->type = tInt;
    assign->value = std::move(add);
    loop->step = std::move(assign);
  }
  auto inner = std::make_unique<HirBlock>();
  auto iidx = std::make_unique<HirIndex>();
  iidx->object = mkVar("__coll", pf->collection->type);
  iidx->index = mkVar("__j", tInt);
  iidx->type = pf->itemType;
  inner->stmts.push_back(mkDecl(pf->itemName, pf->itemType, std::move(iidx)));
  for (auto& s : pf->body->stmts) inner->stmts.push_back(std::move(s));
  loop->body = std::move(inner);
  body->stmts.push_back(std::move(loop));

  std::vector<std::pair<std::string, Type>> caps;
  caps.push_back({"__coll", pf->collection->type});
  if (isBatch) {
    caps.push_back({"__start", tInt});
    caps.push_back({"__count", tInt});
  } else {
    caps.push_back({"__p", tInt});
    caps.push_back({"__step", tInt});
  }
  FunctionDecl* fn = makeHirTaskFunction(std::move(body), caps, "");

  // nome do array global (env carrega a base via label) ou do list local
  std::string collLabel;
  if (pf->collection->kind == HirExprKind::Var) {
    auto* cv = static_cast<HirVar*>(pf->collection.get());
    collLabel = !cv->label.empty() ? cv->label : ".Lg_" + cv->name;
  }

  int is_ = nextSlot_++;
  emitText("movq $0, %rax");
  emitText("movq %rax, " + slotRef(is_));
  std::string lp = newLabel("pf_sp");
  std::string lpEnd = newLabel("pf_spe");
  emitText(lp + ":");
  emitText("movq " + slotRef(is_) + ", %rax");
  emitText("cmpq " + slotRef(ps) + ", %rax");
  emitText("jge " + lpEnd);
  genCallMalloc(24);
  emitText("movq %rax, %r11");  // %r11 = env block (preservado entre clobbers de %rax)
  if (isList) {
    genHirExpr(pf->collection.get());
    emitText("movq %rax, 0(%r11)");
  } else {
    emitText("leaq " + collLabel + "(%rip), %rax");
    emitText("movq %rax, 0(%r11)");
  }
  if (isBatch) {
    emitText("movq " + slotRef(is_) + ", %rax");
    emitText("imulq $" + std::to_string(B) + ", %rax, %rax");
    emitText("movq %rax, 8(%r11)");
    emitText("movq " + slotRef(ns) + ", %rax");
    emitText("subq 8(%r11), %rax");
    std::string c1 = newLabel("pf_cnt");
    emitText("cmpq $" + std::to_string(B) + ", %rax");
    emitText("jle " + c1);
    emitText("movq $" + std::to_string(B) + ", %rax");
    emitText(c1 + ":");
    emitText("movq %rax, 16(%r11)");
  } else {
    emitText("movq " + slotRef(is_) + ", %rax");
    emitText("movq %rax, 8(%r11)");
    emitText("movq " + slotRef(ps) + ", %rax");
    emitText("movq %rax, 16(%r11)");
  }
  emitText("movq %r11, %rdx");
  emitText("leaq " + fnLabel(fn) + "(%rip), %rcx");
  emitRuntimeCall("hphl_spawn_task_ex");
  emitText("movq " + slotRef(is_) + ", %rax");
  emitText("addq $1, %rax");
  emitText("movq %rax, " + slotRef(is_));
  emitText("jmp " + lp);
  emitText(lpEnd + ":");
  emitRuntimeCall(nested ? "hphl_region_join" : "hphl_join_tasks");
}




} // namespace hphl
