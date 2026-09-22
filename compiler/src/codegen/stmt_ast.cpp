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
void Codegen::genBlock(BlockStmt* b) {
  for (auto& s : b->stmts) genStmt(s.get());
}

void Codegen::genStmt(Stmt* s) {
  // debugger (M7): trap por linha executÃ¡vel (line 0 = stmt sintÃ©tico; dedup
  // de linha consecutiva; Block nÃ£o tem execuÃ§Ã£o prÃ³pria)
  if (s->kind != StmtKind::Block && s->line > 0 &&
      s->line != lastTrapLine_) {
    emitText("# [line " + std::to_string(s->line) + "]");
    if (debug_) {
      emitDebugTrap(s->line);
    }
    lastTrapLine_ = s->line;
  }
  switch (s->kind) {
    case StmtKind::Block: {
      scopes_.emplace_back();
      genBlock(static_cast<BlockStmt*>(s));
      scopes_.pop_back();
      break;
    }
    case StmtKind::If: {
      auto st = static_cast<IfStmt*>(s);
      std::string elseL = newLabel("if_else");
      std::string endL = newLabel("if_end");
      genCond(st->cond.get(), elseL);
      if (st->thenBranch) {
        scopes_.emplace_back();
        genStmt(st->thenBranch.get());
        scopes_.pop_back();
      }
      if (st->elseBranch) {
        emitText("jmp " + endL);
        emitText(elseL + ":");
        scopes_.emplace_back();
        genStmt(st->elseBranch.get());
        scopes_.pop_back();
        emitText(endL + ":");
      } else {
        emitText(elseL + ":");
      }
      break;
    }
    case StmtKind::For: {
      auto st = static_cast<ForStmt*>(s);
      std::string condL = newLabel("for_cond");
      std::string bodyL = newLabel("for_body");
      std::string stepL = newLabel("for_step");
      std::string endL = newLabel("for_end");
      loopStack_.push_back({endL, stepL});
      scopes_.emplace_back();
      if (st->init) genStmt(st->init.get());
      emitText("jmp " + condL);
      emitText(bodyL + ":");
      scopes_.emplace_back();
      genStmt(st->body.get());
      scopes_.pop_back();
      emitText(stepL + ":");
      if (st->step) genExpr(st->step.get());
      emitText(condL + ":");
      if (st->cond) {
        genCondTrue(st->cond.get(), bodyL); // true â†’ corpo; false â†’ cai no fim
      } else {
        emitText("jmp " + bodyL);
      }
      emitText(endL + ":");
      scopes_.pop_back();
      loopStack_.pop_back();
      break;
    }
    case StmtKind::While: {
      auto st = static_cast<WhileStmt*>(s);
      std::string condL = newLabel("while_cond");
      std::string bodyL = newLabel("while_body");
      std::string endL = newLabel("while_end");
      loopStack_.push_back({endL, condL});
      emitText("jmp " + condL);
      emitText(bodyL + ":");
      scopes_.emplace_back();
      genStmt(st->body.get());
      scopes_.pop_back();
      emitText(condL + ":");
      genCondTrue(st->cond.get(), bodyL); // true â†’ corpo; false â†’ cai no fim
      emitText(endL + ":");
      loopStack_.pop_back();
      break;
    }
    case StmtKind::DoWhile: {
      auto st = static_cast<DoWhileStmt*>(s);
      std::string bodyL = newLabel("dowhile_body");
      std::string condL = newLabel("dowhile_cond");
      std::string endL = newLabel("dowhile_end");
      loopStack_.push_back({endL, condL});
      emitText(bodyL + ":");
      scopes_.emplace_back();
      genStmt(st->body.get());
      scopes_.pop_back();
      emitText(condL + ":");
      genCondTrue(st->cond.get(), bodyL); // true â†’ repete; false â†’ cai no fim
      emitText(endL + ":");
      loopStack_.pop_back();
      break;
    }
    case StmtKind::Break: {
      if (!loopStack_.empty()) emitText("jmp " + loopStack_.back().breakLabel);
      break;
    }
    case StmtKind::Continue: {
      if (!loopStack_.empty()) emitText("jmp " + loopStack_.back().continueLabel);
      break;
    }
    case StmtKind::Return: {
      auto st = static_cast<ReturnStmt*>(s);
      if (curFn_->isTask || curFn_->isAsync) {
        // return da tarefa (ou funÃ§Ã£o async): grava o payload no resultado
        // (res) e vai direto para o retorno; o runtime guarda t->result para
        // o Wait()/await
        if (st->value) {
genExpr(st->value.get());
          emitText("movq " + slotRef(taskResSlot_) + ", %r10");
          if (isFloatType(st->value->exprType)) {
            emitText("movsd %xmm0, (%r10)");
          } else {
            emitText("movq %rax, (%r10)");
          }
        }
        emitText("jmp .Lret_" + fnLabel(curFn_));
        break;
      }
      if (st->value) {
        // M28 32.1: TCO self-tail-call â€” `return funcSelf(args)` vira
        // `args â†’ slots de parÃ¢metros; jmp .L_<fn>_body`. Reutiliza o
        // frame atual. EspecificaÃ§Ã£o: examples/TCO.txt Â§5, Â§21.
        if (st->value->kind == ExprKind::Call && canSelfTailCall(
                static_cast<CallExpr*>(st->value.get()))) {
          emitSelfTailCall(static_cast<CallExpr*>(st->value.get()));
          break;
        }
        genExpr(st->value.get());
        if (curFn_->hasReturnType) {
          // struct: retorno por valor â€” copia antes que o epÃ­logo libere o
          // bloco da funÃ§Ã£o (a cÃ³pia sobrevive e nÃ£o Ã© liberada)
          if (isStructType(st->value->exprType))
            genStructCopy(st->value->exprType);
          // epÃ­logo libera memÃ³ria (free) e pode clobberar rax/xmm0: preserva
          // o valor de retorno num slot temporÃ¡rio (um Ãºnico por funÃ§Ã£o, pois
          // sÃ³ um caminho de return executa) e restaura antes do leave
          if (st->value->kind == ExprKind::Ident &&
              !isStructType(st->value->exprType)) {
            auto* l = findLocal(static_cast<IdentExpr*>(st->value.get())->name);
            // M10.1b: tuple local retornado escapa como objeto/list
            bool isTuple = st->value->exprType.kind == Type::Kind::Tuple;
            if (l && !isTuple) releaseEscapeSlot(l->slot);
          }
          if (retSlot_ < 0) {
            retSlot_ = nextSlot_++;
            retIsFloat_ = isFloatType(st->value->exprType);
          }
          if (retIsFloat_) emitText("movsd %xmm0, " + slotRef(retSlot_));
          else emitText("movq %rax, " + slotRef(retSlot_));
        }
      }
      emitText("jmp .Lret_" + fnLabel(curFn_));
      break;
    }
    case StmtKind::Panic: {
      auto st = static_cast<PanicStmt*>(s);
      genExpr(st->message.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_panic"); // nÃ£o retorna
      break;
    }
    case StmtKind::Lock: {
      genLockStmt(static_cast<LockStmt*>(s));
      break;
    }
    case StmtKind::Spawn: {
      genSpawnStmt(static_cast<SpawnStmt*>(s));
      break;
    }
    case StmtKind::Parallel: {
      genParallelStmt(static_cast<ParallelStmt*>(s));
      break;
    }
    case StmtKind::Try: {
      genTryStmt(static_cast<TryStmt*>(s));
      break;
    }
    case StmtKind::Throw: {
      genThrowStmt(static_cast<ThrowStmt*>(s));
      break;
    }
    case StmtKind::Assert: {
      auto st = static_cast<AssertStmt*>(s);
      std::string okL = newLabel("assert_ok");
      genCondTrue(st->cond.get(), okL);
      // sÃ³ chega aqui se a condiÃ§Ã£o foi falsa: hphl_assert(0, msg)
      std::string msg = internString("assert falhou (linha " +
                                     std::to_string(st->line) + ")");
      emitText("xorl %ecx, %ecx");
      emitText("leaq " + msg + "(%rip), %rdx");
      emitRuntimeCall("hphl_assert"); // nÃ£o retorna
      emitText(okL + ":");
      break;
    }
    case StmtKind::ExprStmt: {
      auto st = static_cast<ExprStmt*>(s);
      genExpr(st->expr.get());
      break;
    }
    case StmtKind::VarDecl: {
      auto svd = static_cast<StmtVarDecl*>(s);
      for (auto& d : svd->decls) genVarDecl(d.get());
      break;
    }
    case StmtKind::Destructure: {
      genDestructure(static_cast<StmtDestructure*>(s));
      break;
    }
    case StmtKind::Switch: {
      auto st = static_cast<SwitchStmt*>(s);
      std::string endL = newLabel("switch_end");
      std::string contL = loopStack_.empty() ? endL : loopStack_.back().continueLabel;
      loopStack_.push_back({endL, contL});
      genExpr(st->subject.get());
      emitText("movq %rax, %r11"); // subject preservado em r11
      std::vector<std::string> caseLabels;
      for (size_t i = 0; i < st->cases.size(); i++) {
        caseLabels.push_back(newLabel("switch_case"));
        emitText("movq %r11, %rax");
        emitText("cmpq $" + std::to_string(st->cases[i].value) + ", %rax");
        emitText("je " + caseLabels[i]);
      }
      std::string defL = newLabel("switch_default");
      emitText("jmp " + defL);
      for (size_t i = 0; i < st->cases.size(); i++) {
        emitText(caseLabels[i] + ":");
        scopes_.emplace_back();
        for (auto& bodyStmt : st->cases[i].body) genStmt(bodyStmt.get());
        scopes_.pop_back();
        emitText("jmp " + endL); // M1: sem fall-through
      }
      emitText(defL + ":");
      scopes_.emplace_back();
      for (auto& bodyStmt : st->defaultBody) genStmt(bodyStmt.get());
      scopes_.pop_back();
      emitText(endL + ":");
      loopStack_.pop_back();
      break;
    }
    case StmtKind::Foreach: {
      auto st = static_cast<ForeachStmt*>(s);
      if (st->parallel) {
        genParallelForeach(st);
        break;
      }
      std::string bodyL = newLabel("fe_body");
      std::string contL = newLabel("fe_cont");
      std::string checkL = newLabel("fe_check");
      std::string endL = newLabel("fe_end");
      loopStack_.push_back({endL, contL});
      scopes_.emplace_back();
      Local item;
      item.type = st->itemType;
      item.policy = StoragePolicy::Stack;
      item.slot = nextSlot_++;
      scopes_.back()[st->itemName] = item;
      bool isList = st->collection->exprType.kind == Type::Kind::List;
      int baseSlot = nextSlot_++; // ponteiro do list ou base do array
      int idxSlot = nextSlot_++;  // contador
      int countSlot = -1;         // tamanho dinÃ¢mico (list)
      genExpr(st->collection.get()); // list â†’ ponteiro; array â†’ base
      emitText("movq %rax, " + slotRef(baseSlot));
      if (isList) {
        countSlot = nextSlot_++;
        emitText("movq %rax, %rcx");
        emitRuntimeCall("hphl_list_len");
        emitText("movq %rax, " + slotRef(countSlot));
      }
      int stride = typeSize(*st->collection->exprType.elem);
      emitText("movq $0, %rax");
      emitText("movq %rax, " + slotRef(idxSlot));
      emitText("jmp " + checkL);
      emitText(bodyL + ":");
      if (isList) {
        emitText("movq " + slotRef(baseSlot) + ", %rcx");
        emitRuntimeCall("hphl_list_data"); // rax = buffer de itens
        emitText("movq " + slotRef(idxSlot) + ", %rcx");
        emitText("imulq $" + std::to_string(stride) + ", %rcx, %rcx");
        emitText("addq %rcx, %rax"); // endereÃ§o do elemento
      } else {
        emitText("movq " + slotRef(baseSlot) + ", %rcx");
        emitText("movq " + slotRef(idxSlot) + ", %rax");
        emitText("imulq $" + std::to_string(stride) + ", %rax, %rax");
        emitText("addq %rcx, %rax"); // endereÃ§o do elemento
      }
      if (item.type.kind == Type::Kind::Array) {
        // item Ã© sub-array: o "valor" Ã© o endereÃ§o
        emitText("movq %rax, " + slotRef(item.slot));
      } else if (isFloatType(item.type)) {
        emitText("movsd (%rax), %xmm0");
        emitText("movsd %xmm0, " + slotRef(item.slot));
      } else {
        emitText("movq (%rax), %rdx");
        emitText("movq %rdx, " + slotRef(item.slot));
      }
      scopes_.emplace_back();
      genStmt(st->body.get());
      scopes_.pop_back();
      // continue pula para cÃ¡: incrementa o Ã­ndice
      emitText(contL + ":");
      emitText("movq " + slotRef(idxSlot) + ", %rax");
      emitText("addq $1, %rax");
      emitText("movq %rax, " + slotRef(idxSlot));
      // checagem (entrada inicial tambÃ©m vem para cÃ¡, com rax = 0)
      emitText(checkL + ":");
      if (isList) {
        emitText("cmpq " + slotRef(countSlot) + ", %rax");
      } else {
        emitText("cmpq $" + std::to_string(st->collection->exprType.arraySize) + ", %rax");
      }
      emitText("jne " + bodyL);
      emitText(endL + ":");
      scopes_.pop_back();
      loopStack_.pop_back();
      break;
    }
  }
}

// Modelo de custo zero no caminho de sucesso: nenhuma verificaÃ§Ã£o extra por
// chamada â€” o custo fica no `try` (montar o registro) e no `throw` (guardar o
// payload e pular). NÃ£o usa setjmp/longjmp do CRT (que exigem unwind info de
// pdata/xdata que nossos frames nÃ£o tÃªm): o throw restaura rsp/rbp gravados e
// salta para o endereÃ§o do handler (LEA do label). Como o codegen sÃ³ usa
// registradores volÃ¡teis (e rbp), nÃ£o hÃ¡ estado callee-saved a preservar.
// Registro no frame (5 qwords = 40 bytes):
//   +0 rip (endereÃ§o do handler) | +8 rsp | +16 rbp | +24 prev | +32 payload
void Codegen::genTryStmt(TryStmt* st) {
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

  genStmt(st->body.get());

  // caminho de sucesso: restaura o handler anterior e pula o catch
  emitText("movq hphl_exc_hdl(%rip), %rax");
  emitText("movq 24(%rax), %rcx");
  emitText("movq %rcx, hphl_exc_hdl(%rip)");
  emitText("jmp " + endL);

  // caminho de falha (throw no corpo): restaura o handler e liga o payload
  emitText(hL + ":");
  emitText("movq hphl_exc_hdl(%rip), %rax");
  emitText("movq 24(%rax), %rcx");
  emitText("movq %rcx, hphl_exc_hdl(%rip)");
  // M20-A 4.1: cleanup de locks ativos (ordem inversa)
  for (int i = (int)lockStack_.size() - 1; i >= 0; i--) {
    emitText("movq " + slotRef(lockStack_[i]) + ", %rcx");
    emitRuntimeCall("hphl_lock_end");
  }
  // M21 4.1: cleanup de Shared slots (refcount) no caminho de throw
  for (int i = (int)heapSlots_.size() - 1; i >= 0; i--) {
    auto &hs = heapSlots_[i];
    if (hs.policy == StoragePolicy::Shared) {
      emitText("movq " + slotRef(hs.slot) + ", %rcx");
      emitText("testq %rcx, %rcx");
      emitText("je " + endL + "_skip" + std::to_string(i));
      emitRuntimeCall("hphl_shared_release");
      emitText(endL + "_skip" + std::to_string(i) + ":");
    }
  }
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
    genStmt(st->catchBody.get());
    scopes_.pop_back();
  }
  emitText(endL + ":");
}

