// Auto-generated from codegen.cpp
#include "codegen.h"
#include <cstdio>
#include <functional>
#include "stdlib/stdbuiltins.h"
#include <cstdint>
#include <cstring>

/* M27: hphl_set_class_desc â€” called at startup to register class bitmaps. */
extern "C" int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);

// O corpo de cada funÃ§Ã£o com lowering disponÃ­vel Ã© emitido do HIR
// (genFunctionHir). FunÃ§Ãµes sintÃ©ticas (helpers derive, instÃ¢ncias genÃ©ricas
// e mÃ©todos de classe) agora tambÃ©m sÃ£o baixadas para HIR via o pass adicional
// em loweringToHir, eliminando o fallback AST para estes casos.

namespace hphl {

Codegen::Codegen(Semantic& sem, const std::string& filename, bool debug)
    : sem_(sem), filename_(filename), debug_(debug) {}

std::string Codegen::newLabel(const std::string& base) {
  return ".L" + base + "_" + std::to_string(labelCounter_++);
}

void Codegen::emit(std::ostringstream& out, const std::string& line) {
  out << line << "\n";
}

Codegen::Local* Codegen::findLocal(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto sit = it->find(name);
    if (sit != it->end()) return &sit->second;
  }
  return nullptr;
}

bool Codegen::hasHir() const {
  return !sem_.hir().functions.empty();
}

void Codegen::generateFunctionFromHir(HirFunction* hf) {
  if (!hf->decl) return;
  if (hf->body) {
    genFunctionHir(hf->decl, hf->body.get());
  } else {
    genFunction(hf->decl);
  }
}

// O HIR dita a ordem das funÃ§Ãµes declaradas; qualquer funÃ§Ã£o adicional criada
// pela anÃ¡lise (instÃ¢ncias de genÃ©ricos, helpers `derive`, tarefas sintÃ©ticas)
// nÃ£o aparece no HIR e Ã© emitida normalmente da AST.
void Codegen::generateFunctionsFromHir() {
  std::set<FunctionDecl*> emitted;
  auto& funcs = const_cast<std::vector<HirFunction>&>(sem_.hir().functions);
  for (auto& hf : funcs) {
    if (!hf.decl) continue;
    // M10 (v0.45): templates genÃ©ricos nunca sÃ£o emitidos (sÃ³ instÃ¢ncias, que
    // tÃªm label prÃ³prio); emitir o template crasha em TypeVar. MÃ©todos de
    // classe-template idem â€” o dono Ã© o canÃ´nico do template.
    if (!hf.decl->typeParams.empty()) continue;
    if (hf.decl->isMethod && !hf.decl->ownerClass.empty()) {
      auto cit = sem_.classes().find(hf.decl->ownerClass);
      if (cit != sem_.classes().end() && cit->second.isTemplate) continue;
    }
    emitted.insert(hf.decl);
    generateFunctionFromHir(&hf);
  }
  genPendingHirTasks();
  for (auto& fi : sem_.functions()) {
    if (!emitted.count(fi.decl)) genFunction(fi.decl);
  }
}

void Codegen::generateFunctionsFromAst() {
  for (auto& fi : sem_.functions()) {
    genFunction(fi.decl);
  }
}

std::string Codegen::fnLabel(FunctionDecl* fn) {
  auto it = fnLabels_.find(fn);
  if (it != fnLabels_.end()) return it->second;
  std::string label;
  if (fn->isExtern) {
    // FFI: símbolo bruto da DLL/C (ex: MessageBoxA, puts, sin)
    label = fn->name;
  } else if (fn->isEntryPoint) {
    label = "main";
  } else if (fn->isMethod) {
    // overload por arity: aridade declarada no label (Set/0 vs Set/1;
    // Box()/Box(int) deixam de colidir)
    label = "_hphl_" + fn->ownerClass + "_" + fn->name + "_" +
            std::to_string(fn->params.size());
  } else {
    label = "_hphl_" + fn->name + "_" + std::to_string(fn->params.size());
  }
  fnLabels_[fn] = label;
  return label;
}

int Codegen::classSize(const std::string& name) {
  auto& ci = sem_.classes().at(name);
  return ci.size;
}

