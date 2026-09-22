#include "hir.h"
#include "semantic/semantic.h"
#include <cstdio>
#include <cstring>
#include <set>

namespace hphl {

namespace {

std::string hirTypeName(const Type& t) {
  switch (t.kind) {
    case Type::Kind::Void: return "void";
    case Type::Kind::Int: return "int<" + std::to_string(t.bits) + ">";
    case Type::Kind::UInt: return "uint<" + std::to_string(t.bits) + ">";
    case Type::Kind::Float: return "float<" + std::to_string(t.bits) + ">";
    case Type::Kind::Bool: return "bool";
    case Type::Kind::Char: return "char";
    case Type::Kind::String: return "string";
    case Type::Kind::Class: return "class " + t.name;
    case Type::Kind::Enum: return "enum " + t.name;
    case Type::Kind::Array:
      return "[" + hirTypeName(*t.elem) + " x " + std::to_string(t.arraySize) + "]";
    case Type::Kind::List: return "list<" + hirTypeName(*t.elem) + ">";
    case Type::Kind::Option: return "option<" + hirTypeName(*t.elem) + ">";
    case Type::Kind::Result:
      return "result<" + hirTypeName(*t.elem) + ", " + hirTypeName(*t.elem2) + ">";
    case Type::Kind::Task:
      return "task<" + (t.elem ? hirTypeName(*t.elem) : std::string("void")) + ">";
    case Type::Kind::Channel:
      return "channel<" + (t.elem ? hirTypeName(*t.elem) : std::string("void")) + ">";
    case Type::Kind::Func: {
      // v0.95: `func<(params) -> ret>`
      std::string s = "func<(";
      for (size_t i = 0; i < t.genericArgs.size(); i++) {
        if (i) s += ", ";
        s += hirTypeName(t.genericArgs[i]);
      }
      s += ") -> " + (t.elem ? hirTypeName(*t.elem) : std::string("void")) + ">";
      return s;
    }
    case Type::Kind::Mutex: return "mutex";
    case Type::Kind::Semaphore: return "semaphore";
    case Type::Kind::Event: return "event";
    case Type::Kind::Barrier: return "barrier";
    case Type::Kind::TypeVar: return t.name;
    default: return "?";
  }
}

bool isFloatType(const Type& t) { return t.kind == Type::Kind::Float; }

// layout de campos idêntico ao do codegen (sem_.classes())
long long hirFieldOffset(const Semantic& sem, const std::string& cls, const std::string& f) {
  auto& ci = sem.classes().at(cls);
  auto it = ci.fields.find(f);
  if (it != ci.fields.end()) return it->second.second;
  if (!ci.base.empty()) {
    auto bit = sem.classes().find(ci.base);
    if (bit != sem.classes().end()) return hirFieldOffset(sem, ci.base, f);
  }
  return 0;
}

// ---------------------------------------------------------------------------
// helpers de construção de nós
// ---------------------------------------------------------------------------

std::unique_ptr<HirExpr> hVar(const std::string& name, const Type& type) {
  auto e = std::make_unique<HirVar>();
  e->name = name;
  e->type = type;
  return e;
}

std::unique_ptr<HirIntLit> hInt(long long v, const Type& type) {
  auto e = std::make_unique<HirIntLit>();
  e->value = v;
  e->type = type;
  return e;
}

std::unique_ptr<HirFloatLit> hFloat(double v, const Type& type) {
  auto e = std::make_unique<HirFloatLit>();
  e->value = v;
  e->type = type;
  return e;
}

std::unique_ptr<HirBoolLit> hBool(bool v, const Type& type) {
  auto e = std::make_unique<HirBoolLit>();
  e->value = v;
  e->type = type;
  return e;
}

std::unique_ptr<HirNullLit> hNull(const Type& type) {
  auto e = std::make_unique<HirNullLit>();
  e->type = type;
  return e;
}

std::unique_ptr<HirExprStmt> hExpr(std::unique_ptr<HirExpr> e) {
  auto s = std::make_unique<HirExprStmt>();
  s->expr = std::move(e);
  return s;
}

std::unique_ptr<HirVarDecl> hVarDecl(const std::string& name, const Type& t,
                                     std::unique_ptr<HirExpr> init) {
  auto s = std::make_unique<HirVarDecl>();
  s->name = name;
  s->type = t;
  s->init = std::move(init);
  // temps do lowering: slot guarda o valor (sem alocação/free). Arrays ficam
  // não-opacos para ganhar cópia de bloco (semântica de valor do array).
  s->opaqueSlot = (t.kind != Type::Kind::Array);
  return s;
}

std::unique_ptr<HirAssign> hAssign(std::unique_ptr<HirExpr> target, std::unique_ptr<HirExpr> value,
                                   const Type& type = Type::makeInt(64)) {
  auto s = std::make_unique<HirAssign>();
  s->op = AssignOp::Plain;
  s->target = std::move(target);
  s->value = std::move(value);
  s->type = type;
  return s;
}

std::unique_ptr<HirBinary> hBin(BinOp op, std::unique_ptr<HirExpr> l, std::unique_ptr<HirExpr> r,
                                const Type& type = Type::makeInt(64)) {
  auto s = std::make_unique<HirBinary>();
  s->op = op;
  s->lhs = std::move(l);
  s->rhs = std::move(r);
  s->type = type;
  return s;
}

std::unique_ptr<HirUnary> hNot(std::unique_ptr<HirExpr> e, const Type& type = Type::makeBool()) {
  auto s = std::make_unique<HirUnary>();
  s->op = UnOp::Not;
  s->operand = std::move(e);
  s->type = type;
  return s;
}

std::unique_ptr<HirOptCtor> hOptCtor(const std::string& variant,
                                     std::unique_ptr<HirExpr> arg = nullptr,
                                     const Type& type = Type::makeInt(64)) {
  auto s = std::make_unique<HirOptCtor>();
  s->variant = variant;
  s->arg = std::move(arg);
  s->type = type;
  return s;
}

std::unique_ptr<HirCall> hRuntimeCall(const std::string& name,
                                      std::vector<std::unique_ptr<HirExpr>> args,
                                      const Type& type = Type::makeInt(64)) {
  auto s = std::make_unique<HirCall>();
  s->kind = HirCallKind::Runtime;
  s->runtimeName = name;
  s->args = std::move(args);
  s->type = type;
  return s;
}

std::unique_ptr<HirBlock> blkOf(std::vector<std::unique_ptr<HirStmt>>&& v) {
  auto b = std::make_unique<HirBlock>();
  b->stmts = std::move(v);
  return b;
}

std::unique_ptr<HirBlock> blkOfWrap(std::unique_ptr<HirStmt> s) {
  auto b = std::make_unique<HirBlock>();
  b->stmts.push_back(std::move(s));
  return b;
}

// valor neutro para temporários de lowerings (nunca lido em caminhos válidos)
std::unique_ptr<HirExpr> hDefaultValue(const Type& t) {
  switch (t.kind) {
    case Type::Kind::Int:
    case Type::Kind::UInt:
    case Type::Kind::Char:
      return hInt(0, t);
    case Type::Kind::Float:
      return hFloat(0.0, t);
    case Type::Kind::Bool:
      return hBool(false, t);
    default:
      return hNull(t);
  }
}

// clona sujeitos construídos pelo lowering (Var/LoadAt/Call puros)
std::unique_ptr<HirExpr> cloneSubject(const HirExpr& e) {
  switch (e.kind) {
    case HirExprKind::Var:
      return hVar(static_cast<const HirVar&>(e).name, e.type);
    case HirExprKind::LoadAt: {
      const auto& la = static_cast<const HirLoadAt&>(e);
      auto n = std::make_unique<HirLoadAt>();
      n->offset = la.offset;
      n->subject = cloneSubject(*la.subject);
      n->type = la.type;
      return n;
    }
    case HirExprKind::Call: {
      const auto& c = static_cast<const HirCall&>(e);
      auto n = std::make_unique<HirCall>();
      n->kind = c.kind;
      n->resolved = c.resolved;
      if (c.funcValue) n->funcValue = cloneSubject(*c.funcValue);
      n->runtimeName = c.runtimeName;
      n->enumCanon = c.enumCanon;
      n->entryName = c.entryName;
      n->index = c.index;
      n->primOp = c.primOp;
      n->type = c.type;
      for (auto& a : c.args) n->args.push_back(cloneSubject(*a));
      return n;
    }
    default:
      return hNull(e.type);
  }
}

// ---------------------------------------------------------------------------
// Lowerer: AST (pós-semântica) → HIR sem açúcar
// ---------------------------------------------------------------------------

class Lowerer {
public:
  Lowerer(HirProgram& hir, const Semantic& sem) : hir_(hir), sem_(sem) {}

  bool lowerProgram(const Program* prog) {
    for (auto& d : prog->decls) lowerDecl(d.get());
    // Also lower synthesized functions (derive helpers, generic instances, etc.)
    for (auto& fi : sem_.functions()) {
      lowerFunction(fi.decl);
    }
    return true;
  }

private:
  HirProgram& hir_;
  const Semantic& sem_;
  FunctionDecl* curFn_ = nullptr;
  std::vector<std::unique_ptr<HirStmt>>* out_ = nullptr;
  int temp_ = 0;
  int loopDepth_ = 0;
  int lambdaDepth_ = 0; // v0.95: >0 dentro de corpo de lambda (this → captura)
  std::vector<int> switchStack_;

  std::string newTemp() { return "__h" + std::to_string(++temp_); }

  std::set<FunctionDecl*> lowered_;

  bool switchSuppressed() const {
    return !switchStack_.empty() && loopDepth_ == switchStack_.back();
  }

  void lowerDecl(Decl* d) {
    if (auto fn = dynamic_cast<FunctionDecl*>(d)) {
      lowerFunction(fn);
    } else if (auto mod = dynamic_cast<ModuleDecl*>(d)) {
      for (auto& sub : mod->decls) lowerDecl(sub.get());
    } else if (auto cls = dynamic_cast<ClassDecl*>(d)) {
      for (auto& m : cls->methods) lowerDecl(m.get());
    }
  }

  void lowerFunction(FunctionDecl* fn) {
    if (lowered_.count(fn)) return;
    lowered_.insert(fn);

    HirFunction hf;
    hf.name = fn->name;
    hf.decl = fn;
    hf.isMethod = fn->isMethod;
    hf.ownerClass = fn->ownerClass;
    hf.returnType = fn->hasReturnType ? fn->returnType : Type::makeVoid();
    hf.hasReturnType = fn->hasReturnType;
    hf.moduleName = fn->moduleName;
    hf.filePath = fn->filePath;
    for (auto& p : fn->params) hf.params.emplace_back(p->name, p->type);

    std::vector<std::unique_ptr<HirStmt>> bodyList;
    out_ = &bodyList;
    curFn_ = fn;
    temp_ = 0;
    loopDepth_ = 0;
    switchStack_.clear();
    // A4: preconditions primeiro (out_ ativo p/ temps de desugar)
    for (auto& r : fn->requires) {
      auto a = std::make_unique<HirAssert>();
      a->line = r->line;
      a->cond = lowerExpr(r.get());
      out_->push_back(std::move(a));
    }
    if (fn->body) {
      // corpo no mesmo fluxo (equivale ao lowerBlock isolado, mas permite
      // os asserts acima; escopos são resolvidos no codegen)
      for (auto& s : fn->body->stmts) lowerStmt(s.get());
      // A4: funções sem valor de retorno (void/ctor) também checam ensures
      // ao cair do fim do corpo (`return;` explícito já foi instrumentado;
      // `result` aqui é impossível — rejeitado na semântica)
      if (!fn->ensures.empty() &&
          (!fn->hasReturnType || fn->returnType.kind == Type::Kind::Void)) {
        for (auto& e : fn->ensures) {
          auto a = std::make_unique<HirAssert>();
          a->line = e->line;
          a->cond = lowerExpr(e.get());
          bodyList.push_back(std::move(a));
        }
      }
      auto hb = std::make_unique<HirBlock>();
      hb->line = fn->body->line;
      hb->stmts = std::move(bodyList);
      hf.body = std::move(hb);
    }
    out_ = nullptr;
    curFn_ = nullptr;

    hir_.functions.push_back(std::move(hf));
    hir_.declMap[fn->name] = fn;
  }