void Codegen::genLockStmt(LockStmt* st) {
  // M20-A 4.1: spill %rax (obj) em slot temporï¿½rio para cleanup em exception
  int lockSlot = nextSlot_++;
  genExpr(st->target.get());     // alvo em %rax (identidade do objeto)
  emitText("movq %rax, " + slotRef(lockSlot));
  emitText("movq %rax, %rcx");   // hphl_lock_begin(void* obj)
  emitRuntimeCall("hphl_lock_begin");
  lockStack_.push_back(lockSlot);
  scopes_.emplace_back();
  genBlock(st->body.get());
  scopes_.pop_back();
  lockStack_.pop_back();
  emitText("movq " + slotRef(lockSlot) + ", %rcx");
  emitRuntimeCall("hphl_lock_end");
}

// cria a funÃ§Ã£o sintÃ©tica de uma tarefa (`spawn`/`parallel`); a emissÃ£o do
// corpo Ã© adiada (frames nÃ£o se intercalam dentro da funÃ§Ã£o corrente)
FunctionDecl* Codegen::makeTaskFunction(
    std::unique_ptr<BlockStmt> body,
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
  fn->body = std::move(body);
  FunctionDecl* raw = fn.get();
  taskDecls_.push_back(std::move(fn));
  pendingTasks_.push_back(raw);
  return raw;
}