// M31: roots permanentes para globals string. Escalares: 1 root (endereço
// do label). Arrays de string: 1 root por elemento (.Lg+8*i). Tabela .data
// com endereços absolutos + hphl_gc_add_roots_batch (uma chamada).
void Codegen::emitGlobalStringRoots() {
  struct Item { std::string lbl; int count; };
  std::vector<Item> items;
  for (auto* g : sem_.globals()) {
    if (g->storage == StoragePolicy::ThreadLocal) continue;
    std::string lbl = ".Lg_" + g->name;
    if (g->type.kind == Type::Kind::String) {
      items.push_back({lbl, 1});
    } else if (g->type.kind == Type::Kind::Array && g->type.elem &&
               g->type.elem->kind == Type::Kind::String && g->type.arraySize > 0 &&
               g->type.arraySize <= 1000000) {
      items.push_back({lbl, g->type.arraySize});
    }
  }
  if (items.empty()) return;
  int total = 0;
  for (auto& it : items) total += it.count;
  std::string tbl = ".LgcStrRoots_" + std::to_string(labelCounter_++);
  emitData(tbl + ":");
  for (auto& it : items) {
    for (int i = 0; i < it.count; i++) {
      emitData("  .quad " + it.lbl + "+" + std::to_string(8 * i));
    }
  }
  emitText("leaq " + tbl + "(%rip), %rcx");
  emitText("movq $" + std::to_string(total) + ", %rdx");
  emitRuntimeCall("hphl_gc_add_roots_batch");
}

int Codegen::fieldOffset(const std::string& className, const std::string& field) {
  auto& ci = sem_.classes().at(className);
  auto it = ci.fields.find(field);
  if (it != ci.fields.end()) return it->second.second;
  if (!ci.base.empty()) {
    auto bit = sem_.classes().find(ci.base);
    if (bit != sem_.classes().end()) return fieldOffset(ci.base, field);
  }
  return 0;
}

// struct: classe com semÃ¢ntica de valor (cÃ³pia nas fronteiras de passagem)
bool Codegen::isStructType(const Type& t) const {
  if (t.kind != Type::Kind::Class) return false;
  auto it = sem_.classes().find(t.name);
  return it != sem_.classes().end() && it->second.decl->isStruct;
}

// A2 (interface como tipo): Class cujo decl e interface. Vale para o nome
// canonico simples (nao-generica) e para instancias monomorfizadas.
bool Codegen::isInterfaceType(const Type& t) const {
  if (t.kind != Type::Kind::Class) return false;
  auto it = sem_.classes().find(t.name);
  return it != sem_.classes().end() && it->second.decl &&
         it->second.decl->isInterface;
}

// assume o ponteiro do bloco em %rax; produz um bloco novo e independente
void Codegen::genStructCopy(const Type& t) {
  emitText("movq %rax, %rcx");
  emitText("movq $" + std::to_string(classSize(t.name)) + ", %rdx");
  emitRuntimeCall("hphl_struct_copy");
}

// expressÃµes que jÃ¡ produzem um bloco fresco (nÃ£o aliam com o destino)
bool Codegen::isFreshAllocExpr(const Expr* e) const {
  if (!e) return false;
  if (e->kind == ExprKind::New) return true;
  if (e->kind == ExprKind::Call) {
    auto* c = static_cast<const CallExpr*>(e);
    return c->resolved && c->resolved->isConstructor;
  }
  return false;
}

bool Codegen::tryAllocReg(int slot, const Type& t, StoragePolicy p, bool byRef) {
  if (byRef) return false;
  if (p != StoragePolicy::Stack && p != StoragePolicy::Auto) return false;
  if (isFloatType(t)) return false;
  if (t.kind == Type::Kind::String || t.kind == Type::Kind::List ||
      t.kind == Type::Kind::Map || t.kind == Type::Kind::Class) return false;
  if (nextCalleeIdx_ >= calleeSaved_.size()) return false;
  regForSlot_[slot] = calleeSaved_[nextCalleeIdx_++];
  return true;
}