  std::unique_ptr<HirBlock> lowerBlock(BlockStmt* b) {
    auto blk = std::make_unique<HirBlock>();
    auto* prev = out_;
    out_ = &blk->stmts;
    for (auto& s : b->stmts) lowerStmt(s.get());
    out_ = prev;
    return blk;
  }

  std::unique_ptr<HirBlock> lowerBranch(Stmt* s) {
    auto blk = std::make_unique<HirBlock>();
    auto* prev = out_;
    out_ = &blk->stmts;
    lowerStmt(s);
    out_ = prev;
    return blk;
  }

  void lowerStmt(Stmt* s) {
    switch (s->kind) {
      case StmtKind::Block:
        out_->push_back(lowerBlock(static_cast<BlockStmt*>(s)));
        break;
      case StmtKind::If: {
        auto st = static_cast<IfStmt*>(s);
        auto i = std::make_unique<HirIf>();
        i->line = st->line;
        i->cond = lowerExpr(st->cond.get());
        i->thenBranch = lowerBranch(st->thenBranch.get());
        if (st->elseBranch) i->elseBranch = lowerBranch(st->elseBranch.get());
        out_->push_back(std::move(i));
        break;
      }
      case StmtKind::For: {
        auto st = static_cast<ForStmt*>(s);
        auto f = std::make_unique<HirFor>();
        f->line = st->line;
        loopDepth_++;
        if (st->init) {
          f->init = lowerBranch(st->init.get());
          // for-init é VarDecl/ExprStmt: desembrulha bloco de 1 stmt
          if (f->init->kind == HirStmtKind::Block) {
            auto& b = static_cast<HirBlock&>(*f->init);
            if (b.stmts.size() == 1) f->init = std::move(b.stmts[0]);
          }
        }
        if (st->cond) f->cond = lowerExpr(st->cond.get());
        if (st->step) f->step = lowerExpr(st->step.get());
        f->body = lowerBranch(st->body.get());
        loopDepth_--;
        out_->push_back(std::move(f));
        break;
      }
      case StmtKind::While: {
        auto st = static_cast<WhileStmt*>(s);
        auto w = std::make_unique<HirWhile>();
        w->line = st->line;
        w->cond = lowerExpr(st->cond.get());
        loopDepth_++;
        w->body = lowerBranch(st->body.get());
        loopDepth_--;
        out_->push_back(std::move(w));
        break;
      }
      case StmtKind::DoWhile: {
        auto st = static_cast<DoWhileStmt*>(s);
        auto w = std::make_unique<HirDoWhile>();
        w->line = st->line;
        loopDepth_++;
        w->body = lowerBranch(st->body.get());
        loopDepth_--;
        w->cond = lowerExpr(st->cond.get());
        out_->push_back(std::move(w));
        break;
      }
      case StmtKind::Break: {
        auto br = std::make_unique<HirBreak>();
        br->line = s->line;
        if (!switchSuppressed()) out_->push_back(std::move(br));
        break;
      }
      case StmtKind::Continue: {
        auto ct = std::make_unique<HirContinue>();
        ct->line = s->line;
        if (!switchSuppressed()) out_->push_back(std::move(ct));
        break;
      }
      case StmtKind::Return: {
        auto st = static_cast<ReturnStmt*>(s);
        // A4: postconditions — { T result = e; assert(e1); ...; return result; }
        // O bloco aninhado (scope) faz `result` sombrear qualquer local do
        // usuário; sem valor (void) só os asserts (`result` já foi rejeitado).
        if (curFn_ && !curFn_->ensures.empty()) {
          auto blk = std::make_unique<HirBlock>();
          blk->line = st->line;
          if (st->value) {
            auto vd = std::make_unique<HirVarDecl>();
            vd->line = st->line;
            vd->name = "result";
            vd->type = st->value->exprType;
            vd->init = lowerExpr(st->value.get());
            blk->stmts.push_back(std::move(vd));
          }
          for (auto& e : curFn_->ensures) {
            auto a = std::make_unique<HirAssert>();
            a->line = e->line;
            a->cond = lowerExpr(e.get());
            blk->stmts.push_back(std::move(a));
          }
          auto r = std::make_unique<HirReturn>();
          r->line = st->line;
          if (st->value) r->value = hVar("result", st->value->exprType);
          blk->stmts.push_back(std::move(r));
          out_->push_back(std::move(blk));
          break;
        }
        auto r = std::make_unique<HirReturn>();
        r->line = st->line;
        if (st->value) r->value = lowerExpr(st->value.get());
        out_->push_back(std::move(r));
        break;
      }
      case StmtKind::ExprStmt: {
        auto he = hExpr(lowerExpr(static_cast<ExprStmt*>(s)->expr.get()));
        he->line = s->line;
        out_->push_back(std::move(he));
        break;
      }
      case StmtKind::VarDecl: {
        auto st = static_cast<StmtVarDecl*>(s);
        for (auto& decl : st->decls) {
          auto vd = std::make_unique<HirVarDecl>();
          vd->line = st->line;
          vd->name = decl->name;
          vd->type = decl->type;
          vd->storage = decl->storage;
          vd->atomic = decl->atomic;
          vd->primitiveInit = decl->primitiveInit;
          if (decl->init) vd->init = lowerExpr(decl->init.get());
          out_->push_back(std::move(vd));
        }
        break;
      }
      case StmtKind::Destructure: {
        // M10.1b: `var (a, b) = expr;` →
        //   __dtup = expr;
        //   a = __dtup.f0;  b = __dtup.f1;  ...
        auto st = static_cast<StmtDestructure*>(s);
        Type tt = st->tupleType; // tipo canônico já resolvido na semântica
        if (tt.kind != Type::Kind::Tuple) break; // erro já reportado
        auto tmp = std::make_unique<HirVarDecl>();
        tmp->line = st->line;
        tmp->name = "__dtup";
        tmp->type = tt;
        tmp->storage = StoragePolicy::Stack;
        tmp->init = lowerExpr(st->init.get());
        out_->push_back(std::move(tmp));
        for (size_t i = 0; i < st->names.size(); i++) {
          auto src = std::make_unique<HirVar>();
          src->name = "__dtup";
          src->type = tt;
          auto mem = std::make_unique<HirMember>();
          mem->object = std::move(src);
          mem->fieldOffset = (int)(8 * i);
          mem->type = tt.tupleElems[i];
          auto vd = std::make_unique<HirVarDecl>();
          vd->line = st->line;
          vd->name = st->names[i];
          vd->type = tt.tupleElems[i];
          vd->storage = StoragePolicy::Stack;
          vd->init = std::move(mem);
          out_->push_back(std::move(vd));
        }
        break;
      }
      case StmtKind::Switch:
        lowerSwitch(static_cast<SwitchStmt*>(s));
        break;
      case StmtKind::Foreach:
        lowerForeach(static_cast<ForeachStmt*>(s));
        break;
      case StmtKind::Panic: {
        auto st = static_cast<PanicStmt*>(s);
        auto p = std::make_unique<HirPanic>();
        p->line = st->line;
        p->message = lowerExpr(st->message.get());
        out_->push_back(std::move(p));
        break;
      }
      case StmtKind::Assert: {
        auto st = static_cast<AssertStmt*>(s);
        auto a = std::make_unique<HirAssert>();
        a->line = st->line;
        a->cond = lowerExpr(st->cond.get());
        out_->push_back(std::move(a));
        break;
      }
      case StmtKind::Throw: {
        auto st = static_cast<ThrowStmt*>(s);
        auto t = std::make_unique<HirThrow>();
        t->line = st->line;
        t->value = lowerExpr(st->value.get());
        out_->push_back(std::move(t));
        break;
      }
      case StmtKind::Try: {
        auto st = static_cast<TryStmt*>(s);
        auto t = std::make_unique<HirTry>();
        t->line = st->line;
        t->body = lowerBranch(st->body.get());
        t->hasCatch = st->hasCatch;
        t->catchType = st->catchType;
        t->catchVar = st->catchVar;
        if (st->catchBody) t->catchBody = lowerBranch(st->catchBody.get());
        out_->push_back(std::move(t));
        break;
      }
      case StmtKind::Lock: {
        auto st = static_cast<LockStmt*>(s);
        auto l = std::make_unique<HirLock>();
        l->line = st->line;
        l->target = lowerExpr(st->target.get());
        l->body = lowerBlock(st->body.get());
        out_->push_back(std::move(l));
        break;
      }
      case StmtKind::Spawn: {
        auto st = static_cast<SpawnStmt*>(s);
        auto sp = std::make_unique<HirSpawn>();
        sp->line = st->line;
        sp->body = lowerBlock(st->body.get());
        sp->captures = st->captures;
        out_->push_back(std::move(sp));
        break;
      }
      case StmtKind::Parallel: {
        auto st = static_cast<ParallelStmt*>(s);
        auto p = std::make_unique<HirParallel>();
        p->line = st->line;
        p->isDeterministic = st->isDeterministic;
        p->partCaptures = st->partCaptures;
        for (auto& part : st->parts) p->parts.push_back(lowerBranch(part.get()));
        out_->push_back(std::move(p));
        break;
      }
    }
  }

  // --- desugar de `switch` (spec M1): if-chain; subject avaliado uma vez;
  // sem fall-through. break/continue dos cases são suprimidos (o chain já os
  // implementa) — a menos que haja um loop intermediário no case.
  void lowerSwitch(SwitchStmt* st) {
    auto blk = std::make_unique<HirBlock>();
    auto* prev = out_;
    out_ = &blk->stmts;

    std::string subj = newTemp();
    out_->push_back(hVarDecl(subj, st->subject->exprType, lowerExpr(st->subject.get())));

    switchStack_.push_back(loopDepth_);

    std::unique_ptr<HirStmt> chain;  // else-chain construído de trás para frente
    for (int i = (int)st->cases.size() - 1; i >= 0; i--) {
      auto ifs = std::make_unique<HirIf>();
      ifs->cond = hBin(BinOp::Eq, hVar(subj, st->subject->exprType),
                       hInt(st->cases[i].value, st->subject->exprType),
                       Type::makeBool());
      ifs->thenBranch = lowerBranchList(st->cases[i].body);
      ifs->elseBranch = std::move(chain);
      chain = std::move(ifs);
    }
    if (chain) {
      HirStmt* last = chain.get();
      while (last->kind == HirStmtKind::If &&
             static_cast<HirIf*>(last)->elseBranch &&
             static_cast<HirIf*>(last)->elseBranch->kind == HirStmtKind::If)
        last = static_cast<HirIf*>(last)->elseBranch.get();
      static_cast<HirIf*>(last)->elseBranch = lowerBranchList(st->defaultBody);
      out_->push_back(std::move(chain));
    } else if (!st->defaultBody.empty()) {
      out_->push_back(lowerBranchList(st->defaultBody));
    }

    switchStack_.pop_back();
    out_ = prev;
    out_->push_back(std::move(blk));
  }

  std::unique_ptr<HirBlock> lowerBranchList(const std::vector<std::unique_ptr<Stmt>>& stmts) {
    auto blk = std::make_unique<HirBlock>();
    auto* prev = out_;
    out_ = &blk->stmts;
    for (auto& s : stmts) lowerStmt(s.get());
    out_ = prev;
    return blk;
  }