// site de spawn: monta o env das capturas (malloc + cÃ³pias dos valores atuais
// dos locais), carrega a funÃ§Ã£o da tarefa em %rcx e o env em %rdx, e chama
// hphl_spawn_task_ex(fn, env). %rax = handle (HphlTask*).
void Codegen::emitSpawnSite(FunctionDecl* fn,
                            const std::vector<std::pair<std::string, Type>>& captures) {
  if (!captures.empty()) {
    genCallMalloc((long long)captures.size() * 8);
    for (size_t i = 0; i < captures.size(); i++) {
      auto* l = findLocal(captures[i].first);
      if (!l) continue; // inalcanÃ§Ã¡vel (validaÃ§Ã£o semÃ¢ntica)
      emitText("movq " + slotRef(l->slot) + ", %r10");
      emitText("movq %r10, " + std::to_string(i * 8) + "(%rax)");
    }
    emitText("movq %rax, %rdx");
  } else {
    emitText("xorq %rdx, %rdx"); // env = NULL
  }
  emitText("leaq " + fnLabel(fn) + "(%rip), %rcx");
  emitRuntimeCall("hphl_spawn_task_ex");
}

void Codegen::genSpawnStmt(SpawnStmt* st) {
  programParallel_ = true;
  FunctionDecl* fn = makeTaskFunction(std::move(st->body), st->captures, "");
  emitSpawnSite(fn, st->captures);
}