// rhs de binária float pode destruir o lhs estagiado em xmm1? Conservador:
// só folhas sem chamada e sem subexpressão float são seguras (o resto usa
// xmm1 como acumulador ou faz calls). Default = true (spill — correto).
bool Codegen::hirClobbersXmm1(HirExpr* e) const {
  if (!e) return false;
  switch (e->kind) {
    case HirExprKind::IntLit:
    case HirExprKind::FloatLit:
    case HirExprKind::CharLit:
    case HirExprKind::BoolLit:
    case HirExprKind::StringLit:
    case HirExprKind::NullLit:
    case HirExprKind::This:
      return false;
    case HirExprKind::Var: {
      auto* v = static_cast<HirVar*>(e);
      return v->isProperty && v->propGet; // getter = call
    }
    case HirExprKind::Member: {
      auto* m = static_cast<HirMember*>(e);
      if (m->isProperty && m->propGet) return true;
      return hirClobbersXmm1(m->object.get());
    }
    case HirExprKind::Cast: {
      auto* c = static_cast<HirCast*>(e);
      return hirClobbersXmm1(c->operand.get());
    }
    default:
      return true; // Binary/Unary/Call/Index/Assign/Await/...
  }
}

bool Codegen::hirHasCall(HirExpr* e) const {
  if (!e) return false;
  if (e->kind == HirExprKind::Call) return true;
  // recursivo para filhos
  switch (e->kind) {
    case HirExprKind::Binary: {
      auto* b = static_cast<HirBinary*>(e);
      return hirHasCall(b->lhs.get()) || hirHasCall(b->rhs.get());
    }
    case HirExprKind::Unary: {
      auto* u = static_cast<HirUnary*>(e);
      return hirHasCall(u->operand.get());
    }
    case HirExprKind::Assign: {
      auto* a = static_cast<HirAssign*>(e);
      return hirHasCall(a->target.get()) || hirHasCall(a->value.get());
    }
    case HirExprKind::Member: {
      auto* m = static_cast<HirMember*>(e);
      return hirHasCall(m->object.get());
    }
    case HirExprKind::Index: {
      auto* ix = static_cast<HirIndex*>(e);
      return hirHasCall(ix->object.get()) || hirHasCall(ix->index.get());
    }
    case HirExprKind::Call: {
      auto* c = static_cast<HirCall*>(e);
      for (auto &arg : c->args) if (hirHasCall(arg.get())) return true;
      return true;
    }
    case HirExprKind::Await:
      // M14.4: await envolve hphl_wait_task (call) — sem isso, binários com
      // `await` no rhs estagiavam o lhs em %r10 e o wait o clobberizava
      return true;
    case HirExprKind::Lambda:
      // v0.95: criação de closure chama malloc — mesmo tratamento
      return true;
    default: return false;
  }
}

long long Codegen::getLoopBound(const std::string& var) const {
  auto it = loopUpperBounds_.find(var);
  return it == loopUpperBounds_.end() ? -1 : it->second;
}

long long Codegen::maxIndexForHir(HirExpr* e) const {
  if (!e) return -1;
  switch (e->kind) {
    case HirExprKind::IntLit: return static_cast<HirIntLit*>(e)->value;
    case HirExprKind::Var: {
      auto* v = static_cast<HirVar*>(e);
      long long b = getLoopBound(v->name);
      return b >= 0 ? b - 1 : -1;
    }
    case HirExprKind::Binary: {
      auto* b = static_cast<HirBinary*>(e);
      long long l = maxIndexForHir(b->lhs.get());
      long long r = maxIndexForHir(b->rhs.get());
      if (l < 0 || r < 0) return -1;
      switch (b->op) {
        case BinOp::Add: return l + r;
        case BinOp::Sub: return l - r; // conservador: nÃ£o elide se sub
        case BinOp::Mul: return l * r;
        default: return -1;
      }
    }
    default: return -1;
  }
}

bool Codegen::canElideHirBoundsCheck(HirExpr* index, long long arraySize) const {
  if (arraySize <= 0) return false;
  long long m = maxIndexForHir(index);
  bool ok = m >= 0 && m < arraySize;
  return ok;
}

bool Codegen::extractLoopBound(HirFor* f, std::string& var, long long& bound) const {
  if (!f || !f->init || !f->cond) return false;
  // init: HirVarDecl com i=0 ou i=1
  HirVarDecl* vd = nullptr;
  if (f->init->kind == HirStmtKind::VarDecl) vd = static_cast<HirVarDecl*>(f->init.get());
  else return false;
  var = vd->name;
  // cond: HirBinary i < N ou i <= N
  if (!f->cond || f->cond->kind != HirExprKind::Binary) return false;
  auto* cond = static_cast<HirBinary*>(f->cond.get());
  if (cond->lhs->kind != HirExprKind::Var) return false;
  auto* lhsVar = static_cast<HirVar*>(cond->lhs.get());
  if (lhsVar->name != var) return false;
  if (cond->rhs->kind != HirExprKind::IntLit) return false;
  long long rhs = static_cast<HirIntLit*>(cond->rhs.get())->value;
  if (cond->op == BinOp::Lt) bound = rhs;
  else if (cond->op == BinOp::Le) bound = rhs + 1;
  else return false;
  // verifica init valor 0 ou 1 e step i++ ou i+=1 (nÃ£o checado a fundo, assume 0..bound-1)
  return true;
}