  // --- desugar de `foreach` (spec): for + índice. List: tamanho via runtime;
  // array: tamanho estático. O sujeito é avaliado uma vez.
  void lowerForeach(ForeachStmt* st) {
    if (st->parallel) {
      auto pf = std::make_unique<HirParallelForeach>();
      pf->itemName = st->itemName;
      pf->itemType = st->itemType;
      pf->batchSize = st->batchSize;
      pf->collection = lowerExpr(st->collection.get());
      loopDepth_++;
      pf->body = lowerBranch(st->body.get());
      loopDepth_--;
      out_->push_back(std::move(pf));
      return;
    }
    bool isList = st->collection->exprType.kind == Type::Kind::List;
    std::string base = newTemp();
    std::string len = isList ? newTemp() : "";
    std::string idx = newTemp();

    auto blk = std::make_unique<HirBlock>();
    auto* prev = out_;
    out_ = &blk->stmts;
    auto baseDecl = hVarDecl(base, st->collection->exprType, lowerExpr(st->collection.get()));
    out_->push_back(std::move(baseDecl));
    if (isList) {
      std::vector<std::unique_ptr<HirExpr>> args;
      args.push_back(hVar(base, st->collection->exprType));
      out_->push_back(hVarDecl(len, Type::makeInt(64),
                               hRuntimeCall("hphl_list_len", std::move(args),
                                            Type::makeInt(64))));
    }
    out_->push_back(hVarDecl(idx, Type::makeInt(64), hInt(0, Type::makeInt(64))));

    auto f = std::make_unique<HirFor>();
    std::unique_ptr<HirExpr> condR = isList ? hVar(len, Type::makeInt(64))
                                            : hInt(st->collection->exprType.arraySize,
                                                   Type::makeInt(64));
    f->cond = hBin(BinOp::Lt, hVar(idx, Type::makeInt(64)), std::move(condR),
                   Type::makeBool());
    auto step = hAssign(hVar(idx, Type::makeInt(64)), hInt(1, Type::makeInt(64)),
                        Type::makeInt(64));
    step->op = AssignOp::Add;
    f->step = std::move(step);

    auto bodyBlk = std::make_unique<HirBlock>();
    {
      auto* bprev = out_;
      out_ = &bodyBlk->stmts;
      std::unique_ptr<HirExpr> elemObj;
      if (isList) {
        std::vector<std::unique_ptr<HirExpr>> args;
        args.push_back(hVar(base, st->collection->exprType));
        elemObj = hRuntimeCall("hphl_list_data", std::move(args), Type::makePtr());
      } else {
        elemObj = hVar(base, st->collection->exprType);
      }
      auto ix = std::make_unique<HirIndex>();
      ix->object = std::move(elemObj);
      ix->index = hVar(idx, Type::makeInt(64));
      ix->type = st->itemType;
      ix->isListBuffer = isList;
      auto itemDecl = hVarDecl(st->itemName, st->itemType, std::move(ix));
      out_->push_back(std::move(itemDecl));
      loopDepth_++;
      lowerStmt(st->body.get());
      loopDepth_--;
      out_ = bprev;
    }
    f->body = std::move(bodyBlk);
    out_->push_back(std::move(f));
    out_ = prev;
    out_->push_back(std::move(blk));
  }

  // --- desugar de `match` (spec §13): if-chain com flag `done`.
  // O sujeito é avaliado uma vez; cada braço testa o padrão, liga as binds,
  // avalia a guarda e executa o corpo. `done` garante que só o primeiro braço
  // que casar (padrão + guarda) executa; o final faz panic se nada casou.
  std::unique_ptr<HirExpr> lowerMatchExpr(MatchExpr* m) {
    bool isExpr = !m->arms.empty() && m->arms[0].yield != nullptr;
    Type subjType = m->subject->exprType;
    std::string sbj = newTemp();
    std::string done = newTemp();
    std::string val;
    Type valType;
    if (isExpr) {
      valType = m->arms[0].yieldType;
      val = newTemp();
    }

    auto blk = std::make_unique<HirBlock>();
    blk->scope = false;  // temps (sbj/done/val) visíveis no escopo do chamador
    auto* prev = out_;
    out_ = &blk->stmts;
    out_->push_back(hVarDecl(sbj, subjType, lowerExpr(m->subject.get())));
    out_->push_back(hVarDecl(done, Type::makeBool(), hBool(false, Type::makeBool())));
    if (isExpr) out_->push_back(hVarDecl(val, valType, hDefaultValue(valType)));

    for (auto& arm : m->arms) {
      // corpo do braço (ou atribuição do yield) + done = true
      std::vector<std::unique_ptr<HirStmt>> bodyFlow;
      {
        auto* bprev = out_;
        out_ = &bodyFlow;
        if (arm.yield) {
          bodyFlow.push_back(hExpr(hAssign(hVar(val, valType), lowerExpr(arm.yield.get()),
                                           valType)));
        } else {
          for (auto& bs : arm.body) lowerStmt(bs.get());
        }
        out_ = bprev;
      }
      bodyFlow.push_back(hExpr(hAssign(hVar(done, Type::makeBool()), hBool(true, Type::makeBool()),
                                       Type::makeBool())));

      // guarda: `when <expr>` — sugar do guard fica no fluxo do braço
      std::vector<std::unique_ptr<HirStmt>> armFlow;
      if (arm.guard) {
        auto* gprev = out_;
        out_ = &armFlow;
        auto cond = lowerExpr(arm.guard.get());
        out_ = gprev;
        auto gi = std::make_unique<HirIf>();
        gi->cond = std::move(cond);
        gi->thenBranch = blkOf(std::move(bodyFlow));
        armFlow.push_back(std::move(gi));
      } else {
        armFlow = std::move(bodyFlow);
      }

      // padrão: envolve o fluxo com os testes
      std::unique_ptr<HirStmt> wrapped =
          wrapPattern(arm.pattern.get(), subjType, hVar(sbj, subjType), blkOf(std::move(armFlow)));

      // `nome @ padrão`: liga o sujeito antes dos testes
      std::vector<std::unique_ptr<HirStmt>> outer;
      if (arm.hasSubjectBind && arm.subjectBind != "_") {
        outer.push_back(hVarDecl(arm.subjectBind, arm.subjectBindType, hVar(sbj, subjType)));
      }
      outer.push_back(std::move(wrapped));

      // if (!done) { ...braço... }
      auto armIf = std::make_unique<HirIf>();
      armIf->cond = hNot(hVar(done, Type::makeBool()), Type::makeBool());
      armIf->thenBranch = blkOf(std::move(outer));
      blk->stmts.push_back(std::move(armIf));
    }

    // exaustivo; panic somente se todas as guardas falharam
    auto fail = std::make_unique<HirIf>();
    fail->cond = hNot(hVar(done, Type::makeBool()), Type::makeBool());
    auto failBlk = std::make_unique<HirBlock>();
    failBlk->stmts.push_back(hExpr(hRuntimeCall("hphl_match_fail", {}, Type::makeVoid())));
    fail->thenBranch = std::move(failBlk);
    blk->stmts.push_back(std::move(fail));

    out_ = prev;
    out_->push_back(std::move(blk));
    return isExpr ? hVar(val, valType) : nullptr;
  }

  // Testa `p` sobre `subject`; se casar, executa `inner` (que contém binds e o
  // corpo do braço). Devolve o fluxo estruturado (if's aninhados).
  std::unique_ptr<HirStmt> wrapPattern(Pattern* p, const Type& t,
                                       std::unique_ptr<HirExpr> subject,
                                       std::unique_ptr<HirBlock> inner) {
    switch (p->kind) {
      case Pattern::K::Wildcard:
        return inner;
      case Pattern::K::Const: {
        bool cell = (t.kind == Type::Kind::Enum && sem_.isRichEnum(t.name)) ||
                    t.isOption() || t.isResult();
        auto i = std::make_unique<HirIf>();
        if (cell) {
          auto tg = std::make_unique<HirCellTag>();
          tg->type = Type::makeInt(64);
          tg->subject = std::move(subject);
          i->cond = hBin(BinOp::Eq, std::move(tg), hInt(p->constValue, Type::makeInt(64)),
                         Type::makeBool());
        } else {
          i->cond = hBin(BinOp::Eq, std::move(subject), hInt(p->constValue, t),
                         Type::makeBool());
        }
        i->thenBranch = std::move(inner);
        return i;
      }
      case Pattern::K::StrConst: {
        // match sobre string: compara conte�do (hphl_str_eq), como `==`
        auto i = std::make_unique<HirIf>();
        auto lit = std::make_unique<HirStringLit>();
        lit->value = p->strValue;
        lit->type = Type::makeString();
        auto cmp = hBin(BinOp::Eq, std::move(subject), std::move(lit),
                        Type::makeBool());
        cmp->strEqBin = true;
        i->cond = std::move(cmp);
        i->thenBranch = std::move(inner);
        return i;
      }
      case Pattern::K::Range: {
        auto i = std::make_unique<HirIf>();
        auto lo = hBin(BinOp::Ge, cloneSubject(*subject), hInt(p->rangeLo, t),
                       Type::makeBool());
        auto hi = hBin(BinOp::Le, std::move(subject), hInt(p->rangeHi, t),
                       Type::makeBool());
        i->cond = hBin(BinOp::And, std::move(lo), std::move(hi), Type::makeBool());
        i->thenBranch = std::move(inner);
        return i;
      }
      case Pattern::K::Bind: {
        std::vector<std::unique_ptr<HirStmt>> stmts;
        stmts.push_back(hVarDecl(p->bindName, p->bindType, std::move(subject)));
        for (auto& s : inner->stmts) stmts.push_back(std::move(s));
        return blkOf(std::move(stmts));
      }
      case Pattern::K::Variant: {
        auto i = std::make_unique<HirIf>();
        auto tg = std::make_unique<HirCellTag>();
        tg->type = Type::makeInt(64);
        tg->subject = cloneSubject(*subject);
        i->cond = hBin(BinOp::Eq, std::move(tg), hInt(p->constValue, Type::makeInt(64)),
                       Type::makeBool());
        std::unique_ptr<HirBlock> flow = std::move(inner);
        for (int k = (int)p->subs.size() - 1; k >= 0; k--) {
          Type subType;
          if (t.isOption()) {
            subType = *t.elem;
          } else if (t.isResult()) {
            // payload do variante (Ok=0 → ok, Err=1 → err)
            subType = p->constValue == 0 ? *t.elem : *t.elem2;
          } else {
            subType = sem_.enums().at(t.name)->entries[p->constValue].params[k].type;
          }
          auto la = std::make_unique<HirLoadAt>();
          la->subject = cloneSubject(*subject);
          la->offset = 8LL * (k + 1);  // payload i da célula em +8*(i+1)
          la->type = subType;
          flow = blkOfWrap(
              wrapPattern(p->subs[k].get(), subType, std::move(la), std::move(flow)));
        }
        i->thenBranch = std::move(flow);
        return i;
      }
      case Pattern::K::Struct: {
        std::unique_ptr<HirBlock> flow = std::move(inner);
        const auto& ci = sem_.classes().at(t.name);
        for (int k = (int)p->subs.size() - 1; k >= 0; k--) {
          Type ft = ci.fields.at(p->subNames[k]).first;
          long long off = hirFieldOffset(sem_, t.name, p->subNames[k]);
          auto la = std::make_unique<HirLoadAt>();
          la->subject = cloneSubject(*subject);
          la->offset = off;
          la->type = ft;
          flow = blkOfWrap(
              wrapPattern(p->subs[k].get(), ft, std::move(la), std::move(flow)));
        }
        return flow;
      }
      case Pattern::K::List: {
        if (t.kind == Type::Kind::Array) {
          // array: sem checagem de tamanho; rest ignorado (espelha o codegen)
          int stride = typeSize(*t.elem);
          std::unique_ptr<HirBlock> flow = std::move(inner);
          for (int k = (int)p->subs.size() - 1; k >= 0; k--) {
            auto la = std::make_unique<HirLoadAt>();
            la->subject = cloneSubject(*subject);
            la->offset = (long long)stride * k;
            la->type = *t.elem;
            flow = blkOfWrap(
                wrapPattern(p->subs[k].get(), *t.elem, std::move(la), std::move(flow)));
          }
          return flow;
        }
        // list: len (runtime) + data (runtime) + subs no buffer
        int need = (int)p->subs.size();
        std::string dataV = newTemp();
        std::unique_ptr<HirExpr> lenCond;
        std::vector<std::unique_ptr<HirExpr>> lenArgs;
        lenArgs.push_back(cloneSubject(*subject));
        if (p->hasRest && need == 0) {
          // sem checagem de tamanho (espelha o codegen)
        } else if (p->hasRest) {
          lenCond = hBin(BinOp::Ge, hRuntimeCall("hphl_list_len", std::move(lenArgs),
                                                 Type::makeInt(64)),
                         hInt(need, Type::makeInt(64)), Type::makeBool());
        } else {
          lenCond = hBin(BinOp::Eq, hRuntimeCall("hphl_list_len", std::move(lenArgs),
                                                 Type::makeInt(64)),
                         hInt(need, Type::makeInt(64)), Type::makeBool());
        }
        int stride = typeSize(*t.elem);
        // rest: slice após todos os subs casarem
        if (p->hasRest) {
          std::vector<std::unique_ptr<HirExpr>> sargs;
          sargs.push_back(cloneSubject(*subject));
          sargs.push_back(hInt(need, Type::makeInt(64)));
          std::vector<std::unique_ptr<HirExpr>> lenArgs2;
          lenArgs2.push_back(cloneSubject(*subject));
          // count = len - need (espelha o codegen AST: subq $need, %r8)
          sargs.push_back(hBin(BinOp::Sub,
                               hRuntimeCall("hphl_list_len", std::move(lenArgs2),
                                            Type::makeInt(64)),
                               hInt(need, Type::makeInt(64)), Type::makeInt(64)));
          sargs.push_back(hInt(stride, Type::makeInt(64)));
          auto rl = hVarDecl(p->restName, p->restType,
                             hRuntimeCall("hphl_list_slice", std::move(sargs),
                                          p->restType));
          std::vector<std::unique_ptr<HirStmt>> stmts;
          stmts.push_back(std::move(rl));
          for (auto& s : inner->stmts) stmts.push_back(std::move(s));
          inner->stmts = std::move(stmts);
        }
        std::unique_ptr<HirBlock> flow = std::move(inner);
        for (int k = (int)p->subs.size() - 1; k >= 0; k--) {
          auto la = std::make_unique<HirLoadAt>();
          la->subject = hVar(dataV, Type::makePtr());
          la->offset = (long long)stride * k;
          la->type = *t.elem;
          flow = blkOfWrap(
              wrapPattern(p->subs[k].get(), *t.elem, std::move(la), std::move(flow)));
        }
        auto outerBlk = std::make_unique<HirBlock>();
        std::vector<std::unique_ptr<HirExpr>> dataArgs;
        dataArgs.push_back(cloneSubject(*subject));
        outerBlk->stmts.push_back(hVarDecl(dataV, Type::makePtr(),
                                           hRuntimeCall("hphl_list_data", std::move(dataArgs),
                                                        Type::makePtr())));
        if (lenCond) {
          auto li = std::make_unique<HirIf>();
          li->cond = std::move(lenCond);
          li->thenBranch = std::move(flow);
          outerBlk->stmts.push_back(std::move(li));
        } else {
          for (auto& s : flow->stmts) outerBlk->stmts.push_back(std::move(s));
        }
        return outerBlk;
      }
    }
    return inner;
  }