void Codegen::genSpawnExpr(SpawnExpr* se) {
  programParallel_ = true;
  FunctionDecl* fn = makeTaskFunction(std::move(se->body), se->captures,
                                      se->exprType.elem ? "task<T>" : "task<void>");
  emitSpawnSite(fn, se->captures); // %rax = HphlTask*
}

void Codegen::genParallelStmt(ParallelStmt* st) {
  programParallel_ = true;
  // `parallel deterministic` (v0.23.0): NADA de spawn/barreira â€” as partes sÃ£o
  // emitidas INLINE e em ordem no frame corrente; o escopo de cada parte Ã©
  // isolado (nada vaza para as irmÃ£s), como no paralelo real.
  if (st->isDeterministic) {
    for (size_t i = 0; i < st->parts.size(); i++) {
      scopes_.emplace_back();
      genStmt(st->parts[i].get());
      scopes_.pop_back();
    }
    return;
  }
  // `parallel` DENTRO de uma tarefa (v0.22.7): abre uma REGIÃƒO prÃ³pria â€” a
  // barreira espera sÃ³ as tarefas daquela regiÃ£o (quem espera rouba trabalho
  // da fila â†’ sem deadlock) em vez de drenar a fila global
  bool nested = curFn_ && (curFn_->isTask || curFn_->isAsync);
  if (nested) emitRuntimeCall("hphl_region_begin");
  // captura POR VALOR por parte (v0.22.1): parte i usa partCaptures[i] (env
  // prÃ³prio, snapshot no site do spawn â€” mesmo mecanismo de `spawn`)
  const std::vector<std::pair<std::string, Type>> semCaps;
  for (size_t i = 0; i < st->parts.size(); i++) {
    auto wrap = std::make_unique<BlockStmt>();
    wrap->line = st->line;
    wrap->stmts.push_back(std::move(st->parts[i]));
    const auto& caps =
        i < st->partCaptures.size() ? st->partCaptures[i] : semCaps;
    FunctionDecl* fn = makeTaskFunction(std::move(wrap), caps, "");
    emitSpawnSite(fn, caps);
  }
  // barreira: a thread principal espera todas as tarefas do bloco
  emitRuntimeCall(nested ? "hphl_region_join" : "hphl_join_tasks");
}

