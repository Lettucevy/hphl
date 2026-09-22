// Auto-generated from codegen.cpp
#include "codegen.h"
#include <cstdio>
#include <functional>
#include "stdlib/stdbuiltins.h"
#include <cstdint>
#include <cstring>
static int wb_counter_ast = 0;

/* M27: hphl_set_class_desc â€” called at startup to register class bitmaps. */
extern "C" int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);


namespace hphl {
void Codegen::genExpr(Expr* e) {
  switch (e->kind) {
    case ExprKind::IntLit: {
      auto ex = static_cast<IntLitExpr*>(e);
      emitText("movq $" + std::to_string(ex->value) + ", %rax");
      break;
    }
    case ExprKind::FloatLit: {
      auto ex = static_cast<FloatLitExpr*>(e);
      double v = ex->value;
      uint64_t bits;
      std::memcpy(&bits, &v, 8);
      std::string lbl = newLabel("fld");
      emitRodata(".align 8");
      emitRodata(lbl + ": .quad " + std::to_string((long long)bits));
      emitText("movsd " + lbl + "(%rip), %xmm0");
      break;
    }
    case ExprKind::StringLit: {
      auto ex = static_cast<StringLitExpr*>(e);
      std::string lbl = internString(ex->value);
      emitText("leaq " + lbl + "(%rip), %rax");
      break;
    }
    case ExprKind::CharLit: {
      auto ex = static_cast<CharLitExpr*>(e);
      emitText("movq $" + std::to_string(ex->value) + ", %rax");
      break;
    }
    case ExprKind::BoolLit: {
      auto ex = static_cast<BoolLitExpr*>(e);
      emitText("movq $" + std::string(ex->value ? "1" : "0") + ", %rax");
      break;
    }
    case ExprKind::NullLit: {
      emitText("xorl %eax, %eax");
      break;
    }
    case ExprKind::Spawn: {
      genSpawnExpr(static_cast<SpawnExpr*>(e));
      break;
    }
    case ExprKind::Await: {
      // `await E`: espera a task E e devolve o payload (hphl_wait_task);
      // `await ch.Receive()` (v0.24.0) Ã© aÃ§Ãºcar â€” o Receive jÃ¡ bloqueia
      auto ex = static_cast<AwaitExpr*>(e);
      if (ex->operand->kind == ExprKind::Call &&
          static_cast<CallExpr*>(ex->operand.get())->isChannelReceive) {
        genExpr(ex->operand.get());
        break;
      }
      genExpr(ex->operand.get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_wait_task");
      if (isFloatType(ex->exprType)) emitText("movq %rax, %xmm0");
      break;
    }
    case ExprKind::This: {
      auto* l = findLocal("this");
      if (l) emitText("movq " + slotRef(l->slot) + ", %rax");
      else emitText("xorl %eax, %eax");
      break;
    }
    case ExprKind::Ident: {
      auto ex = static_cast<IdentExpr*>(e);
      // M10 (v0.45): parÃ¢metro de valor da instÃ¢ncia genÃ©rica â†’ literal
      if (curFn_ && !curFn_->litParams.empty()) {
        auto lit = curFn_->litParams.find(ex->name);
        if (lit != curFn_->litParams.end()) {
          emitText("movq $" + std::to_string(lit->second) + ", %rax");
          break;
        }
      }
      auto* l = findLocal(ex->name);
      if (!l && ex->isProperty) {
        // M_RV1 A4: propriedade estatica nao recebe `this`
        std::vector<Expr*> noargs;
        if (ex->propGet->isStatic) {
          genCallInternal(ex->propGet, noargs, nullptr);
        } else {
          IdentExpr th;
          th.name = "this";
          genCallInternal(ex->propGet, noargs, &th);
        }
        break;
      }
      if (!l && ex->symbol.kind == SymbolKind::Field) {
        // campo do `this` sem qualificaÃ§Ã£o (spec Â§19)
        auto* th = findLocal("this");
        int off = ex->symbol.slotIndex;
        if (off < 0) off = 0;
        if (th) {
          emitText("movq " + slotRef(th->slot) + ", %rcx");
          if (isFloatType(ex->exprType)) {
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
      if (!l) { emitText("xorl %eax, %eax"); break; }
      if (l->type.kind == Type::Kind::Array) {
        // array: o valor Ã© o endereÃ§o da base
        emitArrayBaseLoad(*l);
        break;
      }
      if (l->type.kind == Type::Kind::List) {
        // list: o valor Ã© o ponteiro do objeto (sem desreferÃªncia)
        if (l->isGlobal) {
          emitText("movq " + l->globalLabel + "(%rip), %rax");
        } else {
          emitText("movq " + slotRef(l->slot) + ", %rax");
        }
        break;
      }
      if (l->isGlobal) {
        if (l->tlsOffset >= 0) {
          // threadlocal: slot da thread corrente (bloco zerado por thread)
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
        // parÃ¢metro ref/out/in: o slot guarda o endereÃ§o da variÃ¡vel do caller
        emitText("movq " + slotRef(l->slot) + ", %rcx");
        if (isFloatType(l->type)) {
          emitText("movsd (%rcx), %xmm0");
        } else {
          emitText("movq (%rcx), %rax");
        }
      } else if (isPointerPolicy(l->policy)) {
        // heap/arena/pool/shared: o slot guarda o ponteiro; desreferencia
        // (v0.47 FIX: tipos por referÃªncia â€” class/list/map/tuple/array â€”
        // NÃƒO dereferenciam: o slot jÃ¡ Ã© o ponteiro do objeto; o duplo
        // deref lia o VPTR e os campos iam parar na vtable compartilhada)
        emitText("movq " + slotRef(l->slot) + ", %rcx");
        if (l->type.kind == Type::Kind::Class || l->type.kind == Type::Kind::List ||
            l->type.kind == Type::Kind::Map || l->type.kind == Type::Kind::Tuple ||
            l->type.kind == Type::Kind::Array) {
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
    case ExprKind::Member: {
      auto ex = static_cast<MemberExpr*>(e);
      if (ex->isProperty) {
        // M_RV1 A4: propriedade estatica nao recebe `this`
        std::vector<Expr*> noargs;
        Expr* thisArg = ex->propGet->isStatic ? nullptr : ex->object.get();
        genCallInternal(ex->propGet, noargs, thisArg);
        break;
      }
      if (ex->isEnumCtor) {
        genEnumCtor(ex->enumCtorEnum, ex->enumCtorIndex, {});
        break;
      }
      if (ex->isEnumConst) {
        emitText("movq $" + std::to_string(ex->enumValue) + ", %rax");
        break;
      }
      if (ex->isModuleTypeRef) {
        // Mod.Classe/Mod.Enum como valor: sem runtime (sÃ³ tipo)
        emitText("xorl %eax, %eax");
        break;
      }
      if (ex->isGlobalRef) {
        auto tit = tlsOffsets_.find(ex->resolvedGlobal->name);
        if (tit != tlsOffsets_.end()) {
          // threadlocal de outro mÃ³dulo: slot da thread corrente
          emitRuntimeCall("hphl_tls_block");
          emitText("movq " + std::to_string(tit->second) + "(%rax), %rax");
        } else {
          emitText("movq .Lg_" + ex->resolvedGlobal->name + "(%rip), %rax");
        }
        break;
      }
      if (ex->isTaskCancelled) {
        // Task.IsCancelled: flag de cancelamento da tarefa corrente (TLS)
        emitRuntimeCall("hphl_task_iscancelled");
        break;
      }
      if (ex->isArrayLength) {
        // arr.Length: tamanho constante definido pela anÃ¡lise semÃ¢ntica
        emitText("movq $" + std::to_string(ex->enumValue) + ", %rax");
        break;
      }
      if (ex->isListLength) {
        // list.Length: tamanho dinÃ¢mico consultado no runtime
        genExpr(ex->object.get());
        emitText("movq %rax, %rcx");
        emitRuntimeCall("hphl_list_len");
        break;
      }
      if (ex->isMapLength) {
        genExpr(ex->object.get());
        emitText("movq %rax, %rcx");
        emitRuntimeCall("hphl_map_len");
        break;
      }
      if (ex->isResultIsOk) {
        genExpr(ex->object.get());
        emitText("cmpq $0, (%rax)");
        emitText("sete %al");
        emitText("movzbq %al, %rax");
        break;
      }
      if (ex->isResultIsError) {
        genExpr(ex->object.get());
        emitText("cmpq $1, (%rax)");
        emitText("sete %al");
        emitText("movzbq %al, %rax");
        break;
      }
      if (ex->isOptionHasValue) {
        genExpr(ex->object.get());
        emitText("cmpq $1, (%rax)");
        emitText("sete %al");
        emitText("movzbq %al, %rax");
        break;
      }
      if (ex->isOptionIsNone) {
        genExpr(ex->object.get());
        emitText("cmpq $0, (%rax)");
        emitText("sete %al");
        emitText("movzbq %al, %rax");
        break;
      }
      if (ex->isResultValue) {
        genExpr(ex->object.get());
        std::string okL = newLabel("res_val_ok");
        emitText("cmpq $0, (%rax)");
        emitText("je " + okL);
        std::string msg = internString("Result.Value called on Err");
        emitText("leaq " + msg + "(%rip), %rcx");
        emitRuntimeCall("hphl_panic");
        emitText(okL + ":");
        if (isFloatType(ex->exprType)) emitText("movsd 8(%rax), %xmm0");
        else emitText("movq 8(%rax), %rax");
        break;
      }
      if (ex->isResultError) {
        genExpr(ex->object.get());
        std::string okL = newLabel("res_err_ok");
        emitText("cmpq $1, (%rax)");
        emitText("je " + okL);
        std::string msg = internString("Result.Error called on Ok");
        emitText("leaq " + msg + "(%rip), %rcx");
        emitRuntimeCall("hphl_panic");
        emitText(okL + ":");
        if (isFloatType(ex->exprType)) emitText("movsd 8(%rax), %xmm0");
        else emitText("movq 8(%rax), %rax");
        break;
      }
      if (ex->isOptionValue) {
        genExpr(ex->object.get());
        std::string okL = newLabel("opt_val_ok");
        emitText("cmpq $1, (%rax)");
        emitText("je " + okL);
        std::string msg = internString("Option.Value called on None");
        emitText("leaq " + msg + "(%rip), %rcx");
        emitRuntimeCall("hphl_panic");
        emitText(okL + ":");
        if (isFloatType(ex->exprType)) emitText("movsd 8(%rax), %xmm0");
        else emitText("movq 8(%rax), %rax");
        break;
      }
      if (ex->exprType.kind == Type::Kind::Array) {
        // campo array: o valor Ã© o endereÃ§o (objeto + offset)
        genExpr(ex->object.get());
        emitText("movq %rax, %rdx");
        if (ex->fieldOffset != 0) {
          emitText("addq $" + std::to_string(ex->fieldOffset) + ", %rdx");
        }
        emitText("movq %rdx, %rax");
        break;
      }
      genExpr(ex->object.get());
      emitText("movq %rax, %rdx");
      if (isFloatType(ex->exprType)) {
        emitText("movsd " + std::to_string(ex->fieldOffset) + "(%rdx), %xmm0");
      } else {
        emitText("movq " + std::to_string(ex->fieldOffset) + "(%rdx), %rax");
      }
      break;
    }
    case ExprKind::Call:
      // v0.46: chamada `compiletime` dobrada na semÃ¢ntica
      if (static_cast<CallExpr*>(e)->folded) {
        emitText("movq $" + std::to_string(static_cast<CallExpr*>(e)->foldValue) +
                 ", %rax");
        break;
      }
      genCall(static_cast<CallExpr*>(e));
      break;
    case ExprKind::Index: {
      auto ex = static_cast<IndexExpr*>(e);
      if (ex->object->exprType.kind == Type::Kind::String) {
        // s[i] — spill do objeto (índice pode clobberar rcx; ver HIR path)
        emitText("subq $16, %rsp");
        genExpr(ex->object.get());
        emitText("movq %rax, (%rsp)");
        genExpr(ex->index.get());
        emitText("movq %rax, %rdx");
        emitText("movq (%rsp), %rcx");
        emitText("addq $16, %rsp");
        emitRuntimeCall("hphl_str_char_index");
        break;
      }
      if (ex->object->exprType.kind == Type::Kind::List) {
        genListIndexAddr(ex); // list: valida o Ã­ndice e calcula o endereÃ§o
        if (ex->exprType.kind == Type::Kind::Array) break; // sub-array: endereÃ§o
        if (isFloatType(ex->exprType)) {
          emitText("movsd (%rax), %xmm0");
        } else {
          emitText("movq (%rax), %rax");
        }
        break;
      }
      int stride = typeSize(*ex->object->exprType.elem); // 8 ou mais (sub-arrays)
      genAddr(ex->object.get()); // base em rax
      emitText("subq $16, %rsp");
      emitText("movq %rax, (%rsp)");
      genExpr(ex->index.get());  // Ã­ndice em rax
      emitBoundsCheck(ex->object->exprType.arraySize);
      emitText("imulq $" + std::to_string(stride) + ", %rax, %rax");
      emitText("addq (%rsp), %rax"); // endereÃ§o do elemento
      emitText("addq $16, %rsp");
      if (ex->exprType.kind == Type::Kind::Array) break; // sub-array: endereÃ§o
      if (isFloatType(ex->exprType)) {
        emitText("movsd (%rax), %xmm0");
      } else {
        emitText("movq (%rax), %rax");
      }
      break;
    }
    case ExprKind::ArrayLit:
      // literal sÃ³ Ã© consumido como inicializador de declaraÃ§Ã£o (genVarDecl)
      emitText("xorl %eax, %eax");
      break;
    case ExprKind::TupleLit: {
      // M10.1b: aloca o bloco e grava os elementos; handle em rax
      auto ex = static_cast<TupleLitExpr*>(e);
      emitText("movq $" + std::to_string(ex->elements.size()) + ", %rcx");
      emitRuntimeCall("hphl_tuple_new");
      int h = nextSlot_++;
      emitText("movq %rax, " + slotRef(h));
      for (size_t i = 0; i < ex->elements.size(); i++) {
        auto& el = ex->elements[i];
        bool flt = isFloatType(el->exprType);
        if (flt) {
          // float: guarda os bits em rax primeiro (genExpr usa xmm0)
          genExpr(el.get());
          emitText("movq %xmm0, %rax");
          emitText("movq " + slotRef(h) + ", %r10");
          emitText("movq %rax, " + std::to_string(8 * i) + "(%r10)");
        } else {
          genExpr(el.get());
          emitText("movq " + slotRef(h) + ", %r10");
          emitText("movq %rax, " + std::to_string(8 * i) + "(%r10)");
        }
      }
      emitText("movq " + slotRef(h) + ", %rax");
      break;
    }
    case ExprKind::Binary:
      genBinary(static_cast<BinaryExpr*>(e));
      break;
    case ExprKind::Unary:
      genUnary(static_cast<UnaryExpr*>(e));
      break;
    case ExprKind::Assign: {
      auto ex = static_cast<AssignExpr*>(e);
      // propriedade com setter (spec Â§19): chama set_X(this, value)
      // propriedade sem qualificaÃ§Ã£o (spec Â§19): chama set_(this, value)
      if (ex->target->kind == ExprKind::Ident &&
          static_cast<IdentExpr*>(ex->target.get())->isProperty) {
        auto id = static_cast<IdentExpr*>(ex->target.get());
        IdentExpr th;
        th.name = "this";
        std::vector<Expr*> args;
        args.push_back(ex->value.get());
        genCallInternal(id->propSet, args, &th);
        break;
      }
      if (ex->target->kind == ExprKind::Member &&
          static_cast<MemberExpr*>(ex->target.get())->isProperty) {
        auto m = static_cast<MemberExpr*>(ex->target.get());
        std::vector<Expr*> args;
        args.push_back(ex->value.get()); // parÃ¢metro 'value'
        genCallInternal(m->propSet, args, m->object.get());
        break;
      }
      // M10.4: MOVE entre referÃªncias politizadas (`b = a`) â€” copia o
      // PONTEIRO do slot da origem para o slot do destino e remove o slot
      // da origem da limpeza do epÃ­logo (evita double free por aliasing);
      // uso posterior da origem Ã© rejeitado na anÃ¡lise semÃ¢ntica
      if (ex->op == AssignOp::Plain && ex->target->kind == ExprKind::Ident &&
          ex->value->kind == ExprKind::Ident) {
        auto* tgtId = static_cast<IdentExpr*>(ex->target.get());
        auto* srcId = static_cast<IdentExpr*>(ex->value.get());
        Local* tl = findLocal(tgtId->name);
        Local* sl = findLocal(srcId->name);
        auto isRefPtr = [this](const Local* l) {
          return l && isPointerPolicy(l->policy) &&
                 (l->type.kind == Type::Kind::Class ||
                  l->type.kind == Type::Kind::List ||
                  l->type.kind == Type::Kind::Map ||
                  l->type.kind == Type::Kind::Array);
        };
        if (tl && sl && isRefPtr(tl) && isRefPtr(sl) &&
            tl->slot != sl->slot) {
          genExpr(ex->value.get()); // rax = bloco da origem
          emitText("movq %rax, " + slotRef(tl->slot));
          releaseEscapeSlot(sl->slot);
          break;
        }
      }
      // destino atomic: escrita em um passo (xchg) ou += / -= via lock xadd
      bool tAtomic = false;
      if (ex->target->kind == ExprKind::Ident)
        tAtomic = static_cast<IdentExpr*>(ex->target.get())->symbol.atomic;
      else if (ex->target->kind == ExprKind::Member)
        tAtomic = static_cast<MemberExpr*>(ex->target.get())->fieldAtomic;
      if (tAtomic) {
        if (ex->op == AssignOp::Plain) {
          int t = nextSlot_++;
          genExpr(ex->value.get());
          emitText("movq %rax, " + slotRef(t));
          genAddr(ex->target.get());
          emitText("movq " + slotRef(t) + ", %rdx");
          emitText("xchgq %rdx, (%rax)"); // lock implÃ­cito no xchg
          emitText("movq %rdx, %rax");    // valor antigo (resultado da expressÃ£o)
        } else {
          // += / -= : valor atÃ´mico em um passo (lock xadd)
          genAddr(ex->target.get());
          emitText("subq $16, %rsp");
          emitText("movq %rax, 8(%rsp)");
          genExpr(ex->value.get());
          if (ex->op == AssignOp::Sub) emitText("negq %rax");
          emitText("movq %rax, %rcx");    // cÃ³pia do incremento
          emitText("movq 8(%rsp), %r8");
          emitText("lock xaddq %rax, (%r8)"); // rax = valor antigo
          emitText("addq %rcx, %rax");        // novo valor (resultado da expressÃ£o)
          emitText("addq $16, %rsp");
        }
        break;
      }
      if (ex->op == AssignOp::Plain) {
        if (isFloatType(ex->exprType)) {
          genExpr(ex->value.get());   // valor em xmm0 (sobrevive a genAddr)
          genAddr(ex->target.get());
          emitText("movsd %xmm0, (%rax)");
        } else {
          // o valor precisa sobreviver ao genAddr (que pode emitir um bounds
          // check com call, clobbering rdx) â€” guarda num slot temporÃ¡rio
          int t = nextSlot_++;
          genExpr(ex->value.get());
          if (isStructType(ex->exprType))
            genStructCopy(ex->exprType); // cÃ³pia por valor (semÃ¢ntica de struct)
          emitText("movq %rax, " + slotRef(t));
          if (ex->target->kind == ExprKind::Member) {
            auto* mem = static_cast<MemberExpr*>(ex->target.get());
            int tb = nextSlot_++;
            genExpr(mem->object.get());
            emitText("movq %rax, " + slotRef(tb));
            emitText("movq " + slotRef(tb) + ", %rax");
            if (mem->fieldOffset != 0) emitText("addq $" + std::to_string(mem->fieldOffset) + ", %rax");
            int taddr = nextSlot_++;
            emitText("movq %rax, " + slotRef(taddr));
            std::string skip = ".Lwb_skip_" + std::to_string(wb_counter_ast++);
            emitText("movq " + slotRef(tb) + ", %rax");
            emitText("movq -8(%rax), %rcx");
            emitText("bt $32, %rcx");
            emitText("jnc " + skip);
            emitText("bt $36, %rcx");
            emitText("jc " + skip);
            emitText("movq " + slotRef(tb) + ", %rcx");
            emitText("movq " + slotRef(taddr) + ", %rdx");
            emitRuntimeCall("hphl_write_barrier_slow");
            emitText(skip + ":");
            emitText("movq " + slotRef(taddr) + ", %rax");
          } else {
            genAddr(ex->target.get());
          }
          emitText("movq " + slotRef(t) + ", %rdx");
          emitText("movq %rdx, (%rax)");
          emitText("movq %rdx, %rax");
        }
      } else {
        // operaÃ§Ã£o composta: preserva endereÃ§o na stack (16 bytes, alinhado)
        // e o valor antigo num slot temporÃ¡rio (genExpr do rhs pode emitir
        // bounds check com call, clobbering rdx)
        genAddr(ex->target.get());
        emitText("subq $16, %rsp");
        emitText("movq %rax, 8(%rsp)");
        int t = nextSlot_++;
        if (isFloatType(ex->exprType)) {
          emitText("movq 8(%rsp), %rax");
          emitText("movsd (%rax), %xmm1");
          genExpr(ex->value.get());
          // valor inteiro: converte rax â†’ xmm0 antes do aritmÃ©tico
          if (!isFloatType(ex->value->exprType)) emitText("cvtsi2sdq %rax, %xmm0");
          switch (ex->op) {
            case AssignOp::Add: emitText("addsd %xmm0, %xmm1"); break;
            case AssignOp::Sub: emitText("subsd %xmm0, %xmm1"); break;
            case AssignOp::Mul: emitText("mulsd %xmm0, %xmm1"); break;
            case AssignOp::Div: emitText("divsd %xmm0, %xmm1"); break;
            default: break;
          }
          emitText("movq 8(%rsp), %rax");
          emitText("movsd %xmm1, (%rax)");
          emitText("addq $16, %rsp");
          emitText("movsd %xmm1, %xmm0");
        } else {
          emitText("movq 8(%rsp), %r10");
          emitText("movq (%r10), %rdx"); // valor antigo
          emitText("movq %rdx, " + slotRef(t));
          genExpr(ex->value.get());
          emitText("movq %rax, %rcx");
          emitText("movq " + slotRef(t) + ", %rax");
          switch (ex->op) {
            case AssignOp::Add: emitText("addq %rcx, %rax"); break;
            case AssignOp::Sub: emitText("subq %rcx, %rax"); break;
            case AssignOp::Mul: emitText("imulq %rcx, %rax"); break;
            case AssignOp::Div: emitText("cqto"); emitText("idivq %rcx"); break;
            case AssignOp::Mod: emitText("cqto"); emitText("idivq %rcx"); emitText("movq %rdx, %rax"); break;
            default: break;
          }
          applyOverflowPolicy(ex->exprType); // policy do alvo (ex.: int<8, wrap>)
          emitText("movq 8(%rsp), %r10");
          emitText("movq %rax, (%r10)");
          emitText("addq $16, %rsp");
        }
      }
      break;
    }
    case ExprKind::Ternary: {
      auto ex = static_cast<TernaryExpr*>(e);
      std::string elseL = newLabel("ter_else");
      std::string endL = newLabel("ter_end");
      genCond(ex->cond.get(), elseL);
      genExpr(ex->thenExpr.get());
      emitText("jmp " + endL);
      emitText(elseL + ":");
      genExpr(ex->elseExpr.get());
      emitText(endL + ":");
      break;
    }
    case ExprKind::Cast:
      genCast(static_cast<CastExpr*>(e));
      break;
    case ExprKind::New:

      genNew(static_cast<NewExpr*>(e));
      break;
    case ExprKind::Match:
      genMatch(static_cast<MatchExpr*>(e));
      break;
    case ExprKind::OptCtor:
      genOptCtor(static_cast<OptCtorExpr*>(e));
      break;
    case ExprKind::OptMember:
      genOptMember(static_cast<OptMemberExpr*>(e));
      break;
    case ExprKind::OptIndex:
      genOptIndex(static_cast<OptIndexExpr*>(e));
      break;
    case ExprKind::Try:
      genTry(static_cast<TryExpr*>(e));
      break;
    case ExprKind::Coalesce:
      genCoalesce(static_cast<CoalesceExpr*>(e));
      break;
    }
}

// endereÃ§o de um lvalue em %rax
void Codegen::genAddr(Expr* e) {
  if (e->kind == ExprKind::Ident) {
    auto ex = static_cast<IdentExpr*>(e);
    auto* l = findLocal(ex->name);
    if (l && l->byRef) {
      // ref/out/in: o slot guarda o endereÃ§o (que Ã© o "lvalue" por trÃ¡s)
      emitText("movq " + slotRef(l->slot) + ", %rax");
    } else if (l && l->isGlobal) {
      if (l->tlsOffset >= 0) {
        // threadlocal: endereÃ§o = bloco da thread + offset
        emitRuntimeCall("hphl_tls_block");
        if (l->tlsOffset != 0)
          emitText("addq $" + std::to_string(l->tlsOffset) + ", %rax");
      } else {
        emitText("leaq " + l->globalLabel + "(%rip), %rax");
      }
    } else if (l && l->type.kind == Type::Kind::List) {
      // list: o "endereÃ§o" de um list nÃ£o Ã© usado como alvo de escrita direta;
      // devolvemos o ponteiro do objeto (indexaÃ§Ã£o usa genListIndexAddr)
      emitText("movq " + slotRef(l->slot) + ", %rax");
    } else if (l && isPointerPolicy(l->policy)) {
      emitText("movq " + slotRef(l->slot) + ", %rax"); // o slot jÃ¡ Ã© o endereÃ§o
    } else if (l) {
      emitText("leaq " + slotRefMem(l->slot) + ", %rax");
    } else if (ex->symbol.kind == SymbolKind::Field && findLocal("this")) {
      // campo do `this` sem qualificaÃ§Ã£o (spec Â§19)
      emitText("movq " + slotRef(findLocal("this")->slot) + ", %rax");
      if (ex->symbol.slotIndex != 0)
        emitText("addq $" + std::to_string(ex->symbol.slotIndex) + ", %rax");
    } else {
      emitText("xorl %eax, %eax");
    }
    return;
  }
  if (e->kind == ExprKind::Member) {
    auto ex = static_cast<MemberExpr*>(e);
    if (ex->isGlobalRef) {
      auto tit = tlsOffsets_.find(ex->resolvedGlobal->name);
      if (tit != tlsOffsets_.end()) {
        // threadlocal de outro mÃ³dulo: endereÃ§o = bloco da thread + offset
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
    genExpr(ex->object.get()); // ponteiro do objeto em rax
    if (ex->fieldOffset != 0) {
      emitText("addq $" + std::to_string(ex->fieldOffset) + ", %rax");
    }
    return;
  }
  if (e->kind == ExprKind::Index) {
    auto ex = static_cast<IndexExpr*>(e);
    if (ex->object->exprType.kind == Type::Kind::List) {
      genListIndexAddr(ex);
      return;
    }
    int stride = typeSize(*ex->object->exprType.elem);
    genAddr(ex->object.get()); // base em rax
    emitText("subq $16, %rsp");
    emitText("movq %rax, (%rsp)");
    genExpr(ex->index.get());  // Ã­ndice em rax
    emitBoundsCheck(ex->object->exprType.arraySize);
    emitText("imulq $" + std::to_string(stride) + ", %rax, %rax");
    emitText("addq (%rsp), %rax");
    emitText("addq $16, %rsp");
    return;
  }
  emitText("xorl %eax, %eax");
}

std::string Codegen::internString(const std::string& value) {
  auto it = stringPool_.find(value);
  if (it != stringPool_.end()) return it->second;
  std::string lbl = ".Lstr_" + std::to_string(stringCounter_++);
  stringPool_[value] = lbl;
  std::string escaped;
  for (unsigned char c : value) {
    switch (c) {
      case '\n': escaped += "\\n"; break;
      case '\t': escaped += "\\t"; break;
      case '\r': escaped += "\\r"; break;
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\0': escaped += "\\0"; break;
      default:
        if (c < 32) {
          escaped += "\\x";
          escaped += "0123456789abcdef"[c >> 4];
          escaped += "0123456789abcdef"[c & 15];
        } else {
          escaped += (char)c;
        }
    }
  }
  emitRodata(lbl + ": .asciz \"" + escaped + "\"");
  return lbl;
}

void Codegen::emitArrayBaseLoad(const Local& l) {
  if (l.isGlobal) {
    emitText("leaq " + l.globalLabel + "(%rip), %rax");
  } else if (isPointerPolicy(l.policy)) {
    emitText("movq " + slotRef(l.slot) + ", %rax");
  } else {
    emitText("leaq " + slotRefMem(l.slot) + ", %rax");
  }
}

void Codegen::storeArrayLit(const Local& l, const Type& arrayType, ArrayLitExpr* al, long long off) {
  int elSize = typeSize(*arrayType.elem); // stride de um elemento (sub-arrays > 8)
  for (size_t i = 0; i < al->elements.size(); i++) {
    Expr* el = al->elements[i].get();
    long long elOff = off + (long long)elSize * (long long)i;
    if (el->kind == ExprKind::ArrayLit) {
      storeArrayLit(l, *arrayType.elem, static_cast<ArrayLitExpr*>(el), elOff);
      continue;
    }
    if (isFloatType(el->exprType)) {
      genExpr(el); // valor em xmm0 (nÃ£o clobbered pela carga da base)
      emitArrayBaseLoad(l);
      emitText("movq %rax, %r10");
      if (elOff) emitText("addq $" + std::to_string(elOff) + ", %r10");
      emitText("movsd %xmm0, (%r10)");
    } else {
      genExpr(el);
      emitText("movq %rax, %rdx");
      emitArrayBaseLoad(l);
      emitText("movq %rax, %r10");
      if (elOff) emitText("addq $" + std::to_string(elOff) + ", %r10");
      emitText("movq %rdx, (%r10)");
    }
  }
}

void Codegen::collectArrayValues(Expr* el, std::vector<std::string>& vals) {
  if (el->kind == ExprKind::ArrayLit) {
    for (auto& c : static_cast<ArrayLitExpr*>(el)->elements) {
      collectArrayValues(c.get(), vals);
    }
    return;
  }
  switch (el->kind) {
    case ExprKind::IntLit:
      vals.push_back(std::to_string(static_cast<IntLitExpr*>(el)->value));
      break;
    case ExprKind::CharLit:
      vals.push_back(std::to_string(static_cast<CharLitExpr*>(el)->value));
      break;
    case ExprKind::BoolLit:
      vals.push_back(static_cast<BoolLitExpr*>(el)->value ? "1" : "0");
      break;
    case ExprKind::FloatLit: {
      double vd = static_cast<FloatLitExpr*>(el)->value;
      uint64_t bits;
      std::memcpy(&bits, &vd, 8);
      vals.push_back(std::to_string((long long)bits));
      break;
    }
    case ExprKind::StringLit:
      vals.push_back(internString(static_cast<StringLitExpr*>(el)->value));
      break;
    default:
      vals.push_back("0");
      break;
  }
}

void Codegen::genCallMalloc(long long size) {
  emitText("movq $" + std::to_string(size) + ", %rcx");
  emitRuntimeCall("malloc");
}

// M10.2 (v0.47): alocaÃ§Ã£o por Storage Policy (spec Â§3)
//   arena  â†’ bump alloc no handle da funÃ§Ã£o (liberado de uma vez no retorno)
//   pool   â†’ freelist por classe de tamanho (reuso O(1))
//   shared â†’ bloco refcounted
//   heap/stack/auto â†’ malloc como antes
void Codegen::genPolicyAlloc(StoragePolicy pol, long long size) {
  switch (pol) {
    case StoragePolicy::Arena:
      if (arenaHandleSlot_ < 0)
        arenaHandleSlot_ = nextSlot_++; // zerado no prÃ³logo (apÃ³s o corpo)
      emitText("leaq " + slotRefMem(arenaHandleSlot_) + ", %rcx");
      emitText("movq $" + std::to_string(size) + ", %rdx");
      emitRuntimeCall("hphl_arena_alloc");
      return;
    case StoragePolicy::Pool:
      emitText("movq $" + std::to_string(size) + ", %rcx");
      emitRuntimeCall("hphl_pool_alloc");
      return;
    case StoragePolicy::Shared:
      emitText("movq $" + std::to_string(size) + ", %rcx");
      emitRuntimeCall("hphl_shared_alloc");
      return;
    default:
      genCallCalloc(size);
  }
}

void Codegen::genCallCalloc(long long size) {
  emitText("movq $" + std::to_string(size) + ", %rcx");
  emitText("movq $1, %rdx");
  emitRuntimeCall("calloc");
}

void Codegen::emitRuntimeCall(const std::string& name) {
  // shadow space (32B) obrigatÃ³rio na ABI Windows x64; 32 Ã© mÃºltiplo de 16
  emitText("subq $32, %rsp");
  emitText("call " + name);
  emitText("addq $32, %rsp");
}

void Codegen::emitBoundsCheck(long long arraySize) {
  // Ã­ndice em %rax. A base do array jÃ¡ estÃ¡ preservada em (%rsp) (reserva de
  // 16 bytes). Guardamos o Ã­ndice em 8(%rsp): quando o call abaixar rsp em 32,
  // o shadow space do callee fica em [rsp, rsp+32) = [orig-48, orig-16), entÃ£o
  // 8(%rsp) (= orig-8) e (%rsp) (= orig-16) nÃ£o sÃ£o atingidos.
  emitText("movq %rax, 8(%rsp)"); // preserva o Ã­ndice
  emitText("movq %rax, %rcx");    // arg0 = Ã­ndice
  emitText("movq $" + std::to_string(arraySize) + ", %rdx"); // arg1 = tamanho
  emitRuntimeCall("hphl_bounds_check");
  emitText("movq 8(%rsp), %rax"); // restaura o Ã­ndice
}

// endereÃ§o do elemento de list em %rax. Reserva de 16 bytes: (%rsp) guarda o
// ponteiro do list e 8(%rsp) o Ã­ndice â€” as chamadas de runtime (que reservam
// seus prÃ³prios 32 bytes de shadow space logo abaixo) nÃ£o atingem essa Ã¡rea.
void Codegen::genListIndexAddr(IndexExpr* ex) {
  int stride = typeSize(*ex->object->exprType.elem);
  emitText("subq $16, %rsp");
  genExpr(ex->object.get());      // list â†’ ponteiro do objeto em rax
  emitText("movq %rax, (%rsp)");
  genExpr(ex->index.get());       // Ã­ndice em rax
  emitText("movq %rax, 8(%rsp)");
  emitText("movq %rax, %rcx");    // arg0 = Ã­ndice
  emitText("movq (%rsp), %rdx");  // arg1 = ponteiro do list
  emitRuntimeCall("hphl_list_check");
  emitText("movq (%rsp), %rcx");
  emitRuntimeCall("hphl_list_data"); // rax = buffer de itens
  emitText("movq 8(%rsp), %rcx");
  emitText("imulq $" + std::to_string(stride) + ", %rcx, %rcx");
  emitText("addq %rcx, %rax");    // endereÃ§o do elemento
  emitText("addq $16, %rsp");
}

// list.Add(x): reserva de 16 bytes guarda o objeto do list em (%rsp) e o
// valor em 8(%rsp) (int) ou xmm (float) durante as chamadas de runtime.
void Codegen::genListAdd(CallExpr* c) {
  auto mem = static_cast<MemberExpr*>(c->callee.get());
  Type et = *mem->object->exprType.elem;
  emitText("subq $16, %rsp");
  genExpr(mem->object.get());     // list â†’ ponteiro do objeto em rax
  emitText("movq %rax, (%rsp)");
  genExpr(c->args[0].get());      // valor em rax / xmm0
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
}

// M10.1b: literal de tupla no caminho HIR â€” aloca o bloco, grava os
// elementos e devolve o handle em rax.
void Codegen::genHirTupleLit(HirTupleLit* tl) {
  emitText("movq $" + std::to_string(tl->elements.size()) + ", %rcx");
  emitRuntimeCall("hphl_tuple_new");
  int h = nextSlot_++;
  emitText("movq %rax, " + slotRef(h));
  for (size_t i = 0; i < tl->elements.size(); i++) {
    auto& el = tl->elements[i];
    if (isFloatType(el->type)) {
      genHirExpr(el.get());
      emitText("movq %xmm0, %rax"); // bits do double para o slot de 8B
      emitText("movq " + slotRef(h) + ", %r10");
      emitText("movq %rax, " + std::to_string(8 * i) + "(%r10)");
    } else {
      genHirExpr(el.get());
      emitText("movq " + slotRef(h) + ", %r10");
      emitText("movq %rax, " + std::to_string(8 * i) + "(%r10)");
    }
  }
  emitText("movq " + slotRef(h) + ", %rax");
}

// M10.1b: `var (a, b) = expr;` â€” handle num temporÃ¡rio do escopo e leitura
// de cada elemento por offset; os nomes viram locais simples.
void Codegen::genDestructure(StmtDestructure* st) {
  Local tmp;
  tmp.type = st->tupleType;
  tmp.policy = StoragePolicy::Stack;
  tmp.slot = nextSlot_++;
  scopes_.back()["__dtup"] = tmp;
  genExpr(st->init.get());
  emitText("movq %rax, " + slotRef(tmp.slot));
  heapSlots_.push_back({tmp.slot, StoragePolicy::Heap, 0, ""}); // liberado no fim da funÃ§Ã£o
  const Type& tt = st->tupleType;
  for (size_t i = 0; i < st->names.size(); i++) {
    Local l;
    l.type = tt.tupleElems[i];
    l.policy = StoragePolicy::Stack;
    l.slot = nextSlot_++;
    scopes_.back()[st->names[i]] = l;
    emitText("movq " + slotRef(tmp.slot) + ", %rax");
    if (isFloatType(l.type)) {
      emitText("movsd " + std::to_string(8 * i) + "(%rax), %xmm0");
      emitText("movsd %xmm0, " + slotRef(l.slot));
    } else {
      emitText("movq " + std::to_string(8 * i) + "(%rax), %rax");
      emitText("movq %rax, " + slotRef(l.slot));
    }
  }
  // o temporÃ¡rio sai do escopo junto: remove para nÃ£o liberar cedo demais
  scopes_.back().erase("__dtup");
}

// Remove um slot das listas de liberaÃ§Ã£o no epÃ­logo. Usado quando uma funÃ§Ã£o
// retorna um objeto/list (ex.: `return p;`): o valor "escapa" e nÃ£o pode ser
// liberado antes de chegar ao chamador (evita use-after-free).
void Codegen::releaseEscapeSlot(int slot) {
  for (auto it = heapSlots_.begin(); it != heapSlots_.end(); ++it) {
    if (it->slot == slot) {
      heapSlots_.erase(it);
      break;
    }
  }
  auto erase = [slot](std::vector<int>& v) {
    for (auto it = v.begin(); it != v.end(); ++it) {
      if (*it == slot) {
        v.erase(it);
        return;
      }
    }
  };
  erase(listSlots_);
}

// Aplica a overflow policy do tipo de resultado (valor em %rax; retorna em
// %rax). Default/fixed = comportamento nativo (sem checagem, sem truncamento â€”
// decisÃ£o M2); promote = operaÃ§Ã£o em 64 bits (nada a fazer aqui); wrap =
// trunca o resultado para a largura; checked = trunca + panic em runtime se o
// valor (antes de truncar) estiver fora da faixa da largura; saturate = clamp
// na faixa. Larguras de 64 bits sÃ£o o caso nativo (nada a fazer).
void Codegen::applyOverflowPolicy(const Type& res) {
  if (res.kind != Type::Kind::Int && res.kind != Type::Kind::UInt) return;
  int W = res.bits == 0 ? 64 : res.bits;
  if (W >= 64) return;
  if (res.policy == OverflowPolicy::Default || res.policy == OverflowPolicy::Fixed ||
      res.policy == OverflowPolicy::Promote) {
    return;
  }
  if (res.policy == OverflowPolicy::Wrap) {
    int s = 64 - W;
    emitText("shlq $" + std::to_string(s) + ", %rax");
    emitText(std::string(res.kind == Type::Kind::UInt ? "shrq" : "sarq") +
             " $" + std::to_string(s) + ", %rax");
    return;
  }
  bool uns = res.kind == Type::Kind::UInt;
  long long minV = uns ? 0 : -(1LL << (W - 1));
  long long maxV = uns ? (1LL << W) - 1 : (1LL << (W - 1)) - 1;
  if (res.policy == OverflowPolicy::Checked) {
    emitText("subq $16, %rsp");
    emitText("movq %rax, (%rsp)");
    emitText("movq %rax, %rcx"); // arg0 = valor
    emitText("movq $" + std::to_string(minV) + ", %rdx"); // arg1 = min
    emitText("movq $" + std::to_string(maxV) + ", %r8");  // arg2 = max
    emitRuntimeCall("hphl_overflow_check");
    emitText("movq (%rsp), %rax");
    emitText("addq $16, %rsp");
    return;
  }
  // saturate: clamp no intervalo [minV, maxV]
  emitText("movq $" + std::to_string(minV) + ", %rdx");
  emitText("cmpq %rdx, %rax");
  emitText("cmovl %rdx, %rax"); // v < min â†’ v = min
  emitText("movq $" + std::to_string(maxV) + ", %rdx");
  emitText("cmpq %rdx, %rax");
  emitText("cmovg %rdx, %rax"); // v > max â†’ v = max
}

// polÃ­tica de overflow do tipo FLOAT (resultado em %xmm0, limites do tipo de
// W bits â€” .Lflt_* sÃ£o FLT_MAX/-FLT_MAX em double para float<32>; .Ldbl_* sÃ£o
// DBL_MAX/-DBL_MAX para float<64>):
//   checked = panic (hphl_panic) se NaN ou fora de Â±limite;
//   saturate = clamp em Â±limite (NaN â†’ +limite); promote/default = nada
void Codegen::applyFloatOverflowPolicy(const Type& res, int W) {
  if (res.policy != OverflowPolicy::Checked && res.policy != OverflowPolicy::Saturate)
    return;
  bool is32 = W == 32;
  std::string maxL = is32 ? ".Lflt_max" : ".Ldbl_max";
  std::string minL = is32 ? ".Lflt_min" : ".Ldbl_min";
  std::string doneL = newLabel(is32 ? "sat_done_32" : "sat_done");
  if (res.policy == OverflowPolicy::Checked) {
    std::string failL = newLabel(is32 ? "flt_chk_fail_32" : "flt_chk_fail");
    emitText("movsd " + maxL + "(%rip), %xmm1");
    emitText("ucomisd %xmm1, %xmm0");
    emitText("jp " + failL);
    emitText("ja " + failL);
    emitText("movsd " + minL + "(%rip), %xmm1");
    emitText("ucomisd %xmm1, %xmm0");
    emitText("jp " + failL);
    emitText("jb " + failL);
    emitText("jmp " + doneL);
    emitText(failL + ":");
    emitRuntimeCall("hphl_panic");
    emitText(doneL + ":");
    return;
  }
  // saturate: clamp em Â±limite (NaN â†’ +limite)
  std::string nanL = newLabel(is32 ? "sat_nan_32" : "sat_nan");
  std::string inffL = newLabel(is32 ? "sat_inf_32" : "sat_inf");
  std::string ninfL = newLabel(is32 ? "sat_neg_inf_32" : "sat_neg_inf");
  emitText("movsd " + maxL + "(%rip), %xmm1");
  emitText("ucomisd %xmm1, %xmm0");
  emitText("jp " + nanL);
  emitText("ja " + inffL);
  emitText("movsd " + minL + "(%rip), %xmm1");
  emitText("ucomisd %xmm1, %xmm0");
  emitText("jp " + nanL);
  emitText("jb " + ninfL);
  emitText("jmp " + doneL);
  emitText(nanL + ":");
  emitText("movsd " + maxL + "(%rip), %xmm0");
  emitText("jmp " + doneL);
  emitText(inffL + ":");
  emitText("movsd " + maxL + "(%rip), %xmm0");
  emitText("jmp " + doneL);
  emitText(ninfL + ":");
  emitText("movsd " + minL + "(%rip), %xmm0");
  emitText(doneL + ":");
}

// pula para falseLabel se a condiÃ§Ã£o for falsa
void Codegen::genCond(Expr* e, const std::string& falseLabel) {
  genExpr(e);
  emitText("testq %rax, %rax");
  emitText("je " + falseLabel);
}

// pula para trueLabel se a condiÃ§Ã£o for verdadeira
void Codegen::genCondTrue(Expr* e, const std::string& trueLabel) {
  genExpr(e);
  emitText("testq %rax, %rax");
  emitText("jne " + trueLabel);
}

void Codegen::genBinary(BinaryExpr* b) {
  bool flt = b->lhs->exprType.kind == Type::Kind::Float ||
             b->rhs->exprType.kind == Type::Kind::Float;

  // comparaÃ§Ãµes
  if (b->op >= BinOp::Eq && b->op <= BinOp::Ge) {
    // M14.4: ==/!= entre strings por conteÃºdo (hphl_str_eq)
    if (b->strEqBin) {
      genExpr(b->lhs.get());
      int h = nextSlot_++;
      emitText("movq %rax, " + slotRef(h));
      genExpr(b->rhs.get());
      emitText("movq %rax, %rdx");
      emitText("movq " + slotRef(h) + ", %rcx");
      emitRuntimeCall("hphl_str_eq");
      if (b->op == BinOp::Ne) emitText("xorq $1, %rax");
      return;
    }
    if (b->derivedEq) {
      std::vector<Expr*> args = {b->lhs.get(), b->rhs.get()};
      genCallInternal(b->derivedEq, args, nullptr);
      // equ_<T> retorna 1 quando igual, 0 quando diferente: `==` Ã© setne
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
      std::vector<Expr*> args = {b->lhs.get(), b->rhs.get()};
      genCallInternal(b->derivedCmp, args, nullptr);
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
    // Option/Result/enums ricos: compara a TAG das cÃ©lulas, nÃ£o o endereÃ§o
    bool richEnum = ((b->lhs->exprType.kind == Type::Kind::Enum &&
                      b->rhs->exprType.kind == Type::Kind::Enum &&
                      sem_.isRichEnum(b->lhs->exprType.name)) ||
                     (b->lhs->exprType.isOption() && b->rhs->exprType.isOption()) ||
                     (b->lhs->exprType.isResult() && b->rhs->exprType.isResult()));
    if (richEnum) {
      genExpr(b->lhs.get());
      emitText("movq (%rax), %rdx"); // tag
      genExpr(b->rhs.get());
      emitText("movq (%rax), %rax"); // tag
      emitText("cmpq %rdx, %rax");
      emitText(b->op == BinOp::Eq ? "sete %al" : "setne %al");
      emitText("movzbl %al, %eax");
      return;
    }
    if (flt) {
      genExpr(b->lhs.get());
      emitText("subq $16, %rsp");
      emitText("movsd %xmm0, (%rsp)");
      genExpr(b->rhs.get());
      emitText("movsd (%rsp), %xmm1");
      emitText("addq $16, %rsp");
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
      genExpr(b->lhs.get());
      emitText("subq $16, %rsp");
      emitText("movq %rax, 8(%rsp)");
      genExpr(b->rhs.get());
      emitText("movq %rax, %rdx");
      emitText("movq 8(%rsp), %rax");
      emitText("addq $16, %rsp");
      emitText("cmpq %rdx, %rax");
      bool uns = b->lhs->exprType.kind == Type::Kind::UInt;
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

  // && e || com curto-circuito
  if (b->op == BinOp::And || b->op == BinOp::Or) {
    std::string endL = newLabel("logic_end");
    if (b->op == BinOp::And) {
      genExpr(b->lhs.get());
      emitText("testq %rax, %rax");
      std::string skipL = newLabel("logic_skip");
      emitText("je " + skipL);
      genExpr(b->rhs.get());
      emitText("testq %rax, %rax");
      emitText("je " + skipL);
      emitText("movq $1, %rax");
      emitText("jmp " + endL);
      emitText(skipL + ":");
      emitText("xorl %eax, %eax");
      emitText(endL + ":");
    } else {
      genExpr(b->lhs.get());
      emitText("testq %rax, %rax");
      std::string trueL = newLabel("logic_true");
      emitText("jne " + trueL);
      genExpr(b->rhs.get());
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
    genExpr(b->lhs.get());
    // M11-bench: lhs INTEIRO em operaÃ§Ã£o float â€” converte ANTES de derramar
    // (antes: `p * d` com p int descartava o valor do lhs)
    if (!isFloatType(b->lhs->exprType)) emitText("cvtsi2sdq %rax, %xmm0");
    emitText("subq $16, %rsp");
    emitText("movsd %xmm0, (%rsp)");
    genExpr(b->rhs.get());
    // rhs inteiro/literal: converte rax â†’ xmm0 (cvtsi2sdq) antes do aritmÃ©tico
    if (!isFloatType(b->rhs->exprType)) emitText("cvtsi2sdq %rax, %xmm0");
    emitText("movsd (%rsp), %xmm1");
    emitText("addq $16, %rsp");
    switch (b->op) {
      case BinOp::Add: emitText("addsd %xmm0, %xmm1"); break;
      case BinOp::Sub: emitText("subsd %xmm0, %xmm1"); break;
      case BinOp::Mul: emitText("mulsd %xmm0, %xmm1"); break;
      case BinOp::Div: emitText("divsd %xmm0, %xmm1"); break;
      default: break;
    }
    emitText("movsd %xmm1, %xmm0");
    // polÃ­tica de overflow do tipo float<W, checked|saturate> (v0.22.9)
    if (b->exprType.kind == Type::Kind::Float &&
        (b->exprType.policy == OverflowPolicy::Checked ||
         b->exprType.policy == OverflowPolicy::Saturate))
      applyFloatOverflowPolicy(b->exprType, b->exprType.bits == 0 ? 64 : b->exprType.bits);
    return;
  }

  // strings: concatenaÃ§Ã£o
  if (b->op == BinOp::Add && b->lhs->exprType.kind == Type::Kind::String) {
    // M14.4: lhs em SLOT â€” rhs pode conter calls/aninhados que corrompem rcx
    genExpr(b->lhs.get());
    int h = nextSlot_++;
    emitText("movq %rax, " + slotRef(h));
    genExpr(b->rhs.get());
    emitText("movq %rax, %rdx");
    emitText("movq " + slotRef(h) + ", %rcx");
    emitRuntimeCall("hphl_str_concat");
    return;
  }

  // inteiros: lhs em rax, rhs em rcx
  genExpr(b->lhs.get());
  if (b->rhs->kind == ExprKind::IntLit && !rcxStaged_) {
    emitText("movq $" + std::to_string(static_cast<IntLitExpr*>(b->rhs.get())->value) + ", %rcx");
  } else {
    emitText("subq $16, %rsp");
    emitText("movq %rax, 8(%rsp)");
    genExpr(b->rhs.get());
    emitText("movq %rax, %rcx");
    emitText("movq 8(%rsp), %rax");
    emitText("addq $16, %rsp");
  }
  switch (b->op) {
    case BinOp::Add: emitText("addq %rcx, %rax"); break;
    case BinOp::Sub: emitText("subq %rcx, %rax"); break;
    case BinOp::Mul: emitText("imulq %rcx, %rax"); break;
    case BinOp::Div: {
      bool uns = b->lhs->exprType.kind == Type::Kind::UInt;
      if (b->exprType.policy == OverflowPolicy::Checked) {
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
      bool uns = b->lhs->exprType.kind == Type::Kind::UInt;
      if (b->exprType.policy == OverflowPolicy::Checked) {
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
      bool uns = b->lhs->exprType.kind == Type::Kind::UInt;
      emitText("movq %rcx, %rdx");
      emitText(uns ? "shrq %cl, %rax" : "sarq %cl, %rax");
      break;
    }
    default: break;
  }
  if (b->op == BinOp::Add || b->op == BinOp::Sub || b->op == BinOp::Mul ||
      b->op == BinOp::Div || b->op == BinOp::Mod) {
    applyOverflowPolicy(b->exprType);
  }
}

void Codegen::genUnary(UnaryExpr* u) {
  switch (u->op) {
    case UnOp::Neg: {
      if (isFloatType(u->exprType)) {
        genExpr(u->operand.get());
        emitText("movabsq $-9223372036854775808, %rax");
        emitText("movq %rax, %xmm1");
        emitText("xorpd %xmm1, %xmm0");
      } else {
        genExpr(u->operand.get());
        emitText("negq %rax");
        applyOverflowPolicy(u->exprType);
      }
      break;
    }
    case UnOp::Not:
      genExpr(u->operand.get());
      emitText("testq %rax, %rax");
      emitText("sete %al");
      emitText("movzbl %al, %eax");
      break;
    case UnOp::BitNot:
      genExpr(u->operand.get());
      emitText("notq %rax");
      break;
    case UnOp::PreInc:
    case UnOp::PreDec: {
      bool uAtomic = false;
      if (u->operand->kind == ExprKind::Ident)
        uAtomic = static_cast<IdentExpr*>(u->operand.get())->symbol.atomic;
      else if (u->operand->kind == ExprKind::Member)
        uAtomic = static_cast<MemberExpr*>(u->operand.get())->fieldAtomic;
      if (uAtomic) {
        // lock xadd +1 / -1: o valor antigo fica em rax imediatamente
        genAddr(u->operand.get());
        emitText("subq $16, %rsp");
        emitText("movq %rax, 8(%rsp)");
        emitText(u->op == UnOp::PreInc ? "movq $1, %rax" : "movq $-1, %rax");
        emitText("movq 8(%rsp), %r8");
        emitText("lock xaddq %rax, (%r8)"); // rax = valor antigo
        emitText(u->op == UnOp::PreInc ? "addq $1, %rax" : "subq $1, %rax");
        emitText("addq $16, %rsp");
        break;
      }
      genAddr(u->operand.get());
      emitText("movq %rax, %r10"); // endereÃ§o preservado (guard pode clobber rax)
      emitText("movq (%r10), %rdx");
      emitText(u->op == UnOp::PreInc ? "addq $1, %rdx" : "subq $1, %rdx");
      emitText("movq %rdx, %rax");
      applyOverflowPolicy(u->exprType);
      emitText("movq %rax, %rdx");
      emitText("movq %rdx, (%r10)");
      emitText("movq %rdx, %rax");
      break;
    }
    case UnOp::PostInc:
    case UnOp::PostDec: {
      bool uAtomic = false;
      if (u->operand->kind == ExprKind::Ident)
        uAtomic = static_cast<IdentExpr*>(u->operand.get())->symbol.atomic;
      else if (u->operand->kind == ExprKind::Member)
        uAtomic = static_cast<MemberExpr*>(u->operand.get())->fieldAtomic;
      if (uAtomic) {
        genAddr(u->operand.get());
        emitText("subq $16, %rsp");
        emitText("movq %rax, 8(%rsp)");
        emitText(u->op == UnOp::PostInc ? "movq $1, %rax" : "movq $-1, %rax");
        emitText("movq 8(%rsp), %r8");
        emitText("lock xaddq %rax, (%r8)"); // rax = valor antigo (resultado)
        emitText("addq $16, %rsp");
        break;
      }
      int rt = nextSlot_++; // valor antigo (resultado da expressÃ£o)
      genAddr(u->operand.get());
      emitText("movq %rax, %r10"); // endereÃ§o
      emitText("movq (%r10), %rdx"); // valor antigo
      emitText("movq %rdx, " + slotRef(rt));
      emitText(u->op == UnOp::PostInc ? "addq $1, %rdx" : "subq $1, %rdx");
      emitText("movq %rdx, %rax");
      applyOverflowPolicy(u->exprType);
      emitText("movq %rax, %rdx");
      emitText("movq %rdx, (%r10)");
      emitText("movq " + slotRef(rt) + ", %rax");
      break;
    }
  }
}

void Codegen::genCast(CastExpr* c) {
  genExpr(c->operand.get());
  Type from = c->operand->exprType;
  Type to = c->target;
  if (from.isInteger() && to.kind == Type::Kind::Float) {
    emitText("cvtsi2sdq %rax, %xmm0");
  } else if (from.kind == Type::Kind::Float && to.isInteger()) {
    emitText("cvttsd2siq %xmm0, %rax");
  }
  // intâ†’int e stringâ†”classe: sem operaÃ§Ã£o no M1
}

void Codegen::genNew(NewExpr* n) {
  if (n->isChannel) {
    // `new channel<T>(cap)`: handle de fila circular com capacidade fixa
    emitText("movq $" + std::to_string(n->channelCapacity) + ", %rcx");
    emitRuntimeCall("hphl_channel_new");
    return;
  }
  if (n->isList) {
    if (n->args.empty()) {
      emitRuntimeCall("hphl_list_new");
    } else {
      genExpr(n->args[0].get());
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_list_with_cap");
    }
    return;
  }
  if (n->isArrayNew) {
    // `new int[N]` — array heap (bug 1.7)
    Type et = n->exprType.elem ? *n->exprType.elem : Type::makeInt(32);
    long long elemSz = typeSize(et);
    emitText("movq $" + std::to_string(n->arraySize * elemSz) + ", %rcx");
    emitRuntimeCall("hphl_shared_alloc");
    return;
  }
  // `new int(0)` — primitivo na heap: retorna o valor (bug 1.7)
  {
    bool prim = n->exprType.kind == Type::Kind::Int || n->exprType.kind == Type::Kind::UInt ||
                n->exprType.kind == Type::Kind::Bool || n->exprType.kind == Type::Kind::Char;
    if (prim && sem_.classes().count(n->className) == 0) {
      if (!n->args.empty()) genExpr(n->args[0].get());
      return;
    }
  }
  bool named = false;
  for (auto& nm : n->argNames)
    if (!nm.empty()) named = true;
  // parÃ¢metros opcionais do construtor: completa a lista com os padrÃµes
  std::vector<Expr*> args;
  if (n->ctor && !named) {
    for (size_t i = 0; i < n->ctor->params.size(); i++)
      args.push_back(i < n->args.size() ? n->args[i].get()
                                        : n->ctor->params[i]->defaultVal.get());
  } else {
    for (auto& a : n->args) args.push_back(a.get());
  }
  int stackArgs = (int)args.size() > 3 ? (int)args.size() - 3 : 0;
  // 16 = Ã¡rea extra p/ preservar o ponteiro do objeto em 0(%rsp);
  // args (posiÃ§Ã£o 1+) spillam em 8*pos(%rsp), sem colidir com o this.
  int bytes = 16 + ((32 + stackArgs * 8 + 15) & ~15);
  emitText("subq $" + std::to_string(bytes) + ", %rsp");
  /* M27: typed allocation if class has descriptor (bitmap-based GC) */
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
  emitText("movq %rax, 0(%rsp)"); // this preservado (rcx Ã© clobbered pelas chamadas)

  // avalia args (this ocupa a posiÃ§Ã£o 0); reg args spillam no shadow
  for (size_t i = 0; i < args.size(); i++) {
    Expr* a = args[i];
    Param* p = !named && n->ctor ? n->ctor->params[i].get() : nullptr;
    bool byRef = p && p->isByRef();
    bool pf = p ? isFloatType(p->type) : isFloatType(a->exprType);
    int pos = (int)i + 1;
    if (byRef) {
      if (p->byIn && !isAddressable(a)) {
        int t = nextSlot_++;
        genExpr(a);
        emitText("movq %rax, " + slotRef(t));
        emitText("leaq " + slotRefMem(t) + ", %rax");
      } else {
        genAddr(a);
      }
      emitText("movq %rax, " + std::to_string(pos < 4 ? 8 * pos : 32 + 8 * (pos - 4)) + "(%rsp)");
    } else {
      genExpr(a);
      if (p && isStructType(p->type))
        genStructCopy(p->type);
      if (pf && p && !isFloatType(a->exprType))
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
    Param* p = !named && n->ctor ? n->ctor->params[i].get() : nullptr;
    if (p && p->isByRef()) {
      emitText("movq " + std::to_string(8 * pos) + "(%rsp), " + regs[pos]);
      continue;
    }
    if (p ? isFloatType(p->type) : isFloatType(n->args[i]->exprType)) {
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

void Codegen::genInitClassFields(const std::string& className) {
  auto it = sem_.classes().find(className);
  if (it == sem_.classes().end()) return;
  auto& ci = it->second;
  std::vector<std::pair<std::string, std::pair<Type, int>>> allFields;
  std::function<void(const std::string&)> collect;
  collect = [&](const std::string& cn) {
    auto& parent = sem_.classes().at(cn);
    if (!parent.base.empty()) collect(parent.base);
    for (auto& f : parent.fields) allFields.push_back(f);
  };
  if (!ci.base.empty()) collect(ci.base);
  for (auto& f : ci.fields) allFields.push_back(f);
  int objSlot = nextSlot_++;
  emitText("movq %r10, " + slotRef(objSlot));
  for (auto& f : allFields) {
    Type ft = f.second.first;
    int offset = f.second.second;
    if (ft.kind == Type::Kind::List) {
      emitRuntimeCall("hphl_list_new");
      emitText("movq " + slotRef(objSlot) + ", %r10");
      emitText("movq %rax, " + std::to_string(offset) + "(%r10)");
    } else if (ft.kind == Type::Kind::Map) {
      int tag = 0;
      if (ft.elem && ft.elem->kind == Type::Kind::String) tag = 1;
      else if (ft.elem && ft.elem->kind == Type::Kind::Bool) tag = 2;
      else if (ft.elem && ft.elem->kind == Type::Kind::Char) tag = 3;
      emitText("movq $" + std::to_string(tag) + ", %rcx");
      emitRuntimeCall("hphl_map_new");
      emitText("movq " + slotRef(objSlot) + ", %r10");
      emitText("movq %rax, " + std::to_string(offset) + "(%r10)");
    }
  }
  emitText("movq " + slotRef(objSlot) + ", %r10"); // restore r10 after all field init
}

/* M20.1.3 4.3: genClassDestructor - libera list/map fields de uma classe
 * (incluindo herdados). Chamado antes do hphl_shared_release/free no epï¿½logo. */
void Codegen::genClassDestructor(const std::string& className) {
  auto it = sem_.classes().find(className);
  if (it == sem_.classes().end()) return;
  auto& ci = it->second;
  std::vector<std::pair<std::string, std::pair<Type, int>>> allFields;
  std::function<void(const std::string&)> collect;
  collect = [&](const std::string& cn) {
    auto& parent = sem_.classes().at(cn);
    if (!parent.base.empty()) collect(parent.base);
    for (auto& f : parent.fields) allFields.push_back(f);
  };
  if (!ci.base.empty()) collect(ci.base);
  for (auto& f : ci.fields) allFields.push_back(f);
  for (auto& f : allFields) {
    Type ft = f.second.first;
    int offset = f.second.second;
    if (ft.kind == Type::Kind::List) {
      emitText("movq " + std::to_string(offset) + "(%r12), %rcx");
      emitText("testq %rcx, %rcx");
      std::string skip = newLabel("class_dtor_list_skip");
      emitText("je " + skip);
      emitRuntimeCall("hphl_list_free");
      emitText(skip + ":");
    } else if (ft.kind == Type::Kind::Map) {
      emitText("movq " + std::to_string(offset) + "(%r12), %rcx");
      emitText("testq %rcx, %rcx");
      std::string skip = newLabel("class_dtor_map_skip");
      emitText("je " + skip);
      emitRuntimeCall("hphl_map_free");
      emitText(skip + ":");
    }
  }
}

// CÃ©lula `[tag][payload]`: Option: None=0/Some=1; Result: Ok=0/Err=1
void Codegen::genOptCtor(OptCtorExpr* o) {
  long long tag = (o->variant == "Some" || o->variant == "Err") ? 1 : 0;
  bool hasArg = o->arg != nullptr;
  genCallCalloc(16);
  int h = nextSlot_++; // slot temporÃ¡rio com o handle da cÃ©lula
  emitText("movq %rax, " + slotRef(h));
  emitText("movq $" + std::to_string(tag) + ", %rax");
  emitText("movq " + slotRef(h) + ", %rcx");
  emitText("movq %rax, (%rcx)");
  if (hasArg) {
    genExpr(o->arg.get());
    emitText("movq " + slotRef(h) + ", %rcx");
    if (isFloatType(o->arg->exprType)) emitText("movsd %xmm0, 8(%rcx)");
    else emitText("movq %rax, 8(%rcx)");
  }
  emitText("movq " + slotRef(h) + ", %rax");
}

// `x?`: no None/Err, o valor vira o retorno da funÃ§Ã£o (cÃ©lula None nova para
// Option; a prÃ³pria cÃ©lula Err para Result) e salta direto para o epÃ­logo.
void Codegen::genTry(TryExpr* t) {
  genExpr(t->operand.get());          // cÃ©lula em %rax
  int cellSlot = nextSlot_++;
  emitText("movq %rax, " + slotRef(cellSlot));
  emitText("movq (%rax), %rdx");      // tag
  bool isOpt = t->operand->exprType.isOption();
  long long badTag = isOpt ? 0 : 1;   // None / Err
  std::string okL = newLabel("try_ok");
  emitText("cmpq $" + std::to_string(badTag) + ", %rdx");
  emitText("jne " + okL);
  if (isOpt) {
    // propaga None: cÃ©lula None do tipo de retorno
    genCallCalloc(16);
    emitText("movq $0, (%rax)");
  }
  // Result: %rax jÃ¡ Ã© a prÃ³pria cÃ©lula Err
  if (retSlot_ < 0) {
    retSlot_ = nextSlot_++;
    retIsFloat_ = false;
  }
  emitText("movq %rax, " + slotRef(retSlot_));
  emitText("jmp .Lret_" + fnLabel(curFn_));
  emitText(okL + ":");
  emitText("movq " + slotRef(cellSlot) + ", %rax");
  // Result<void, E>: `?` sÃ³ sinaliza (void) â€” nÃ£o hÃ¡ payload para ler
  if (t->exprType.kind != Type::Kind::Void) {
    if (isFloatType(t->exprType)) emitText("movsd 8(%rax), %xmm0");
    else emitText("movq 8(%rax), %rax");
  }
}

// `e?.campo` (spec Â§12): campo de referÃªncia de classe com acesso condicional.
// Campo de classe/string â†’ a prÃ³pria referÃªncia (null quando e Ã© null);
// campo de tipo de valor â†’ Option<T> (None quando e Ã© null).
void Codegen::genOptMember(OptMemberExpr* om) {
  Type ft = om->exprType;
  bool refResult = ft.kind == Type::Kind::Class || ft.kind == Type::Kind::String;
  bool floatPay = !refResult && isFloatType(*ft.elem);
  int off = om->fieldOffset;

  int objSlot = nextSlot_++;
  genExpr(om->object.get());
  emitText("movq %rax, " + slotRef(objSlot));

  std::string contL = newLabel("optm_cont");
  std::string nullL = newLabel("optm_null");
  std::string doneL = newLabel("optm_done");
  emitText("cmpq $0, " + slotRef(objSlot));
  emitText("jne " + contL);
  emitText("jmp " + nullL);

  emitText(contL + ":");
  emitText("movq " + slotRef(objSlot) + ", %rax");
  if (refResult) {
    emitText("movq " + std::to_string(off) + "(%rax), %rax");
    emitText("jmp " + doneL);
  } else {
    // Option<T>: envolve o valor do campo em Some
    if (floatPay) emitText("movsd " + std::to_string(off) + "(%rax), %xmm0");
    else emitText("movq " + std::to_string(off) + "(%rax), %rax");
    int pay = nextSlot_++;
    if (floatPay) emitText("movsd %xmm0, " + slotRef(pay));
    else emitText("movq %rax, " + slotRef(pay));
    genCallCalloc(16);
    int cell = nextSlot_++;
    emitText("movq %rax, " + slotRef(cell)); // cÃ©lula Some
    emitText("movq $1, %rax");
    emitText("movq " + slotRef(cell) + ", %rcx");
    emitText("movq %rax, (%rcx)");
    if (floatPay) emitText("movsd " + slotRef(pay) + ", 8(%rcx)");
    else {
      emitText("movq " + slotRef(pay) + ", %rax");
      emitText("movq %rax, 8(%rcx)");
    }
    emitText("movq " + slotRef(cell) + ", %rax");
    emitText("jmp " + doneL);
  }

  emitText(nullL + ":");
  if (refResult) {
    emitText("movq $0, %rax"); // e == null â†’ resultado null
  } else {
    genCallCalloc(16);
    emitText("movq $0, (%rax)"); // cÃ©lula None
  }
  emitText(doneL + ":");
}

void Codegen::genOptIndex(OptIndexExpr* oi) {
  Type ot = oi->object->exprType;
  Type resT = oi->exprType;
  bool refResult = (resT.kind == Type::Kind::Class || resT.kind == Type::Kind::String);
  bool floatPay = !refResult && isFloatType(ot.kind == Type::Kind::String ? Type::makeChar() : *ot.elem);

  int objSlot = nextSlot_++;
  genExpr(oi->object.get());
  emitText("movq %rax, " + slotRef(objSlot));

  int idxSlot = nextSlot_++;
  genExpr(oi->index.get());
  emitText("movq %rax, " + slotRef(idxSlot));

  std::string nullL = newLabel("opti_null");
  std::string doneL = newLabel("opti_done");

  // 1. Para List ou String: check if obj == null
  if (ot.kind == Type::Kind::List || ot.kind == Type::Kind::String) {
    emitText("cmpq $0, " + slotRef(objSlot));
    emitText("je " + nullL);
  }

  // 2. Check if index < 0
  emitText("cmpq $0, " + slotRef(idxSlot));
  emitText("jl " + nullL);

  // 3. Check if index >= length
  if (ot.kind == Type::Kind::Array) {
    emitText("cmpq $" + std::to_string(ot.arraySize) + ", " + slotRef(idxSlot));
    emitText("jge " + nullL);
  } else if (ot.kind == Type::Kind::List) {
    emitText("movq " + slotRef(objSlot) + ", %rcx");
    emitRuntimeCall("hphl_list_len");
    emitText("cmpq %rax, " + slotRef(idxSlot));
    emitText("jge " + nullL);
  } else { // String
    emitText("movq " + slotRef(objSlot) + ", %rcx");
    emitRuntimeCall("hphl_str_len");
    emitText("cmpq %rax, " + slotRef(idxSlot));
    emitText("jge " + nullL);
  }

  // Em limites: carregar elemento
  if (ot.kind == Type::Kind::String) {
    emitText("movq " + slotRef(objSlot) + ", %rcx");
    emitText("movq " + slotRef(idxSlot) + ", %rdx");
    emitRuntimeCall("hphl_str_char_index");
  } else if (ot.kind == Type::Kind::List) {
    int stride = typeSize(*ot.elem);
    emitText("movq " + slotRef(objSlot) + ", %rcx");
    emitRuntimeCall("hphl_list_data");
    emitText("movq " + slotRef(idxSlot) + ", %rcx");
    emitText("imulq $" + std::to_string(stride) + ", %rcx, %rcx");
    emitText("addq %rcx, %rax");
    if (floatPay) {
      emitText("movsd (%rax), %xmm0");
    } else {
      emitText("movq (%rax), %rax");
    }
  } else { // Array
    int stride = typeSize(*ot.elem);
    genAddr(oi->object.get());
    emitText("movq " + slotRef(idxSlot) + ", %rcx");
    emitText("imulq $" + std::to_string(stride) + ", %rcx, %rcx");
    emitText("addq %rcx, %rax");
    if (floatPay) {
      emitText("movsd (%rax), %xmm0");
    } else {
      emitText("movq (%rax), %rax");
    }
  }

  if (refResult) {
    emitText("jmp " + doneL);
  } else {
    // Option<T>: envolve em Some
    int pay = nextSlot_++;
    if (floatPay) emitText("movsd %xmm0, " + slotRef(pay));
    else emitText("movq %rax, " + slotRef(pay));
    genCallCalloc(16);
    int cell = nextSlot_++;
    emitText("movq %rax, " + slotRef(cell));
    emitText("movq $1, %rax");
    emitText("movq " + slotRef(cell) + ", %rcx");
    emitText("movq %rax, (%rcx)");
    if (floatPay) emitText("movsd " + slotRef(pay) + ", 8(%rcx)");
    else {
      emitText("movq " + slotRef(pay) + ", %rax");
      emitText("movq %rax, 8(%rcx)");
    }
    emitText("movq " + slotRef(cell) + ", %rax");
    emitText("jmp " + doneL);
  }

  emitText(nullL + ":");
  if (refResult) {
    emitText("movq $0, %rax");
  } else {
    genCallCalloc(16);
    emitText("movq $0, (%rax)");
  }
  emitText(doneL + ":");
}

// `e?.metodo(args)` (spec Â§12): chamada condicional. e == null â†’ skip (mÃ©todo
// void), resultado null (referÃªncia) ou cÃ©lula None (Option<T>); senÃ£o chama e
// (quando Option) envolve o payload em Some.
void Codegen::genOptCall(CallExpr* c) {
  auto om = static_cast<OptMemberExpr*>(c->callee.get());
  FunctionDecl* fn = om->resolved;
  Type rt = c->exprType;
  bool refResult = rt.kind == Type::Kind::Class || rt.kind == Type::Kind::String;
  bool optResult = rt.isOption();
  bool floatPay = optResult && isFloatType(*rt.elem);

  int objSlot = nextSlot_++;
  genExpr(om->object.get());
  emitText("movq %rax, " + slotRef(objSlot));

  std::string contL = newLabel("optc_cont");
  std::string nullL = newLabel("optc_null");
  std::string doneL = newLabel("optc_done");
  emitText("cmpq $0, " + slotRef(objSlot));
  emitText("jne " + contL);
  emitText("jmp " + nullL);

  emitText(contL + ":");
  // espelho de genCallInternal, com o this vindo do slot preservado
  int total = 1 + (int)c->args.size();
  int stackArgs = total > 4 ? total - 4 : 0;
  int bytes = (32 + stackArgs * 8 + 15) & ~15;
  emitText("subq $" + std::to_string(bytes) + ", %rsp");
  emitText("movq " + slotRef(objSlot) + ", %rax");
  emitText("movq %rax, 0(%rsp)");
  for (size_t i = 0; i < c->args.size(); i++) {
    Expr* a = c->args[i].get();
    int k = (int)i + 1;
    genExpr(a);
    if (isStructType(a->exprType))
      genStructCopy(a->exprType);
    if (k < 4) {
      if (isFloatType(a->exprType))
        emitText("movsd %xmm0, " + std::to_string(8 * k) + "(%rsp)");
      else
        emitText("movq %rax, " + std::to_string(8 * k) + "(%rsp)");
    } else {
      int s = k - 4;
      if (isFloatType(a->exprType))
        emitText("movsd %xmm0, " + std::to_string(32 + 8 * s) + "(%rsp)");
      else
        emitText("movq %rax, " + std::to_string(32 + 8 * s) + "(%rsp)");
    }
  }
  emitText("movq 0(%rsp), %rcx");
  const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
  for (size_t i = 0; i < c->args.size(); i++) {
    int k = (int)i + 1;
    if (k >= 4) continue;
    if (isFloatType(c->args[i]->exprType))
      emitText("movsd " + std::to_string(8 * k) + "(%rsp), %xmm" + std::to_string(k));
    else
      emitText("movq " + std::to_string(8 * k) + "(%rsp), " + regs[k]);
  }
  emitText("call " + fnLabel(fn));
  emitText("addq $" + std::to_string(bytes) + ", %rsp");

  if (optResult) {
    // envolve o resultado do mÃ©todo em Some (payload salvo antes do calloc)
    int pay = nextSlot_++;
    if (floatPay) emitText("movsd %xmm0, " + slotRef(pay));
    else emitText("movq %rax, " + slotRef(pay));
    genCallCalloc(16);
    int cell = nextSlot_++;
    emitText("movq %rax, " + slotRef(cell)); // cÃ©lula Some
    emitText("movq $1, %rax");
    emitText("movq " + slotRef(cell) + ", %rcx");
    emitText("movq %rax, (%rcx)");
    if (floatPay) emitText("movsd " + slotRef(pay) + ", 8(%rcx)");
    else {
      emitText("movq " + slotRef(pay) + ", %rax");
      emitText("movq %rax, 8(%rcx)");
    }
    emitText("movq " + slotRef(cell) + ", %rax");
    emitText("jmp " + doneL);
  } else if (refResult) {
    emitText("jmp " + doneL);
  }
  // void: segue direto para doneL (nada a produzir)

  emitText(nullL + ":");
  if (refResult) {
    emitText("movq $0, %rax");
  } else if (optResult) {
    genCallCalloc(16);
    emitText("movq $0, (%rax)");
  }
  emitText(doneL + ":");
}

// `a ?? padrao`: payload do Some(a) / Ok(a) ou o padrão
void Codegen::genCoalesce(CoalesceExpr* c) {
  genExpr(c->lhs.get());              // célula em %rax
  int cellSlot = nextSlot_++;
  emitText("movq %rax, " + slotRef(cellSlot));
  emitText("movq (%rax), %rdx");      // tag
  std::string elseL = newLabel("coal_else");
  std::string endL = newLabel("coal_end");
  long long badTag = c->lhs->exprType.isOption() ? 0 : 1;
  emitText("cmpq $" + std::to_string(badTag) + ", %rdx"); // None (0) or Err (1)?
  emitText("je " + elseL);
  emitText("movq " + slotRef(cellSlot) + ", %rax");
  if (isFloatType(c->exprType)) emitText("movsd 8(%rax), %xmm0");
  else emitText("movq 8(%rax), %rax");
  emitText("jmp " + endL);
  emitText(elseL + ":");
  genExpr(c->rhs.get());
  emitText(endL + ":");
}




} // namespace hphl