  // --- desugar de `e?.campo` / `e?.M(args)` (spec §12) ---
  std::unique_ptr<HirExpr> lowerOptMember(OptMemberExpr* om) {
    Type ft = om->exprType;
    bool refResult = ft.kind == Type::Kind::Class || ft.kind == Type::Kind::String;
    std::string obj = newTemp();
    std::string val = newTemp();

    auto blk = std::make_unique<HirBlock>();
    blk->scope = false;  // temps (obj/val) visíveis no escopo do chamador
    auto* prev = out_;
    out_ = &blk->stmts;
    out_->push_back(hVarDecl(obj, om->object->exprType, lowerExpr(om->object.get())));
    out_->push_back(hVarDecl(val, ft, hDefaultValue(ft)));

    auto i = std::make_unique<HirIf>();
    i->cond = hBin(BinOp::Ne, hVar(obj, om->object->exprType),
                   hNull(om->object->exprType), Type::makeBool());
    auto thenBlk = std::make_unique<HirBlock>();
    {
      auto* tprev = out_;
      out_ = &thenBlk->stmts;
      auto m = std::make_unique<HirMember>();
      m->object = hVar(obj, om->object->exprType);
      m->member = om->member;
      m->fieldOffset = om->fieldOffset;
      m->type = ft;
      if (refResult) {
        out_->push_back(hExpr(hAssign(hVar(val, ft), std::move(m), ft)));
      } else {
        out_->push_back(hExpr(hAssign(hVar(val, ft), hOptCtor("Some", std::move(m), ft), ft)));
      }
      out_ = tprev;
    }
    auto elseBlk = std::make_unique<HirBlock>();
    {
      auto* eprev = out_;
      out_ = &elseBlk->stmts;
      if (refResult) {
        out_->push_back(hExpr(hAssign(hVar(val, ft), hNull(ft), ft)));
      } else {
        out_->push_back(hExpr(hAssign(hVar(val, ft), hOptCtor("None", nullptr, ft), ft)));
      }
      out_ = eprev;
    }
    i->thenBranch = std::move(thenBlk);
    i->elseBranch = std::move(elseBlk);
    blk->stmts.push_back(std::move(i));

    out_ = prev;
    out_->push_back(std::move(blk));
    return hVar(val, ft);
  }

  // --- desugar de `e?[i]` (Item 2.16) ---
  std::unique_ptr<HirExpr> lowerOptIndex(OptIndexExpr* oi) {
    Type ot = oi->object->exprType;
    Type resT = oi->exprType;
    bool refResult = (resT.kind == Type::Kind::Class || resT.kind == Type::Kind::String);
    std::string obj = newTemp();
    std::string idx = newTemp();
    std::string val = newTemp();

    auto blk = std::make_unique<HirBlock>();
    blk->scope = false;  // temps visíveis no escopo do chamador
    auto* prev = out_;
    out_ = &blk->stmts;
    out_->push_back(hVarDecl(obj, ot, lowerExpr(oi->object.get())));
    out_->push_back(hVarDecl(idx, Type::makeInt(64), lowerExpr(oi->index.get())));
    out_->push_back(hVarDecl(val, resT, hDefaultValue(resT)));

    if (ot.kind == Type::Kind::Array) {
      // Array tem tamanho fixo em ot.arraySize
      auto ge0 = hBin(BinOp::Ge, hVar(idx, Type::makeInt(64)), hInt(0, Type::makeInt(64)), Type::makeBool());
      auto ltLen = hBin(BinOp::Lt, hVar(idx, Type::makeInt(64)), hInt(ot.arraySize, Type::makeInt(64)), Type::makeBool());
      auto inBounds = hBin(BinOp::And, std::move(ge0), std::move(ltLen), Type::makeBool());

      auto i = std::make_unique<HirIf>();
      i->cond = std::move(inBounds);

      auto thenBlk = std::make_unique<HirBlock>();
      {
        auto* tprev = out_;
        out_ = &thenBlk->stmts;
        auto ix = std::make_unique<HirIndex>();
        ix->object = hVar(obj, ot);
        ix->index = hVar(idx, Type::makeInt(64));
        ix->type = *ot.elem;
        if (refResult) {
          out_->push_back(hExpr(hAssign(hVar(val, resT), std::move(ix), resT)));
        } else {
          out_->push_back(hExpr(hAssign(hVar(val, resT), hOptCtor("Some", std::move(ix), resT), resT)));
        }
        out_ = tprev;
      }
      auto elseBlk = std::make_unique<HirBlock>();
      {
        auto* eprev = out_;
        out_ = &elseBlk->stmts;
        if (refResult) {
          out_->push_back(hExpr(hAssign(hVar(val, resT), hNull(resT), resT)));
        } else {
          out_->push_back(hExpr(hAssign(hVar(val, resT), hOptCtor("None", nullptr, resT), resT)));
        }
        out_ = eprev;
      }
      i->thenBranch = std::move(thenBlk);
      i->elseBranch = std::move(elseBlk);
      blk->stmts.push_back(std::move(i));
    } else {
      // List ou String: pode ser null e tem comprimento dinâmico
      auto iObj = std::make_unique<HirIf>();
      iObj->cond = hBin(BinOp::Ne, hVar(obj, ot), hNull(ot), Type::makeBool());

      auto thenObjBlk = std::make_unique<HirBlock>();
      {
        auto* tprev = out_;
        out_ = &thenObjBlk->stmts;

        // chama função de comprimento: hphl_list_len ou hphl_str_len
        std::vector<std::unique_ptr<HirExpr>> lenArgs;
        lenArgs.push_back(hVar(obj, ot));
        std::string lenFn = (ot.kind == Type::Kind::String) ? "hphl_str_len" : "hphl_list_len";
        auto callLen = hRuntimeCall(lenFn, std::move(lenArgs), Type::makeInt(64));

        auto ge0 = hBin(BinOp::Ge, hVar(idx, Type::makeInt(64)), hInt(0, Type::makeInt(64)), Type::makeBool());
        auto ltLen = hBin(BinOp::Lt, hVar(idx, Type::makeInt(64)), std::move(callLen), Type::makeBool());
        auto inBounds = hBin(BinOp::And, std::move(ge0), std::move(ltLen), Type::makeBool());

        auto iBounds = std::make_unique<HirIf>();
        iBounds->cond = std::move(inBounds);

        auto thenBoundsBlk = std::make_unique<HirBlock>();
        {
          auto* bprev = out_;
          out_ = &thenBoundsBlk->stmts;

          if (ot.kind == Type::Kind::String) {
            std::vector<std::unique_ptr<HirExpr>> charArgs;
            charArgs.push_back(hVar(obj, ot));
            charArgs.push_back(hVar(idx, Type::makeInt(64)));
            auto charCall = hRuntimeCall("hphl_str_char_index", std::move(charArgs), Type::makeChar());
            out_->push_back(hExpr(hAssign(hVar(val, resT), hOptCtor("Some", std::move(charCall), resT), resT)));
          } else {
            auto ix = std::make_unique<HirIndex>();
            ix->object = hVar(obj, ot);
            ix->index = hVar(idx, Type::makeInt(64));
            ix->type = *ot.elem;
            if (refResult) {
              out_->push_back(hExpr(hAssign(hVar(val, resT), std::move(ix), resT)));
            } else {
              out_->push_back(hExpr(hAssign(hVar(val, resT), hOptCtor("Some", std::move(ix), resT), resT)));
            }
          }
          out_ = bprev;
        }

        auto elseBoundsBlk = std::make_unique<HirBlock>();
        {
          auto* bprev = out_;
          out_ = &elseBoundsBlk->stmts;
          if (refResult) {
            out_->push_back(hExpr(hAssign(hVar(val, resT), hNull(resT), resT)));
          } else {
            out_->push_back(hExpr(hAssign(hVar(val, resT), hOptCtor("None", nullptr, resT), resT)));
          }
          out_ = bprev;
        }

        iBounds->thenBranch = std::move(thenBoundsBlk);
        iBounds->elseBranch = std::move(elseBoundsBlk);
        out_->push_back(std::move(iBounds));

        out_ = tprev;
      }

      auto elseObjBlk = std::make_unique<HirBlock>();
      {
        auto* eprev = out_;
        out_ = &elseObjBlk->stmts;
        if (refResult) {
          out_->push_back(hExpr(hAssign(hVar(val, resT), hNull(resT), resT)));
        } else {
          out_->push_back(hExpr(hAssign(hVar(val, resT), hOptCtor("None", nullptr, resT), resT)));
        }
        out_ = eprev;
      }

      iObj->thenBranch = std::move(thenObjBlk);
      iObj->elseBranch = std::move(elseObjBlk);
      blk->stmts.push_back(std::move(iObj));
    }

    out_ = prev;
    out_->push_back(std::move(blk));
    return hVar(val, resT);
  }