// `parallel foreach (x in coll) { ... }` (v0.22.5): as iteraÃ§Ãµes sÃ£o divididas
// em P partes por um loop STRIDED (`for (j = p; j < N; j += P)`), uma thread
// por parte; P Ã© fixado no M2 (min(4, N) p/ arrays de tamanho estÃ¡tico, 4 p/
// lists com N em runtime). O sujeito chega por ponteiro num env prÃ³prio por
// parte (array global: base; list local: objeto heap) â€” a MESMA funÃ§Ã£o de
// tarefa serve a todos os sites, sÃ³ o env muda. Barreira no final (como o
// `parallel { }`).
void Codegen::genParallelForeach(ForeachStmt* st) {
  programParallel_ = true;
  bool isList = st->collection->exprType.kind == Type::Kind::List;
  long long n = isList ? 0 : st->collection->exprType.arraySize;
  int B = st->batchSize; // `batch: N` (v0.23.2): chunks CONTÃGUOS fixos
  bool isBatch = B > 0;
  // `parallel foreach` DENTRO de uma tarefa (v0.22.7): regiÃ£o prÃ³pria â€” a
  // barreira espera sÃ³ as tarefas desta regiÃ£o, nÃ£o drena a fila global
  bool nested = curFn_ && (curFn_->isTask || curFn_->isAsync);
  if (nested) emitRuntimeCall("hphl_region_begin");
  // N TOTAL em slot temporÃ¡rio (list: Length do runtime; array estÃ¡tico:
  // constante) â€” o spawner precisa de N para computar os chunks
  int ns = nextSlot_++; // N
  int ps = nextSlot_++; // P = nÂº de partes
  if (isList) {
    auto* l = findLocal(st->collectionName); // list local: ponteiro do objeto
    if (l) emitText("movq " + slotRef(l->slot) + ", %rcx");
    else emitText("xorl %ecx, %ecx");
    emitRuntimeCall("hphl_list_len");
  } else {
    emitText("movq $" + std::to_string(n) + ", %rax");
  }
  emitText("movq %rax, " + slotRef(ns));
  if (isBatch) {
    // P = ceil(N / B) = (N + B - 1) / B (N, B positivos)
    emitText("addq $" + std::to_string(B - 1) + ", %rax");
    emitText("movq $" + std::to_string(B) + ", %rcx");
    emitText("cqto");
    emitText("idivq %rcx");
  } else {
    // particionamento ADAPTATIVO (v0.23.2): P = max(1, min(workers, N))
    emitRuntimeCall("hphl_worker_count"); // W (inicializa o pool)
    emitText("movq " + slotRef(ns) + ", %rcx"); // N
    emitText("cmpq %rcx, %rax");
    emitText("cmovg %rcx, %rax"); // W > N â†’ N (N partes bastam)
    emitText("testq %rax, %rax");
    std::string p1 = newLabel("pf_gt0");
    emitText("jne " + p1);
    emitText("movq $1, %rax"); // N < 1 nÃ£o ocorre (bounds); defesa
    emitText(p1 + ":");
  }
  emitText("movq %rax, " + slotRef(ps));
  std::vector<std::pair<std::string, Type>> caps;
  caps.push_back({"__coll", st->collection->exprType});
  if (isBatch) {
    caps.push_back({"__start", Type::makeInt(64)});
    caps.push_back({"__count", Type::makeInt(64)});
  } else {
    caps.push_back({"__p", Type::makeInt(64)});
    caps.push_back({"__step", Type::makeInt(64)});
  }
  FunctionDecl* fn = makeTaskFunction(makeParallelForeachBody(st, isList, n, isBatch),
                                      caps, "");
  // loop de spawns: i < P (executa em runtime; P pode ser > 4 p/ listas
  // grandes â€” as partes sobram na fila e o pool drena)
  int is_ = nextSlot_++; // contador i
  emitText("movq $0, %rax");
  emitText("movq %rax, " + slotRef(is_));
  std::string lp = newLabel("pf_sp");
  std::string lpEnd = newLabel("pf_spe");
  emitText(lp + ":");
  emitText("movq " + slotRef(is_) + ", %rax");
  emitText("cmpq " + slotRef(ps) + ", %rax");
  emitText("jge " + lpEnd);
  genCallMalloc(24); // env (mesmo tamanho nos dois modos)
  if (isList) {
    auto* l = findLocal(st->collectionName);
    if (l) emitText("movq " + slotRef(l->slot) + ", %r10");
    else emitText("xorq %r10, %r10");
  } else {
    emitText("leaq .Lg_" + st->collectionName + "(%rip), %r10"); // array global
  }
  emitText("movq %r10, 0(%rax)");
  if (isBatch) {
    // env[8] = start = i*B; env[16] = count = min(B, N - start)
    emitText("movq " + slotRef(is_) + ", %r10");
    emitText("imulq $" + std::to_string(B) + ", %r10, %r10");
    emitText("movq %r10, 8(%rax)");
    emitText("movq " + slotRef(ns) + ", %r10");
    emitText("subq 8(%rax), %r10"); // N - start
    std::string c1 = newLabel("pf_cnt");
    emitText("cmpq $" + std::to_string(B) + ", %r10");
    emitText("jle " + c1);
    emitText("movq $" + std::to_string(B) + ", %r10");
    emitText(c1 + ":");
    emitText("movq %r10, 16(%rax)");
  } else {
    // env[8] = i (__p); env[16] = P (__step) â€” body faz o loop strided
    emitText("movq " + slotRef(is_) + ", %r10");
    emitText("movq %r10, 8(%rax)");
    emitText("movq " + slotRef(ps) + ", %r10");
    emitText("movq %r10, 16(%rax)");
  }
  emitText("movq %rax, %rdx");
  emitText("leaq " + fnLabel(fn) + "(%rip), %rcx");
  emitRuntimeCall("hphl_spawn_task_ex");
  emitText("movq " + slotRef(is_) + ", %rax");
  emitText("addq $1, %rax");
  emitText("movq %rax, " + slotRef(is_));
  emitText("jmp " + lp);
  emitText(lpEnd + ":");
  emitRuntimeCall(nested ? "hphl_region_join" : "hphl_join_tasks"); // barreira
}