// Marca variáveis locais cujo endereço é tomado. Um slot reg-backed vive no
// registrador, mas ++/--/ref/out/in tomam o endereço via genHirAddr (que usa
// slotRefMem) — o que leria um slot de memória desatualizado. Para evitar a
// divergência reg/memória, essas variáveis são excluídas do regalloc.
void Codegen::hirMarkAddrTakenExpr(HirExpr* e) {
  if (!e) return;
  switch (e->kind) {
    case HirExprKind::Var: {
      auto* v = static_cast<HirVar*>(e);
      if (!v->isGlobal && !v->isConst) addrTakenVars_.insert(v->name);
      break;
    }
    case HirExprKind::Unary: {
      auto* u = static_cast<HirUnary*>(e);
      hirMarkAddrTakenExpr(u->operand.get());
      break;
    }
    case HirExprKind::Assign: {
      auto* a = static_cast<HirAssign*>(e);
      hirMarkAddrTakenExpr(a->target.get());
      hirMarkAddrTakenExpr(a->value.get());
      break;
    }
    case HirExprKind::Binary: {
      auto* b = static_cast<HirBinary*>(e);
      hirMarkAddrTakenExpr(b->lhs.get());
      hirMarkAddrTakenExpr(b->rhs.get());
      break;
    }
    case HirExprKind::Member: {
      auto* m = static_cast<HirMember*>(e);
      hirMarkAddrTakenExpr(m->object.get());
      break;
    }
    case HirExprKind::Index: {
      auto* ix = static_cast<HirIndex*>(e);
      hirMarkAddrTakenExpr(ix->object.get());
      hirMarkAddrTakenExpr(ix->index.get());
      break;
    }
    case HirExprKind::Call: {
      auto* c = static_cast<HirCall*>(e);
      if (c->resolved) {
        for (size_t i = 0; i < c->args.size(); i++) {
          bool byRef = i < c->resolved->params.size() && c->resolved->params[i]->isByRef();
          if (byRef) hirMarkAddrTakenExpr(c->args[i].get());
          else hirMarkAddrTakenExpr(c->args[i].get());
        }
      } else {
        for (auto& a : c->args) hirMarkAddrTakenExpr(a.get());
      }
      break;
    }
    case HirExprKind::Cast: {
      auto* c = static_cast<HirCast*>(e);
      hirMarkAddrTakenExpr(c->operand.get());
      break;
    }
    case HirExprKind::CellTag: {
      auto* c = static_cast<HirCellTag*>(e);
      hirMarkAddrTakenExpr(c->subject.get());
      break;
    }
    case HirExprKind::LoadAt: {
      auto* l = static_cast<HirLoadAt*>(e);
      hirMarkAddrTakenExpr(l->subject.get());
      break;
    }
    case HirExprKind::TupleLit: {
      auto* t = static_cast<HirTupleLit*>(e);
      for (auto& el : t->elements) hirMarkAddrTakenExpr(el.get());
      break;
    }
    case HirExprKind::ArrayLit: {
      auto* a = static_cast<HirArrayLit*>(e);
      for (auto& el : a->elements) hirMarkAddrTakenExpr(el.get());
      break;
    }
    case HirExprKind::OptCtor: {
      auto* o = static_cast<HirOptCtor*>(e);
      hirMarkAddrTakenExpr(o->arg.get());
      break;
    }
    case HirExprKind::Await: {
      auto* a = static_cast<HirAwaitExpr*>(e);
      hirMarkAddrTakenExpr(a->operand.get());
      break;
    }
    default:
      break;
  }
}