  // `e?.M(args)` — chamada condicional (método void → skip)
  std::unique_ptr<HirExpr> lowerOptCall(CallExpr* c) {
    auto om = static_cast<OptMemberExpr*>(c->callee.get());
    FunctionDecl* fn = om->resolved;
    Type rt = c->exprType;
    bool refResult = rt.kind == Type::Kind::Class || rt.kind == Type::Kind::String;
    bool optResult = rt.isOption();
    bool isVoid = rt.kind == Type::Kind::Void;
    std::string obj = newTemp();
    std::string val;
    if (!isVoid) val = newTemp();

    auto blk = std::make_unique<HirBlock>();
    blk->scope = false;  // temps (obj/val) visíveis no escopo do chamador
    auto* prev = out_;
    out_ = &blk->stmts;
    out_->push_back(hVarDecl(obj, om->object->exprType, lowerExpr(om->object.get())));
    if (!isVoid) out_->push_back(hVarDecl(val, rt, hDefaultValue(rt)));

    auto i = std::make_unique<HirIf>();
    i->cond = hBin(BinOp::Ne, hVar(obj, om->object->exprType),
                   hNull(om->object->exprType), Type::makeBool());
    auto thenBlk = std::make_unique<HirBlock>();
    {
      auto* tprev = out_;
      out_ = &thenBlk->stmts;
      auto call = std::make_unique<HirCall>();
      call->kind = HirCallKind::Normal;
      call->resolved = fn;
      // M10.5: a chamada interna carrega o retorno REAL do método — o tipo
      // Option<T> pertence ao envelope Some; com o tipo errado no nó, o
      // payload era gravado como ptr e o wasm32 quebra (ptr≠i64)
      call->type = optResult ? fn->returnType : rt;
      call->args.push_back(hVar(obj, om->object->exprType));  // this
      for (auto& a : c->args) call->args.push_back(lowerExpr(a.get()));
      if (isVoid) {
        out_->push_back(hExpr(std::move(call)));
      } else if (optResult) {
        out_->push_back(
            hExpr(hAssign(hVar(val, rt), hOptCtor("Some", std::move(call), rt), rt)));
      } else {
        out_->push_back(hExpr(hAssign(hVar(val, rt), std::move(call), rt)));
      }
      out_ = tprev;
    }
    auto elseBlk = std::make_unique<HirBlock>();
    {
      auto* eprev = out_;
      out_ = &elseBlk->stmts;
      if (refResult) {
        out_->push_back(hExpr(hAssign(hVar(val, rt), hNull(rt), rt)));
      } else if (optResult) {
        out_->push_back(hExpr(hAssign(hVar(val, rt), hOptCtor("None", nullptr, rt), rt)));
      }
      out_ = eprev;
    }
    i->thenBranch = std::move(thenBlk);
    i->elseBranch = std::move(elseBlk);
    blk->stmts.push_back(std::move(i));

    out_ = prev;
    out_->push_back(std::move(blk));
    return isVoid ? std::make_unique<HirNullLit>() : hVar(val, rt);
  }

  // --- desugar de `a ?? b` (spec §12): if + temp ---
  std::unique_ptr<HirExpr> lowerCoalesce(CoalesceExpr* c) {
    Type lt = c->lhs->exprType;
    // Bug 1.9: referencia nulável (classe/string/etc) — `tail ?? firstNo`.
    bool nullableRef = lt.kind == Type::Kind::Class || lt.kind == Type::Kind::String ||
                       lt.kind == Type::Kind::List || lt.kind == Type::Kind::Map ||
                       lt.kind == Type::Kind::Task || lt.kind == Type::Kind::Channel;
    if (nullableRef) {
      Type vt = lt;
      std::string cell = newTemp();
      std::string val = newTemp();
      auto blk = std::make_unique<HirBlock>();
      blk->scope = false;
      auto* prev = out_;
      out_ = &blk->stmts;
      out_->push_back(hVarDecl(cell, lt, lowerExpr(c->lhs.get())));
      out_->push_back(hVarDecl(val, vt, hDefaultValue(vt)));
      auto i = std::make_unique<HirIf>();
      // `lhs == null ? rhs : lhs`
      i->cond = hBin(BinOp::Eq, hVar(cell, lt), hNull(lt), Type::makeBool());
      auto thenBlk = std::make_unique<HirBlock>();
      {
        auto* tprev = out_;
        out_ = &thenBlk->stmts;
        out_->push_back(hExpr(hAssign(hVar(val, vt), lowerExpr(c->rhs.get()), vt)));
        out_ = tprev;
      }
      auto elseBlk = std::make_unique<HirBlock>();
      {
        auto* eprev = out_;
        out_ = &elseBlk->stmts;
        out_->push_back(hExpr(hAssign(hVar(val, vt), hVar(cell, lt), vt)));
        out_ = eprev;
      }
      i->thenBranch = std::move(thenBlk);
      i->elseBranch = std::move(elseBlk);
      blk->stmts.push_back(std::move(i));
      out_ = prev;
      out_->push_back(std::move(blk));
      return hVar(val, vt);
    }
    Type vt = *lt.elem;
    std::string cell = newTemp();
    std::string val = newTemp();

    auto blk = std::make_unique<HirBlock>();
    blk->scope = false;  // temps (cell/val) visíveis no escopo do chamador
    auto* prev = out_;
    out_ = &blk->stmts;
    out_->push_back(hVarDecl(cell, lt, lowerExpr(c->lhs.get())));
    out_->push_back(hVarDecl(val, vt, hDefaultValue(vt)));

    auto i = std::make_unique<HirIf>();
    auto tg = std::make_unique<HirCellTag>();
    tg->type = Type::makeInt(64);
    tg->subject = hVar(cell, lt);
    long long badTag = lt.isOption() ? 0 : 1; // None=0, Err=1
    i->cond = hBin(BinOp::Eq, std::move(tg), hInt(badTag, Type::makeInt(64)),
                   Type::makeBool());
    auto thenBlk = std::make_unique<HirBlock>();
    {
      auto* tprev = out_;
      out_ = &thenBlk->stmts;
      out_->push_back(hExpr(hAssign(hVar(val, vt), lowerExpr(c->rhs.get()), vt)));
      out_ = tprev;
    }
    auto elseBlk = std::make_unique<HirBlock>();
    {
      auto* eprev = out_;
      out_ = &elseBlk->stmts;
      auto la = std::make_unique<HirLoadAt>();
      la->subject = hVar(cell, lt);
      la->offset = 8;
      la->type = vt;
      out_->push_back(hExpr(hAssign(hVar(val, vt), std::move(la), vt)));
      out_ = eprev;
    }
    i->thenBranch = std::move(thenBlk);
    i->elseBranch = std::move(elseBlk);
    blk->stmts.push_back(std::move(i));

    out_ = prev;
    out_->push_back(std::move(blk));
    return hVar(val, vt);
  }

  // --- desugar de `c ? a : b` (spec): if + temp ---
  std::unique_ptr<HirExpr> lowerTernary(TernaryExpr* t) {
    Type vt = t->exprType;
    std::string val = newTemp();

    auto blk = std::make_unique<HirBlock>();
    blk->scope = false;  // temp (val) visível no escopo do chamador
    auto* prev = out_;
    out_ = &blk->stmts;
    out_->push_back(hVarDecl(val, vt, hDefaultValue(vt)));

    auto i = std::make_unique<HirIf>();
    i->cond = lowerExpr(t->cond.get());
    auto thenBlk = std::make_unique<HirBlock>();
    {
      auto* tprev = out_;
      out_ = &thenBlk->stmts;
      out_->push_back(hExpr(hAssign(hVar(val, vt), lowerExpr(t->thenExpr.get()), vt)));
      out_ = tprev;
    }
    auto elseBlk = std::make_unique<HirBlock>();
    {
      auto* eprev = out_;
      out_ = &elseBlk->stmts;
      out_->push_back(hExpr(hAssign(hVar(val, vt), lowerExpr(t->elseExpr.get()), vt)));
      out_ = eprev;
    }
    i->thenBranch = std::move(thenBlk);
    i->elseBranch = std::move(elseBlk);
    blk->stmts.push_back(std::move(i));

    out_ = prev;
    out_->push_back(std::move(blk));
    return hVar(val, vt);
  }

  // --- desugar de `x?` (spec §12): tag ruim → return propagado ---
  std::unique_ptr<HirExpr> lowerTry(TryExpr* t) {
    Type ot = t->operand->exprType;
    bool isOpt = ot.isOption();
    long long badTag = isOpt ? 0 : 1;  // None / Err
    Type vt = t->exprType;
    bool hasVal = vt.kind != Type::Kind::Void;
    std::string cell = newTemp();
    std::string val;
    if (hasVal) val = newTemp();

    auto blk = std::make_unique<HirBlock>();
    blk->scope = false;  // temps (cell/val) visíveis no escopo do chamador
    auto* prev = out_;
    out_ = &blk->stmts;
    out_->push_back(hVarDecl(cell, ot, lowerExpr(t->operand.get())));
    if (hasVal) out_->push_back(hVarDecl(val, vt, hDefaultValue(vt)));

    auto i = std::make_unique<HirIf>();
    auto tg = std::make_unique<HirCellTag>();
    tg->type = Type::makeInt(64);
    tg->subject = hVar(cell, ot);
    i->cond = hBin(BinOp::Eq, std::move(tg), hInt(badTag, Type::makeInt(64)),
                   Type::makeBool());

    // propagação: Option → None do retorno; Result → a própria célula Err
    // (layout compatível: tag em 0, ok em +8, err em +16)
    auto ret = std::make_unique<HirReturn>();
    if (isOpt) {
      ret->value = hOptCtor("None", nullptr, curFn_->hasReturnType ? curFn_->returnType
                                                                   : Type::makeVoid());
    } else {
      ret->value = hVar(cell, ot);
    }
    auto thenBlk = std::make_unique<HirBlock>();
    thenBlk->stmts.push_back(std::move(ret));
    i->thenBranch = std::move(thenBlk);

    auto elseBlk = std::make_unique<HirBlock>();
    {
      auto* eprev = out_;
      out_ = &elseBlk->stmts;
      if (hasVal) {
        auto la = std::make_unique<HirLoadAt>();
        la->subject = hVar(cell, ot);
        la->offset = 8;
        la->type = vt;
        out_->push_back(hExpr(hAssign(hVar(val, vt), std::move(la), vt)));
      }
      out_ = eprev;
    }
    i->elseBranch = std::move(elseBlk);
    blk->stmts.push_back(std::move(i));

    out_ = prev;
    out_->push_back(std::move(blk));
    return hasVal ? hVar(val, vt) : std::make_unique<HirNullLit>();
  }