// corpo sintÃ©tico da tarefa (nunca passa pela semÃ¢ntica â€” os nÃ³s sÃ£o
// construÃ­dos aqui com as mesmas formas do cÃ³digo do usuÃ¡rio):
//   STRIDE:  [list] __n = __coll.Length;   for (__j = __p; __j < N; __j += __step)
//   BATCH :  (sem __n)                     for (__j = __start; __j < __start + __count; __j++)
//   em ambos: { <item> x = __coll[__j]; <instruÃ§Ãµes originais> }
std::unique_ptr<BlockStmt> Codegen::makeParallelForeachBody(ForeachStmt* st,
                                                            bool isList,
                                                            long long n,
                                                            bool isBatch) {
  auto makeIdent = [](const std::string& name, const Type& t) {
    auto id = std::make_unique<IdentExpr>();
    id->name = name;
    id->exprType = t;
    return id;
  };
  Type tInt = Type::makeInt(64);
  auto body = std::make_unique<BlockStmt>();
  body->line = st->line;
  if (isList && !isBatch) {
    // STRIDE com list: __n = __coll.Length (tamanho em runtime)
    auto len = std::make_unique<MemberExpr>();
    len->object = makeIdent("__coll", st->collection->exprType);
    len->member = "Length";
    len->isListLength = true;
    len->exprType = tInt;
    auto vd = std::make_unique<VarDecl>();
    vd->line = st->line;
    vd->name = "__n";
    vd->type = tInt;
    vd->init = std::move(len);
    auto svd = std::make_unique<StmtVarDecl>();
    svd->line = st->line;
    svd->decls.push_back(std::move(vd));
    body->stmts.push_back(std::move(svd));
  }
  auto loop = std::make_unique<ForStmt>();
  loop->line = st->line;
  auto jd = std::make_unique<VarDecl>();
  jd->line = st->line;
  jd->name = "__j";
  jd->type = tInt;
  jd->init = makeIdent(isBatch ? "__start" : "__p", tInt);
  auto jInit = std::make_unique<StmtVarDecl>();
  jInit->line = st->line;
  jInit->decls.push_back(std::move(jd));
  loop->init = std::move(jInit);
  auto lt = std::make_unique<BinaryExpr>();
  lt->line = st->line;
  lt->op = BinOp::Lt;
  lt->lhs = makeIdent("__j", tInt);
  lt->exprType = Type::makeBool();
  if (isBatch) {
    // batch: limite dinÃ¢mico = __start + __count (nÃ£o precisa de N no corpo)
    auto up = std::make_unique<BinaryExpr>();
    up->line = st->line;
    up->op = BinOp::Add;
    up->lhs = makeIdent("__start", tInt);
    up->rhs = makeIdent("__count", tInt);
    up->exprType = tInt;
    lt->rhs = std::move(up);
  } else if (isList) {
    lt->rhs = makeIdent("__n", tInt);
  } else {
    auto nn = std::make_unique<IntLitExpr>();
    nn->line = st->line;
    nn->value = n;
    nn->exprType = tInt;
    lt->rhs = std::move(nn);
  }
  loop->cond = std::move(lt);
  if (isBatch) {
    // __j++ (EXPRESSÃƒO de passo â€” PostInc devolve o valor ANTIGO; nÃ£o usar
    // `__j = __j++`, que resetaria o contador â†’ loop infinito)
    auto inc = std::make_unique<UnaryExpr>();
    inc->line = st->line;
    inc->op = UnOp::PostInc;
    inc->operand = makeIdent("__j", tInt);
    inc->exprType = tInt;
    loop->step = std::move(inc);
  } else {
    auto assign = std::make_unique<AssignExpr>();
    assign->line = st->line;
    assign->op = AssignOp::Plain;
    assign->target = makeIdent("__j", tInt);
    assign->exprType = tInt;
    auto add = std::make_unique<BinaryExpr>();
    add->line = st->line;
    add->op = BinOp::Add;
    add->lhs = makeIdent("__j", tInt);
    add->rhs = makeIdent("__step", tInt);
    add->exprType = tInt;
    assign->value = std::move(add);
    loop->step = std::move(assign);
  }
  auto inner = std::make_unique<BlockStmt>();
  inner->line = st->line;
  auto ivd = std::make_unique<VarDecl>();
  ivd->line = st->line;
  ivd->name = st->itemName;
  ivd->type = st->itemType;
  auto iidx = std::make_unique<IndexExpr>();
  iidx->line = st->line;
  iidx->object = makeIdent("__coll", st->collection->exprType);
  iidx->index = makeIdent("__j", tInt);
  iidx->exprType = st->itemType;
  ivd->init = std::move(iidx);
  auto isvd = std::make_unique<StmtVarDecl>();
  isvd->line = st->line;
  isvd->decls.push_back(std::move(ivd));
  inner->stmts.push_back(std::move(isvd));
  if (auto blk = dynamic_cast<BlockStmt*>(st->body.get())) {
    for (auto& bstmt : blk->stmts) inner->stmts.push_back(std::move(bstmt));
  } else {
    inner->stmts.push_back(std::move(st->body));
  }
  loop->body = std::move(inner);
  body->stmts.push_back(std::move(loop));
  return body;
}