void Codegen::hirMarkAddrTaken(HirBlock* b) {
  if (!b) return;
  for (auto& s : b->stmts) {
    switch (s->kind) {
      case HirStmtKind::VarDecl: {
        auto* v = static_cast<HirVarDecl*>(s.get());
        hirMarkAddrTakenExpr(v->init.get());
        break;
      }
      case HirStmtKind::ExprStmt: {
        auto* es = static_cast<HirExprStmt*>(s.get());
        hirMarkAddrTakenExpr(es->expr.get());
        break;
      }
      case HirStmtKind::Return: {
        auto* r = static_cast<HirReturn*>(s.get());
        hirMarkAddrTakenExpr(r->value.get());
        break;
      }
      case HirStmtKind::If: {
        auto* i = static_cast<HirIf*>(s.get());
        hirMarkAddrTakenExpr(i->cond.get());
        hirMarkAddrTaken(i->thenBranch.get());
        if (i->elseBranch) {
          if (i->elseBranch->kind == HirStmtKind::Block)
            hirMarkAddrTaken(static_cast<HirBlock*>(i->elseBranch.get()));
          else if (i->elseBranch->kind == HirStmtKind::If) {
            auto* ei = static_cast<HirIf*>(i->elseBranch.get());
            hirMarkAddrTakenExpr(ei->cond.get());
            hirMarkAddrTaken(ei->thenBranch.get());
            if (ei->elseBranch && ei->elseBranch->kind == HirStmtKind::Block)
              hirMarkAddrTaken(static_cast<HirBlock*>(ei->elseBranch.get()));
          }
        }
        break;
      }
      case HirStmtKind::While: {
        auto* w = static_cast<HirWhile*>(s.get());
        hirMarkAddrTakenExpr(w->cond.get());
        hirMarkAddrTaken(w->body.get());
        break;
      }
      case HirStmtKind::DoWhile: {
        auto* d = static_cast<HirDoWhile*>(s.get());
        hirMarkAddrTakenExpr(d->cond.get());
        hirMarkAddrTaken(d->body.get());
        break;
      }
      case HirStmtKind::For: {
        auto* f = static_cast<HirFor*>(s.get());
        if (f->init) {
          if (f->init->kind == HirStmtKind::VarDecl) {
            auto* vd = static_cast<HirVarDecl*>(f->init.get());
            hirMarkAddrTakenExpr(vd->init.get());
          } else if (f->init->kind == HirStmtKind::ExprStmt) {
            auto* es = static_cast<HirExprStmt*>(f->init.get());
            hirMarkAddrTakenExpr(es->expr.get());
          }
        }
        hirMarkAddrTakenExpr(f->cond.get());
        hirMarkAddrTakenExpr(f->step.get());
        hirMarkAddrTaken(f->body.get());
        break;
      }
      case HirStmtKind::Block: {
        hirMarkAddrTaken(static_cast<HirBlock*>(s.get()));
        break;
      }
      case HirStmtKind::Panic: {
        auto* p = static_cast<HirPanic*>(s.get());
        hirMarkAddrTakenExpr(p->message.get());
        break;
      }
      case HirStmtKind::Assert: {
        auto* a = static_cast<HirAssert*>(s.get());
        hirMarkAddrTakenExpr(a->cond.get());
        break;
      }
      case HirStmtKind::Throw: {
        auto* t = static_cast<HirThrow*>(s.get());
        hirMarkAddrTakenExpr(t->value.get());
        break;
      }
      case HirStmtKind::Try: {
        auto* t = static_cast<HirTry*>(s.get());
        hirMarkAddrTaken(t->body.get());
        hirMarkAddrTaken(t->catchBody.get());
        break;
      }
      case HirStmtKind::Lock: {
        auto* l = static_cast<HirLock*>(s.get());
        hirMarkAddrTakenExpr(l->target.get());
        hirMarkAddrTaken(l->body.get());
        break;
      }
      case HirStmtKind::Spawn: {
        auto* sp = static_cast<HirSpawn*>(s.get());
        hirMarkAddrTaken(sp->body.get());
        break;
      }
      case HirStmtKind::Parallel: {
        auto* p = static_cast<HirParallel*>(s.get());
        for (auto& part : p->parts) hirMarkAddrTaken(part.get());
        break;
      }
      case HirStmtKind::ParallelForeach: {
        auto* pf = static_cast<HirParallelForeach*>(s.get());
        hirMarkAddrTakenExpr(pf->collection.get());
        hirMarkAddrTaken(pf->body.get());
        break;
      }
      default:
        break;
    }
  }
}




} // namespace hphl