  // --- expressões ---
  std::unique_ptr<HirExpr> lowerCall(CallExpr* c) {
    // `e?.M(args)` — chamada condicional
    if (c->callee->kind == ExprKind::OptMember) return lowerOptCall(c);
    // v0.95 (lambdas): `f(args)` com f valor `func` — call indireto
    if (c->isFuncCall) {
      auto call = std::make_unique<HirCall>();
      call->kind = HirCallKind::Indirect;
      call->funcValue = lowerExpr(c->callee.get());
      call->type = c->exprType;
      for (auto& a : c->args) call->args.push_back(lowerExpr(a.get()));
      return call;
    }

    auto call = std::make_unique<HirCall>();
    call->resolved = c->resolved;
    call->type = c->exprType;
    call->folded = c->folded;       // v0.46: compiletime dobrada na semântica
    call->foldValue = c->foldValue;
    call->isResultIsOk = c->isResultIsOk;
    call->isResultIsErr = c->isResultIsErr;
    call->isResultUnwrap = c->isResultUnwrap;
    call->isResultUnwrapErr = c->isResultUnwrapErr;
    call->isResultUnwrapOr = c->isResultUnwrapOr;
    call->isOptionIsSome = c->isOptionIsSome;
    call->isOptionIsNone = c->isOptionIsNone;
    call->isOptionUnwrap = c->isOptionUnwrap;
    call->isOptionUnwrapOr = c->isOptionUnwrapOr;

    // receiver (this/objeto) como args[0] — espelha a semântica do codegen AST
    bool needsReceiver = false;
    if (c->isWait || c->isTaskCancel || c->isChannelSend || c->isChannelReceive ||
        c->isActorCall || c->isSerialize || c->primOp != CallExpr::PrimOp::None ||
        c->isListAdd || c->isMapPut || c->isMapGet || c->isMapContains || c->isMapRemove || c->isMapClear ||
        c->isResultIsOk || c->isResultIsErr || c->isResultUnwrap || c->isResultUnwrapErr || c->isResultUnwrapOr ||
        c->isOptionIsSome || c->isOptionIsNone || c->isOptionUnwrap || c->isOptionUnwrapOr)
      needsReceiver = true;
    else if (c->resolved && c->resolved->isMethod && !c->resolved->isStatic)
      needsReceiver = true;
    else if (c->isBaseCall)
      needsReceiver = true;
    std::unique_ptr<HirExpr> receiver;
    if (needsReceiver && c->callee->kind == ExprKind::Member) {
      auto* me = static_cast<MemberExpr*>(c->callee.get());
      if (me->object) {
        receiver = lowerExpr(me->object.get());
      } else {
        // M10: `base.M(...)` — receiver implícito é `this`
        auto th = std::make_unique<HirThis>();
        th->type = Type::makeClass(curFn_ ? curFn_->ownerClass : "");
        receiver = std::move(th);
      }
    }

    size_t userArgc = c->args.size();
    for (auto& a : c->args) call->args.push_back(lowerExpr(a.get()));

    // completa parâmetros opcionais (spec §5) — defaults são literais; o tipo
    // canônico do parâmetro é aplicado (semantic não seta exprType nos defaults)
    if (call->kind == HirCallKind::Normal || call->kind == HirCallKind::ModuleCall ||
        call->kind == HirCallKind::Async) {
      auto fn = c->resolved;
      if (fn && userArgc < fn->params.size()) {
        for (size_t i = userArgc; i < fn->params.size(); i++) {
          auto dv = fn->params[i]->defaultVal.get();
          auto lowered = dv ? lowerExpr(dv) : hDefaultValue(fn->params[i]->type);
          lowered->type = fn->params[i]->type;
          call->args.push_back(std::move(lowered));
        }
      }
    }

    if (needsReceiver) call->args.insert(call->args.begin(), std::move(receiver));

    if (c->isPrint) {
      call->kind = HirCallKind::Print;
    } else if (c->isClockNs) {
      call->kind = HirCallKind::ClockNs;
    } else if (c->isSqrt) {
      call->kind = HirCallKind::Sqrt;
    } else if (c->isListNew) {
      // B11: list<T>(n) — construtor com capacidade
      call->kind = HirCallKind::ListNew;
    } else if (c->isArenaReset) {
      call->kind = HirCallKind::ArenaReset;
    } else if (c->stdBuiltin >= 0) {
      // M12.0: builtin da biblioteca padrão → chamada de runtime genérica
      call->kind = HirCallKind::Runtime;
      call->runtimeName = c->stdSymbol;
    } else if (c->isBaseCall) {
      call->kind = HirCallKind::Normal;
      call->isBaseCall = true;
    } else if (c->isListAdd) {
      call->kind = HirCallKind::ListAdd;
    } else if (c->isMapPut) {
      call->kind = HirCallKind::MapPut;
    } else if (c->isMapGet) {
      call->kind = HirCallKind::MapGet;
    } else if (c->isMapContains) {
      call->kind = HirCallKind::MapContains;
    } else if (c->isMapRemove) {
      call->kind = HirCallKind::MapRemove;
    } else if (c->isMapClear) {
      call->kind = HirCallKind::MapClear;
    } else if (c->isWait) {
      call->kind = HirCallKind::Wait;
    } else if (c->isTaskCancel) {
      call->kind = HirCallKind::TaskCancel;
    } else if (c->isActorCall) {
      call->kind = HirCallKind::ActorCall;
    } else if (c->isChannelSend) {
      call->kind = HirCallKind::ChannelSend;
    } else if (c->isChannelReceive) {
      call->kind = HirCallKind::ChannelReceive;
    } else if (c->primOp != CallExpr::PrimOp::None) {
      call->kind = HirCallKind::PrimOp;
      call->primOp = (int)c->primOp;
    } else if (c->isAsyncCall) {
      call->kind = HirCallKind::Async;
    } else if (c->isModuleCall) {
      call->kind = HirCallKind::ModuleCall;
    } else if (c->isEnumCtor) {
      call->kind = HirCallKind::EnumCtor;
      call->enumCanon = c->enumCtorEnum;
      call->index = c->enumCtorIndex;
      auto it = sem_.enums().find(c->enumCtorEnum);
      if (it != sem_.enums().end() && c->enumCtorIndex >= 0 &&
          c->enumCtorIndex < (int)it->second->entries.size())
        call->entryName = it->second->entries[c->enumCtorIndex].name;
    } else if (c->isFromInt) {
      call->kind = HirCallKind::FromInt;
      call->enumCanon = c->fromIntEnum;
    } else if (c->isSerialize) {
      call->kind = HirCallKind::Serialize;
      call->enumCanon = c->serializeEnum;
    } else if (c->isStrEq) {
      call->kind = HirCallKind::StrEq;
    } else if (c->isStrCmp) {
      call->kind = HirCallKind::StrCmp;
    } else if (c->isToStr) {
      call->kind = HirCallKind::ToStr;
    } else if (c->isAddrOf) {
      call->kind = HirCallKind::AddrOf; // FFI v2: endereço do lvalue (ptr)
    } else if (c->isResultIsOk) {
      call->kind = HirCallKind::ResultIsOk;
    } else if (c->isResultIsErr) {
      call->kind = HirCallKind::ResultIsErr;
    } else if (c->isResultUnwrap) {
      call->kind = HirCallKind::ResultUnwrap;
    } else if (c->isResultUnwrapErr) {
      call->kind = HirCallKind::ResultUnwrapErr;
    } else if (c->isResultUnwrapOr) {
      call->kind = HirCallKind::ResultUnwrapOr;
    } else if (c->isOptionIsSome) {
      call->kind = HirCallKind::OptionIsSome;
    } else if (c->isOptionIsNone) {
      call->kind = HirCallKind::OptionIsNone;
    } else if (c->isOptionUnwrap) {
      call->kind = HirCallKind::OptionUnwrap;
    } else if (c->isOptionUnwrapOr) {
      call->kind = HirCallKind::OptionUnwrapOr;
    } else {
      call->kind = HirCallKind::Normal;
    }
    return call;
  }