void Codegen::genPendingTasks() {
  // loop por ÃNDICE: emitir uma tarefa pode criar outras (paralelismo
  // aninhado â€” `parallel foreach`/`parallel` dentro de uma tarefa) e o
  // push_back invalidaria iteradores de um range-for
  for (size_t i = 0; i < pendingTasks_.size(); i++) genFunction(pendingTasks_[i]);
  pendingTasks_.clear();
}

void Codegen::genThrowStmt(ThrowStmt* st) {
  needExc_ = true;
  std::string okL = newLabel("throw_ok");
  genExpr(st->value.get()); // valor em %rax (int/ref) ou %xmm0 (float)
  emitText("movq hphl_exc_hdl(%rip), %rcx");
  emitText("testq %rcx, %rcx");
  emitText("jne " + okL);
  // sem handler ativo: erro fatal de runtime
  std::string msg = internString("throw sem 'catch' ativo");
  emitText("leaq " + msg + "(%rip), %rcx");
  emitRuntimeCall("hphl_panic"); // nÃ£o retorna
  emitText(okL + ":");
  // payload no registro (32) e salto para o handler (restaura rsp e rbp)
  if (isFloatType(st->value->exprType)) emitText("movq %xmm0, 32(%rcx)");
  else emitText("movq %rax, 32(%rcx)");
  emitText("movq 16(%rcx), %rbp");
  emitText("movq 8(%rcx), %rsp");
  emitText("jmpq *0(%rcx)");
}

