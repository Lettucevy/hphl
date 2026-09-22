// Auto-generated from codegen.cpp
#include "codegen.h"
#include <cstdio>
#include <functional>
#include "stdlib/stdbuiltins.h"
#include <cstdint>
#include <cstring>

/* M27: hphl_set_class_desc â€” called at startup to register class bitmaps. */
extern "C" int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);

// site de chamada de funÃ§Ã£o `async`: aloca o env dos parÃ¢metros (por valor),
// spawna a thread da tarefa e devolve o HphlTask* (o valor da `task<T>`);
// `await F()` / `t.Wait()` re-aguardam o mesmo handle.

namespace hphl {
void Codegen::genAsyncCall(CallExpr* c) {
  FunctionDecl* f = c->resolved;
  std::vector<Expr*> full;
  full.reserve(f->params.size());
  for (size_t i = 0; i < f->params.size(); i++)
    full.push_back(i < c->args.size() ? c->args[i].get()
                                      : f->params[i]->defaultVal.get());
  // A9 (async array): array é copiado (dados) p/ dentro do env (ver HIR path)
  auto arrBytes = [&](const Expr* a) -> long long {
    if (!a || a->exprType.kind != Type::Kind::Array || !a->exprType.elem) return 0;
    if (a->exprType.arraySize <= 0) return 0; // dimensão simbólica: sem cópia
    return (long long)typeSize(a->exprType);
  };
  if (full.empty()) {
    emitText("xorq %rdx, %rdx"); // env nulo (sem parâmetros)
  } else {
    int h = nextSlot_++; // slot temporário com o ponteiro do env
    long long dataOff = (long long)full.size() * 8;
    long long total = dataOff;
    for (auto* a : full) total += arrBytes(a);
    genCallMalloc(total);
    emitText("movq %rax, " + slotRef(h));
    dataOff = (long long)full.size() * 8;
    for (size_t i = 0; i < full.size(); i++) {
      long long ab = arrBytes(full[i]);
      if (ab > 0) {
        long long nq = ab / 8;
        genExpr(full[i]); // rax = base origem
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
        genExpr(full[i]);
        if (isFloatType(full[i]->exprType)) emitText("movq %xmm0, %rax");
        emitText("movq %rax, %r10");
        emitText("movq " + slotRef(h) + ", %rax");
        emitText("movq %r10, " + std::to_string(8 * i) + "(%rax)");
      }
    }
    emitText("movq " + slotRef(h) + ", %rdx");
  }
  emitText("leaq " + fnLabel(f) + "(%rip), %rcx");
  emitRuntimeCall("hphl_spawn_task_ex");
}

void Codegen::genCall(CallExpr* c) {
  if (c->isPrint) {
    genPrint(c);
    return;
  }
  if (c->isClockNs) {
    // clock_ns() (v0.22.9): QPC do runtime em nanossegundos â†’ %rax
    emitRuntimeCall("hphl_clock_ns");
    return;
  }
  if (c->isSqrt) {
    // M11-bench: sqrt(x) â€” sqrtsd (arg double jÃ¡ em xmm0; resultado xmm0)
    genExpr(c->args[0].get());
    emitText("sqrtsd %xmm0, %xmm0");
    return;
  }
  if (c->isArenaReset) {
    // M11.9: arena_reset() â€” devolve todos os blocos da frame-arena
    // (o handle zera; o prÃ³ximo alloc recomeÃ§a do zero)
    if (arenaHandleSlot_ < 0) {
      int h = nextSlot_++;
      arenaHandleSlot_ = h;
      emitText("movq $0, " + slotRef(h));
    }
    emitText("leaq " + slotRefMem(arenaHandleSlot_) + ", %rcx");
    emitRuntimeCall("hphl_arena_reset");
    return;
  }
  if (c->isStrEq || c->isStrCmp) {
    // __hphl_str_eq(a, b) / __hphl_str_cmp(a, b) (v0.25.0)
    int h = nextSlot_++;
    genExpr(c->args[0].get());
    emitText("movq %rax, " + slotRef(h));
    genExpr(c->args[1].get());
    emitText("movq %rax, %rdx");
    emitText("movq " + slotRef(h) + ", %rcx");
    emitRuntimeCall(c->isStrEq ? "hphl_str_eq" : "hphl_str_cmp");
    return;
  }
  if (c->isToStr) {
    // M5 (v0.36.0): __hphl_to_str(x)/toString(x) — escalar → string heap
    genExpr(c->args[0].get());
    Type at = c->args[0]->exprType;
    if (at.kind == Type::Kind::Bool) emitRuntimeCall("hphl_str_from_bool");
    else if (at.kind == Type::Kind::Char) emitRuntimeCall("hphl_str_from_char");
    else if (at.kind == Type::Kind::Float) emitRuntimeCall("hphl_str_from_float");
    else if (at.kind == Type::Kind::UInt) emitRuntimeCall("hphl_str_from_uint");
    else emitRuntimeCall("hphl_str_from_int");
    return;
  }
  if (c->isAddrOf) {
    // FFI v2: addr_of(x) — endereço do lvalue em %rax (ponteiro opaco)
    genAddr(c->args[0].get());
    return;
  }
  if (c->callee->kind == ExprKind::OptMember) {
    genOptCall(c);
    return;
  }
  if (c->isListAdd) {
    genListAdd(c);
    return;
  }
  if (c->isMapPut) {
    auto mem = static_cast<MemberExpr*>(c->callee.get());
    emitText("subq $32, %rsp");
    genExpr(mem->object.get()); emitText("movq %rax, (%rsp)");
    genExpr(c->args[0].get()); emitText("movq %rax, 8(%rsp)");
    genExpr(c->args[1].get());
    if (isFloatType(c->args[1]->exprType)) emitText("movq %rax, 16(%rsp)");
    else emitText("movq %rax, 16(%rsp)");
    emitText("movq (%rsp), %rcx"); emitText("movq 8(%rsp), %rdx"); emitText("movq 16(%rsp), %r8");
    emitRuntimeCall("hphl_map_put");
    emitText("addq $32, %rsp");
    return;
  }
  if (c->isMapGet) {
    auto mem = static_cast<MemberExpr*>(c->callee.get());
    emitText("subq $16, %rsp");
    genExpr(mem->object.get()); emitText("movq %rax, (%rsp)");
    genExpr(c->args[0].get()); emitText("movq %rax, 8(%rsp)");
    emitText("movq (%rsp), %rcx"); emitText("movq 8(%rsp), %rdx");
    emitRuntimeCall("hphl_map_get");
    if (isFloatType(c->exprType)) emitText("movq %rax, %xmm0");
    emitText("addq $16, %rsp");
    return;
  }
  if (c->isMapContains) {
    auto mem = static_cast<MemberExpr*>(c->callee.get());
    emitText("subq $16, %rsp");
    genExpr(mem->object.get()); emitText("movq %rax, (%rsp)");
    genExpr(c->args[0].get()); emitText("movq %rax, 8(%rsp)");
    emitText("movq (%rsp), %rcx"); emitText("movq 8(%rsp), %rdx");
    emitRuntimeCall("hphl_map_contains");
    emitText("addq $16, %rsp");
    return;
  }
  if (c->isMapRemove) {
    auto mem = static_cast<MemberExpr*>(c->callee.get());
    emitText("subq $16, %rsp");
    genExpr(mem->object.get()); emitText("movq %rax, (%rsp)");
    genExpr(c->args[0].get()); emitText("movq %rax, 8(%rsp)");
    emitText("movq (%rsp), %rcx"); emitText("movq 8(%rsp), %rdx");
    emitRuntimeCall("hphl_map_remove");
    emitText("addq $16, %rsp");
    return;
  }
  if (c->isMapClear) {
    auto mem = static_cast<MemberExpr*>(c->callee.get());
    genExpr(mem->object.get()); emitText("movq %rax, %rcx"); emitRuntimeCall("hphl_map_clear"); return;
  }
  if (c->isWait) {
    // t.Wait(): espera a conclusÃ£o e devolve o payload (task.remove)
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("movq %rax, %rcx");
    emitRuntimeCall("hphl_wait_task");
    if (isFloatType(c->exprType)) emitText("movq %rax, %xmm0");
    return;
  }
  if (c->isTaskCancel) {
    // t.Cancel(): sinal cooperativo de cancelamento (v0.23.0) â€” nÃ£o preempta
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("movq %rax, %rcx");
    emitRuntimeCall("hphl_cancel_task");
    return;
  }
  if (c->isChannelSend) {
    // ch.Send(x): spill do handle (avaliar o argumento clobberaria rcx);
    // valor em rdx (slots de 8 bytes; float passa os bits via xmm0â†’rax)
    int h = nextSlot_++;
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("movq %rax, " + slotRef(h));
    genExpr(c->args[0].get());
    if (isFloatType(c->args[0]->exprType)) emitText("movq %xmm0, %rax");
    emitText("movq %rax, %rdx");
    emitText("movq " + slotRef(h) + ", %rcx");
    emitRuntimeCall("hphl_channel_send");
    return;
  }
  if (c->isChannelReceive) {
    // ch.Receive(): bloqueia atÃ© haver valor; devolve o payload
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("movq %rax, %rcx");
    emitRuntimeCall("hphl_channel_receive");
    if (isFloatType(c->exprType)) emitText("movq %rax, %xmm0");
    return;
  }
  switch (c->primOp) {
    case CallExpr::PrimOp::MutexLock:
      genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_mutex_lock");
      return;
    case CallExpr::PrimOp::MutexUnlock:
      genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_mutex_unlock");
      return;
    case CallExpr::PrimOp::SemaphoreWait:
      genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_semaphore_wait");
      return;
    case CallExpr::PrimOp::SemaphoreSignal:
      genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_semaphore_signal");
      return;
    case CallExpr::PrimOp::EventWait:
      genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_event_wait");
      return;
    case CallExpr::PrimOp::EventSet:
      genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_event_set");
      return;
    case CallExpr::PrimOp::EventReset:
      genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_event_reset");
      return;
    case CallExpr::PrimOp::BarrierWait:
      genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_barrier_wait");
      return;
    case CallExpr::PrimOp::None:
      break;
  }
  if (c->isResultIsOk) {
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("cmpq $0, (%rax)");
    emitText("sete %al");
    emitText("movzbq %al, %rax");
    return;
  }
  if (c->isResultIsErr) {
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("cmpq $1, (%rax)");
    emitText("sete %al");
    emitText("movzbq %al, %rax");
    return;
  }
  if (c->isOptionIsSome) {
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("cmpq $1, (%rax)");
    emitText("sete %al");
    emitText("movzbq %al, %rax");
    return;
  }
  if (c->isOptionIsNone) {
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("cmpq $0, (%rax)");
    emitText("sete %al");
    emitText("movzbq %al, %rax");
    return;
  }
  if (c->isResultUnwrap) {
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    std::string okL = newLabel("res_unw_ok");
    emitText("cmpq $0, (%rax)");
    emitText("je " + okL);
    std::string msg = internString("unwrap called on Err");
    emitText("leaq " + msg + "(%rip), %rcx");
    emitRuntimeCall("hphl_panic");
    emitText(okL + ":");
    if (isFloatType(c->exprType)) emitText("movsd 8(%rax), %xmm0");
    else emitText("movq 8(%rax), %rax");
    return;
  }
  if (c->isOptionUnwrap) {
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    std::string okL = newLabel("opt_unw_ok");
    emitText("cmpq $1, (%rax)");
    emitText("je " + okL);
    std::string msg = internString("unwrap called on None");
    emitText("leaq " + msg + "(%rip), %rcx");
    emitRuntimeCall("hphl_panic");
    emitText(okL + ":");
    if (isFloatType(c->exprType)) emitText("movsd 8(%rax), %xmm0");
    else emitText("movq 8(%rax), %rax");
    return;
  }
  if (c->isResultUnwrapErr) {
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    std::string okL = newLabel("res_unw_err_ok");
    emitText("cmpq $1, (%rax)");
    emitText("je " + okL);
    std::string msg = internString("unwrap_err called on Ok");
    emitText("leaq " + msg + "(%rip), %rcx");
    emitRuntimeCall("hphl_panic");
    emitText(okL + ":");
    if (isFloatType(c->exprType)) emitText("movsd 8(%rax), %xmm0");
    else emitText("movq 8(%rax), %rax");
    return;
  }
  if (c->isResultUnwrapOr) {
    int h = nextSlot_++;
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("movq %rax, " + slotRef(h));
    emitText("cmpq $0, (%rax)");
    std::string elseL = newLabel("res_unw_or_else");
    std::string endL = newLabel("res_unw_or_end");
    emitText("jne " + elseL);
    emitText("movq " + slotRef(h) + ", %rax");
    if (isFloatType(c->exprType)) emitText("movsd 8(%rax), %xmm0");
    else emitText("movq 8(%rax), %rax");
    emitText("jmp " + endL);
    emitText(elseL + ":");
    genExpr(c->args[0].get());
    emitText(endL + ":");
    return;
  }
  if (c->isOptionUnwrapOr) {
    int h = nextSlot_++;
    genExpr(static_cast<MemberExpr*>(c->callee.get())->object.get());
    emitText("movq %rax, " + slotRef(h));
    emitText("cmpq $1, (%rax)");
    std::string elseL = newLabel("opt_unw_or_else");
    std::string endL = newLabel("opt_unw_or_end");
    emitText("jne " + elseL);
    emitText("movq " + slotRef(h) + ", %rax");
    if (isFloatType(c->exprType)) emitText("movsd 8(%rax), %xmm0");
    else emitText("movq 8(%rax), %rax");
    emitText("jmp " + endL);
    emitText(elseL + ":");
    genExpr(c->args[0].get());
    emitText(endL + ":");
    return;
  }
  if (c->isAsyncCall) {
    // F(...) async: monta o env (params por valor), spawna e devolve a task
    genAsyncCall(c);
    return;
  }
  if (c->isEnumCtor) {
    std::vector<Expr*> args;
    for (auto& a : c->args) args.push_back(a.get());
    genEnumCtor(c->enumCtorEnum, c->enumCtorIndex, args);
    return;
  }
  if (c->isFromInt) {
    genFromInt(c->fromIntEnum, c->args[0].get());
    return;
  }
  if (c->isSerialize) {
    // `e.Serialize()` â€” valor inteiro (tag) do enum; rico: tag no inÃ­cio da cÃ©lula
    Expr* obj = static_cast<MemberExpr*>(c->callee.get())->object.get();
    genExpr(obj);
    if (sem_.isRichEnum(c->serializeEnum)) emitText("movq (%rax), %rax");
    return;
  }
  if (c->isActorCall) {
    // chamada EXTERNA a mÃ©todo de actor (v0.23.0): avalia `this`, serializa
    // pelo spinlock por objeto (hphl_locks[slot(ptr)]), chama e libera.
    // Chamadas internas (`this.m()`) nÃ£o passam por aqui (ver semÃ¢ntica).
    emitActorCall(c);
    return;
  }
  Expr* thisArg = nullptr;
  if (c->callee->kind == ExprKind::Member && !c->isModuleCall) {
    if (c->isBaseCall) {
      // M10: `base.M(...)` â€” this implÃ­cito, chamada estÃ¡tica (sem vtable)
      IdentExpr th;
      th.name = "this";
      th.exprType = Type::makeClass(curFn_ ? curFn_->ownerClass : "");
      std::vector<Expr*> bargs;
      for (auto& a : c->args) bargs.push_back(a.get());
      genCallInternal(c->resolved, bargs, &th, /*forceStatic=*/true);
      return;
    }
    thisArg = static_cast<MemberExpr*>(c->callee.get())->object.get();
  }
  std::vector<Expr*> args;
  for (auto& a : c->args) args.push_back(a.get());
  genCallInternal(c->resolved, args, thisArg);
}

// chamada EXTERNA a mÃ©todo de actor (v0.23.0): `this` Ã© avaliado UMA vez e
// guardado em slot; lock do objeto (hphl_lock_begin); chamada normal com o
// mesmo thisArg (reavaliaÃ§Ã£o de `this`/captura Ã© pura e barata); o RESULTADO
// Ã© spÃ­llado antes do unlock (a runtime pode clobber %rax); unlock; restaura.
// M2: se o mÃ©todo lanÃ§ar exceÃ§Ã£o o lock fica retido (documentado).
void Codegen::emitActorCall(CallExpr* c) {
  auto* mem = static_cast<MemberExpr*>(c->callee.get());
  int h = nextSlot_++;
  genExpr(mem->object.get());
  emitText("movq %rax, " + slotRef(h));
  emitText("movq " + slotRef(h) + ", %rcx");
  emitRuntimeCall("hphl_lock_begin");
  std::vector<Expr*> args;
  for (auto& a : c->args) args.push_back(a.get());
  genCallInternal(c->resolved, args, mem->object.get());
  bool rFloat = isFloatType(c->exprType);
  if (c->exprType.kind != Type::Kind::Void) {
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
}

void Codegen::genCallInternal(FunctionDecl* fn, std::vector<Expr*>& args, Expr* thisArg,
                              bool forceStatic) {
  // parÃ¢metros opcionais: completa a lista com os valores padrÃ£o (literais)
  std::vector<Expr*> full;
  full.reserve(fn->params.size());
  for (size_t i = 0; i < fn->params.size(); i++)
    full.push_back(i < args.size() ? args[i] : fn->params[i]->defaultVal.get());
  args = full;

  std::string label = fnLabel(fn);
  int total = (thisArg ? 1 : 0) + (int)args.size();
  int stackArgs = total > 4 ? total - 4 : 0;

  // shadow space (32B) + stack args; alinhado a 16. Arg k>=4 fica em
  // [rsp+32+8*(k-4)] no ponto da call â†’ callee lÃª [rbp+48+8*(k-4)].
  int bytes = (32 + stackArgs * 8 + 15) & ~15;
  emitText("subq $" + std::to_string(bytes) + ", %rsp");

  // avalia this (posiÃ§Ã£o 0) e spill no shadow
  if (thisArg) {
    genExpr(thisArg);
    emitText("movq %rax, 0(%rsp)");
  }

  // avalia args: reg args (k<4) spillam no shadow; stack args na Ã¡rea 32+8s
  for (size_t i = 0; i < args.size(); i++) {
    Expr* a = args[i];
    Param* p = fn->params[i].get();
    bool pf = isFloatType(p->type);
    int k = (int)i + (thisArg ? 1 : 0);
    if (p->isByRef()) {
      // ref/out/in: passa o ENDEREÃ‡O do argumento (qualquer tipo em reg. inteiro)
      if (p->byIn && !isAddressable(a)) {
        // `in` com rvalue: materializa num temporÃ¡rio e passa o endereÃ§o
        int t = nextSlot_++;
        genExpr(a);
        emitText("movq %rax, " + slotRef(t));
        emitText("leaq " + slotRefMem(t) + ", %rax");
      } else {
        genAddr(a);
      }
      if (k < 4) {
        emitText("movq %rax, " + std::to_string(8 * k) + "(%rsp)");
      } else {
        emitText("movq %rax, " + std::to_string(32 + 8 * (k - 4)) + "(%rsp)");
      }
      continue;
    }
    genExpr(a);
    if (isStructType(p->type))
      genStructCopy(p->type); // struct por valor: cópia independente na callee
    if (pf && !isFloatType(a->exprType))
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

  // carrega registradores do shadow (depois de todos os genExpr)
  if (thisArg) emitText("movq 0(%rsp), %rcx");
  const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
  for (size_t i = 0; i < args.size(); i++) {
    int k = (int)i + (thisArg ? 1 : 0);
    if (k >= 4) continue;
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

  // M10 (v0.44): dispatch virtual â€” mÃ©todo de instÃ¢ncia com override em
  // alguma derivada chama via vptr do objeto
  // A2 (interface como tipo): receiver interface SEMPRE despacha via vtable
  // (o metodo resolvido e o contrato bodyless — sem corpo p/ chamada estatica)
  bool recvIface = thisArg && isInterfaceType(thisArg->exprType);
  bool virt = thisArg && !forceStatic && fn->isMethod && !fn->isConstructor &&
              ((thisArg->exprType.kind == Type::Kind::Class &&
                !isStructType(thisArg->exprType)) || recvIface);
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

// M28 32.1: TCO self-tail-call (TCO.txt Â§5, Â§21)
//
// canSelfTailCall decide se `return funcSelf(args)` pode virar um tail jump
// que reusa o frame atual. RestriÃ§Ãµes:
//   - funÃ§Ã£o nÃ£o-mÃ©todo (this nÃ£o vira parÃ¢metro de cauda sem ABI extra)
//   - resolved == curFn_
//   - nÃºmero de args == params.size() (sem defaults)
//   - nenhum arg usa `ref`/`out`/`in` (passagem por endereÃ§o inibe reuso)
//   - sem spread/tuple (nÃ£o suportado nessa versÃ£o)
bool Codegen::canSelfTailCall(const CallExpr* c) const {
  if (!curFn_) return false;
  if (!c->resolved) return false;
  if (c->resolved != curFn_) return false;
  if (curFn_->isMethod && !curFn_->isStatic) return false;
  if (c->args.size() != curFn_->params.size()) return false;
  for (auto& p : curFn_->params) {
    if (p->isByRef()) return false;
  }
  return true;
}

// emitSelfTailCall â€” avalia args e grava cada um no slot do parÃ¢metro
// correspondente, depois emite `jmp .L_<fn>_body`. O frame atual Ã©
// preservado (subq jÃ¡ executou na entrada original); epÃ­logo nunca roda.
void Codegen::emitSelfTailCall(CallExpr* c) {
  for (size_t i = 0; i < c->args.size(); i++) {
    Expr* a = c->args[i].get();
    Param* p = curFn_->params[i].get();
    bool pf = isFloatType(p->type);
    genExpr(a);
    if (isStructType(p->type)) genStructCopy(p->type);
    Local* l = findLocal(p->name);
    int slot = l ? l->slot : -1;
    if (slot < 0) {
      emitText("jmp .Lret_" + fnLabel(curFn_));
      return;
    }
    if (pf) emitText("movsd %xmm0, " + slotRef(slot));
    else emitText("movq %rax, " + slotRef(slot));
  }
  emitText("jmp .L_" + fnLabel(curFn_) + "_body");
}

// M28 32.1: variantes HIR dos helpers de TCO (TCO.txt Â§5, Â§21)
bool Codegen::canSelfTailCallHir(const HirCall* c) const {
  if (!curFn_) return false;
  if (!c->resolved) return false;
  if (c->resolved != curFn_) return false;
  if (curFn_->isMethod && !curFn_->isStatic) return false;
  if (c->args.size() != curFn_->params.size()) return false;
  for (auto& p : curFn_->params) {
    if (p->isByRef()) return false;
  }
  return true;
}
void Codegen::emitSelfTailCallHir(const HirCall* c) {
  for (size_t i = 0; i < c->args.size(); i++) {
    HirExpr* a = c->args[i].get();
    Param* p = curFn_->params[i].get();
    bool pf = isFloatType(p->type);
    genHirExpr(a);
    if (isStructType(p->type)) genStructCopy(p->type);
    Local* l = findLocal(p->name);
    int slot = l ? l->slot : -1;
    if (slot < 0) {
      emitText("jmp .Lret_" + fnLabel(curFn_));
      return;
    }
    if (pf) emitText("movsd %xmm0, " + slotRef(slot));
    else emitText("movq %rax, " + slotRef(slot));
  }
  emitText("jmp .L_" + fnLabel(curFn_) + "_body");
}

bool Codegen::isAddressable(Expr* e) const {
  switch (e->kind) {
    case ExprKind::Ident:
      return !static_cast<IdentExpr*>(e)->isProperty;
    case ExprKind::Member: {
      // propriedade NÃƒO Ã© addressable: `in` materializa o valor recebido do
      // getter num temporÃ¡rio; `ref/out` sÃ£o rejeitados na semÃ¢ntica
      auto* m = static_cast<MemberExpr*>(e);
      return !m->isProperty && !m->isEnumConst && !m->isArrayLength &&
             !m->isListLength && !m->isModuleTypeRef && !m->isGlobalRef;
    }
    case ExprKind::Index:
      return true;
    default:
      return false;
  }
}

void Codegen::genPrint(CallExpr* c) {
  for (auto& a : c->args) {
    Type t = a->exprType;
    if (isFloatType(t)) {
      genExpr(a.get());
      emitRuntimeCall("hphl_print_float");
    } else if (t.kind == Type::Kind::String) {
      // M5 (v0.36.0): libera apenas strings que SABEMOS estar na heap â€”
      // concat (Binary de string) e __hphl_to_str. Chamadas de funÃ§Ã£o que
      // retornam string podem devolver literal estÃ¡tico (.rodata): free
      // nelas corrompe a heap (STATUS_HEAP_CORRUPTION nos testes match/
      // option/generics/properties/match_expr).
      bool fresh =
          a->kind == ExprKind::Binary ||
          (a->kind == ExprKind::Call && static_cast<CallExpr*>(a.get())->isToStr);
      int h = -1;
      if (fresh) {
        h = nextSlot_++;
        genExpr(a.get());
        emitText("movq %rax, " + slotRef(h));
      } else {
        genExpr(a.get());
      }
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_string");
      if (h >= 0) {
        emitText("movq " + slotRef(h) + ", %rcx");
        emitRuntimeCall("hphl_str_free");
      }
    } else if (t.kind == Type::Kind::Bool) {
      genExpr(a.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_bool");
    } else if (t.kind == Type::Kind::Char) {
      genExpr(a.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_char");
    } else if (t.kind == Type::Kind::UInt) {
      genExpr(a.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_uint");
    } else {
      genExpr(a.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_print_int");
    }
  }
}




} // namespace hphl