  std::unique_ptr<HirExpr> lowerExpr(Expr* e) {
    switch (e->kind) {
      case ExprKind::IntLit: {
        auto st = static_cast<IntLitExpr*>(e);
        auto n = std::make_unique<HirIntLit>();
        n->value = st->value;
        n->isUnsigned = st->isUnsigned;
        n->type = st->exprType;
        return n;
      }
      case ExprKind::FloatLit: {
        auto st = static_cast<FloatLitExpr*>(e);
        return hFloat(st->value, st->exprType);
      }
      case ExprKind::StringLit: {
        auto st = static_cast<StringLitExpr*>(e);
        auto s = std::make_unique<HirStringLit>();
        s->value = st->value;
        s->type = st->exprType;
        return s;
      }
      case ExprKind::CharLit: {
        auto st = static_cast<CharLitExpr*>(e);
        auto c = std::make_unique<HirCharLit>();
        c->value = st->value;
        c->type = st->exprType;
        return c;
      }
      case ExprKind::BoolLit:
        return hBool(static_cast<BoolLitExpr*>(e)->value,
                     static_cast<BoolLitExpr*>(e)->exprType);
      case ExprKind::NullLit:
        return hNull(static_cast<NullLitExpr*>(e)->exprType);
      case ExprKind::Ident: {
        auto st = static_cast<IdentExpr*>(e);
        auto v = std::make_unique<HirVar>();
        v->name = st->name;
        v->type = st->exprType;
        v->isGlobal = st->symbol.kind == SymbolKind::GlobalVar;
        v->label = st->symbol.label;
        if (st->symbol.kind == SymbolKind::EnumConst) {
          v->isConst = true;
          v->constValue = st->symbol.constValue;
        }
        v->isProperty = st->isProperty;
        v->propGet = st->propGet;
        v->propSet = st->propSet;
        if (st->symbol.kind == SymbolKind::Field) {
          v->isThisField = true;
          v->thisFieldOffset = st->symbol.slotIndex;
        }
        v->atomic = st->symbol.atomic;
        return v;
      }
      case ExprKind::Member: {
        auto st = static_cast<MemberExpr*>(e);
        auto m = std::make_unique<HirMember>();
        m->object = lowerExpr(st->object.get());
        m->member = st->member;
        m->fieldOffset = st->fieldOffset;
        m->isEnumConst = st->isEnumConst;
        m->enumValue = st->enumValue;
        m->isArrayLength = st->isArrayLength;
        m->arraySize = st->isArrayLength ? st->enumValue : 0;
        m->isListLength = st->isListLength;
        m->isMapLength = st->isMapLength;
        m->isGlobalRef = st->isGlobalRef;
        m->resolvedGlobal = st->resolvedGlobal;
        m->isModuleTypeRef = st->isModuleTypeRef;
        m->isEnumCtor = st->isEnumCtor;
        m->enumCtorEnum = st->enumCtorEnum;
        m->enumCtorIndex = st->enumCtorIndex;
        m->isProperty = st->isProperty;
        m->propType = st->propType;
        m->propGet = st->propGet;
        m->propSet = st->propSet;
        m->fieldAtomic = st->fieldAtomic;
        m->isWait = st->isWait;
        m->isTaskCancelled = st->isTaskCancelled;
        m->isResultIsOk = st->isResultIsOk;
        m->isResultIsError = st->isResultIsError;
        m->isResultValue = st->isResultValue;
        m->isResultError = st->isResultError;
        m->isOptionHasValue = st->isOptionHasValue;
        m->isOptionIsNone = st->isOptionIsNone;
        m->isOptionValue = st->isOptionValue;
        m->type = st->exprType;
        return m;
      }
      case ExprKind::OptMember:
        return lowerOptMember(static_cast<OptMemberExpr*>(e));
      case ExprKind::OptIndex:
        return lowerOptIndex(static_cast<OptIndexExpr*>(e));
      case ExprKind::Index: {
        auto st = static_cast<IndexExpr*>(e);
        auto ix = std::make_unique<HirIndex>();
        ix->object = lowerExpr(st->object.get());
        ix->index = lowerExpr(st->index.get());
        ix->type = st->exprType;
        return ix;
      }
      case ExprKind::ArrayLit: {
        auto st = static_cast<ArrayLitExpr*>(e);
        auto al = std::make_unique<HirArrayLit>();
        for (auto& el : st->elements) al->elements.push_back(lowerExpr(el.get()));
        al->type = st->exprType;
        return al;
      }
      case ExprKind::TupleLit: {
        // M10.1b: literal de tupla vira nó próprio (bloco heap N×8)
        auto st = static_cast<TupleLitExpr*>(e);
        auto tl = std::make_unique<HirTupleLit>();
        for (auto& el : st->elements) tl->elements.push_back(lowerExpr(el.get()));
        tl->type = st->exprType;
        return tl;
      }
      case ExprKind::Call:
        return lowerCall(static_cast<CallExpr*>(e));
      case ExprKind::Binary: {
        auto st = static_cast<BinaryExpr*>(e);
        auto b = std::make_unique<HirBinary>();
        b->op = st->op;
        b->lhs = lowerExpr(st->lhs.get());
        b->rhs = lowerExpr(st->rhs.get());
        b->derivedEq = st->derivedEq;
        b->derivedCmp = st->derivedCmp;
        b->strEqBin = st->strEqBin; // M14.4: ==/!= de strings por conteúdo
        b->type = st->exprType;
        return b;
      }
      case ExprKind::Unary: {
        auto st = static_cast<UnaryExpr*>(e);
        auto u = std::make_unique<HirUnary>();
        u->op = st->op;
        u->operand = lowerExpr(st->operand.get());
        u->type = st->exprType;
        return u;
      }
      case ExprKind::Assign: {
        auto st = static_cast<AssignExpr*>(e);
        auto a = std::make_unique<HirAssign>();
        a->op = st->op;
        a->target = lowerExpr(st->target.get());
        a->value = lowerExpr(st->value.get());
        a->type = st->exprType;
        return a;
      }
      case ExprKind::Ternary:
        return lowerTernary(static_cast<TernaryExpr*>(e));
      case ExprKind::Cast: {
        auto st = static_cast<CastExpr*>(e);
        auto c = std::make_unique<HirCast>();
        c->target = st->target;
        c->operand = lowerExpr(st->operand.get());
        c->type = st->exprType;
        return c;
      }
      case ExprKind::New: {
        auto st = static_cast<NewExpr*>(e);
        auto n = std::make_unique<HirNew>();
        n->className = st->className;
        n->ctor = st->ctor;
        n->isChannel = st->isChannel;
        n->isList = st->isList;
        n->isArrayNew = st->isArrayNew;
        n->arraySize = st->arraySize;
        n->channelCapacity = st->channelCapacity;
        n->hasNamedArgs = [&]() {
        for (auto& n2 : st->argNames)
          if (n2 != "") return true;
        return false;
      }();
        n->type = st->exprType;
        for (auto& a : st->args) n->args.push_back(lowerExpr(a.get()));
        if (n->ctor && st->args.size() < n->ctor->params.size()) {
          for (size_t i = st->args.size(); i < n->ctor->params.size(); i++) {
            auto dv = n->ctor->params[i]->defaultVal.get();
            auto lowered = dv ? lowerExpr(dv) : hDefaultValue(n->ctor->params[i]->type);
            lowered->type = n->ctor->params[i]->type;
            n->args.push_back(std::move(lowered));
          }
        }
        return n;
      }
      case ExprKind::This: {
        // v0.95: dentro de lambda, `this` é a captura (local comum), não o
        // receiver do método — vira HirVar para ler o slot da captura
        if (lambdaDepth_ > 0) {
          auto v = std::make_unique<HirVar>();
          v->name = "this";
          v->type = e->exprType;
          return v;
        }
        auto t = std::make_unique<HirThis>();
        t->type = e->exprType;
        return t;
      }
      case ExprKind::Match: {
        auto v = lowerMatchExpr(static_cast<MatchExpr*>(e));
        return v ? std::move(v) : std::make_unique<HirNullLit>();
      }
      case ExprKind::OptCtor: {
        auto st = static_cast<OptCtorExpr*>(e);
        auto o = std::make_unique<HirOptCtor>();
        o->variant = st->variant;
        o->type = st->exprType;
        if (st->arg) o->arg = lowerExpr(st->arg.get());
        return o;
      }
      case ExprKind::Try:
        return lowerTry(static_cast<TryExpr*>(e));
      case ExprKind::Coalesce:
        return lowerCoalesce(static_cast<CoalesceExpr*>(e));
      case ExprKind::Spawn: {
        auto st = static_cast<SpawnExpr*>(e);
        auto sp = std::make_unique<HirSpawnExpr>();
        sp->body = lowerBlock(st->body.get());
        sp->captures = st->captures;
        sp->type = st->exprType;
        return sp;
      }
      case ExprKind::Lambda: {
        // v0.95 (lambdas): corpo + capturas + assinatura (tipos já resolvidos)
        auto st = static_cast<LambdaExpr*>(e);
        auto lm = std::make_unique<HirLambda>();
        for (auto& p : st->params) lm->params.emplace_back(p.name, p.type);
        lm->retType = st->returnType;
        lambdaDepth_++;
        lm->body = lowerBlock(st->body.get());
        lambdaDepth_--;
        lm->captures = st->captures;
        lm->selfName = st->selfName;
        lm->type = st->exprType;
        return lm;
      }
      case ExprKind::Await: {
        auto st = static_cast<AwaitExpr*>(e);
        auto a = std::make_unique<HirAwaitExpr>();
        a->operand = lowerExpr(st->operand.get());
        a->type = st->exprType;
        return a;
      }
    }
    return hNull(e->exprType);
  }
};

// ---------------------------------------------------------------------------
// Printer do HIR (--dump-hir)
// ---------------------------------------------------------------------------

std::string binOpName(BinOp op) {
  switch (op) {
    case BinOp::Add: return "+";
    case BinOp::Sub: return "-";
    case BinOp::Mul: return "*";
    case BinOp::Div: return "/";
    case BinOp::Mod: return "%";
    case BinOp::Eq: return "==";
    case BinOp::Ne: return "!=";
    case BinOp::Lt: return "<";
    case BinOp::Gt: return ">";
    case BinOp::Le: return "<=";
    case BinOp::Ge: return ">=";
    case BinOp::And: return "&&";
    case BinOp::Or: return "||";
    case BinOp::BitAnd: return "&";
    case BinOp::BitOr: return "|";
    case BinOp::BitXor: return "^";
    case BinOp::Shl: return "<<";
    case BinOp::Shr: return ">>";
  }
  return "?";
}

std::string assignOpName(AssignOp op) {
  switch (op) {
    case AssignOp::Plain: return "=";
    case AssignOp::Add: return "+=";
    case AssignOp::Sub: return "-=";
    case AssignOp::Mul: return "*=";
    case AssignOp::Div: return "/=";
    case AssignOp::Mod: return "%=";
  }
  return "?";
}

std::string primOpName(int op) {
  switch ((CallExpr::PrimOp)op) {
    case CallExpr::PrimOp::None: return "?";
    case CallExpr::PrimOp::MutexLock: return "mutex_lock";
    case CallExpr::PrimOp::MutexUnlock: return "mutex_unlock";
    case CallExpr::PrimOp::SemaphoreWait: return "semaphore_wait";
    case CallExpr::PrimOp::SemaphoreSignal: return "semaphore_signal";
    case CallExpr::PrimOp::EventWait: return "event_wait";
    case CallExpr::PrimOp::EventSet: return "event_set";
    case CallExpr::PrimOp::EventReset: return "event_reset";
    case CallExpr::PrimOp::BarrierWait: return "barrier_wait";
  }
  return "?";
}

std::string strEscape(const std::string& s) {
  std::string out;
  for (char ch : s) {
    if (ch == '"') out += "\\\"";
    else if (ch == '\\') out += "\\\\";
    else if (ch == '\n') out += "\\n";
    else if (ch == '\t') out += "\\t";
    else if (ch == '\r') out += "\\r";
    else if (ch >= 32 && ch < 127) out += ch;
    else {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\x%02x", (unsigned char)ch);
      out += buf;
    }
  }
  return out;
}

std::string hirStmtStr(const HirStmt& s, int d);
std::string hirExprStr(const HirExpr& e);

std::string hirCallStr(const HirCall& c) {
  std::string name;
  switch (c.kind) {
    case HirCallKind::Normal:
      name = c.resolved ? (c.resolved->isMethod ? c.resolved->ownerClass + "." + c.resolved->name
                                                : c.resolved->name)
                        : "?";
      break;
    case HirCallKind::Print: name = "print"; break;
    case HirCallKind::ClockNs: name = "clock_ns"; break;
    case HirCallKind::ArenaReset: name = "arena_reset"; break;
    case HirCallKind::Sqrt: name = "sqrt"; break;
    case HirCallKind::ListAdd: name = "list_add"; break;
    case HirCallKind::Wait: name = "wait"; break;
    case HirCallKind::TaskCancel: name = "cancel"; break;
    case HirCallKind::ActorCall: name = "actor_call"; break;
    case HirCallKind::ChannelSend: name = "send"; break;
    case HirCallKind::ChannelReceive: name = "receive"; break;
    case HirCallKind::PrimOp: name = primOpName(c.primOp); break;
    case HirCallKind::Async:
      name = "async " + (c.resolved ? c.resolved->name : std::string("?"));
      break;
    case HirCallKind::ModuleCall:
      name = c.resolved ? c.resolved->name : "?";
      break;
    case HirCallKind::EnumCtor:
      name = c.enumCanon + "." + (c.entryName.empty() ? std::to_string(c.index) : c.entryName);
      break;
    case HirCallKind::FromInt: name = "from_int<" + c.enumCanon + ">"; break;
    case HirCallKind::Serialize: name = "serialize<" + c.enumCanon + ">"; break;
    case HirCallKind::StrEq: name = "str_eq"; break;
    case HirCallKind::StrCmp: name = "str_cmp"; break;
    case HirCallKind::ToStr: name = "to_str"; break;
    case HirCallKind::AddrOf: name = "addr_of"; break;
    case HirCallKind::Runtime: name = c.runtimeName; break;
  }
  std::string out = name + "(";
  for (size_t i = 0; i < c.args.size(); i++) {
    if (i) out += ", ";
    out += hirExprStr(*c.args[i]);
  }
  return out + ")";
}

std::string hirExprStr(const HirExpr& e) {
  switch (e.kind) {
    case HirExprKind::IntLit: {
      auto& n = static_cast<const HirIntLit&>(e);
      return std::to_string(n.value) + (n.isUnsigned ? "u" : "");
    }
    case HirExprKind::FloatLit: {
      auto& f = static_cast<const HirFloatLit&>(e);
      char buf[32];
      snprintf(buf, sizeof(buf), "%g", f.value);
      return buf;
    }
    case HirExprKind::StringLit:
      return "\"" + strEscape(static_cast<const HirStringLit&>(e).value) + "\"";
    case HirExprKind::CharLit: {
      auto& c = static_cast<const HirCharLit&>(e);
      char buf[16];
      if (c.value >= 32 && c.value < 127) {
        snprintf(buf, sizeof(buf), "'%c'", (char)c.value);
      } else {
        snprintf(buf, sizeof(buf), "'\\x%02x'", (unsigned char)c.value);
      }
      return buf;
    }
    case HirExprKind::BoolLit:
      return static_cast<const HirBoolLit&>(e).value ? "true" : "false";
    case HirExprKind::NullLit:
      return "null";
    case HirExprKind::Var: {
      auto& v = static_cast<const HirVar&>(e);
      if (v.isGlobal) return "@" + (v.label.empty() ? v.name : v.label);
      if (v.isConst) return v.name + "=" + std::to_string(v.constValue);
      return v.name;
    }
    case HirExprKind::Member: {
      auto& m = static_cast<const HirMember&>(e);
      std::string out = hirExprStr(*m.object) + "." + m.member;
      if (m.isEnumConst) out += "[" + std::to_string(m.enumValue) + "]";
      if (m.isEnumCtor) out += "[ctor " + std::to_string(m.enumCtorIndex) + "]";
      if (m.isProperty) out += "[prop]";
      if (m.isWait) out += "[wait]";
      if (m.isTaskCancelled) out += "[cancelled]";
      return out;
    }
    case HirExprKind::Index: {
      auto& ix = static_cast<const HirIndex&>(e);
      return hirExprStr(*ix.object) + "[" + hirExprStr(*ix.index) + "]";
    }
    case HirExprKind::ArrayLit: {
      auto& al = static_cast<const HirArrayLit&>(e);
      std::string out = "[";
      for (size_t i = 0; i < al.elements.size(); i++) {
        if (i) out += ", ";
        out += hirExprStr(*al.elements[i]);
      }
      return out + "]";
    }
    case HirExprKind::TupleLit: {
      auto& tl = static_cast<const HirTupleLit&>(e);
      std::string out = "(";
      for (size_t i = 0; i < tl.elements.size(); i++) {
        if (i) out += ", ";
        out += hirExprStr(*tl.elements[i]);
      }
      return out + ")";
    }
    case HirExprKind::Call:
      return hirCallStr(static_cast<const HirCall&>(e));
    case HirExprKind::Binary: {
      auto& b = static_cast<const HirBinary&>(e);
      std::string out = "(" + hirExprStr(*b.lhs) + " " + binOpName(b.op) + " " +
                        hirExprStr(*b.rhs) + ")";
      if (b.derivedEq) out += " [equ]";
      if (b.derivedCmp) out += " [cmp]";
      return out;
    }
    case HirExprKind::Unary: {
      auto& u = static_cast<const HirUnary&>(e);
      switch (u.op) {
        case UnOp::Neg: return "(-" + hirExprStr(*u.operand) + ")";
        case UnOp::Not: return "!" + hirExprStr(*u.operand);
        case UnOp::BitNot: return "~" + hirExprStr(*u.operand);
        case UnOp::PreInc: return "++" + hirExprStr(*u.operand);
        case UnOp::PreDec: return "--" + hirExprStr(*u.operand);
        case UnOp::PostInc: return hirExprStr(*u.operand) + "++";
        case UnOp::PostDec: return hirExprStr(*u.operand) + "--";
      }
      return "?";
    }
    case HirExprKind::Assign: {
      auto& a = static_cast<const HirAssign&>(e);
      return hirExprStr(*a.target) + " " + assignOpName(a.op) + " " + hirExprStr(*a.value);
    }
    case HirExprKind::Cast: {
      auto& c = static_cast<const HirCast&>(e);
      return "((" + hirTypeName(c.target) + ") " + hirExprStr(*c.operand) + ")";
    }
    case HirExprKind::New: {
      auto& n = static_cast<const HirNew&>(e);
      if (n.isChannel) {
        return "new channel<" + std::to_string(n.channelCapacity) + ">";
      }
      std::string out = "new " + n.className + "(";
      for (size_t i = 0; i < n.args.size(); i++) {
        if (i) out += ", ";
        out += hirExprStr(*n.args[i]);
      }
      return out + ")";
    }
    case HirExprKind::This:
      return "this";
    case HirExprKind::CellTag: {
      auto& t = static_cast<const HirCellTag&>(e);
      return "tag(" + hirExprStr(*t.subject) + ")";
    }
    case HirExprKind::LoadAt: {
      auto& l = static_cast<const HirLoadAt&>(e);
      return "*(" + hirExprStr(*l.subject) + " + " + std::to_string(l.offset) + ")";
    }
    case HirExprKind::OptCtor: {
      auto& o = static_cast<const HirOptCtor&>(e);
      if (!o.arg) return o.variant;
      return o.variant + "(" + hirExprStr(*o.arg) + ")";
    }
    case HirExprKind::Spawn: {
      auto& s = static_cast<const HirSpawnExpr&>(e);
      std::string out = "spawn { ";
      for (auto& st : s.body->stmts) out += hirStmtStr(*st, 0);
      return out + " }";
    }
    case HirExprKind::Await: {
      auto& a = static_cast<const HirAwaitExpr&>(e);
      return "await " + hirExprStr(*a.operand);
    }
    case HirExprKind::Lambda: {
      auto& l = static_cast<const HirLambda&>(e);
      std::string out = "lambda(";
      for (size_t i = 0; i < l.params.size(); i++) {
        if (i) out += ", ";
        out += l.params[i].first + ": " + hirTypeName(l.params[i].second);
      }
      return out + ") => ...";
    }
  }
  return "?";
}

// bloco sem pad inicial ("{" alinhado ao opener); stmts internos recebem pad
std::string hirBlockStr(const HirBlock* b, int d) {
  std::string pad(2 * d, ' ');
  std::string out = "{\n";
  if (b) {
    for (auto& s : b->stmts) out += hirStmtStr(*s, d + 1);
  }
  out += pad + "}\n";
  return out;
}

// conteúdo de um if SEM o pad inicial (para else-if encadeado)
std::string hirIfContent(const HirIf& i, int d) {
  std::string out = "if (" + hirExprStr(*i.cond) + ") " + hirBlockStr(i.thenBranch.get(), d);
  if (i.elseBranch) {
    out += std::string(2 * d, ' ') + "else ";
    if (i.elseBranch->kind == HirStmtKind::If) {
      out += hirIfContent(static_cast<const HirIf&>(*i.elseBranch), d);
    } else {
      out += hirBlockStr(&static_cast<const HirBlock&>(*i.elseBranch), d);
    }
  }
  return out;
}

std::string hirForInitStr(const HirStmt& s) {
  if (s.kind == HirStmtKind::VarDecl) {
    auto& v = static_cast<const HirVarDecl&>(s);
    std::string out = "var " + v.name + ": " + hirTypeName(v.type);
    if (v.init) out += " = " + hirExprStr(*v.init);
    return out;
  }
  if (s.kind == HirStmtKind::ExprStmt)
    return hirExprStr(*static_cast<const HirExprStmt&>(s).expr);
  return "?";
}

std::string hirStmtStr(const HirStmt& s, int d) {
  std::string pad(2 * d, ' ');
  std::string out = pad;
  switch (s.kind) {
    case HirStmtKind::Block:
      return pad + hirBlockStr(&static_cast<const HirBlock&>(s), d);
    case HirStmtKind::VarDecl: {
      auto& v = static_cast<const HirVarDecl&>(s);
      out += "var " + v.name + ": " + hirTypeName(v.type);
      if (v.init) out += " = " + hirExprStr(*v.init);
      return out + "\n";
    }
    case HirStmtKind::ExprStmt:
      return out + hirExprStr(*static_cast<const HirExprStmt&>(s).expr) + "\n";
    case HirStmtKind::Return: {
      auto& r = static_cast<const HirReturn&>(s);
      out += "return";
      if (r.value) out += " " + hirExprStr(*r.value);
      return out + "\n";
    }
    case HirStmtKind::If: {
      auto& i = static_cast<const HirIf&>(s);
      if (i.elseBranch) return pad + hirIfContent(i, d);
      return pad + "if (" + hirExprStr(*i.cond) + ") " + hirBlockStr(i.thenBranch.get(), d);
    }
    case HirStmtKind::While: {
      auto& w = static_cast<const HirWhile&>(s);
      return out + "while (" + hirExprStr(*w.cond) + ") " + hirBlockStr(w.body.get(), d);
    }
    case HirStmtKind::DoWhile: {
      auto& w = static_cast<const HirDoWhile&>(s);
      return out + "do " + hirBlockStr(w.body.get(), d) + pad + "while (" +
             hirExprStr(*w.cond) + ")\n";
    }
    case HirStmtKind::For: {
      auto& f = static_cast<const HirFor&>(s);
      out += "for (";
      if (f.init) out += hirForInitStr(*f.init);
      out += "; ";
      if (f.cond) out += hirExprStr(*f.cond);
      out += "; ";
      if (f.step) out += hirExprStr(*f.step);
      out += ") " + hirBlockStr(f.body.get(), d);
      return out;
    }
    case HirStmtKind::Break:
      return out + "break\n";
    case HirStmtKind::Continue:
      return out + "continue\n";
    case HirStmtKind::Panic: {
      auto& p = static_cast<const HirPanic&>(s);
      return out + "panic(" + hirExprStr(*p.message) + ")\n";
    }
    case HirStmtKind::Assert: {
      auto& a = static_cast<const HirAssert&>(s);
      return out + "assert(" + hirExprStr(*a.cond) + ")\n";
    }
    case HirStmtKind::Throw: {
      auto& t = static_cast<const HirThrow&>(s);
      return out + "throw " + hirExprStr(*t.value) + "\n";
    }
    case HirStmtKind::Try: {
      auto& t = static_cast<const HirTry&>(s);
      out += "try " + hirBlockStr(t.body.get(), d);
      if (t.hasCatch) {
        out += pad + "catch (" + hirTypeName(t.catchType) + " " + t.catchVar + ") " +
               hirBlockStr(t.catchBody.get(), d);
      }
      return out;
    }
    case HirStmtKind::Lock: {
      auto& l = static_cast<const HirLock&>(s);
      return out + "lock (" + hirExprStr(*l.target) + ") " + hirBlockStr(l.body.get(), d);
    }
    case HirStmtKind::Spawn: {
      auto& sp = static_cast<const HirSpawn&>(s);
      return out + "spawn " + hirBlockStr(sp.body.get(), d);
    }
    case HirStmtKind::Parallel: {
      auto& p = static_cast<const HirParallel&>(s);
      out += p.isDeterministic ? "parallel deterministic {\n" : "parallel {\n";
      for (auto& part : p.parts) {
        out += hirStmtStr(*part, d + 1);
      }
      out += pad + "}\n";
      return out;
    }
    case HirStmtKind::ParallelForeach: {
      auto& pf = static_cast<const HirParallelForeach&>(s);
      out += "parallel foreach (" + pf.itemName + ": " + hirTypeName(pf.itemType) + " in " +
             hirExprStr(*pf.collection) + ")";
      if (pf.batchSize > 0) out += " [batch " + std::to_string(pf.batchSize) + "]";
      out += " " + hirBlockStr(pf.body.get(), d);
      return out;
    }
  }
  return "?\n";
}

} // namespace

std::string hirToString(const HirProgram& hir) {
  std::string out;
  for (auto& f : hir.functions) {
    if (!out.empty()) out += "\n";
    out += "fn " + f.name + "(";
    for (size_t i = 0; i < f.params.size(); i++) {
      if (i) out += ", ";
      out += f.params[i].first + ": " + hirTypeName(f.params[i].second);
    }
    out += ")";
    if (f.hasReturnType) out += " -> " + hirTypeName(f.returnType);
    if (f.isMethod) out += " [method of " + f.ownerClass + "]";
    if (!f.moduleName.empty() && f.moduleName != "main") out += " [module " + f.moduleName + "]";
    if (f.body) out += " " + hirBlockStr(f.body.get(), 0);
  }
  return out;
}

bool loweringToHir(HirProgram& hir, const Program* prog, const Semantic& sem) {
  Lowerer l(hir, sem);
  return l.lowerProgram(prog);
}

} // namespace hphl