void Codegen::genVarDecl(VarDecl* v) {
  Local l;
  l.type = v->type;
  l.policy = v->storage;
  l.slot = nextSlot_++;
  l.atomic = v->atomic;
  if (v->isGlobal) {
    l.isGlobal = true;
    l.globalLabel = ".Lg_" + v->name;
  }

  // arrays stack: reserva slots contÃ­guos; slots DECRESCEM no frame, entÃ£o a
  // base do array Ã© o ÃšLTIMO slot do bloco (endereÃ§o mais baixo) para que os
  // elementos base+i*elSize fiquem todos dentro do bloco
  if (!v->isGlobal && l.type.kind == Type::Kind::Array &&
      !isPointerPolicy(v->storage)) {
    int slots = (typeSize(l.type) + 7) / 8;
    nextSlot_ += slots - 1;
    l.slot += slots - 1;
  }

  scopes_.back()[v->name] = l;
  if (debug_ && !v->isGlobal)
    dbgAddLocal(v->name, l.type, l.slot, false, false,
                l.type.kind == Type::Kind::Array &&
                    !isPointerPolicy(v->storage));

  if (v->isGlobal) {
    // valores iniciais constantes sÃ£o emitidos na seÃ§Ã£o .data (ver generate())
    // inicializadores dinÃ¢micos de globais nÃ£o sÃ£o suportados no M1
    return;
  }

  if (l.type.kind == Type::Kind::Array) {
    if (isPointerPolicy(v->storage)) {
      // M10.2: alocaÃ§Ã£o por polÃ­tica (arena/pool/shared/heap)
      genPolicyAlloc(v->storage, typeSize(l.type));
      emitText("movq %rax, " + slotRef(l.slot));
      heapSlots_.push_back({l.slot, v->storage, typeSize(l.type), l.type.kind==Type::Kind::Class?l.type.name:""});
      gcRefSlots_.push_back(l.slot);  // M28 28.1
    } else if (!v->init) {
      // v0.46: array stack nasce ZERADO (antes ficava com lixo da frame)
      long long qwords = (typeSize(l.type) + 7) / 8;
      emitText("leaq " + slotRefMem(l.slot) + ", %rdi");
      emitText("xorl %eax, %eax");
      emitText("movq $" + std::to_string(qwords) + ", %rcx");
      emitText("rep stosq");
    }
    if (v->init && v->init->kind == ExprKind::ArrayLit) {
      storeArrayLit(l, l.type, static_cast<ArrayLitExpr*>(v->init.get()), 0);
    }
    return;
  }

  if (l.type.kind == Type::Kind::List) {
    // list: init com valor avalia e guarda; sem init cria vazia (ver HIR path)
    if (v->init) {
      genExpr(v->init.get());
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
    gcRefSlots_.push_back(l.slot);  // M28 28.1
    return;
  }

  if (l.type.kind == Type::Kind::Mutex || l.type.kind == Type::Kind::Semaphore ||
      l.type.kind == Type::Kind::Event || l.type.kind == Type::Kind::Barrier) {
    // primitivas de sincronizaÃ§Ã£o (v0.24.0): o handle nasce na declaraÃ§Ã£o
    // (semaphore/barrier recebem o contador em rcx; mutex/event nÃ£o)
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
    // classes vivem na heap no M1 (calloc + ctor opcional); structs: semÃ¢ntica
    // de valor â€” inicializaÃ§Ã£o a partir de expressÃ£o copia o bloco (exceto
    // construtor/new, que jÃ¡ produzem bloco fresco)
    if (v->init) {
      genExpr(v->init.get());
      if (isStructType(l.type) && !isFreshAllocExpr(v->init.get()))
        genStructCopy(l.type);
    } else if (isInterfaceType(l.type)) {
      // A2 (interface como tipo): sem init nasce null (sem ctor/default)
      emitText("xorl %eax, %eax");
    } else {
      // M10.2: alocação da classe por política
      genPolicyAlloc(v->storage, classSize(l.type.name));
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
    // M28 28.1: slots que guardam refs GC-managed são registrados como roots
    // (sempre, independente da storage policy — o GC precisa alcançar o objeto
    // a partir do slot para liberá-lo corretamente).
    // M28 28.4: cobre TANTO sem-init QUANTO com-init (= new Node(...)).
    // Antes só registrava se !v->init, fazendo o caso `Node n = new Node(...)`
    // (binary-trees) escapar do root set — o GC não alcançava o objeto.
    gcRefSlots_.push_back(l.slot);
    // M10: alias de outra variï¿½vel classe Nï¿½O registra segunda liberaï¿½ï¿½o
    // M14.4: auto-free de local classe REMOVIDO exceto polï¿½ticas com posse
    // explï¿½cita ï¿½ sem anï¿½lise de escape, objetos guardados em list/map/campo
    // sofriam double free no epï¿½logo (Token em list<Token> liberado como
    // local). O default (Stack/Heap) jï¿½ vazava por sobrescrita; o free final
    // era o ï¿½nico inconsistente.
    if (v->storage == StoragePolicy::Arena || v->storage == StoragePolicy::Pool ||
        v->storage == StoragePolicy::Shared)
      heapSlots_.push_back({l.slot, v->storage, classSize(l.type.name), l.type.name});
      return;
  }

  if (l.type.kind == Type::Kind::Tuple) {
    // M10.1b: tuple local = handle de bloco heap (NÃ—8). Literal jÃ¡ aloca;
    // sem init: calloc zerado. AtribuiÃ§Ã£o posterior clona (valor).
    int n = (int)l.type.tupleElems.size();
    if (v->init) {
      genExpr(v->init.get());
      // semÃ¢ntica de valor: clona se a origem nÃ£o Ã© um literal fresh
      if (v->init->kind != ExprKind::TupleLit) {
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
    gcRefSlots_.push_back(l.slot);  // M28 28.1
    return;
  }

  if (isPointerPolicy(v->storage)) {
    // primitivo/string com polÃ­tica de ponteiro (heap/arena/pool/shared):
    // o slot guarda o ponteiro; alocado e liberado conforme a polÃ­tica
    genPolicyAlloc(v->storage, 8); // M10.2
    emitText("movq %rax, " + slotRef(l.slot));
    heapSlots_.push_back({l.slot, v->storage, 8, ""});
    gcRefSlots_.push_back(l.slot);  // M28 28.1
    if (v->init) {
      if (isFloatType(l.type)) {
        genExpr(v->init.get());
        emitText("movq " + slotRef(l.slot) + ", %rcx");
        emitText("movsd %xmm0, (%rcx)");
      } else {
        genExpr(v->init.get());
        emitText("movq " + slotRef(l.slot) + ", %rcx");
        emitText("movq %rax, (%rcx)");
      }
    }
    return;
  }

  // normal (stack)
  if (v->init) {
    genExpr(v->init.get());
    if (isFloatType(l.type)) {
      // M11-bench: init inteiro em double — converte antes de gravar
      if (!isFloatType(v->init->exprType))
        emitText("cvtsi2sdq %rax, %xmm0");
      emitText("movsd %xmm0, " + slotRef(l.slot));
    } else {
      emitText("movq %rax, " + slotRef(l.slot));
    }
  }
  // M31: local string é root do GC (ver paridade em expr_hir.cpp).
  if (l.type.kind == Type::Kind::String) {
    gcRefSlots_.push_back(l.slot);
  }
}




} // namespace hphl