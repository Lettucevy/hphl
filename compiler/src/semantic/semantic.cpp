#include "semantic.h"
#include "hir/hirOpt.h"
#include "stdlib/stdbuiltins.h"
#include "messages/message_loader.h"
#include <algorithm>
#include <functional>
#include <sstream>
#include <iostream>

namespace hphl {

// ---------------------------------------------------------------------------
// Clone + substituição (monomorfização de genéricos)
// ---------------------------------------------------------------------------
namespace {

using Subst = std::map<std::string, Type>;

// torna um nome seguro para símbolo de assembly (labels _hphl_...)
std::string labelSafe(const std::string& s) {
  std::string r;
  char buf[16];
  for (const char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '.')
      r += c;
    else {
      snprintf(buf, sizeof buf, "_x%02x", (unsigned char)c);
      r += buf;
    }
  }
  return r;
}

std::unique_ptr<Stmt> cloneStmt(const Stmt& s);

static bool typeUsesTypeVar(const Type& t, const std::string& name) {
  if (t.isTypeVar() && t.name == name) return true;
  if (t.kind == Type::Kind::Class && t.name == name) return true;
  if (t.elem && typeUsesTypeVar(*t.elem, name)) return true;
  if (t.elem2 && typeUsesTypeVar(*t.elem2, name)) return true;
  for (auto& a : t.genericArgs) {
    if (typeUsesTypeVar(a, name)) return true;
  }
  for (auto& e : t.tupleElems) {
    if (typeUsesTypeVar(e, name)) return true;
  }
  return false;
}

Type substType(const Type& t, const Subst& m) {
  if (t.isTypeVar()) {
    auto it = m.find(t.name);
    return it != m.end() ? it->second : t;
  }
  // `T Primeiro<T>(...)`: o tipo de retorno é parseado ANTES da lista <T>
  // (o parser ainda não sabe que 'T' é parâmetro) — trata como TypeVar aqui
  if (t.kind == Type::Kind::Class && t.genericArgs.empty() &&
      t.name.find('.') == std::string::npos) {
    auto it = m.find(t.name);
    if (it != m.end()) return it->second;
  }
  Type r = t;
  if (t.kind == Type::Kind::Array || t.kind == Type::Kind::List ||
      t.kind == Type::Kind::Option || t.kind == Type::Kind::Channel) {
    if (t.elem) r.elem = std::make_shared<Type>(substType(*t.elem, m));
    // M10 (v0.45): dimensão simbólica `T[N]` → concreta com o valor do param
    if (t.kind == Type::Kind::Array && !t.arraySizeSym.empty()) {
      auto sit = m.find(t.arraySizeSym);
      if (sit != m.end() && sit->second.kind == Type::Kind::LitValue) {
        r.arraySize = sit->second.arraySize;
        r.arraySizeSym.clear();
      }
    }
  } else if (t.kind == Type::Kind::Result) {
    if (t.elem) r.elem = std::make_shared<Type>(substType(*t.elem, m));
    if (t.elem2) r.elem2 = std::make_shared<Type>(substType(*t.elem2, m));
  } else if (t.kind == Type::Kind::Class) {
    if (!t.genericArgs.empty()) {
      r.genericArgs.clear();
      for (auto& a : t.genericArgs) r.genericArgs.push_back(substType(a, m));
    }
  }
  return r;
}

// primitivas de sincronização (v0.24.0, spec §10): mutex/semaphore/event/barrier
static bool isPrimitiveSyncType(Type::Kind k) {
  return k == Type::Kind::Mutex || k == Type::Kind::Semaphore ||
         k == Type::Kind::Event || k == Type::Kind::Barrier;
}

std::unique_ptr<Pattern> clonePattern(const Pattern& p) {
  auto np = std::make_unique<Pattern>();
  np->kind = p.kind;
  np->constValue = p.constValue;
  np->strValue = p.strValue;
  np->rangeLo = p.rangeLo;
  np->rangeHi = p.rangeHi;
  np->path = p.path;
  np->bindName = p.bindName;
  np->bindType = p.bindType;
  np->subNames = p.subNames;
  np->hasRest = p.hasRest;
  np->restName = p.restName;
  np->restType = p.restType;
  for (auto& s : p.subs) np->subs.push_back(clonePattern(*s));
  return np;
}

// M10 (v0.45): substituição ativa de parâmetros de VALOR durante o clone do
// corpo da instância genérica (Ident `N` → literal inteiro)
static const Subst* g_litSubst = nullptr;
struct LitSubstGuard {
  const Subst* prev;
  LitSubstGuard(const Subst* s) : prev(g_litSubst) { g_litSubst = s; }
  ~LitSubstGuard() { g_litSubst = prev; }
};

std::unique_ptr<Expr> cloneExpr(const Expr& e) {
  std::unique_ptr<Expr> r;
  // M10: Ident que casa com parâmetro de valor vira literal
  if (g_litSubst && e.kind == ExprKind::Ident) {
    auto it = g_litSubst->find(static_cast<const IdentExpr&>(e).name);
    if (it != g_litSubst->end() && it->second.kind == Type::Kind::LitValue) {
      auto n = std::make_unique<IntLitExpr>();
      n->line = e.line;
      n->value = it->second.arraySize;
      n->exprType = Type::makeInt(64);
      return n;
    }
  }
  switch (e.kind) {
    case ExprKind::IntLit: {
      auto n = std::make_unique<IntLitExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->value = static_cast<const IntLitExpr&>(e).value;
      n->isUnsigned = static_cast<const IntLitExpr&>(e).isUnsigned;
      r = std::move(n);
      break;
    }
    case ExprKind::FloatLit: {
      auto n = std::make_unique<FloatLitExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->value = static_cast<const FloatLitExpr&>(e).value;
      r = std::move(n);
      break;
    }
    case ExprKind::StringLit: {
      auto n = std::make_unique<StringLitExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->value = static_cast<const StringLitExpr&>(e).value;
      r = std::move(n);
      break;
    }
    case ExprKind::CharLit: {
      auto n = std::make_unique<CharLitExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->value = static_cast<const CharLitExpr&>(e).value;
      r = std::move(n);
      break;
    }
    case ExprKind::BoolLit: {
      auto n = std::make_unique<BoolLitExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->value = static_cast<const BoolLitExpr&>(e).value;
      r = std::move(n);
      break;
    }
    case ExprKind::NullLit: {
      auto n = std::make_unique<NullLitExpr>();
      n->line = e.line; n->exprType = e.exprType;
      r = std::move(n);
      break;
    }
    case ExprKind::TupleLit: {
      auto& s = static_cast<const TupleLitExpr&>(e);
      auto n = std::make_unique<TupleLitExpr>();
      n->line = e.line; n->exprType = e.exprType;
      for (auto& el : s.elements) n->elements.push_back(cloneExpr(*el));
      r = std::move(n);
      break;
    }
    case ExprKind::Ident: {
      auto n = std::make_unique<IdentExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->name = static_cast<const IdentExpr&>(e).name;
      r = std::move(n);
      break;
    }
    case ExprKind::Member: {
      auto& s = static_cast<const MemberExpr&>(e);
      auto n = std::make_unique<MemberExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->object = cloneExpr(*s.object);
      n->member = s.member;
      n->isEnumCtor = s.isEnumCtor;
      n->enumCtorEnum = s.enumCtorEnum;
      n->enumCtorIndex = s.enumCtorIndex;
      r = std::move(n);
      break;
    }
    case ExprKind::OptMember: {
      auto& s = static_cast<const OptMemberExpr&>(e);
      auto n = std::make_unique<OptMemberExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->object = cloneExpr(*s.object);
      n->member = s.member;
      r = std::move(n);
      break;
    }
    case ExprKind::Index: {
      auto& s = static_cast<const IndexExpr&>(e);
      auto n = std::make_unique<IndexExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->object = cloneExpr(*s.object);
      n->index = cloneExpr(*s.index);
      r = std::move(n);
      break;
    }
    case ExprKind::OptIndex: {
      auto& s = static_cast<const OptIndexExpr&>(e);
      auto n = std::make_unique<OptIndexExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->object = cloneExpr(*s.object);
      n->index = cloneExpr(*s.index);
      r = std::move(n);
      break;
    }
    case ExprKind::ArrayLit: {
      auto& s = static_cast<const ArrayLitExpr&>(e);
      auto n = std::make_unique<ArrayLitExpr>();
      n->line = e.line; n->exprType = e.exprType;
      for (auto& el : s.elements) n->elements.push_back(cloneExpr(*el));
      r = std::move(n);
      break;
    }
    case ExprKind::Call: {
      auto& s = static_cast<const CallExpr&>(e);
      auto n = std::make_unique<CallExpr>();
      n->line = e.line;
      n->exprType = g_litSubst ? substType(e.exprType, *g_litSubst) : e.exprType;
      n->callee = cloneExpr(*s.callee);
      for (auto& a : s.args) n->args.push_back(cloneExpr(*a));
      if (g_litSubst) {
        for (auto& ga : s.genericArgs) n->genericArgs.push_back(substType(ga, *g_litSubst));
      } else {
        n->genericArgs = s.genericArgs;
      }
      n->isActorCall = s.isActorCall;
      n->isChannelSend = s.isChannelSend;
      n->isChannelReceive = s.isChannelReceive;
      n->primOp = s.primOp;
      r = std::move(n);
      break;
    }
    case ExprKind::Binary: {
      auto& s = static_cast<const BinaryExpr&>(e);
      auto n = std::make_unique<BinaryExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->op = s.op;
      n->derivedEq = s.derivedEq;
      n->derivedCmp = s.derivedCmp;
      n->lhs = cloneExpr(*s.lhs);
      n->rhs = cloneExpr(*s.rhs);
      r = std::move(n);
      break;
    }
    case ExprKind::Unary: {
      auto& s = static_cast<const UnaryExpr&>(e);
      auto n = std::make_unique<UnaryExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->op = s.op;
      n->operand = cloneExpr(*s.operand);
      r = std::move(n);
      break;
    }
    case ExprKind::Assign: {
      auto& s = static_cast<const AssignExpr&>(e);
      auto n = std::make_unique<AssignExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->op = s.op;
      n->target = cloneExpr(*s.target);
      n->value = cloneExpr(*s.value);
      r = std::move(n);
      break;
    }
    case ExprKind::Ternary: {
      auto& s = static_cast<const TernaryExpr&>(e);
      auto n = std::make_unique<TernaryExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->cond = cloneExpr(*s.cond);
      n->thenExpr = cloneExpr(*s.thenExpr);
      n->elseExpr = cloneExpr(*s.elseExpr);
      r = std::move(n);
      break;
    }
    case ExprKind::Cast: {
      auto& s = static_cast<const CastExpr&>(e);
      auto n = std::make_unique<CastExpr>();
      n->line = e.line;
      n->exprType = g_litSubst ? substType(e.exprType, *g_litSubst) : e.exprType;
      n->target = g_litSubst ? substType(s.target, *g_litSubst) : s.target;
      n->operand = cloneExpr(*s.operand);
      r = std::move(n);
      break;
    }
    case ExprKind::New: {
      auto& s = static_cast<const NewExpr&>(e);
      auto n = std::make_unique<NewExpr>();
      n->line = e.line;
      if (g_litSubst) {
        n->exprType = substType(e.exprType, *g_litSubst);
        auto it = g_litSubst->find(s.className);
        if (it != g_litSubst->end()) {
          n->className = it->second.name;
          n->genericArgs = it->second.genericArgs;
        } else {
          n->className = s.className;
          for (auto& ga : s.genericArgs) n->genericArgs.push_back(substType(ga, *g_litSubst));
        }
      } else {
        n->exprType = e.exprType;
        n->className = s.className;
        n->genericArgs = s.genericArgs;
      }
      n->argNames = s.argNames;
      for (auto& a : s.args) n->args.push_back(cloneExpr(*a));
      r = std::move(n);
      break;
    }
    case ExprKind::This: {
      auto n = std::make_unique<ThisExpr>();
      n->line = e.line; n->exprType = e.exprType;
      r = std::move(n);
      break;
    }
    case ExprKind::Match: {
      auto& s = static_cast<const MatchExpr&>(e);
      auto n = std::make_unique<MatchExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->subject = cloneExpr(*s.subject);
      for (auto& arm : s.arms) {
        MatchArm na;
        na.line = arm.line;
        na.pattern = arm.pattern ? clonePattern(*arm.pattern) : nullptr;
        na.hasSubjectBind = arm.hasSubjectBind;
        na.subjectBind = arm.subjectBind;
        na.subjectBindType = arm.subjectBindType;
        na.guard = arm.guard ? cloneExpr(*arm.guard) : nullptr;
        for (auto& st : arm.body) na.body.push_back(cloneStmt(*st));
        na.yield = arm.yield ? cloneExpr(*arm.yield) : nullptr;
        na.yieldType = arm.yieldType;
        n->arms.push_back(std::move(na));
      }
      r = std::move(n);
      break;
    }
    case ExprKind::OptCtor: {
      auto& s = static_cast<const OptCtorExpr&>(e);
      auto n = std::make_unique<OptCtorExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->variant = s.variant;
      n->arg = s.arg ? cloneExpr(*s.arg) : nullptr;
      r = std::move(n);
      break;
    }
    case ExprKind::Try: {
      auto& s = static_cast<const TryExpr&>(e);
      auto n = std::make_unique<TryExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->operand = cloneExpr(*s.operand);
      r = std::move(n);
      break;
    }
    case ExprKind::Coalesce: {
      auto& s = static_cast<const CoalesceExpr&>(e);
      auto n = std::make_unique<CoalesceExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->lhs = cloneExpr(*s.lhs);
      n->rhs = cloneExpr(*s.rhs);
      r = std::move(n);
      break;
    }
    case ExprKind::Spawn: {
      auto& s = static_cast<const SpawnExpr&>(e);
      auto n = std::make_unique<SpawnExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->body = std::unique_ptr<BlockStmt>(
          static_cast<BlockStmt*>(cloneStmt(*s.body).release()));
      n->captures = s.captures;
      r = std::move(n);
      break;
    }
    case ExprKind::Await: {
      auto& s = static_cast<const AwaitExpr&>(e);
      auto n = std::make_unique<AwaitExpr>();
      n->line = e.line; n->exprType = e.exprType;
      n->operand = cloneExpr(*s.operand);
      r = std::move(n);
      break;
    }
  }
  return r;
}

std::unique_ptr<Stmt> cloneStmt(const Stmt& s) {
  std::unique_ptr<Stmt> r;
  switch (s.kind) {
    case StmtKind::Block: {
      auto& b = static_cast<const BlockStmt&>(s);
      auto n = std::make_unique<BlockStmt>();
      n->line = s.line;
      for (auto& st : b.stmts) n->stmts.push_back(cloneStmt(*st));
      r = std::move(n);
      break;
    }
    case StmtKind::If: {
      auto& b = static_cast<const IfStmt&>(s);
      auto n = std::make_unique<IfStmt>();
      n->line = s.line;
      n->cond = cloneExpr(*b.cond);
      n->thenBranch = cloneStmt(*b.thenBranch);
      n->elseBranch = b.elseBranch ? cloneStmt(*b.elseBranch) : nullptr;
      r = std::move(n);
      break;
    }
    case StmtKind::For: {
      auto& b = static_cast<const ForStmt&>(s);
      auto n = std::make_unique<ForStmt>();
      n->line = s.line;
      n->init = b.init ? cloneStmt(*b.init) : nullptr;
      n->cond = b.cond ? cloneExpr(*b.cond) : nullptr;
      n->step = b.step ? cloneExpr(*b.step) : nullptr;
      n->body = cloneStmt(*b.body);
      r = std::move(n);
      break;
    }
    case StmtKind::While: {
      auto& b = static_cast<const WhileStmt&>(s);
      auto n = std::make_unique<WhileStmt>();
      n->line = s.line;
      n->cond = cloneExpr(*b.cond);
      n->body = cloneStmt(*b.body);
      r = std::move(n);
      break;
    }
    case StmtKind::DoWhile: {
      auto& b = static_cast<const DoWhileStmt&>(s);
      auto n = std::make_unique<DoWhileStmt>();
      n->line = s.line;
      n->cond = cloneExpr(*b.cond);
      n->body = cloneStmt(*b.body);
      r = std::move(n);
      break;
    }
    case StmtKind::Break: {
      auto n = std::make_unique<BreakStmt>();
      n->line = s.line;
      r = std::move(n);
      break;
    }
    case StmtKind::Continue: {
      auto n = std::make_unique<ContinueStmt>();
      n->line = s.line;
      r = std::move(n);
      break;
    }
    case StmtKind::Return: {
      auto& b = static_cast<const ReturnStmt&>(s);
      auto n = std::make_unique<ReturnStmt>();
      n->line = s.line;
      n->value = b.value ? cloneExpr(*b.value) : nullptr;
      r = std::move(n);
      break;
    }
    case StmtKind::ExprStmt: {
      auto& b = static_cast<const ExprStmt&>(s);
      auto n = std::make_unique<ExprStmt>();
      n->line = s.line;
      n->expr = cloneExpr(*b.expr);
      r = std::move(n);
      break;
    }
    case StmtKind::Panic: {
      auto& b = static_cast<const PanicStmt&>(s);
      auto n = std::make_unique<PanicStmt>();
      n->line = s.line;
      n->message = cloneExpr(*b.message);
      r = std::move(n);
      break;
    }
    case StmtKind::Assert: {
      auto& b = static_cast<const AssertStmt&>(s);
      auto n = std::make_unique<AssertStmt>();
      n->line = s.line;
      n->cond = cloneExpr(*b.cond);
      r = std::move(n);
      break;
    }
    case StmtKind::Throw: {
      auto& b = static_cast<const ThrowStmt&>(s);
      auto n = std::make_unique<ThrowStmt>();
      n->line = s.line;
      n->value = cloneExpr(*b.value);
      r = std::move(n);
      break;
    }
    case StmtKind::Try: {
      auto& b = static_cast<const TryStmt&>(s);
      auto n = std::make_unique<TryStmt>();
      n->line = s.line;
      n->body = cloneStmt(*b.body);
      n->hasCatch = b.hasCatch;
      n->catchType = b.catchType;
      n->catchVar = b.catchVar;
      n->catchBody = b.catchBody ? cloneStmt(*b.catchBody) : nullptr;
      r = std::move(n);
      break;
    }
    case StmtKind::VarDecl: {
      auto& b = static_cast<const StmtVarDecl&>(s);
      auto n = std::make_unique<StmtVarDecl>();
      n->line = s.line;
      for (auto& srcPtr : b.decls) {
        const VarDecl& src = *srcPtr;
        auto d = std::make_unique<VarDecl>();
        d->line = src.line;
        d->storage = src.storage;
        d->overflow = src.overflow;
        d->type = g_litSubst ? substType(src.type, *g_litSubst) : src.type;
        d->hasType = src.hasType;
        d->isVar = src.isVar;
        d->name = src.name;
        d->access = src.access;
        d->isConst = src.isConst;
        d->isReadonly = src.isReadonly;
        d->isGlobal = src.isGlobal;
        d->atomic = src.atomic;
        d->init = src.init ? cloneExpr(*src.init) : nullptr;
        n->decls.push_back(std::move(d));
      }
      r = std::move(n);
      break;
    }
    case StmtKind::Lock: {
      auto& b = static_cast<const LockStmt&>(s);
      auto n = std::make_unique<LockStmt>();
      n->line = s.line;
      n->target = cloneExpr(*b.target);
      n->body = std::unique_ptr<BlockStmt>(
          static_cast<BlockStmt*>(cloneStmt(*b.body).release()));
      r = std::move(n);
      break;
    }
    case StmtKind::Spawn: {
      auto& b = static_cast<const SpawnStmt&>(s);
      auto n = std::make_unique<SpawnStmt>();
      n->line = s.line;
      n->body = std::unique_ptr<BlockStmt>(
          static_cast<BlockStmt*>(cloneStmt(*b.body).release()));
      n->captures = b.captures;
      r = std::move(n);
      break;
    }
    case StmtKind::Parallel: {
      auto& b = static_cast<const ParallelStmt&>(s);
      auto n = std::make_unique<ParallelStmt>();
      n->line = s.line;
      n->isDeterministic = b.isDeterministic;
      for (auto& p : b.parts) n->parts.push_back(cloneStmt(*p));
      for (auto& caps : b.partCaptures) n->partCaptures.push_back(caps);
      r = std::move(n);
      break;
    }
    case StmtKind::Switch: {
      auto& b = static_cast<const SwitchStmt&>(s);
      auto n = std::make_unique<SwitchStmt>();
      n->line = s.line;
      n->subject = cloneExpr(*b.subject);
      for (auto& c : b.cases) {
        SwitchCase nc;
        nc.value = c.value;
        for (auto& st : c.body) nc.body.push_back(cloneStmt(*st));
        n->cases.push_back(std::move(nc));
      }
      for (auto& st : b.defaultBody) n->defaultBody.push_back(cloneStmt(*st));
      r = std::move(n);
      break;
    }
    case StmtKind::Foreach: {
      auto& b = static_cast<const ForeachStmt&>(s);
      auto n = std::make_unique<ForeachStmt>();
      n->line = s.line;
      n->itemName = b.itemName;
      n->itemType = b.itemType;
      n->hasType = b.hasType;
      n->isVar = b.isVar;
      n->parallel = b.parallel;
      n->batchSize = b.batchSize;
      n->subjectIsGlobal = b.subjectIsGlobal;
      n->collectionName = b.collectionName;
      n->collection = cloneExpr(*b.collection);
      n->body = cloneStmt(*b.body);
      r = std::move(n);
      break;
    }
  }
  return r;
}

std::unique_ptr<FunctionDecl> cloneFunction(const FunctionDecl& f, const Subst& m) {
  auto n = std::make_unique<FunctionDecl>();
  // M10 (v0.45): propaga constantes de parâmetros de valor para o codegen
  for (auto& [k, t] : m)
    if (t.kind == Type::Kind::LitValue) n->litParams[k] = t.arraySize;
  LitSubstGuard guard(&m);
  n->access = f.access;
  n->isStatic = f.isStatic;
  n->isInline = f.isInline;
  n->isConstructor = f.isConstructor;
  n->isMethod = f.isMethod;
  n->isAsync = f.isAsync;
  n->ownerClass = f.ownerClass;
  n->name = f.name;
  n->returnType = substType(f.returnType, m);
  n->hasReturnType = f.hasReturnType;
  n->line = f.line;
  n->moduleName = f.moduleName;
  n->filePath = f.filePath;
  n->isEntryPoint = false; // instâncias nunca são Main
  for (auto& p : f.params) {
    auto np = std::make_unique<Param>();
    np->type = substType(p->type, m);
    np->name = p->name;
    np->line = p->line;
    n->params.push_back(std::move(np));
  }
  if (f.body) {
    auto cloned = cloneStmt(*f.body);
    n->body.reset(static_cast<BlockStmt*>(cloned.release()));
  }
  // A4: contratos acompanham a instância (tipos já substituídos via cloneExpr)
  for (auto& e : f.requires) n->requires.push_back(cloneExpr(*e));
  for (auto& e : f.ensures) n->ensures.push_back(cloneExpr(*e));
  return n;
}

} // namespace

Semantic::Semantic(Program* prog, const std::string& filename)
    : prog_(prog), filename_(filename) {
  scopes_.reserve(64);
  // módulos declarados (nome �?? arquivo) e aliases (alias �?? módulo)
  for (auto& m : prog_->modules) {
    if (modules_.insert(m.first).second) curModule_ = m.first;
  }
  for (auto& a : prog_->aliases) {
    auto ait = aliases_.find(a.first);
    if (ait != aliases_.end()) {
      if (ait->second == a.second) continue; // mesmo alias�??módulo em arquivos distintos
      error(0, "alias de módulo '" + a.first + "' conflitante (aponta para '" +
                   ait->second + "' e '" + a.second + "')");
    }
    aliases_[a.first] = a.second;
    modules_.insert(a.first);
  }
  // decls por módulo (para qualificação A.B.X); garante que o nome do módulo
  // corrente (incluindo 'main' do arquivo de entrada) seja um prefixo válido
  for (auto& d : prog_->decls) {
    if (d->moduleName.empty()) continue;
    modules_.insert(d->moduleName);
    modDecls_[d->moduleName].push_back(d.get());
  }
  validateDeps();
}

void Semantic::error(int line, const std::string& msg) {
  std::ostringstream ss;
  ss << filename_ << ":" << line << ": " << hphl::messages().get("semantic_error_prefix") << ": " << msg;
  throw CompileError{line, 0, ss.str()};
}
// A10: warning (não-erro) — não aborta compilação
void Semantic::warn(int line, const std::string& msg) {
  std::cerr << filename_ << ":" << line << ": " << hphl::messages().get("warning_prefix") << ": " << msg << "\n";
}

// ---------------------------------------------------------------------------
// Dependências declaradas: `module A depends on B, C;`
// ---------------------------------------------------------------------------
void Semantic::validateDeps() {
  for (auto& [mod, deps] : prog_->moduleDeps) {
    if (!modules_.count(mod)) continue; // módulo não declarado �?? erro normal abaixo
    for (auto& dep : deps) {
      if (!modules_.count(dep)) {
        error(0, "módulo '" + mod + "' depende de '" + dep +
                     "', mas '" + dep + "' não foi declarado/importado");
      }
    }
  }
  // ciclos (DFS com estados branco/cinza/preto)
  std::map<std::string, int> state; // 0=branco, 1=cinza, 2=preto
  std::vector<std::string> chain;
  std::function<void(const std::string&)> visit = [&](const std::string& m) {
    state[m] = 1;
    chain.push_back(m);
    for (auto& [mod, deps] : prog_->moduleDeps) {
      if (mod != m) continue;
      for (auto& dep : deps) {
        if (state[dep] == 1) {
          size_t start = 0;
          for (size_t i = 0; i < chain.size(); i++)
            if (chain[i] == dep) { start = i; break; }
          std::string loop;
          for (size_t i = start; i < chain.size(); i++)
            loop += chain[i] + " -> ";
          loop += dep;
          error(0, "ciclo de dependência: " + loop);
        }
        if (state[dep] == 0 && modules_.count(dep)) visit(dep);
      }
    }
    chain.pop_back();
    state[m] = 2;
  };
  for (auto& [mod, deps] : prog_->moduleDeps)
    if (state[mod] == 0 && modules_.count(mod)) visit(mod);
}

void Semantic::pushScope() { scopes_.emplace_back(); }
void Semantic::popScope() { scopes_.pop_back(); }

bool Semantic::lookup(const std::string& name, SymbolInfo& out, Type& typeOut,
                      StoragePolicy* policy, bool* isConst, size_t* foundScope) {
  size_t idx = 0;
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it, ++idx) {
    auto sit = it->symbols.find(name);
    if (sit != it->symbols.end()) {
      out = sit->second;
      if (foundScope) *foundScope = scopes_.size() - 1 - idx;
      auto tit = it->varTypes.find(name);
      if (tit != it->varTypes.end()) typeOut = tit->second;
      else typeOut = Type{};
      if (policy) {
        auto pit = it->policies.find(name);
        *policy = pit != it->policies.end() ? pit->second : StoragePolicy::Stack;
      }
      if (isConst) {
        auto cit = it->isConst.find(name);
        *isConst = cit != it->isConst.end() && cit->second;
      }
      return true;
    }
  }
  return false;
}

SymbolInfo Semantic::declareVar(const std::string& name, const Type& t, StoragePolicy policy,
                                bool isConst, int line, SymbolKind kind, bool atomic,
                                bool isVolatile) {
  auto& scope = topScope();
  if (scope.symbols.count(name)) {
    error(line, "variável '" + name + "' já declarada neste escopo");
  }
  SymbolInfo si;
  si.kind = kind;
  si.name = name;
  si.atomic = atomic;
  si.isVolatile = isVolatile;
  scope.symbols[name] = si;
  scope.varTypes[name] = t;
  scope.policies[name] = policy;
  scope.isConst[name] = isConst;
  if (!parallelPartStack_.empty())
    parallelSiblings_.back()[name] = (int)parallelPartStack_.back();
  return si;
}

// ---------------------------------------------------------------------------
// Passada 1: registrar classes, enums, funções, globais
// ---------------------------------------------------------------------------
void Semantic::registerDeclarations() {
  pushScope(); // escopo global
  // passada A: classes e enums (chaves canônicas; resolução de tipos precisa
  // de tudo registrado)
  for (auto& d : prog_->decls) {
    curModule_ = d->moduleName;
    curFile_ = d->filePath;
    if (auto cls = dynamic_cast<ClassDecl*>(d.get())) {
      std::string canon = canonicalType(cls->name, cls->line);
      if (classes_.count(canon)) error(cls->line, "classe '" + cls->name + "' duplicada");
      ClassInfo ci;
      ci.decl = cls;
      if (!cls->typeParams.empty()) {
        // classe genérica: guarda o template; instâncias são criadas sob demanda
        ci.isTemplate = true;
        for (auto& tp : cls->typeParams) {
          if (!cls->isInterface && tp.variance != Variance::Invariant) {
            error(cls->line, "variância (in/out) só é permitida em interfaces ('" + tp.name + "' em '" + cls->name + "')");
          }
          ci.typeParamNames.push_back(tp.name);
        }
      }
      classes_[canon] = ci;
      simpleClasses_[cls->name].push_back(canon);
      // métodos: ownerClass canônico (labels únicos + resolução de this).
      // Classes genéricas não emitem métodos do template ?? cada instância tem
      // cópias substituídas (monomorfização); nomes visíveis são os canônicos.
      if (!ci.isTemplate) {
        for (auto& m : cls->methods) {
          m->ownerClass = canon;
          FunctionInfo mfi;
          mfi.decl = m.get();
          mfi.moduleName = d->moduleName;
          functions_.push_back(mfi);
        }
      } else {
        for (auto& m : cls->methods) m->ownerClass = canon;
      }
      if (!topScope().symbols.count(cls->name)) {
        SymbolInfo si;
        si.kind = SymbolKind::ClassName;
        si.name = cls->name;
        topScope().symbols[cls->name] = si;
        topScope().varTypes[cls->name] = Type::makeClass(canon);
      }
    } else if (auto en = dynamic_cast<EnumDecl*>(d.get())) {
      std::string canon = canonicalType(en->name, en->line);
      if (enums_.count(canon)) error(en->line, "enum '" + en->name + "' duplicado");
      enums_[canon] = en;
      simpleEnums_[en->name].push_back(canon);
      if (!topScope().symbols.count(en->name)) {
        SymbolInfo si;
        si.kind = SymbolKind::TypeName;
        si.name = en->name;
        topScope().symbols[en->name] = si;
        topScope().varTypes[en->name] = Type::makeEnum(canon);
      }
    } else if (auto ud = dynamic_cast<UsingDecl*>(d.get())) {
      if (ud->isReexport) {
        // `public use A.Nome;` �?? 'Mod.Nome' re-exportado do módulo 'A'
        std::string key = (curModule_.empty() ? "main" : curModule_) + "." + ud->alias;
        if (reexports_.count(key))
          error(ud->line, "reexportação duplicada de '" + ud->alias + "'");
        reexports_[key] = ud->target.name;
      } else {
        if (typeAliases_.count(ud->alias))
          error(ud->line, "alias '" + ud->alias + "' já definido");
        typeAliases_[ud->alias] = ud->target;
      }
    }
  }
  // passada B: funções e globais
  for (auto& d : prog_->decls) {
    curModule_ = d->moduleName;
    curFile_ = d->filePath;
    if (auto fn = dynamic_cast<FunctionDecl*>(d.get())) {
      if (fn->isExtern && fn->name == "Main")
        error(fn->line, "função extern não pode se chamar 'Main'");
      if (!fn->typeParams.empty() && !fn->isMethod && !fn->isExtern) {
        for (auto& tp : fn->typeParams) {
          if (tp.variance != Variance::Invariant) {
            error(fn->line, "variância (in/out) só é permitida em interfaces ('" + tp.name + "' em '" + fn->name + "')");
          }
        }
        // função genérica global: modelo para instanciação; nunca emite código
        genericFuncs_[fn->name].push_back(fn);
      } else {
        // overload global exige aridades distintas (resolução é por arity)
        for (auto& fi : functions_)
          if (!fi.decl->isMethod && fi.decl->name == fn->name &&
              fi.decl->params.size() == fn->params.size() &&
              visibleFrom(fi.decl->moduleName, fi.decl->filePath, fi.decl->access))
            error(fn->line, "função '" + fn->name + "' com " +
                            std::to_string(fn->params.size()) + " parâmetro(s) já " +
                            "declarada (overload exige aridades distintas)");
        FunctionInfo fi;
        fi.decl = fn;
        fi.moduleName = d->moduleName;
        fn->isEntryPoint = (fn->name == "Main" && !fn->isMethod);
        functions_.push_back(fi);
      }
    } else if (auto v = dynamic_cast<VarDecl*>(d.get())) {
      Type t = resolveType(v->type, v->line);
      v->type = t; // tipo canônico (kind/resolução para o codegen)
      // M_RV1 A5: variáveis globais de list/map agora são suportadas.
      // O codegen inicializa com hphl_list_new/hphl_map_new no Main (entry
      // point) e armazena o ponteiro no label .Lg_NAME em .data.
      if (isPrimitiveSyncType(t.kind)) {
        error(v->line, "primitiva de sincronização não pode ser global: crie a "
                       "variável localmente (a inicialização roda "
                       "no ponto da declaração)");
      }
      if (v->storage == StoragePolicy::ThreadLocal) {
        // slot por thread criado pelo runtime; proibido o que não cabe num
        // slot de 8 bytes zerado ou que precisaria de inicialização por thread
        if (v->atomic) {
          warn(v->line, "combinar 'atomic' com 'threadlocal' tem semântica de RMW "
                       "por thread; comportamento pode surpreender");
        }
        if (t.kind == Type::Kind::Array || t.kind == Type::Kind::List ||
            t.kind == Type::Kind::Class || t.kind == Type::Kind::String) {
          warn(v->line, "'threadlocal' para tipos complexos (array/list/class/string) "
                       "tem suporte experimental (apenas tipos escalares são totalmente "
                       "suportados)");
        }
        if (v->init)
          warn(v->line, "'threadlocal' com inicializador tem suporte limitado "
                       "(inicializador ignorado, bloco da thread inicializado zerado)");
      } else if (v->storage != StoragePolicy::Auto && v->storage != StoragePolicy::Stack &&
                 v->storage != StoragePolicy::Shared) {
        error(v->line, "política '" + std::string(storagePolicyName(v->storage)) +
                           "' não se aplica a variáveis globais (globais vivem em .data)");
      }
      validateAtomicDecl(v->line, t, v->atomic);
      if (topScope().symbols.count(v->name)) {
        error(v->line, "global '" + v->name + "' definida em mais de um módulo; "
                       "use nome qualificado (ex.: '" + curModule_ + "." + v->name + "')");
      }
      SymbolInfo si;
      si.kind = SymbolKind::GlobalVar;
      si.name = v->name;
      si.label = ".Lg_" + v->name;
      si.atomic = v->atomic;
      topScope().symbols[v->name] = si;
      topScope().varTypes[v->name] = t;
      topScope().policies[v->name] = v->storage;
      topScope().isConst[v->name] = v->isConst;
      globals_.push_back(v);
      if (v->init) {
        checkExpr(v->init.get());
        if (v->isConst && v->type.isInteger()) {
          std::map<std::string, long long> ctEnv;
          auto ctVal = evalCtExpr(v->init.get(), ctEnv, nullptr, 0);
          if (ctVal) {
            auto lit = std::make_unique<IntLitExpr>();
            lit->line = v->init->line;
            lit->exprType = v->type;
            lit->value = *ctVal;
            v->init = std::move(lit);
          }
        }
      }
    }
  }
  // layout das classes (campos + base já podem ser resolvidos com tudo registrado)
  for (auto& [name, ci] : classes_) {
    curModule_ = ci.decl->moduleName;
    curFile_ = ci.decl->filePath;

    if (ci.decl->isInterface) {
      // contrato sem estado
      for (auto& f : ci.decl->fields)
        error(f->line, "interface '" + ci.decl->name + "' não pode ter campo de dados");
      for (auto& m : ci.decl->methods) {
        if (m->isConstructor)
          error(m->line, "interface '" + ci.decl->name + "' não pode ter construtor");
        if (m->isStatic)
          error(m->line, "interface '" + ci.decl->name + "' não pode ter método static");
        if (m->body)
          error(m->line, "método de interface '" + ci.decl->name + "." + m->name +
                         "' não pode ter corpo");
      }
    }

    // nomes pós ':': o primeiro pode ser classe base (só em class); o resto
    // (ou todos, em struct/interface) são interfaces
    std::vector<std::string> supers = ci.decl->interfaces;
    auto supersArgs = ci.decl->interfaceTypeArgs; // paralelo a `supers`
    std::vector<std::string> ifaces;
    std::vector<std::vector<Type>> ifaceArgs;
    for (size_t si = 0; si < supers.size(); si++) {
      std::string& s = supers[si];
      std::string baseName = s;
      {
        size_t ltPos = s.find('<');
        if (ltPos != std::string::npos) baseName = s.substr(0, ltPos);
      }
      std::string canon;
      {
        std::vector<std::string> cands;
        for (auto& c : simpleClasses_[baseName]) {
          auto it = classes_.find(c);
          if (it == classes_.end()) continue;
          if (visibleFrom(it->second.decl->moduleName, it->second.decl->filePath, it->second.decl->access))
            cands.push_back(c);
        }
        if (cands.empty())
          error(ci.decl->line, "tipo base ou interface '" + s + "' não encontrado");
        std::sort(cands.begin(), cands.end());
        cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
        if (cands.size() > 1)
          error(ci.decl->line, "tipo base ou interface '" + s + "' é ambíguo");
        canon = cands[0];
      }
      bool isIfc = classes_[canon].decl->isInterface;
      if (si == 0 && !ci.decl->isStruct && !ci.decl->isInterface && !isIfc) {
        if (canon == name || canon == ci.decl->name)
          error(ci.decl->line, "classe '" + ci.decl->name + "' não pode herdar de si mesma");
        ci.base = canon; // classe base
      } else if (!isIfc) {
        if (ci.decl->isStruct)
          error(ci.decl->line, "struct '" + ci.decl->name +
                                 "' não herda de classe; '" + s + "' é classe "
                                 "(struct só implementa interfaces)");
        if (ci.decl->isInterface)
          error(ci.decl->line, "interface '" + ci.decl->name +
                                 "' não pode herdar de classe '" + s + "'");
        error(ci.decl->line, "em '" + ci.decl->name + "' só o primeiro tipo pós ':' "
                             "pode ser classe base; '" + s + "' é classe "
                             "(declare-o antes das interfaces)");
      } else {
        ifaces.push_back(canon);
        ifaceArgs.push_back(si < supersArgs.size() ? std::move(supersArgs[si])
                                                   : std::vector<Type>{});
      }
    }
    ci.decl->interfaces = ifaces;         // canônicos
    ci.decl->interfaceTypeArgs = std::move(ifaceArgs); // type args por interface
    // A2 (interface como tipo): resolve os args no módulo da classe (prefixo
    // canônico), para que implementsInterface compare nomes monomorfizados.
    // TypeVar de templates (ex.: `class Box<T> : Store<T>`) passa inalterado.
    for (auto& va : ci.decl->interfaceTypeArgs) {
      for (auto& a : va) {
        if (ci.isTemplate && (a.isTypeVar() || std::find(ci.typeParamNames.begin(), ci.typeParamNames.end(), a.name) != ci.typeParamNames.end())) {
          a = Type::makeTypeVar(a.name);
        } else {
          a = resolveType(a, ci.decl->line);
        }
      }
    }
    ci.interfaceTypeArgs = ci.decl->interfaceTypeArgs;

    // `actor` (v0.23.0): estado próprio serializado por lock — sem herança,
    // sem interfaces e sem propriedades externas; métodos só por valor
    if (ci.decl->isActor) {
      if (!ci.base.empty())
        error(ci.decl->line, "actor '" + ci.decl->name + "' não pode herdar de classe");
      if (!ifaces.empty())
        error(ci.decl->line, "actor '" + ci.decl->name + "' não pode implementar interface");
      if (!ci.decl->properties.empty())
        error(ci.decl->line, "actor '" + ci.decl->name + "' não pode ter propriedade; use método");
      for (auto& m : ci.decl->methods) {
        for (auto& p : m->params)
          if (p->byRef || p->byOut)
            error(p->line, "método de actor '" + m->name + "': parâmetros somente por valor");
      }
    }

    if (ci.isTemplate && !ci.base.empty()) {
      // A7 deferred: herança genérica precisa de monomorfização propagada;
      // por ora apenas warning (compila mas pode dar resultados inesperados
      // se a classe base usar o tipo genérico)
      warn(ci.decl->line, "herança de classe genérica é experimental "
                         "(prefira classes não-genéricas ou composição para garantir "
                         "máxima estabilidade)");
    }

    // Validação de herança circular
    if (!ci.base.empty()) {
      std::string curBase = ci.base;
      for (size_t k = 0; k < classes_.size() + 2; k++) {
        if (curBase.empty()) break;
        if (curBase == name) {
          error(ci.decl->line, "herança circular detectada na classe '" + ci.decl->name + "'");
          break;
        }
        auto bit = classes_.find(curBase);
        if (bit == classes_.end()) break;
        curBase = bit->second.base;
      }
    }

    layoutClass(ci);
    for (auto& m : ci.decl->methods) {
      for (auto& tp : m->typeParams) {
        if (tp.variance != Variance::Invariant) {
          error(m->line, "variância (in/out) só é permitida em interfaces ('" + tp.name + "' em '" + m->name + "')");
        }
      }
      if (m->isConstructor) {
        // mesma arity declarada = duplicado (overload exige aridades distintas;
        // resolução é por arity — ver findMethod/checkNew)
        for (auto* c : ci.ctors)
          if (c->params.size() == m->params.size())
            error(m->line, "construtor de '" + ci.decl->name + "' com " +
                           std::to_string(m->params.size()) + " parâmetro(s) já " +
                           "declarado (overload exige aridades distintas)");
        ci.ctors.push_back(m.get());
      } else {
        for (auto* e : ci.methods)
          if (e->name == m->name && e->params.size() == m->params.size())
            error(m->line, "método '" + m->name + "' com " +
                           std::to_string(m->params.size()) + " parâmetro(s) já " +
                           "declarado em '" + ci.decl->name + "' (overload exige " +
                           "aridades distintas)");
        ci.methods.push_back(m.get());
      }
    }
    if (ci.decl->isInterface && ci.isTemplate) {
      for (auto& tp : ci.decl->typeParams) {
        if (tp.variance == Variance::Covariant) {
          for (auto* m : ci.methods) {
            for (auto& p : m->params) {
              if (typeUsesTypeVar(p->type, tp.name)) {
                error(p->line, "tipo covariante 'out " + tp.name +
                               "' não pode ser usado como parâmetro de método (posição contravariante)");
              }
            }
          }
        } else if (tp.variance == Variance::Contravariant) {
          for (auto* m : ci.methods) {
            if (m->hasReturnType && typeUsesTypeVar(m->returnType, tp.name)) {
              error(m->line, "tipo contravariante 'in " + tp.name +
                             "' não pode ser usado como retorno de método (posição covariante)");
            }
          }
        }
      }
    }
    // propriedades (spec §19): criam os acessadores como funções internas
    for (auto& p : ci.decl->properties) {
      Type pt = resolveType(p->type, p->line);
      if (ci.decl->isInterface)
        error(p->line, "interface '" + ci.decl->name + "' não pode ter propriedade");
      if (ci.isTemplate)
        warn(p->line, "propriedade em classe genérica tem suporte experimental");
      if (ci.properties.count(p->name))
        error(p->line, "propriedade '" + p->name + "' duplicada em '" + ci.decl->name + "'");
      PropInfo pi;
      pi.type = pt;
      pi.access = p->access;
      auto mk = [&](bool isGet) -> FunctionDecl* {
        auto fn = std::make_unique<FunctionDecl>();
        fn->isMethod = true;
        // M_RV1 A4: propriedade estatica -> acessador static (sem `this`)
        fn->isStatic = p->isStatic;
        fn->ownerClass = name;
        fn->name = (isGet ? "get_" : "set_") + p->name;
        fn->access = p->access;
        fn->hasReturnType = isGet;
        fn->returnType = isGet ? pt : Type::makeVoid();
        // o corpo sai da declaração da propriedade (só os acessadores o usam)
        fn->body = isGet ? std::move(p->getBody) : std::move(p->setBody);
        fn->moduleName = ci.decl->moduleName;
        fn->filePath = ci.decl->filePath;
        if (!isGet) {
          auto vp = std::make_unique<Param>();
          vp->type = pt;
          vp->name = "value";
          vp->line = p->line;
          fn->params.push_back(std::move(vp));
        }
        FunctionInfo mfi;
        mfi.decl = fn.get();
        mfi.moduleName = ci.decl->moduleName;
        functions_.push_back(mfi);
        return fn.release(); // a posse é de functions_ (como nos métodos)
      };
      if (p->getBody) pi.getter = mk(true);
      if (p->setBody) pi.setter = mk(false);
      ci.properties[p->name] = pi;
    }
    // v0.25.0: derive de traços — valida os nomes e, em classes não-
    // genéricas, sintetiza os helpers na hora (ver synthDerivedHelpers); em
    // templates a síntese acontece por instância em instantiateClassInfo
    ci.derives = ci.decl->derives;
    if (!ci.derives.empty()) {
      for (auto& tr : ci.derives)
        if (tr != "Equatable" && tr != "Comparable" && tr != "Hashable" &&
            tr != "Cloneable" && tr != "Sendable")
          error(ci.decl->line, "traço '" + tr + "' não existe para struct/class "
                               "(disponíveis: Equatable, Comparable, Hashable, "
                               "Cloneable, Sendable)");
      if (!ci.isTemplate) synthDerivedHelpers(ci, false);
    }
  }
  // passada separada: valida implementação das interfaces (todas as classes já
  // tiveram seus métodos coletados �?? ordem de map não afeta o resultado)
  for (auto& [name, ci] : classes_) {
    if (ci.decl->isInterface || ci.isTemplate || ci.decl->interfaces.empty()) continue;
    curModule_ = ci.decl->moduleName;
    curFile_ = ci.decl->filePath;
    auto sameSig = [&](FunctionDecl* a, FunctionDecl* b, const Subst* subst) -> bool {
      if (a->name != b->name) return false;
      if (a->hasReturnType != b->hasReturnType) return false;
      if (a->params.size() != b->params.size()) return false;
      auto typeEq = [&](Type ta, Type tb) -> bool {
        if (subst) tb = substType(tb, *subst); // método da interface: T → arg concreto
        std::string m0 = curModule_, f0 = curFile_;
        curModule_ = a->moduleName; curFile_ = a->filePath;
        ta = resolveType(ta, a->line);
        curModule_ = b->moduleName; curFile_ = b->filePath;
        tb = resolveType(tb, b->line);
        curModule_ = m0; curFile_ = f0;
        return ta == tb;
      };
      if (a->hasReturnType && !typeEq(a->returnType, b->returnType)) return false;
      for (size_t i = 0; i < a->params.size(); i++)
        if (!typeEq(a->params[i]->type, b->params[i]->type)) return false;
      return true;
    };
    std::function<bool(const ClassInfo&, FunctionDecl*, const Subst*)> hasMethod =
        [&](const ClassInfo& c2, FunctionDecl* want, const Subst* subst) -> bool {
      for (auto* cand : c2.methods)
        if (sameSig(cand, want, subst)) return true;
      if (!c2.base.empty()) {
        auto it = classes_.find(c2.base);
        if (it != classes_.end()) return hasMethod(it->second, want, subst);
      }
      return false;
    };
    for (size_t ii = 0; ii < ci.decl->interfaces.size(); ii++) {
      std::string iname = ci.decl->interfaces[ii];
      auto ifit = classes_.find(iname);
      if (ifit == classes_.end()) continue; // erro já reportado
      auto& ifc = ifit->second;
      // A2: interface genérica — substitui os TypeVars dos métodos pelos
      // argumentos concretos (ex.: Repo<int> ⇒ `T Get()` vira `int Get()`).
      Subst subst;
      const Subst* substPtr = nullptr;
      if (ifc.isTemplate && ii < ci.decl->interfaceTypeArgs.size() &&
          !ci.decl->interfaceTypeArgs[ii].empty()) {
        auto& args = ci.decl->interfaceTypeArgs[ii];
        for (size_t ai = 0; ai < ifc.typeParamNames.size() && ai < args.size(); ai++)
          subst[ifc.typeParamNames[ai]] = args[ai];
        substPtr = &subst;
      }
      for (auto* im : ifc.methods) {
        if (!hasMethod(ci, im, substPtr))
          error(ci.decl->line, "'" + ci.decl->name + "' não implementa o método '" +
                               im->name + "' da interface '" + ifc.decl->name + "'");
      }
    }
  }
  // payloads de enums (tipos de soma): resolve tipos e calcula o layout da célula
  for (auto& [name, en] : enums_) {
    curModule_ = en->moduleName;
    curFile_ = en->filePath;
    int maxBytes = 0;
    for (auto& e : en->entries) {
      if (!e.hasPayload) continue;
      en->anyPayload = true;
      int bytes = 0;
      for (auto& p : e.params) {
        Type pt = resolveType(p.type, p.line);
        if (pt.kind == Type::Kind::Void)
          error(p.line, "payload de variante não pode ser void");
        if (pt.kind == Type::Kind::Array)
          error(p.line, "payload de array não suportado (use list ou classe)");
        p.type = pt;
        bytes += 8; // slots de 8 bytes
      }
      maxBytes = maxBytes > bytes ? maxBytes : bytes;
    }
    // "derive Equatable / Comparable / Serializable" �?? traços de automação
    for (auto& d : en->derives) {
      if (d != "Equatable" && d != "Comparable" && d != "Serializable")
        error(en->line, "traço derivado desconhecido '" + d +
                           "' (disponíveis: Equatable, Comparable, Serializable)");
      if (en->anyPayload && d == "Comparable")
        error(en->line, "'derive Comparable' exige enum de valores simples");
    }
    en->payloadBytes = maxBytes;
  }
}

// ---------------------------------------------------------------------------
// Módulos: qualificação e visibilidade
// ---------------------------------------------------------------------------
std::string Semantic::moduleKey(const std::string& name) const {
  auto it = aliases_.find(name);
  if (it != aliases_.end()) return it->second;
  if (modules_.count(name)) return name;
  return "";
}

bool Semantic::visibleFrom(const std::string& declModule, const std::string& declFile,
                           Access access) const {
  if (access == Access::Internal)
    return declModule == curModule_ || declFile == curFile_;
  return true; // public sempre; private/protected são de membros de tipo (fora do escopo)
}

bool Semantic::isModulePrefix(const std::string& name) const {
  if (name.empty()) return false;
  for (auto& m : modules_) {
    if (m.size() > name.size() && m.compare(0, name.size(), name) == 0 &&
        m[name.size()] == '.')
      return true;
  }
  return false;
}

std::string Semantic::canonicalType(const std::string& dotted, int line) {
  if (dotted.find('.') == std::string::npos) {
    // nome simples: canônico no módulo corrente
    return (curModule_.empty() ? "main" : curModule_) + "." + dotted;
  }
  // qualificado 'A.B.Type': tenta o prefixo de módulo mais longo (alias incluído)
  size_t last = dotted.size();
  for (;;) {
    size_t pos = dotted.rfind('.', last - 1);
    if (pos == std::string::npos) break;
    std::string prefix = dotted.substr(0, pos);
    std::string mk = moduleKey(prefix);
    if (!mk.empty()) return mk + "." + dotted.substr(pos + 1);
    last = pos;
  }
  error(line, "módulo desconhecido '" + dotted.substr(0, dotted.find('.')) + "'");
  return "";
}

Type Semantic::moduleTypeMember(const std::string& modKey, const std::string& member, int line) {
  std::string canon = modKey + "." + member;
  auto cit = classes_.find(canon);
  if (cit != classes_.end()) {
    if (!visibleFrom(cit->second.decl->moduleName, cit->second.decl->filePath, cit->second.decl->access))
      error(line, "classe '" + member + "' é internal (não visível fora de seu módulo)");
    return Type::makeClass(canon);
  }
  auto eit = enums_.find(canon);
  if (eit != enums_.end()) {
    if (!visibleFrom(eit->second->moduleName, eit->second->filePath, eit->second->access))
      error(line, "enum '" + member + "' é internal (não visível fora de seu módulo)");
    return Type::makeEnum(canon);
  }
  // reexportação (`public use X.Nome;` no módulo): encaminha para o alvo
  auto rit = reexports_.find(canon);
  if (rit != reexports_.end()) {
    if (std::find(aliasStack_.begin(), aliasStack_.end(), canon) != aliasStack_.end())
      error(line, "reexportação cíclica envolvendo '" + canon + "'");
    aliasStack_.push_back(canon);
    Type r = resolveType(Type::makeClass(rit->second), line);
    aliasStack_.pop_back();
    return r;
  }
  return Type::makeVoid();
}

VarDecl* Semantic::moduleGlobalVar(const std::string& modKey, const std::string& name, int line) {
  for (auto* d : modDecls_[modKey]) {
    auto* v = dynamic_cast<VarDecl*>(d);
    if (v && v->name == name) {
      if (!visibleFrom(v->moduleName, v->filePath, v->access))
        error(line, "global '" + name + "' é internal no módulo '" + modKey + "'");
      return v;
    }
  }
  return nullptr;
}

FunctionDecl* Semantic::moduleFunction(const std::string& modKey, const std::string& name,
                                       size_t argc, int line) {
  for (auto* d : modDecls_[modKey]) {
    auto* f = dynamic_cast<FunctionDecl*>(d);
    if (f && !f->isMethod && f->name == name && arityMatches(f, argc)) {
      if (!visibleFrom(f->moduleName, f->filePath, f->access))
        error(line, "função '" + name + "' é internal no módulo '" + modKey + "'");
      return f;
    }
  }
  return nullptr;
}

Type Semantic::resolveType(const Type& t, int line) {
  if (t.isTypeVar()) return t;
  // M11.3: `channel<T>` numa declaração — canonicaliza o payload (parse deixa
  // nomes de classe sem o prefixo de módulo, quebrando o Send/atribuição)
  if (t.kind == Type::Kind::Channel && t.elem) {
    Type re = resolveType(*t.elem, line);
    if (!(re == *t.elem)) return Type::makeChannel(re);
    return t;
  }
  if (t.kind == Type::Kind::Class || t.kind == Type::Kind::Enum) {
    if (!t.genericArgs.empty()) {
// classe genérica: `Nome<T1, T2>` — resolve o template e instancia (mono)
      Type bare = t;
      bare.genericArgs.clear();
      Type canonT = resolveType(bare, line); // visibilidade + canônico
      if (canonT.kind != Type::Kind::Class)
        error(line, "'" + t.name + "' não é classe (não aceita <...>)");
      if (!classes_.at(canonT.name).isTemplate)
        error(line, "'" + t.name + "' não é classe genérica (não aceita <...>)");
      auto& ci = classes_[canonT.name];
      std::vector<Type> rargs;
      for (auto& a : t.genericArgs) {
        Type ra = resolveType(a, line);
        if (ra.kind == Type::Kind::Void)
          error(line, "argumento de tipo inválido (void) para '" + t.name + "'");
        rargs.push_back(ra);
      }
      if (rargs.size() != ci.typeParamNames.size())
        error(line, "'" + t.name + "' espera " + std::to_string(ci.typeParamNames.size()) +
                        " argumento(s) de tipo, veio " + std::to_string(rargs.size()));
      std::string mg = mangleClassName(canonT.name, rargs);
      if (!classes_.count(mg)) instantiateClassInfo(canonT.name, rargs, line);
      return Type::makeClass(mg);
    }
    if (t.name.find('.') == std::string::npos) {
      // alias `using X = Mod.Tipo;` (ou reexport local): resolve o alvo
      auto ait = typeAliases_.find(t.name);
      if (ait != typeAliases_.end()) {
        if (std::find(aliasStack_.begin(), aliasStack_.end(), t.name) != aliasStack_.end())
          error(line, "alias cíclico envolvendo '" + t.name + "'");
        aliasStack_.push_back(t.name);
        Type r = resolveType(ait->second, line);
        aliasStack_.pop_back();
        return r;
      }
      // nome simples: candidatos de todas as declarações, com ambiguidade e
      // visibilidade (internal visível apenas no próprio arquivo)
      std::vector<std::string> cands;
      for (auto& c : simpleClasses_[t.name]) {
        auto cit = classes_.find(c);
        if (cit == classes_.end()) continue;
        if (visibleFrom(cit->second.decl->moduleName, cit->second.decl->filePath, cit->second.decl->access))
          cands.push_back(c);
      }
      for (auto& c : simpleEnums_[t.name]) {
        auto eit = enums_.find(c);
        if (eit == enums_.end()) continue;
        if (visibleFrom(eit->second->moduleName, eit->second->filePath, eit->second->access))
          cands.push_back(c);
      }
      if (cands.empty()) {
        if (!simpleClasses_[t.name].empty() || !simpleEnums_[t.name].empty())
          error(line, "tipo '" + t.name + "' é internal (não visível fora de seu módulo)");
        error(line, "tipo desconhecido '" + t.name + "'");
      }
      std::sort(cands.begin(), cands.end());
      cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
      if (cands.size() > 1) {
        std::string mods;
        for (auto& c : cands) mods += " '" + c.substr(0, c.find_last_of('.')) + "'";
        error(line, "tipo '" + t.name + "' é ambíguo (definido em" + mods +
                        "); use nome qualificado (ex.: 'A.B." + t.name + "')");
      }
      Type r = t;
      r.name = cands[0];
      if (classes_.count(r.name)) {
        r.kind = Type::Kind::Class;
        // A2 (interface como tipo): interfaces são referências como classes
        // (ponteiro único; dispatch via vtable da implementação concreta).
        // `new` em interface continua proibido (ver checkNew).
      }
      else r.kind = Type::Kind::Enum;
      return r;
    }
    // qualificado (ou já canônico): 'A.B.Type' ou 'main.Type'
    std::string canon = canonicalType(t.name, line);
    auto cit = classes_.find(canon);
    if (cit != classes_.end()) {
      if (!visibleFrom(cit->second.decl->moduleName, cit->second.decl->filePath, cit->second.decl->access))
        error(line, "tipo '" + t.name + "' é internal (não visível fora de seu módulo)");
      // A2 (interface como tipo): interfaces são referências como classes.
      return Type::makeClass(canon);
    }
    auto eit = enums_.find(canon);
    if (eit != enums_.end()) {
      if (!visibleFrom(eit->second->moduleName, eit->second->filePath, eit->second->access))
        error(line, "tipo '" + t.name + "' é internal (não visível fora de seu módulo)");
      return Type::makeEnum(canon);
    }
    // reexportação: 'Mod.Nome' pode ter sido re-exportado de outro módulo
    auto rit = reexports_.find(canon);
    if (rit != reexports_.end()) {
      if (std::find(aliasStack_.begin(), aliasStack_.end(), canon) != aliasStack_.end())
        error(line, "reexportação cíclica envolvendo '" + canon + "'");
      aliasStack_.push_back(canon);
      Type r = resolveType(Type::makeClass(rit->second), line);
      aliasStack_.pop_back();
      return r;
    }
    size_t dot = canon.find_last_of('.');
    error(line, "tipo '" + t.name + "' não existe no módulo '" +
                    (dot == std::string::npos ? canon : canon.substr(0, dot)) + "'");
  }
  if (t.kind == Type::Kind::Array) {
    Type r = resolveType(*t.elem, line);
    if (r != *t.elem) {
      Type arr = t; // cópia: elem é shared_ptr
      arr.elem = std::make_shared<Type>(r);
      return arr;
    }
    return t;
  }
  if (t.kind == Type::Kind::List) {
    Type r = resolveType(*t.elem, line);
    // M2: elementos de list ocupam um slot (8 bytes); classe/string/list ok
    if (typeSize(r) != 8) {
      error(line, "elemento de 'list' deve ocupar 8 bytes (use classe, string, "
                  "list aninhada ou primitivo)");
    }
    if (r != *t.elem) {
      Type l = t;
      l.elem = std::make_shared<Type>(r);
      return l;
    }
    return t;
  }
  if (t.kind == Type::Kind::Map) {
    Type rk = resolveType(*t.elem, line);
    Type rv = resolveType(*t.elem2, line);
    if (typeSize(rk) != 8 || typeSize(rv) != 8) {
      error(line, "chave e valor de 'map' devem ocupar 8 bytes");
    }
    // chaves suportadas v0.41: int, string, bool, char
    if (rk.kind != Type::Kind::Int && rk.kind != Type::Kind::UInt &&
        rk.kind != Type::Kind::String && rk.kind != Type::Kind::Bool &&
        rk.kind != Type::Kind::Char) {
      error(line, "chave de 'map' deve ser int, string, bool ou char (v0.41)");
    }
    if (rk != *t.elem || rv != *t.elem2) {
      Type m = t;
      m.elem = std::make_shared<Type>(rk);
      m.elem2 = std::make_shared<Type>(rv);
      return m;
    }
    return t;
  }
  if (t.kind == Type::Kind::Tuple) {
    // M10.1b: cada elemento ocupa 8 bytes (sem arrays/tuplas aninhadas)
    Type out;
    out.kind = Type::Kind::Tuple;
    for (auto& el : t.tupleElems) {
      Type r = resolveType(el, line);
      if (r.kind == Type::Kind::Tuple || r.kind == Type::Kind::Array ||
          typeSize(r) != 8) {
        error(line, "elemento de tupla deve ser escalar/ponteiro de 8 bytes "
                    "(arrays e tuplas aninhadas não são suportados no v0.43)");
      }
      out.tupleElems.push_back(r);
    }
    return out;
  }
  if (t.kind == Type::Kind::Option) {
    Type r = resolveType(*t.elem, line);
    if (r != *t.elem) {
      Type o = t;
      o.elem = std::make_shared<Type>(r);
      return o;
    }
    return t;
  }
  if (t.kind == Type::Kind::Result) {
    Type ok = resolveType(*t.elem, line);
    Type err = resolveType(*t.elem2, line);
    if (ok != *t.elem || err != *t.elem2) {
      Type r = t;
      r.elem = std::make_shared<Type>(ok);
      r.elem2 = std::make_shared<Type>(err);
      return r;
    }
    return t;
  }
  if (t.kind == Type::Kind::Func) {
    // v0.95 (lambdas): `func<R, P...>` — resolve retorno + params
    bool changed = false;
    Type out = t;
    if (t.elem) {
      Type r = resolveType(*t.elem, line);
      if (r != *t.elem) {
        out.elem = std::make_shared<Type>(r);
        changed = true;
      }
    }
    for (size_t i = 0; i < t.genericArgs.size(); i++) {
      Type p = resolveType(t.genericArgs[i], line);
      if (p != t.genericArgs[i]) {
        out.genericArgs[i] = p;
        changed = true;
      }
    }
    if (changed) return out;
    return t;
  }
  return t;
}

// ---------------------------------------------------------------------------
// Passada 2: checagem das funções
// ---------------------------------------------------------------------------
void Semantic::checkFunctions() {
  for (auto& fi : functions_) {
    checkFunction(fi.decl);
  }
}

void Semantic::checkFunction(FunctionDecl* fn) {
  if (!fn->body && !fn->isExtern) return; // assinatura de interface (validada no layout de classes)
  if (fn->isExtern) {
    // FFI nativo: valida assinatura, resolve tipos, sem corpo.
    if (!fn->typeParams.empty())
      error(fn->line, "função extern '" + fn->name + "' não pode ser genérica");
    if (fn->isMethod)
      error(fn->line, "função extern '" + fn->name + "' não pode ser método");
    auto ffiOk = [](const Type& t, bool isRet) {
      switch (t.kind) {
        case Type::Kind::Int: case Type::Kind::UInt:
        case Type::Kind::Float: case Type::Kind::Bool:
        case Type::Kind::Char: case Type::Kind::String:
        case Type::Kind::Ptr: // FFI v2: ponteiro opaco (void*) p/ interop C/Vulkan
          return true;
        case Type::Kind::Void:
          return isRet;
        default:
          return false;
      }
    };
    if (fn->hasReturnType) {
      Type rt = resolveType(fn->returnType, fn->line);
      fn->returnType = rt;
      if (!ffiOk(rt, true))
        error(fn->line, "tipo de retorno de extern '" + fn->name + "' não suportado no FFI (use int/float/double/bool/char/string/ptr/void)");
    }
    for (auto& p : fn->params) {
      Type pt = resolveType(p->type, p->line);
      p->type = pt;
      if (!ffiOk(pt, false))
        error(p->line, "parâmetro '" + p->name + "' de extern '" + fn->name + "' tem tipo não suportado no FFI (use int/float/double/bool/char/string/ptr)");
      if (p->isByRef())
        error(p->line, "parâmetro '" + p->name + "' de extern não pode ser ref/out/in");
      if (p->defaultVal)
        error(p->line, "parâmetro '" + p->name + "' de extern não pode ter valor padrão");
    }
    if (!fn->externLib.empty()) {
      if (std::find(externLibs_.begin(), externLibs_.end(), fn->externLib) == externLibs_.end())
        externLibs_.push_back(fn->externLib);
      if (std::find(prog_->externLibs.begin(), prog_->externLibs.end(), fn->externLib) == prog_->externLibs.end())
        prog_->externLibs.push_back(fn->externLib);
    }
    return;
  }
  currentFunction_ = fn;
  curFile_ = fn->filePath;
  curModule_ = fn->moduleName;
  // atribuição definida para 'out': cada função recomeça com o estado vazio
  outAssigned_.clear();
  outWriteName_.clear();
  movedVars_.clear();
  maybeNull_.clear();
  arenaLocals_.clear();
  arenaFreed_.clear();
  pushScope();

  if (fn->hasReturnType) {
    Type rt = resolveType(fn->returnType, fn->line);
    // A8: função retornando array agora suportada (shadow space com ptr+len)
    fn->returnType = rt;
  }

  int slot = 0;
  bool seenDefault = false;
  for (auto& p : fn->params) {
    Type pt = resolveType(p->type, p->line);
    // A8: parâmetro de tipo array agora suportado (passa ptr+len)
    /* if (pt.kind == Type::Kind::Array) {
      error(p->line, "parâmetro de tipo array não implementado no Milestone 2 "
                     "(use variável global ou campo de classe)");
    } */
    if (fn->isAsync && (p->byRef || p->byOut || p->byIn))
      error(p->line, "parâmetro '" + p->name + "' com 'ref/out/in' em função "
                     "'async' não é suportado (ambiente de execução assíncrono copia parâmetros por valor)");
    p->type = pt; // tipo canônico
    // parâmetros opcionais (spec §5): `= literal`; obrigatórios depois de
    // opcionais não é permitido; ref/out/in não aceitam valor padrão
    if (p->defaultVal) {
      Expr* dv = p->defaultVal.get();
      Type dvType;
      const Expr* lv = dv;
      if (lv->kind == ExprKind::Unary && static_cast<const UnaryExpr*>(lv)->op == UnOp::Neg)
        lv = static_cast<const UnaryExpr*>(lv)->operand.get();
      switch (lv->kind) {
        case ExprKind::IntLit: dvType = Type::makeInt(0); break;
        case ExprKind::FloatLit: dvType = Type::makeFloat(0); break;
        case ExprKind::CharLit: dvType = Type::makeChar(); break;
        case ExprKind::BoolLit: dvType = Type::makeBool(); break;
        case ExprKind::StringLit: dvType = Type::makeString(); break;
        case ExprKind::NullLit:
          if (!(pt.isPointer() || pt.kind == Type::Kind::String))
            error(p->line, "valor padrão de '" + p->name + "' só pode ser null "
                           "para parâmetros de referência");
          dvType = Type::makeVoid();
          break;
        default:
          error(p->line, "valor padrão de '" + p->name +
                             "' deve ser um literal (constante de compilação)");
      }
      if (dvType.kind != Type::Kind::Void && !isAssignable(pt, dvType))
        error(p->line, "valor padrão de '" + p->name + "' incompatível com o tipo do parâmetro");
      seenDefault = true;
    } else if (seenDefault) {
      error(p->line, "parâmetro sem valor padrão '" + p->name +
                         "' não pode vir depois de parâmetro opcional");
    }
    if (p->isByRef() && p->defaultVal)
      error(p->line, "parâmetro '" + p->name + "' com 'ref/out/in' não pode ter valor padrão");
    SymbolInfo si;
    si.kind = SymbolKind::Param;
    si.name = p->name;
    si.slotIndex = slot++;
    topScope().symbols[p->name] = si;
    topScope().varTypes[p->name] = pt;
    topScope().policies[p->name] = StoragePolicy::Stack;
  }

  // métodos: this
  if (fn->isMethod && !fn->isStatic) {
    SymbolInfo si;
    si.kind = SymbolKind::Param;
    si.name = "this";
    si.slotIndex = -1; // this fica em rcx, gerenciado pelo codegen
    topScope().symbols["this"] = si;
    topScope().varTypes["this"] = Type::makeClass(fn->ownerClass);
  }

  // M10 (v0.45): parâmetros de valor da instância genérica (`N` → int64 const)
  for (auto& [lname, lval] : fn->litParams) {
    SymbolInfo si;
    si.kind = SymbolKind::LocalVar;
    si.name = lname;
    si.slotIndex = -1; // constante: o codegen a dobra para literal
    topScope().symbols[lname] = si;
    topScope().varTypes[lname] = Type::makeInt(64);
    topScope().policies[lname] = StoragePolicy::Stack;
    topScope().isConst[lname] = true;
  }

  // A4: preconditions (params/this visíveis; `result` não existe aqui)
  for (auto& e : fn->requires) {
    Type ct = checkExpr(e.get());
    if (ct.kind != Type::Kind::Bool)
      error(e->line, "require exige condição bool");
  }
  // A4: postconditions (`result` = valor de retorno; só se houver retorno)
  if (!fn->ensures.empty()) {
    bool hasResult =
        fn->hasReturnType && fn->returnType.kind != Type::Kind::Void;
    if (hasResult) {
      pushScope();
      SymbolInfo si;
      si.kind = SymbolKind::LocalVar;
      si.name = "result";
      si.slotIndex = -1; // só p/ checagem; o lowering liga ao temp do return
      topScope().symbols["result"] = si;
      topScope().varTypes["result"] = fn->returnType;
      topScope().policies["result"] = StoragePolicy::Stack;
    }
    for (auto& e : fn->ensures) {
      Type ct = checkExpr(e.get());
      if (ct.kind != Type::Kind::Bool)
        error(e->line, "ensure exige condição bool");
    }
    if (hasResult) popScope();
  }

  // corpo
  if (fn->body) {
    pushScope();
    checkBlock(fn->body.get(), fn->returnType);
    popScope();
  }
  currentFunction_ = nullptr;
  popScope();
}

void Semantic::checkBlock(BlockStmt* b, const Type& returnType) {
  if (b->suppressDeprecation) suppressDeprecationDepth_++;
  for (auto& s : b->stmts) checkStatement(s.get(), returnType);
  if (b->suppressDeprecation) suppressDeprecationDepth_--;
}

void Semantic::checkStatement(Stmt* s, const Type& returnType) {
  switch (s->kind) {
    case StmtKind::Block:
      pushScope();
      checkBlock(static_cast<BlockStmt*>(s), returnType);
      popScope();
      break;
    case StmtKind::If: {
      auto st = static_cast<IfStmt*>(s);
      Type ct = checkExpr(st->cond.get());
      if (ct.kind != Type::Kind::Bool) error(st->line, "condição do if deve ser bool");
      // atribuição definida para 'out': depois do if, só o que foi atribuído em
      // AMBOS os caminhos (sem else, o caminho de não-entrar mantém o anterior)
      std::set<std::string> before = outAssigned_;
      // M11.6 Safety: USE AFTER MOVE vira dataflow por ramos — move num ramo
      // não envenena o outro nem os usos que não são dominados pela mudança
      std::set<std::string> movedBefore = movedVars_;
      // M11.7: polaridade da condição (`x != null` / `x == null`) ajusta o
      // estado possivelmente-null de cada ramo
      std::string ncName;
      int ncp = nullCondPolarity(st->cond.get(), ncName);
      std::set<std::string> maybeThen = maybeNull_;
      std::set<std::string> maybeElse = maybeNull_;
      if (ncp && (maybeNull_.count(ncName))) {
        if (ncp == 1) { maybeThen.erase(ncName); maybeElse.insert(ncName); }
        else { maybeThen.insert(ncName); maybeElse.erase(ncName); }
      }
      pushScope();
      maybeNull_ = maybeThen;
      checkStatement(st->thenBranch.get(), returnType);
      popScope();
      std::set<std::string> thenSet = outAssigned_;
      std::set<std::string> movedThen = movedVars_;
      std::set<std::string> maybeThenEnd = maybeNull_;
      outAssigned_ = before;
      movedVars_ = movedBefore;
      std::set<std::string> elseSet = before;
      maybeNull_ = maybeElse;
      if (st->elseBranch) {
        pushScope();
        checkStatement(st->elseBranch.get(), returnType);
        popScope();
        elseSet = outAssigned_;
      }
      std::set<std::string> maybeElseEnd = maybeNull_;
      outAssigned_ = outIntersect(thenSet, elseSet);
      // movido definitivamente = movido em AMBOS os caminhos; sem else, o
      // caminho de não-entrar mantém o anterior (só o base sobrevive)
      std::set<std::string> movedMerged;
      for (auto& m : movedVars_)
        if (movedThen.count(m)) movedMerged.insert(m);
      movedVars_ = movedMerged;
      // possivelmente-null depois do if = presente nos DOIS caminhos finais
      std::set<std::string> maybeMerged;
      for (auto& m : maybeThenEnd)
        if (maybeElseEnd.count(m)) maybeMerged.insert(m);
      maybeNull_ = maybeMerged;
      break;
    }
    case StmtKind::For: {
      auto st = static_cast<ForStmt*>(s);
      pushScope();
      if (st->init) checkStatement(st->init.get(), returnType);
      // atribuição definida: o for pode rodar 0 vezes → depois do for vale o
      // estado pós-init; cond/body/step checam leituras contra esse início
      std::set<std::string> initEnd = outAssigned_;
      if (st->cond) {
        Type ct = checkExpr(st->cond.get());
        if (ct.kind != Type::Kind::Bool) error(st->line, "condição do for deve ser bool");
      }
      pushScope();
      checkStatement(st->body.get(), returnType);
      popScope();
      if (st->step) checkExpr(st->step.get());
      outAssigned_ = initEnd;
      popScope();
      break;
    }
    case StmtKind::While: {
      auto st = static_cast<WhileStmt*>(s);
      Type ct = checkExpr(st->cond.get());
      if (ct.kind != Type::Kind::Bool) error(st->line, "condição do while deve ser bool");
      // atribuição definida: o corpo pode não rodar nenhuma vez → o estado
      // anterior vale depois do while (o corpo é analisado a partir dele)
      std::set<std::string> before = outAssigned_;
      pushScope();
      checkStatement(st->body.get(), returnType);
      popScope();
      outAssigned_ = before;
      break;
    }
    case StmtKind::DoWhile: {
      auto st = static_cast<DoWhileStmt*>(s);
      pushScope();
      checkStatement(st->body.get(), returnType);
      popScope();
      Type ct = checkExpr(st->cond.get());
      if (ct.kind != Type::Kind::Bool) error(st->line, "condição do do-while deve ser bool");
      break;
    }
    case StmtKind::Break:
    case StmtKind::Continue:
      if (inLockDepth_ > 0)
        error(s->line, "break/continue dentro de 'lock' não suportado (trap de deadlock)");
      if (inSpawnDepth_ > 0)
        error(s->line, "break/continue dentro de 'spawn' não faz sentido "
                       "(cada tarefa é uma função nova)");
      if (inLambdaDepth_ > 0)
        error(s->line, "break/continue dentro de lambda não atravessa a "
                       "fronteira da função (a lambda é uma função nova)");
      break;
    case StmtKind::Return: {
      auto st = static_cast<ReturnStmt*>(s);
      if (inLambdaDepth_ > 0) {
        // v0.95: return no corpo da lambda vira o tipo de retorno inferido
        // (primeiro define; demais devem combinar — mesmo desenho do spawn)
        if (st->value) {
          Type vt = checkExpr(st->value.get());
          lambdaHadReturn_ = true;
          if (!lambdaReturnSet_) {
            *lambdaReturn_ = vt;
            lambdaReturnSet_ = true;
          } else if (!isAssignable(*lambdaReturn_, vt) &&
                     !isAssignable(vt, *lambdaReturn_)) {
            error(st->line, "'return' com tipo incompatível com o retorno "
                            "já definido nesta lambda");
          }
        } else if (lambdaReturnSet_ && lambdaReturn_->kind != Type::Kind::Void) {
          error(st->line, "'return;' sem valor após 'return <valor>;' na lambda");
        }
        break;
      }
      if (inSpawnExpr_ > 0) {
        // return dentro do corpo de `spawn {}` como expressão: vira o payload
        // da task (primeiro return define o tipo; os demais devem combinar)
        if (st->value) {
          Type vt = checkExpr(st->value.get());
          if (!spawnPayloadSet_) {
            *spawnPayload_ = vt;
            spawnPayloadSet_ = true;
          } else if (!isAssignable(*spawnPayload_, vt) &&
                     !isAssignable(vt, *spawnPayload_)) {
            error(st->line, "'return' com tipo incompatível com o payload "
                            "já definido neste 'spawn'");
          }
        } else {
          error(st->line, "'spawn' com payload precisa terminar com "
                          "'return <valor>;'");
        }
        break;
      }
      if (inSpawnDepth_ > 0)
        error(st->line, "return dentro de 'spawn' não suportado no M2 "
                        "(cada tarefa é uma função nova; use uma global)");
      if (inLockDepth_ > 0)
        error(st->line, "return dentro de 'lock' não suportado (trap de deadlock)");
      if (st->value) {
        Type vt = checkExpr(st->value.get(), returnType);
        if (returnType.kind == Type::Kind::Void) {
          error(st->line, "função sem retorno não pode retornar valor");
        }
        // M10.4 Safety Analysis (spec §3): ARENA ESCAPE — objeto alocado em
        // arena não pode escapar da função: a arena inteira é destruída no
        // retorno, o chamador receberia ponteiro morto
        {
          const Expr* v = st->value.get();
          while (v->kind == ExprKind::Member)
            v = static_cast<const MemberExpr*>(v)->object.get();
          if (v->kind == ExprKind::Ident) {
            SymbolInfo si;
            Type t2;
            StoragePolicy pol;
            if (lookup(static_cast<const IdentExpr*>(v)->name, si, t2, &pol)) {
              if (pol == StoragePolicy::Arena)
                error(st->line, "ARENA ESCAPE: '" +
                                    static_cast<const IdentExpr*>(v)->name +
                                    "' vive em arena e escapa da função "
                                    "(a arena é destruída no retorno)");
              // M11.5 Safety (spec §3): POOL ESCAPE — o slot do pool também
              // volta ao freelist no epílogo; o chamador receberia memória
              // que o próximo alloc recicla (dangling latente)
              if (pol == StoragePolicy::Pool)
                error(st->line, "POOL ESCAPE: '" +
                                    static_cast<const IdentExpr*>(v)->name +
                                    "' vive em pool e escapa da função "
                                    "(o slot volta ao freelist no retorno)");
            }
          }
        }
        if (!isAssignable(returnType, vt)) {
          error(st->line, "tipo de retorno incompatível");
        }
      } else {
        if (returnType.kind != Type::Kind::Void) {
          error(st->line, "função com retorno precisa retornar um valor");
        }
      }
      break;
    }
    case StmtKind::ExprStmt: {
      auto st = static_cast<ExprStmt*>(s);
      checkExpr(st->expr.get());
      break;
    }
    case StmtKind::Panic: {
      auto st = static_cast<PanicStmt*>(s);
      Type mt = checkExpr(st->message.get());
      if (mt.kind != Type::Kind::String)
        error(st->line, "panic exige uma mensagem string");
      break;
    }
    case StmtKind::Assert: {
      auto st = static_cast<AssertStmt*>(s);
      Type ct = checkExpr(st->cond.get());
      if (ct.kind != Type::Kind::Bool)
        error(st->line, "assert exige condição bool");
      break;
    }
    case StmtKind::Lock: {
      auto st = static_cast<LockStmt*>(s);
      // A10: lock aninhado agora é warning (não erro) — reentrance counter
      // detecta locks do MESMO objeto (deadlock real); locks em objetos
      // diferentes são apenas warning.
      if (inLockDepth_ > 0) {
        warn(st->line, "lock aninhado (potencial deadlock se for o mesmo objeto)");
      }
      Type tt = checkExpr(st->target.get());
      if (tt.kind != Type::Kind::Class)
        error(st->line, "lock exige uma referência de classe (objeto); "
                        "tipos de valor não têm lock próprio");
      // M11.8: rastreia o alvo para TOCTOU — entrando no próprio lock, o
      // objeto sai do conjunto "velho"; se o corpo ler campos, ao sair ele
      // VOLTA a ficar velho (leituras fora do lock ficam desatualizadas)
      std::string prevLockObj = lockObjName_;
      bool prevRead = lockBodyRead_;
      lockObjName_ = st->target->kind == ExprKind::Ident
                         ? static_cast<IdentExpr*>(st->target.get())->name
                         : "";
      staleAfterLock_.erase(lockObjName_);
      lockBodyRead_ = false;
      inLockDepth_++;
      // lock = try/finally: um throw no corpo pode pular o restante → depois
      // do lock nada do corpo é garantido (atribuição definida conservadora)
      std::set<std::string> lockBefore = outAssigned_;
      checkBlock(st->body.get(), returnType);
      outAssigned_ = lockBefore;
      inLockDepth_--;
      if (!lockObjName_.empty() && lockBodyRead_)
        staleAfterLock_.insert(lockObjName_);
      lockObjName_ = prevLockObj;
      lockBodyRead_ = prevRead;
      break;
    }
    case StmtKind::Spawn: {
      auto st = static_cast<SpawnStmt*>(s);
      inSpawnDepth_++;
      spawnBaseScope_ = scopes_.size();
      auto* prevSink = spawnCaptureSink_;
      spawnCaptureSink_ = &st->captures;
      // atribuição definida: o corpo lê o ESTADO ATUAL (captura por valor é um
      // snapshot no site do spawn); escritas no corpo são locais à tarefa →
      // nada se propaga para depois do spawn
      std::set<std::string> spawnBefore = outAssigned_;
      // M11.6: move dentro da task é isolado — a origem no CHAMADOR continua
      // válida (a task recebeu uma cópia do ponteiro; a posse não saiu daqui)
      std::set<std::string> spawnMovedBefore = movedVars_;
      checkBlock(st->body.get(), returnType);
      movedVars_ = spawnMovedBefore;
      outAssigned_ = spawnBefore;
      spawnCaptureSink_ = prevSink;
      inSpawnDepth_--;
      break;
    }
    case StmtKind::Parallel: {
      auto st = static_cast<ParallelStmt*>(s);
      inSpawnDepth_++;
      spawnBaseScope_ = scopes_.size();
      auto* prevSink = spawnCaptureSink_;
      // captura POR VALOR por parte (v0.22.1): cada parte tem seu próprio env
      // de capturas (snapshot dos locais no site do spawn), como em `spawn`
      st->partCaptures.assign(st->parts.size(), {});
      std::set<std::string> parBefore = outAssigned_;
      // diagnóstico de OUTRA parte (v0.22.8): cada parte é uma TAREFA com
      // escopo próprio — um nível novo da pilha guarda nome → índice da parte
      parallelSiblings_.emplace_back();
      // M11.6: moves dentro das partes são isolados por parte e não vazam
      // para o chamador (mesma razão do spawn)
      std::set<std::string> parMovedBefore = movedVars_;
      for (size_t i = 0; i < st->parts.size(); i++) {
        pushScope(); // escopo da parte (nada dela vaza para as irmãs)
        parallelPartStack_.push_back(i);
        spawnCaptureSink_ = &st->partCaptures[i];
        checkStatement(st->parts[i].get(), returnType);
        parallelPartStack_.pop_back();
        popScope();
        movedVars_ = parMovedBefore;
      }
      parallelSiblings_.pop_back();
      movedVars_ = parMovedBefore;
      outAssigned_ = parBefore;
      spawnCaptureSink_ = prevSink;
      inSpawnDepth_--;
      break;
    }
    case StmtKind::Try: {
      auto st = static_cast<TryStmt*>(s);
      if (inSpawnDepth_ > 0)
        error(st->line, "try/catch dentro de 'spawn' não suportado no M2 "
                        "(o handler de exceção é único no processo)");
      Type ct = resolveType(st->catchType, st->line);
      st->catchType = ct; // tipo canônico para o codegen
      if (ct.kind == Type::Kind::Void)
        error(st->line, "'catch' precisa de um tipo (void não captura nada)");
      catchTypes_.push_back(ct);
      // atribuição definida: o corpo pode lançar a qualquer momento → depois do
      // try só vale o que foi atribuído no corpo E no catch (sem catch, nada)
      std::set<std::string> before = outAssigned_;
      checkStatement(st->body.get(), returnType);
      std::set<std::string> trySet = outAssigned_;
      catchTypes_.pop_back();
      if (st->hasCatch) {
        outAssigned_ = before;
        pushScope();
        declareVar(st->catchVar, ct, StoragePolicy::Stack, false, st->line);
        checkStatement(st->catchBody.get(), returnType);
        popScope();
        outAssigned_ = outIntersect(trySet, outAssigned_);
      } else {
        outAssigned_ = before;
      }
      break;
    }
    case StmtKind::Throw: {
      auto st = static_cast<ThrowStmt*>(s);
      if (inSpawnDepth_ > 0)
        error(st->line, "'throw' dentro de 'spawn' não suportado no M2 "
                        "(o handler de exceção é global no processo)");
      Type vt = checkExpr(st->value.get());
      if (vt.kind == Type::Kind::Void)
        error(st->line, "'throw' exige um valor");
      if (!catchTypes_.empty() && !isAssignable(catchTypes_.back(), vt))
        error(st->line, "'throw' com tipo incompatível com o 'catch' deste try");
      break;
    }
    case StmtKind::VarDecl: {
      // M5 (v0.36.0): `int a = 1, b, c = 3;` — um VarDecl por nome
      auto svd = static_cast<StmtVarDecl*>(s);
      for (auto& decl : svd->decls) {
        VarDecl* st = decl.get();
        Type t;
        if (st->isVar) {
          if (!st->init) error(st->line, "'var' exige inicializador");
          t = checkExpr(st->init.get());
          st->type = t; // codegen usa o tipo inferido (importante para arrays/floats)
        } else {
          t = resolveType(st->type, st->line);
          st->type = t; // tipo canônico (kind/resolução para o codegen)
        }
        // v0.95 (lambdas): `func<R, P...> f = lambda` — pré-declara para
        // permitir recursão (`f` visível no próprio corpo). `var` não tem
        // tipo ainda: sem recursão (erro natural de símbolo desconhecido).
        bool lambdaPredeclared = false;
        if (!st->isVar && st->init && st->init->kind == ExprKind::Lambda &&
            t.kind == Type::Kind::Func) {
          declareVar(st->name, t, st->storage, st->isConst, st->line,
                     SymbolKind::LocalVar, st->atomic);
          // v0.95: auto-referência — o codegen grava o box no env (ponto fixo)
          static_cast<LambdaExpr*>(st->init.get())->selfName = st->name;
          lambdaPredeclared = true;
        }
        if (st->init) {
          Type it = checkExpr(st->init.get(), t);
          if (t.kind == Type::Kind::Array && st->init->kind == ExprKind::ArrayLit) {
            checkArrayInit(st->init.get(), t);
          } else if (t.kind == Type::Kind::List && st->init->kind == ExprKind::ArrayLit && t.elem) {
            // B10: `list<T> xs = {e1, e2, ...}` — array literal como list init
            auto* al = static_cast<ArrayLitExpr*>(st->init.get());
            for (auto& el : al->elements) {
              if (t.elem->kind == Type::Kind::List && el->kind == ExprKind::ArrayLit && t.elem->elem) {
                // aninhado: list<list<T>> = {{...}, {...}}
                auto* inner = static_cast<ArrayLitExpr*>(el.get());
                for (auto& e2 : inner->elements) {
                  if (!isAssignable(*t.elem->elem, e2->exprType)) {
                    error(st->line, "tipo incompatível na inicialização de '" + st->name + "'");
                  }
                }
              } else if (!isAssignable(*t.elem, el->exprType)) {
                error(st->line, "tipo incompatível na inicialização de '" + st->name + "'");
              }
            }
          } else if (!isAssignable(t, it)) {
            error(st->line, "tipo incompatível na inicialização de '" + st->name + "'");
          }
        }
        // M11.7: estado possivelmente-null do novo local
        nullNoteAssign(st->name, t, st->init.get());
        // M11.9: registra local 'arena' p/ arena_reset() e análise UAF
        if (st->storage == StoragePolicy::Arena) {
          arenaLocals_.insert(st->name);
          arenaFreed_.erase(st->name); // declarado após o reset: fresco
        }
        StoragePolicy pol = st->storage;
        if (pol == StoragePolicy::ThreadLocal) {
          error(st->line, "'threadlocal' só se aplica a variáveis globais "
                          "(o runtime gerencia armazenamento por thread para variáveis em escopo global)");
        }
        if (pol != StoragePolicy::Stack && pol != StoragePolicy::Heap &&
            pol != StoragePolicy::Arena && pol != StoragePolicy::Pool &&
            pol != StoragePolicy::Shared && pol != StoragePolicy::Auto) {
          error(st->line, "política de memória '" + std::string(storagePolicyName(pol)) +
                              "' inválida");
        }
        if (t.kind == Type::Kind::Class && pol == StoragePolicy::Stack) {
          error(st->line, "política 'stack' não se aplica a tipos classe "
                           "(classes vivem na heap gerenciada pelo GC; use 'struct' para alocação na stack)");
        }
        // v0.95 (lambdas): `var` com init lambda não tem tipo ainda — nada a
        // pré-declarar aqui (recursão exige `func<...> f = ...` explícito).
        if (t.kind == Type::Kind::List) {
          if (st->init) {
            // B11: permite `list<T> x = list<T>(n)` (construtor com cap)
            // e `list<T> x = new list<T>()` (bug 1.4)
            bool isListCtor = st->init->kind == ExprKind::Call &&
                static_cast<CallExpr*>(st->init.get())->isListNew;
            bool isNewList = st->init->kind == ExprKind::New &&
                static_cast<NewExpr*>(st->init.get())->isList;
            // M_RV1 IMG: chamada que retorna lista (ex.: img_grayscale(px))
            // também é inicializador válido (tipo já validado acima)
            bool isListValue = st->init->exprType.kind == Type::Kind::List;
            bool isNull = st->init->kind == ExprKind::NullLit;
            if (!isListCtor && !isNewList && !isListValue && !isNull) {
              error(st->line, "list não possui inicializador (começa vazio)");
            }
          }
          if (pol == StoragePolicy::Arena || pol == StoragePolicy::Pool ||
              pol == StoragePolicy::Shared) {
            error(st->line, "list exige política de memória 'stack' ou 'heap'");
          }
        }
        if (t.kind == Type::Kind::Map) {
          if (st->init) {
            error(st->line, "map não possui inicializador (começa vazio)");
          }
          // if (pol == StoragePolicy::Auto) {
          //   error(st->line, "map exige política explícita: 'stack map<K,V> m;' ...");
          // }
          if (pol == StoragePolicy::Arena || pol == StoragePolicy::Pool ||
              pol == StoragePolicy::Shared || pol == StoragePolicy::ThreadLocal) {
            error(st->line, "map exige política 'stack' ou 'heap' (v0.41)");
          }
        }
        if (isPrimitiveSyncType(t.kind)) {
          if (st->init)
            error(st->line, "primitiva de sincronização usa o construtor '(N)', "
                            "não o operador '='");
          if (t.kind == Type::Kind::Mutex || t.kind == Type::Kind::Event) {
            if (st->primitiveInit >= 0)
              error(st->line, "'" + st->name + "' não aceita argumento (use sem "
                              "argumento: somente semaphore e barrier recebem N)");
          } else if (t.kind == Type::Kind::Semaphore) {
            if (st->primitiveInit < 0)
              error(st->line, "semaphore exige contador inicial N >= 0: semaphore " +
                                  st->name + "(N)");
          } else if (t.kind == Type::Kind::Barrier && st->primitiveInit < 1) {
            error(st->line, "barrier exige o número de participantes: barrier " +
                                st->name + "(N) com N >= 1");
          }
        }
        if (!lambdaPredeclared)
          declareVar(st->name, t, pol, st->isConst, st->line, SymbolKind::LocalVar, st->atomic);
        validateAtomicDecl(st->line, t, st->atomic);
      }
      break;
    }
    case StmtKind::Destructure: {
      // M10.1b: `var (a, b) = tup;` — cada nome vira um local do tipo do
      // elemento correspondente
      auto st = static_cast<StmtDestructure*>(s);
      Type tt = checkExpr(st->init.get());
      if (tt.kind != Type::Kind::Tuple) {
        error(st->line, "destructuring exige uma tupla (encontrado outro tipo)");
        break;
      }
      if (tt.tupleElems.size() != st->names.size()) {
        error(st->line, "destructuring com " + std::to_string(st->names.size()) +
                            " nomes para uma tupla de " +
                            std::to_string(tt.tupleElems.size()) + " elementos");
        break;
      }
      st->tupleType = tt; // canônico para o lowering/codegen
      for (size_t i = 0; i < st->names.size(); i++) {
        declareVar(st->names[i], tt.tupleElems[i], StoragePolicy::Stack,
                   false, st->line, SymbolKind::LocalVar, false);
        outAssigned_.insert(st->names[i]);
      }
      break;
    }
    case StmtKind::Switch: {
      auto st = static_cast<SwitchStmt*>(s);
      Type stType = checkExpr(st->subject.get());
      if (!stType.isInteger()) error(st->line, "switch exige valor inteiro");
      // atribuição definida: qualquer case pode ser executado (ou nenhum) →
      // depois do switch vale a interseção de todos os caminhos com o anterior
      std::set<std::string> before = outAssigned_;
      std::set<std::string> after = before;
      for (auto& c : st->cases) {
        outAssigned_ = before;
        pushScope();
        for (auto& bodyStmt : c.body) checkStatement(bodyStmt.get(), returnType);
        popScope();
        after = outIntersect(after, outAssigned_);
      }
      outAssigned_ = before;
      pushScope();
      for (auto& bodyStmt : st->defaultBody) checkStatement(bodyStmt.get(), returnType);
      popScope();
      outAssigned_ = outIntersect(after, outAssigned_);
      break;
    }
    case StmtKind::Foreach: {
      auto st = static_cast<ForeachStmt*>(s);
      Type ct = checkExpr(st->collection.get());
      if (ct.kind != Type::Kind::Array && ct.kind != Type::Kind::List) {
        error(st->line, "foreach exige um array ou list");
      }
      if (st->parallel) {
        // `parallel foreach` (v0.22.5): o sujeito é compartilhado pelo
        // ponteiro — array GLOBAL (memória .data visível) ou list LOCAL
        // (objeto heap; o ponteiro é compartilhado pelas tarefas, como o
        // channel). Arrays locais são blob inline na stack → não visíveis.
        if (st->collection->kind != ExprKind::Ident)
          error(st->line, "'parallel foreach' no M2: o sujeito deve ser um "
                          "identificador (array global ou list local)");
        auto id = static_cast<IdentExpr*>(st->collection.get());
        SymbolInfo si;
        Type t;
        StoragePolicy pol;
        bool isC;
        size_t foundScope = 0;
        if (!lookup(id->name, si, t, &pol, &isC, &foundScope))
          error(st->line, "coleção do 'parallel foreach' não encontrada");
        st->collectionName = id->name;
        if (ct.kind == Type::Kind::Array) {
          if (si.kind != SymbolKind::GlobalVar)
            error(st->line, "'parallel foreach' no M2: arrays locais não são "
                            "visíveis às tarefas; use um array global");
          st->subjectIsGlobal = true;
        } else {
          if (si.kind != SymbolKind::LocalVar && si.kind != SymbolKind::Param)
            error(st->line, "'parallel foreach' no M2: o list deve ser local "
                            "(objeto heap) para o ponteiro ser compartilhado");
        }
        // `parallel foreach` aninhado (v0.22.7) é permitido dentro de tarefas
      }
      // atribuição definida para 'out': foreach pode iterar 0 vezes → depois
      // vale o estado anterior; o corpo é analisado a partir dele
      std::set<std::string> feBefore = outAssigned_;
      auto* prevSink = spawnCaptureSink_;
      if (st->parallel) {
        // corpo: mesmos vetos de spawn/parallel (return/break/continue/try/
        // spawn aninhado) e SEM captura: nenhum local do escopo externo é
        // visível (o corpo roda em outras threads; só globais/shared).
        // spawnBaseScope_ = índice do escopo do ITEM (por iteração, permitido)
        inSpawnDepth_++;
        spawnBaseScope_ = scopes_.size();
        spawnCaptureSink_ = nullptr; // leitura de local externo → erro
        inParallelForeach_ = true;
      }
      pushScope();
      if (st->isVar) {
        st->itemType = *ct.elem;
      } else {
        st->itemType = resolveType(st->itemType, st->line);
        if (!isAssignable(st->itemType, *ct.elem)) {
          error(st->line, "tipo do item do foreach incompatível com a coleção");
        }
      }
      declareVar(st->itemName, st->itemType, StoragePolicy::Stack, false, st->line);
      pushScope();
      checkStatement(st->body.get(), returnType);
      popScope();
      popScope();
      if (st->parallel) {
        inParallelForeach_ = false;
        spawnCaptureSink_ = prevSink;
        inSpawnDepth_--;
      }
      outAssigned_ = feBefore;
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Expressões
// ---------------------------------------------------------------------------
Type Semantic::checkExpr(Expr* e, const Type& expected) {
  switch (e->kind) {
    case ExprKind::IntLit: {
      auto ex = static_cast<IntLitExpr*>(e);
      // literal inteiro: bits = 0 significa "cabe em qualquer tipo inteiro"
      // (o alvo define a largura real; evita rejeitar `int<32> x = 5;`)
      ex->exprType = Type::makeInt(0);
      return ex->exprType;
    }
    case ExprKind::FloatLit: {
      auto ex = static_cast<FloatLitExpr*>(e);
      // literal float: bits = 0 = "cabe em qualquer largura de float"
      ex->exprType = Type::makeFloat(0);
      return ex->exprType;
    }
    case ExprKind::StringLit: {
      auto ex = static_cast<StringLitExpr*>(e);
      ex->exprType = Type::makeString();
      return ex->exprType;
    }
    case ExprKind::CharLit: {
      auto ex = static_cast<CharLitExpr*>(e);
      ex->exprType = Type::makeChar();
      return ex->exprType;
    }
    case ExprKind::BoolLit: {
      auto ex = static_cast<BoolLitExpr*>(e);
      ex->exprType = Type::makeBool();
      return ex->exprType;
    }
    case ExprKind::NullLit: {
      auto ex = static_cast<NullLitExpr*>(e);
      ex->exprType = Type::makeVoid();
      return ex->exprType;
    }
    case ExprKind::TupleLit: {
      // M10.1b: `(e1, e2, ...)` — o tipo é a tupla dos tipos dos elementos
      auto ex = static_cast<TupleLitExpr*>(e);
      std::vector<Type> elems;
      for (auto& el : ex->elements) elems.push_back(checkExpr(el.get(), expected));
      Type tt = resolveType(Type::makeTuple(elems), e->line);
      // normaliza literais int/float (bits=0) para os bits do próprio literal
      for (size_t i = 0; i < ex->elements.size(); i++)
        ex->elements[i]->exprType = elems[i];
      ex->exprType = tt;
      return ex->exprType;
    }
    case ExprKind::Spawn: {
      // `task<T> t = spawn { ... }` — payload inferido do `return` do corpo
      auto ex = static_cast<SpawnExpr*>(e);
      inSpawnDepth_++;
      spawnBaseScope_ = scopes_.size();
      Type payload;
      Type* prevPayload = spawnPayload_;
      bool prevSet = spawnPayloadSet_;
      auto* prevSink = spawnCaptureSink_;
      spawnPayload_ = &payload;
      spawnPayloadSet_ = false;
      spawnCaptureSink_ = &ex->captures;
      inSpawnExpr_++;
      // atribuição definida: o snapshot das capturas é tirado no site do spawn
      // → o corpo lê o estado atual; escritas dentro são locais à tarefa
      std::set<std::string> spawnBefore = outAssigned_;
      checkBlock(ex->body.get(), Type::makeVoid());
      outAssigned_ = spawnBefore;
      inSpawnExpr_--;
      bool hasPayload = spawnPayloadSet_;
      inSpawnDepth_--;
      spawnPayload_ = prevPayload;
      spawnPayloadSet_ = prevSet;
      spawnCaptureSink_ = prevSink;
      if (!hasPayload)
        error(e->line, "'spawn' como expressão precisa de um 'return <valor>;' "
                       "no corpo (use 'spawn' de instrução para tarefa sem payload)");
      ex->exprType = Type::makeTask(payload);
      return ex->exprType;
    }
    case ExprKind::Lambda: {
      return checkLambda(static_cast<LambdaExpr*>(e), expected);
    }
    case ExprKind::Await: {
      // `await E` (spec §10): espera a conclusão de E e devolve o payload.
      // Vale em qualquer função (espera bloqueante simples); o modificador
      // `async` é o que roda o corpo numa thread. E pode ser:
      //   - task<T> (tarefa): espera e devolve o payload;
      //   - ch.Receive() (v0.24.0): o Receive já bloqueia até haver valor —
      //     `await` é açúcar sobre o próprio Receive.
      auto ex = static_cast<AwaitExpr*>(e);
      Type t = checkExpr(ex->operand.get());
      bool isReceive = t.kind != Type::Kind::Task &&
                       ex->operand->kind == ExprKind::Call &&
                       static_cast<CallExpr*>(ex->operand.get())->isChannelReceive;
      if (t.kind != Type::Kind::Task && !isReceive)
        error(e->line, "'await' espera uma expressão do tipo 'task<T>' (ou ch.Receive())");
      ex->exprType = isReceive ? t : (t.elem ? *t.elem : Type::makeVoid());
      return ex->exprType;
    }
    case ExprKind::This: {
      auto ex = static_cast<ThisExpr*>(e);
      if (inSpawnDepth_ > 0)
        error(e->line, "'spawn' não captura 'this' no M2");
      if (!currentFunction_ || !currentFunction_->isMethod || currentFunction_->isStatic) {
        error(e->line, "'this' usado fora de método de instância");
      }
      // v0.95: lambda em método captura `this` (ponteiro da instância)
      if (inLambdaDepth_ > 0 && lambdaCaptureSink_) {
        bool dup = false;
        for (auto& c : *lambdaCaptureSink_) {
          if (c.first == "this") { dup = true; break; }
        }
        if (!dup)
          lambdaCaptureSink_->emplace_back("this",
                                           Type::makeClass(currentFunction_->ownerClass));
      }
      ex->exprType = Type::makeClass(currentFunction_->ownerClass);
      return ex->exprType;
    }
    case ExprKind::Ident: {
      auto ex = static_cast<IdentExpr*>(e);
      SymbolInfo si;
      Type t;
      StoragePolicy pol;
      bool isC;
      size_t foundScope = 0;
      if (lookup(ex->name, si, t, &pol, &isC, &foundScope)) {
        // v0.95: lambda captura locais/parâmetros externos POR VALOR
        // (cópia no env no ponto de criação). Sem restrições de thread do
        // spawn (chamada síncrona); arrays locais são rejeitados (o env tem
        // slots de 8 bytes — use heap/global para sequências).
        if (inLambdaDepth_ > 0 && foundScope < lambdaBaseScope_ &&
            (si.kind == SymbolKind::LocalVar || si.kind == SymbolKind::Param)) {
          if (t.kind == Type::Kind::Array)
            error(e->line, "lambda não captura array local '" + ex->name +
                               "' (env de 8 bytes/slot); use 'heap' ou global");
          if (lambdaCaptureSink_) {
            bool dup = false;
            for (auto& c : *lambdaCaptureSink_) {
              if (c.first == ex->name) { dup = true; break; }
            }
            if (!dup) lambdaCaptureSink_->emplace_back(ex->name, t);
          }
        }
        if (inSpawnDepth_ > 0 && foundScope < spawnBaseScope_ &&
            (si.kind == SymbolKind::LocalVar || si.kind == SymbolKind::Param)) {
          if (spawnCaptureSink_) {
            // M11.2 Safety (spec §3): FRAME ESCAPE — arena/pool têm vida útil
            // do frame da função (liberados no epílogo); capturar o PONTEIRO
            // por valor num spawn deixa a task lendo memória morta se a
            // função retornar antes (use-after-free não-determinístico)
            if (pol == StoragePolicy::Arena || pol == StoragePolicy::Pool) {
              error(e->line, "FRAME ESCAPE: '" + ex->name + "' tem política '" +
                                 storagePolicyName(pol) + "' (vida útil do " +
                                 "frame da função) e não pode ser capturada " +
                                 "por 'spawn'/'parallel' — a task pode rodar " +
                                 "depois do retorno; use 'heap'/'shared'");
            }
            // M11.4 Safety (spec §3): arrays locais são blocos NO FRAME do
            // chamador (access violation confirmada em runtime) — nunca
            // atravessam spawn
            if (t.kind == Type::Kind::Array)
              error(e->line, "FRAME ESCAPE: '" + ex->name +
                                 "' é um array local (alocado no frame da " +
                                 "função) e não pode ser capturado por " +
                                 "'spawn'/'parallel'; use uma global 'shared'");
            // M11.4 Safety (spec §3/§10): SENDABLE — classe/list/map
            // capturado por valor compartilha o OBJETO entre as threads
            // (corrida latente); exige capacidade sendable ou política
            // `shared` (intenção explícita de compartilhar)
            if ((t.kind == Type::Kind::Class || t.kind == Type::Kind::List ||
                 t.kind == Type::Kind::Map) &&
                pol != StoragePolicy::Shared && !isSendableType(t)) {
              std::string tyName =
                  t.kind == Type::Kind::Class ? t.name : typeKey(t);
              error(e->line, "NOT SENDABLE: '" + ex->name + "' (" + tyName +
                                 ") é capturado por 'spawn' mas não pode " +
                                 "atravessar threads com segurança — derive " +
                                 "'Sendable' na classe ou declare-a como " +
                                 "'shared'");
            }
            // captura por valor: copia do local no momento do spawn para o env
            bool dup = false;
            for (auto& c : *spawnCaptureSink_) {
              if (c.first == ex->name) { dup = true; break; }
            }
            if (!dup) spawnCaptureSink_->emplace_back(ex->name, t);
          } else {
            if (inParallelForeach_)
              error(e->line, "'parallel foreach' no M2 não acessa variáveis "
                             "locais do escopo externo; use globais (ou "
                             "'threadlocal'/'atomic')");
            error(e->line, "'spawn' não captura variáveis locais no M2; "
                           "use uma variável 'global' (ou 'shared')");
          }
        }
        // atribuição definida para 'out' (SPEC §5.2): ler antes de atribuir em
        // todos os caminhos é erro; `=` puro e argumento `out` não leem (nome
        // em outWriteName_ durante o checkExpr correspondente)
        if (isOutParam(si) && si.name != outWriteName_) outReadCheck(e->line, si.name);
        // M10.4 Safety (spec §3): USE AFTER MOVE — referência cuja posse foi
        // transferida por atribuição não pode mais ser lida
        if (!inAssignLhs_ && si.kind == SymbolKind::LocalVar &&
            movedVars_.count(ex->name))
          error(e->line, "USE AFTER MOVE: '" + ex->name +
                             "' teve sua posse transferida e não pode mais ser "
                             "usada");
        ex->symbol = si;
        ex->exprType = t;
        return t;
      }
      // módulo (nome declarado ou alias)? permite 'Mod.membro'
      std::string mk = moduleKey(ex->name);
      if (!mk.empty()) {
        ex->exprType = Type::makeModule(mk);
        return ex->exprType;
      }
      // prefixo de namespace: algo da forma 'A.B' começa com 'A.'
      if (isModulePrefix(ex->name)) {
        ex->exprType = Type::makeModule(ex->name);
        return ex->exprType;
      }
      // alias de tipo (`using X = Tipo;`): 'X.RUNNING', 'X.ALGO', etc.
      auto ait = typeAliases_.find(ex->name);
      if (ait != typeAliases_.end()) {
        ex->exprType = resolveType(ait->second, ex->line);
        return ex->exprType;
      }
// campo do método sem qualificação (spec §19: `return vida;` em método
      // de instância → `this.vida`; herança incluída)
      if (currentFunction_ && currentFunction_->isMethod && !currentFunction_->isStatic) {
        if (inSpawnDepth_ > 0)
          error(e->line, "'spawn' não captura 'this' no M2; use uma variável global");
        auto it = classes_.find(currentFunction_->ownerClass);
        if (it != classes_.end()) {
          ClassInfo* cix = &it->second;
          while (true) {
            auto fit = cix->fields.find(ex->name);
            if (fit != cix->fields.end()) {
              // v0.46: visibilidade private/protected (campo herdado
              // privado continua inacessível na derivada)
              Access bacc = Access::Public;
              auto bait = cix->fieldAccess.find(ex->name);
              if (bait != cix->fieldAccess.end()) bacc = bait->second;
              if (!canAccessMember(*cix, bacc))
                error(e->line, "campo '" + ex->name + "' não é acessível aqui (" +
                                   (bacc == Access::Private ? "privado" : "protegido") +
                                   ")");
              SymbolInfo fs;
              fs.kind = SymbolKind::Field;
              fs.name = ex->name;
              fs.ownerClass = cix->decl;
              fs.slotIndex = fit->second.second; // offset no objeto
              fs.atomic = cix->fieldAtomic.count(ex->name)
                              ? cix->fieldAtomic[ex->name] : false;
              ex->symbol = fs;
              ex->exprType = fit->second.first;
              return ex->exprType;
            }
            if (cix->base.empty()) break;
            auto bit = classes_.find(cix->base);
            if (bit == classes_.end()) break;
            cix = &bit->second;
          }
          // propriedade sem qualificação (spec §19): usa o getter/setter
          PropInfo* pr = findProperty(it->second, ex->name);
          if (pr) {
            ex->isProperty = true;
            ex->propType = pr->type;
            ex->propGet = pr->getter;
            ex->propSet = pr->setter;
            if (!pr->getter && !inAssignLhs_)
              error(e->line, "propriedade '" + ex->name +
                                 "' não possui 'get' (somente escrita)");
            ex->exprType = pr->type;
            return ex->exprType;
          }
        }
      }
      // variável de OUTRA parte do `parallel` (v0.22.8): cada parte é uma
      // tarefa com escopo próprio — a pilha registra nome → parte por nível
      if (!parallelPartStack_.empty()) {
        for (size_t lvl = parallelSiblings_.size(); lvl-- > 0;) {
          auto sit = parallelSiblings_[lvl].find(ex->name);
          if (sit != parallelSiblings_[lvl].end() &&
              sit->second != (int)parallelPartStack_[lvl])
            error(e->line, "variável '" + ex->name + "' pertence a OUTRA parte do "
                           "`parallel` (cada parte tem escopo próprio de tarefa; "
                           "declare-a num bloco { } ou use uma variável global)");
        }
      }
      error(e->line, "símbolo desconhecido '" + ex->name + "'");
    }
    case ExprKind::Index: {
      auto ex = static_cast<IndexExpr*>(e);
      Type ot = checkExpr(ex->object.get());
      if (ot.kind == Type::Kind::String) {
        // M: indexação de string `s[i]` — retorna um char (bug 1.8)
        Type it = checkExpr(ex->index.get());
        if (!it.isInteger()) error(e->line, "índice de string deve ser inteiro");
        ex->exprType = Type::makeChar();
        return ex->exprType;
      }
      if (ot.kind != Type::Kind::Array && ot.kind != Type::Kind::List) {
        error(e->line, "indexação exige um array ou list");
      }
      Type it = checkExpr(ex->index.get());
      if (!it.isInteger()) error(e->line, "índice de array/lis deve ser inteiro");
      ex->exprType = *ot.elem;
      return ex->exprType;
    }
    case ExprKind::ArrayLit: {
      auto ex = static_cast<ArrayLitExpr*>(e);
      if (ex->elements.empty()) {
        error(e->line, "array literal vazio exige tipo explícito (ex.: int[0])");
      }
      Type et = checkExpr(ex->elements[0].get());
      for (size_t i = 1; i < ex->elements.size(); i++) {
        Type t2 = checkExpr(ex->elements[i].get());
        if (!isAssignable(et, t2) && !isAssignable(t2, et)) {
          error(ex->elements[i]->line, "elementos do array literal com tipos incompatíveis");
        }
      }
      ex->exprType = Type::makeArray(et, (int)ex->elements.size());
      return ex->exprType;
    }
    case ExprKind::Member: {
      auto ex = static_cast<MemberExpr*>(e);
      // `Task.IsCancelled` (v0.23.0): pseudomembro da runtime — sinal de
      // cancelamento cooperativo da tarefa corrente. O Ident "Task" não é um
      // valor do programa; tratado antes de qualquer resolução de objeto.
      if (ex->member == "IsCancelled" && ex->object->kind == ExprKind::Ident) {
        auto* id = static_cast<IdentExpr*>(ex->object.get());
        if (id->name == "Task") {
          if (inAssignLhs_)
            error(e->line, "Task.IsCancelled é somente leitura");
          ex->isTaskCancelled = true;
          ex->exprType = Type::makeInt(64);
          return ex->exprType;
        }
      }
      Type objType = checkExpr(ex->object.get());
      // M11.7: acesso direto a campo em possivelmente-null
      checkNullDeref(ex->object.get(), objType, e->line);
      // M11.9: acesso a objeto de arena resetada
      checkArenaUaf(ex->object.get(), e->line);
      // M11.8: TOCTOU — leitura pós-lock de objeto com leitura vencida
      noteLockAccess(ex->object.get(), e->line);
      if (objType.kind == Type::Kind::Module) {
        // sub-módulo? (ex.: 'A.B' quando 'A.B.C' é um módulo) �?? continua a cadeia
        if (!objType.name.empty() && moduleKey(objType.name + "." + ex->member).size() > 0) {
          ex->isModuleTypeRef = true;
          ex->exprType = Type::makeModule(moduleKey(objType.name + "." + ex->member));
          return ex->exprType;
        }
        // Mod.membro: classe/enum (tipo) ou variável global
        Type mt = moduleTypeMember(objType.name, ex->member, e->line);
        if (mt.kind != Type::Kind::Void) {
          ex->isModuleTypeRef = true;
          ex->exprType = mt;
          return mt;
        }
        VarDecl* gv = moduleGlobalVar(objType.name, ex->member, e->line);
        if (gv) {
          ex->isGlobalRef = true;
          ex->resolvedGlobal = gv;
          ex->exprType = resolveType(gv->type, e->line);
          return ex->exprType;
        }
        error(e->line, "membro '" + ex->member + "' não existe no módulo '" +
                           objType.name + "'");
      }
      if (objType.kind == Type::Kind::Array) {
        if (ex->member == "Length") {
          ex->isArrayLength = true;
          ex->enumValue = objType.arraySize;
          // v0.45.2: `int` da plataforma (32) — antes era i64 e `int n =
          // arr.Length;` falhava (não há estreitamento implícito)
          ex->exprType = Type::makeInt(32);
          return ex->exprType;
        }
        error(e->line, "arrays possuem apenas o membro 'Length'");
      }
      if (objType.kind == Type::Kind::List) {
        if (ex->member == "Length") {
          ex->isListLength = true;
          ex->exprType = Type::makeInt(32);
          return ex->exprType;
        }
        error(e->line, "lists possuem apenas 'Add(x)' (chamada) e 'Length'");
      }
      if (objType.kind == Type::Kind::Map) {
        if (ex->member == "Length") {
          ex->isMapLength = true;
          ex->exprType = Type::makeInt(32);
          return ex->exprType;
        }
        error(e->line, "maps possuem apenas 'Put(k,v)', 'Get(k)', 'Contains(k)', 'Remove(k)', 'Clear()' e 'Length'");
      }
      if (objType.kind == Type::Kind::Task) {
        if (ex->member == "Wait") {
          ex->isWait = true;
          ex->exprType = objType.elem ? *objType.elem : Type::makeVoid();
          return ex->exprType;
        }
        error(e->line, "tasks possuem apenas o membro 'Wait()' (chamada)");
      }
      if (objType.kind == Type::Kind::Channel) {
        error(e->line, "channels possuem apenas 'Send(x)' e 'Receive()' (chamadas)");
      }
      if (objType.kind == Type::Kind::Option) {
        if (ex->member == "HasValue" || ex->member == "IsSome") {
          ex->isOptionHasValue = true;
          ex->exprType = Type::makeBool();
          return ex->exprType;
        }
        if (ex->member == "IsNone") {
          ex->isOptionIsNone = true;
          ex->exprType = Type::makeBool();
          return ex->exprType;
        }
        if (ex->member == "Value") {
          ex->isOptionValue = true;
          ex->exprType = objType.elem ? *objType.elem : Type::makeVoid();
          return ex->exprType;
        }
        error(e->line, "options possuem apenas os membros 'HasValue', 'IsSome', 'IsNone' e 'Value'");
      }
      if (objType.kind == Type::Kind::Result) {
        if (ex->member == "IsOk") {
          ex->isResultIsOk = true;
          ex->exprType = Type::makeBool();
          return ex->exprType;
        }
        if (ex->member == "IsError" || ex->member == "IsErr") {
          ex->isResultIsError = true;
          ex->exprType = Type::makeBool();
          return ex->exprType;
        }
        if (ex->member == "Value") {
          ex->isResultValue = true;
          ex->exprType = objType.elem ? *objType.elem : Type::makeVoid();
          return ex->exprType;
        }
        if (ex->member == "Error") {
          ex->isResultError = true;
          ex->exprType = objType.elem2 ? *objType.elem2 : Type::makeVoid();
          return ex->exprType;
        }
        error(e->line, "results possuem apenas os membros 'IsOk', 'IsError', 'IsErr', 'Value' e 'Error'");
      }
      if (objType.kind == Type::Kind::Mutex ||
          objType.kind == Type::Kind::Semaphore ||
          objType.kind == Type::Kind::Event ||
          objType.kind == Type::Kind::Barrier) {
        error(e->line, "primitiva de sincronização só expõe métodos (chamadas)");
      }
      if (objType.kind == Type::Kind::Enum) {
        auto en = enums_[objType.name];
        for (auto& entry : en->entries) {
          if (entry.name == ex->member) {
            if (en->anyPayload) {
              // enum rico: mesmo sem payload, o valor é uma célula alocada
              ex->isEnumCtor = true;
              ex->enumCtorEnum = objType.name;
              ex->enumCtorIndex = (int)(&entry - en->entries.data());
              ex->exprType = objType;
            } else {
              ex->isEnumConst = true;
              ex->enumValue = entry.value;
              ex->exprType = Type::makeInt(0); // literal: cabe em qualquer inteiro
            }
            return ex->exprType;
          }
        }
        error(e->line, "variante '" + ex->member + "' não existe no enum '" + objType.name + "'");
      }
      if (objType.kind == Type::Kind::Class) {
        auto& ci = classes_[objType.name];
        if (ci.decl->isActor) {
          // encapsulamento do actor: campos só dentro dos métodos do próprio
          // actor (construtor incluído); de fora, somente via métodos
          bool inside =
              currentFunction_ && currentFunction_->ownerClass == objType.name;
          if (!inside)
            error(e->line, "campo do actor '" + objType.name + "' só é acessível "
                           "de dentro do actor (use método)");
        }
        int offset = -1;
        Type ft = typeOfMember(ci, ex->member, &offset);
        if (ft.kind != Type::Kind::Void) {
          // v0.46: visibilidade private/protected do campo
          ClassInfo* fo = findFieldOwner(ci, ex->member);
          Access facc = Access::Public;
          if (fo) {
            auto fait = fo->fieldAccess.find(ex->member);
            if (fait != fo->fieldAccess.end()) facc = fait->second;
          } else {
            fo = &ci;
          }
          if (!canAccessMember(*fo, facc))
            error(e->line, "campo '" + ex->member + "' de '" + objType.name +
                               "' não é acessível aqui (" +
                               (facc == Access::Private ? "privado" : "protegido") +
                               ")");
        }
        if (ft.kind == Type::Kind::Void) {
          // propriedade? (spec §19) — o getter é opcional: só leitura
          // exige; alvo de atribuição é validado em checkAssign
          PropInfo* pr = findProperty(ci, ex->member);
          if (pr) {
            // v0.46: visibilidade private/protected da propriedade
            FunctionDecl* acc = pr->getter ? pr->getter : pr->setter;
            if (acc) {
              auto pit = classes_.find(acc->ownerClass);
              if (pit != classes_.end() && !canAccessMember(pit->second, pr->access))
                error(e->line, "propriedade '" + ex->member + "' de '" +
                                   objType.name + "' não é acessível aqui (" +
                                   (pr->access == Access::Private ? "privada"
                                                                  : "protegida") +
                                   ")");
            }
            ex->isProperty = true;
            ex->propType = pr->type;
            ex->exprType = pr->type;
            ex->propGet = pr->getter;
            ex->propSet = pr->setter;
            if (!pr->getter && !inAssignLhs_)
              error(e->line, "propriedade '" + ex->member + "' não possui 'get' (somente escrita)");
            return pr->type;
          }
          error(e->line, "membro '" + ex->member + "' não existe em '" + objType.name + "'");
        }
        ex->fieldOffset = offset;
        ex->fieldAtomic = isFieldAtomic(ci, ex->member);
        ex->exprType = ft;
        return ft;
      }
      error(e->line, "acesso a membro em tipo não-classe");
    }
    case ExprKind::Call:
      return checkCall(static_cast<CallExpr*>(e));
    case ExprKind::Binary: {
      auto ex = static_cast<BinaryExpr*>(e);
      Type lt = checkExpr(ex->lhs.get());
      Type rt = checkExpr(ex->rhs.get());

      if (ex->op == BinOp::And || ex->op == BinOp::Or) {
        if (lt.kind != Type::Kind::Bool || rt.kind != Type::Kind::Bool) {
          error(e->line, "operador lógico exige bool");
        }
        ex->exprType = Type::makeBool();
        return ex->exprType;
      }
      if (ex->op == BinOp::Eq || ex->op == BinOp::Ne) {
        // M14.4: igualdade de STRING é por CONTEÚDO (hphl_str_eq) — antes
        // comparava ponteiros (só funcionava para literais internados)
        if (lt.kind == Type::Kind::String && rt.kind == Type::Kind::String) {
          ex->strEqBin = true;
          ex->exprType = Type::makeBool();
          return ex->exprType;
        }
        // derive Equatable (v0.25.0): struct/class com o mesmo tipo pode ser
        // comparado campo a campo pela função equ_<canon> sintetizada
        if (lt.kind == Type::Kind::Class && rt.kind == Type::Kind::Class &&
            lt.name == rt.name) {
          auto cit = classes_.find(lt.name);
          if (cit != classes_.end() && cit->second.equFn) {
            ex->derivedEq = cit->second.equFn;
            ex->exprType = Type::makeBool();
            return ex->exprType;
          }
        }
        // ponteiros/nulos podem ser comparados com qualquer coisa do mesmo grupo
        bool ok = isAssignable(lt, rt) || isAssignable(rt, lt);
        if (!ok) error(e->line, "tipos incompatíveis na comparação");
        ex->exprType = Type::makeBool();
        return ex->exprType;
      }
      if (ex->op == BinOp::Lt || ex->op == BinOp::Gt || ex->op == BinOp::Le ||
          ex->op == BinOp::Ge) {
        // derive Comparable (v0.25.0): struct/class do mesmo tipo ordenam via
        // cmp_<canon> sintetizada (devolve <0, 0, >0)
        if (lt.kind == Type::Kind::Class && rt.kind == Type::Kind::Class &&
            lt.name == rt.name) {
          auto cit = classes_.find(lt.name);
          if (cit != classes_.end() && cit->second.cmpFn) {
            ex->derivedCmp = cit->second.cmpFn;
            ex->exprType = Type::makeBool();
            return ex->exprType;
          }
        }
        // `derive Comparable` — enum simples pode usar <, >, <=, >=
        bool ltCmp = lt.kind == Type::Kind::Enum && derived(lt.name, "Comparable") &&
                     rt.isInteger();
        bool rtCmp = rt.kind == Type::Kind::Enum && derived(rt.name, "Comparable") &&
                     lt.isInteger();
        if (ltCmp || rtCmp) {
          ex->exprType = Type::makeBool();
          return ex->exprType;
        }
        if (!isNumericBinary(lt, rt)) error(e->line, "comparação exige números");
        ex->exprType = Type::makeBool();
        return ex->exprType;
      }
      if (ex->op == BinOp::Shl || ex->op == BinOp::Shr) {
        if (!lt.isInteger() || !rt.isInteger()) error(e->line, "deslocamento exige inteiros");
        ex->exprType = lt;
        return ex->exprType;
      }
      if (ex->op == BinOp::Add && (lt.kind == Type::Kind::String || rt.kind == Type::Kind::String)) {
        // M5 (v0.36.0): coer��o � o operando n�o-string vira __hphl_to_str(...)
        // (reescrita do AST; os backends s� veem strings no concat). O
        // operando j� foi checkExpr'ado � a valida��o � feita aqui para n�o
        // re-checkar a sub�rvore.
        auto wrap = [&](std::unique_ptr<Expr>& operand, const Type& ot, bool isLhs) {
          auto callee = std::make_unique<IdentExpr>();
          callee->name = "__hphl_to_str";
          callee->line = operand->line;
          callee->exprType = Type::makeString();
          auto call = std::make_unique<CallExpr>();
          call->line = operand->line;
          call->callee = std::move(callee);
          call->args.push_back(std::move(operand));
          call->isToStr = true;
          call->exprType = Type::makeString();
          if (!(ot.isInteger() || ot.kind == Type::Kind::Float ||
                ot.kind == Type::Kind::Bool || ot.kind == Type::Kind::Char ||
                ot.kind == Type::Kind::UInt))
            error(e->line, "concatena��o aceita apenas int, uint, float, bool ou "
                           "char ao lado de string (M5)");
          operand = std::move(call);
        };
        if (lt.kind != Type::Kind::String) wrap(ex->lhs, lt, true);
        if (rt.kind != Type::Kind::String) wrap(ex->rhs, rt, false);
        ex->exprType = Type::makeString();
        return ex->exprType;
      }
      if (!isNumericBinary(lt, rt)) error(e->line, "operador aritmético exige números");
      ex->exprType = commonNumeric(lt, rt);
      return ex->exprType;
    }
    case ExprKind::Unary: {
      auto ex = static_cast<UnaryExpr*>(e);
      Type t = checkExpr(ex->operand.get());
      switch (ex->op) {
        case UnOp::Neg:
          if (!t.isNumeric()) error(e->line, "negação exige número");
          ex->exprType = t;
          break;
        case UnOp::Not:
          if (t.kind != Type::Kind::Bool) error(e->line, "'!' exige bool");
          ex->exprType = Type::makeBool();
          break;
        case UnOp::BitNot:
          if (!t.isInteger()) error(e->line, "'~' exige inteiro");
          ex->exprType = t;
          break;
        case UnOp::PreInc: case UnOp::PreDec:
        case UnOp::PostInc: case UnOp::PostDec:
          if (!t.isInteger()) error(e->line, "++/-- exige inteiro");
          if (ex->operand->kind != ExprKind::Ident && ex->operand->kind != ExprKind::Member &&
              ex->operand->kind != ExprKind::Index) {
            error(e->line, "++/-- exige lvalue");
          }
          if (ex->operand->kind == ExprKind::Member &&
              static_cast<MemberExpr*>(ex->operand.get())->isProperty)
            error(e->line, "++/-- em propriedade não suportado; use 'Prop = Prop + 1'");
          // ++/-- escreve (e lê): o parâmetro 'out' passa a estar atribuído
          if (ex->operand->kind == ExprKind::Ident) {
            auto oid = static_cast<IdentExpr*>(ex->operand.get());
            if (isOutParam(oid->symbol)) outMarkAssigned(oid->name);
          }
          // M11.1 Safety (spec §3/§10): ++/-- é escrita composta — global
          // sem shared/threadlocal/atomic dentro de spawn é THREAD RACE
          checkGlobalRaceWrite(e->line, ex->operand.get());
          ex->exprType = t;
          break;
      }
      return ex->exprType;
    }
    case ExprKind::Assign:
      return checkAssign(static_cast<AssignExpr*>(e));
    case ExprKind::Ternary: {
      auto ex = static_cast<TernaryExpr*>(e);
      Type ct = checkExpr(ex->cond.get());
      if (ct.kind != Type::Kind::Bool) error(e->line, "condição do ternário deve ser bool");
      // M11.7: polaridade null no ternário (`s == null ? a : s`) — cada braço
      // é analisado com o estado correto; depois, merge por interseção
      std::string ncName;
      int ncp = nullCondPolarity(ex->cond.get(), ncName);
      std::set<std::string> maybeBase = maybeNull_;
      std::set<std::string> maybeThen = maybeBase;
      std::set<std::string> maybeElse = maybeBase;
      if (ncp && maybeNull_.count(ncName)) {
        if (ncp == 1) { maybeThen.erase(ncName); maybeElse.insert(ncName); }
        else { maybeThen.insert(ncName); maybeElse.erase(ncName); }
      }
      maybeNull_ = maybeThen;
      Type at = checkExpr(ex->thenExpr.get());
      std::set<std::string> thenEnd = maybeNull_;
      maybeNull_ = maybeElse;
      Type bt = checkExpr(ex->elseExpr.get());
      std::set<std::string> elseEnd = maybeNull_;
      std::set<std::string> merged;
      for (auto& m : thenEnd)
        if (elseEnd.count(m)) merged.insert(m);
      maybeNull_ = merged;
      if (isAssignable(at, bt) || isAssignable(bt, at)) {
        ex->exprType = at.kind == Type::Kind::Void ? bt : at;
      } else if (isNumericBinary(at, bt)) {
        ex->exprType = commonNumeric(at, bt);
      } else {
        error(e->line, "tipos incompatíveis no ternário");
      }
      return ex->exprType;
    }
    case ExprKind::Cast: {
      auto ex = static_cast<CastExpr*>(e);
      Type operandType = checkExpr(ex->operand.get());
      Type target = resolveType(ex->target, e->line);
      if (target.kind == Type::Kind::Void) error(e->line, "cast para void inválido");
      if (operandType.kind == Type::Kind::Enum && isRichEnum(operandType.name) &&
          target.isInteger())
        error(e->line, "cast de enum com dados para inteiro não suportado; use match");
      // `(int)Enum.Const` �?? enum simples: o valor é o inteiro da variante
      if (operandType.kind == Type::Kind::Enum && target.isInteger()) {
        ex->exprType = target;
        return target;
      }
      if (!target.isNumeric() && !operandType.isNumeric() &&
          !(target.isPointer() && operandType.isPointer())) {
        error(e->line, "cast inválido entre estes tipos");
      }
      ex->exprType = target;
      return target;
    }
    case ExprKind::New: {
      auto ex = static_cast<NewExpr*>(e);
      checkNew(ex);
      if (ex->isChannel || ex->isList || ex->isArrayNew) return ex->exprType;
      if (ex->exprType.kind != Type::Kind::Void && ex->exprType.kind != Type::Kind::Class &&
          ex->exprType.kind != Type::Kind::Unknown)
        return ex->exprType; // `new int(0)` — primitivo na heap
      ex->exprType = Type::makeClass(ex->className);
      return ex->exprType;
    }
    case ExprKind::Match:
      return checkMatch(static_cast<MatchExpr*>(e), expected);
    case ExprKind::OptMember:
      return checkOptMember(static_cast<OptMemberExpr*>(e));
    case ExprKind::OptIndex:
      return checkOptIndex(static_cast<OptIndexExpr*>(e));
    case ExprKind::OptCtor:
      return checkOptCtor(static_cast<OptCtorExpr*>(e), expected);
    case ExprKind::Try:
      return checkTry(static_cast<TryExpr*>(e));
    case ExprKind::Coalesce:
      return checkCoalesce(static_cast<CoalesceExpr*>(e));
  }
  error(e->line, "expressão inválida");
}

// ---------------------------------------------------------------------------
// v0.95 (lambdas)
// ---------------------------------------------------------------------------
namespace {
// nome legível p/ mensagens de erro de inferência (sem dependência do HIR)
std::string lambdaTypeName(const Type& t) {
  switch (t.kind) {
    case Type::Kind::Void: return "void";
    case Type::Kind::Int: return "int";
    case Type::Kind::UInt: return "uint";
    case Type::Kind::Float: return "float";
    case Type::Kind::Bool: return "bool";
    case Type::Kind::Char: return "char";
    case Type::Kind::String: return "string";
    case Type::Kind::Ptr: return "ptr"; // FFI v2
    case Type::Kind::Func: {
      std::string s = "func<";
      s += t.elem ? lambdaTypeName(*t.elem) : std::string("void");
      for (auto& p : t.genericArgs) s += ", " + lambdaTypeName(p);
      return s + ">";
    }
    case Type::Kind::Unknown: return "?";
    default: return "?";
  }
}
} // namespace

void Semantic::collectLambdaParams(Expr* e,
                                    const std::map<std::string, size_t>& paramIdx,
                                    const std::set<std::string>& shadow,
                                    std::set<std::string>& out) {
  if (!e) return;
  switch (e->kind) {
    case ExprKind::Ident: {
      auto id = static_cast<IdentExpr*>(e);
      if (!shadow.count(id->name) && paramIdx.count(id->name))
        out.insert(id->name);
      break;
    }
    case ExprKind::Lambda: {
      auto lam = static_cast<LambdaExpr*>(e);
      std::set<std::string> inner = shadow;
      for (auto& p : lam->params) inner.insert(p.name);
      // corpo pode ser BlockStmt embrulhado; percorre stmts
      if (lam->body) {
        for (auto& s : lam->body->stmts) {
          if (s->kind == StmtKind::ExprStmt) {
            auto es = static_cast<ExprStmt*>(s.get());
            collectLambdaParams(es->expr.get(), paramIdx, inner, out);
          } else if (s->kind == StmtKind::Return) {
            auto rs = static_cast<ReturnStmt*>(s.get());
            if (rs->value) collectLambdaParams(rs->value.get(), paramIdx, inner, out);
          }
        }
      }
      break;
    }
    case ExprKind::Binary: {
      auto b = static_cast<BinaryExpr*>(e);
      collectLambdaParams(b->lhs.get(), paramIdx, shadow, out);
      collectLambdaParams(b->rhs.get(), paramIdx, shadow, out);
      break;
    }
    case ExprKind::Unary: {
      auto u = static_cast<UnaryExpr*>(e);
      collectLambdaParams(u->operand.get(), paramIdx, shadow, out);
      break;
    }
    case ExprKind::Ternary: {
      auto t = static_cast<TernaryExpr*>(e);
      collectLambdaParams(t->cond.get(), paramIdx, shadow, out);
      collectLambdaParams(t->thenExpr.get(), paramIdx, shadow, out);
      collectLambdaParams(t->elseExpr.get(), paramIdx, shadow, out);
      break;
    }
    case ExprKind::Call: {
      auto c = static_cast<CallExpr*>(e);
      collectLambdaParams(c->callee.get(), paramIdx, shadow, out);
      for (auto& a : c->args) collectLambdaParams(a.get(), paramIdx, shadow, out);
      break;
    }
    case ExprKind::Assign: {
      auto a = static_cast<AssignExpr*>(e);
      collectLambdaParams(a->target.get(), paramIdx, shadow, out);
      collectLambdaParams(a->value.get(), paramIdx, shadow, out);
      break;
    }
    case ExprKind::Index: {
      auto ix = static_cast<IndexExpr*>(e);
      collectLambdaParams(ix->object.get(), paramIdx, shadow, out);
      collectLambdaParams(ix->index.get(), paramIdx, shadow, out);
      break;
    }
    case ExprKind::Member: {
      auto m = static_cast<MemberExpr*>(e);
      collectLambdaParams(m->object.get(), paramIdx, shadow, out);
      break;
    }
    case ExprKind::OptMember: {
      auto m = static_cast<OptMemberExpr*>(e);
      collectLambdaParams(m->object.get(), paramIdx, shadow, out);
      break;
    }
    case ExprKind::Cast: {
      auto c = static_cast<CastExpr*>(e);
      collectLambdaParams(c->operand.get(), paramIdx, shadow, out);
      break;
    }
    default: break;
  }
}

Type Semantic::checkFuncCall(CallExpr* c, const Type& funcType) {
  if (c->args.size() != funcType.genericArgs.size()) {
    error(c->line, "lambda espera " + std::to_string(funcType.genericArgs.size()) +
                       " argumento(s), recebeu " + std::to_string(c->args.size()));
  }
  if (!c->genericArgs.empty())
    error(c->line, "chamada de lambda não aceita argumentos de tipo <...>");
  for (size_t i = 0; i < c->args.size(); i++) {
    Type pt = resolveType(funcType.genericArgs[i], c->line);
    Type at = checkExpr(c->args[i].get(), pt);
    if (!isAssignable(pt, at)) {
      error(c->args[i]->line,
            "argumento " + std::to_string(i + 1) + " incompatível: esperado '" +
                lambdaTypeName(pt) + "'");
    }
  }
  c->isFuncCall = true;
  c->funcType = funcType;
  c->exprType = funcType.elem ? resolveType(*funcType.elem, c->line)
                              : Type::makeVoid();
  return c->exprType;
}

void Semantic::lambdaConstraints(Expr* e, const Expr* parent,
                                 const std::map<std::string, size_t>& paramIdx,
                                 const std::set<std::string>& shadow,
                                 std::vector<std::vector<Type>>& out) {
  if (!e) return;
  // tipo conhecido de um irmão literal ou variável já tipada
  auto knownTypeOf = [&](Expr* x) -> Type {
    if (!x) return Type();
    switch (x->kind) {
      // literais sem sufixo: padrão da plataforma (igual a `int`/`float`)
      case ExprKind::IntLit: return Type::makeInt(32);
      case ExprKind::FloatLit: return Type::makeFloat(32);
      case ExprKind::StringLit: return Type::makeString();
      case ExprKind::BoolLit: return Type::makeBool();
      case ExprKind::CharLit: return Type::makeChar();
      case ExprKind::Ident: {
        auto id = static_cast<IdentExpr*>(x);
        if (shadow.count(id->name)) return Type();
        SymbolInfo si;
        Type t;
        if (lookup(id->name, si, t) && t.kind != Type::Kind::Unknown) return t;
        return Type();
      }
      default: return Type();
    }
  };
  auto constrain = [&](Expr* x, const Type& t) {
    if (x->kind != ExprKind::Ident) return;
    auto id = static_cast<IdentExpr*>(x);
    if (shadow.count(id->name)) return;
    auto it = paramIdx.find(id->name);
    if (it != paramIdx.end() && t.kind != Type::Kind::Unknown)
      out[it->second].push_back(t);
  };
  switch (e->kind) {
    case ExprKind::Ident: {
      if (!parent) break;
      auto id = static_cast<IdentExpr*>(e);
      if (shadow.count(id->name)) break;
      if (!paramIdx.count(id->name)) break;
      if (parent->kind == ExprKind::Binary) {
        auto b = static_cast<const BinaryExpr*>(parent);
        Expr* sib = (b->lhs.get() == e) ? b->rhs.get() : b->lhs.get();
        constrain(e, knownTypeOf(sib));
        // v0.95: operando aritmético sem tipo conhecido defaulta para `int`
        // (igual ao `int` da plataforma); a resolução pelo mais largo decide
        // (`(a,b) => a*b` → int,int; `(x) => x+1.5` → float). Irmão string
        // (concat) não gera marcador — senão `(s) => s + "x"` conflitaria.
        if (b->op == BinOp::Add || b->op == BinOp::Sub || b->op == BinOp::Mul ||
            b->op == BinOp::Div || b->op == BinOp::Mod ||
            b->op == BinOp::BitAnd || b->op == BinOp::BitOr ||
            b->op == BinOp::BitXor || b->op == BinOp::Shl || b->op == BinOp::Shr) {
          Type sibT = knownTypeOf(sib);
          if (sibT.kind != Type::Kind::String) {
            auto it = paramIdx.find(id->name);
            if (it != paramIdx.end()) out[it->second].push_back(Type::makeInt(32));
          }
        }
      } else if (parent->kind == ExprKind::Unary) {
        auto u = static_cast<const UnaryExpr*>(parent);
        if (u->op == UnOp::Not) constrain(e, Type::makeBool());
      } else if (parent->kind == ExprKind::Ternary) {
        auto t = static_cast<const TernaryExpr*>(parent);
        if (t->cond.get() == e) constrain(e, Type::makeBool());
      } else if (parent->kind == ExprKind::Assign) {
        auto a = static_cast<const AssignExpr*>(parent);
        // `x = v` restringe x ao tipo de v (quando conhecido)
        if (a->target.get() == e) constrain(e, knownTypeOf(a->value.get()));
      } else if (parent->kind == ExprKind::Index) {
        auto ix = static_cast<const IndexExpr*>(parent);
        if (ix->index.get() == e) constrain(e, Type::makeInt(64));
      }
      break;
    }
    case ExprKind::Lambda: {
      // desce com shadowing dos próprios parâmetros (não restringem o exterior,
      // mas refs a params externos dentro restringem)
      auto lam = static_cast<LambdaExpr*>(e);
      std::set<std::string> inner = shadow;
      for (auto& p : lam->params) inner.insert(p.name);
      lambdaConstraintsStmt(lam->body.get(), paramIdx, inner, out);
      break;
    }
    case ExprKind::Binary: {
      auto b = static_cast<BinaryExpr*>(e);
      lambdaConstraints(b->lhs.get(), e, paramIdx, shadow, out);
      lambdaConstraints(b->rhs.get(), e, paramIdx, shadow, out);
      // v0.95: propaga tipo numérico conhecido para params da subárvore
      // oposta em ops aritméticas (`(a*b)+off` com off:int → a,b:int).
      // Só numerics (concat com string coage qualquer coisa: sem propagação).
      if (b->op == BinOp::Add || b->op == BinOp::Sub || b->op == BinOp::Mul ||
          b->op == BinOp::Div || b->op == BinOp::Mod ||
          b->op == BinOp::BitAnd || b->op == BinOp::BitOr ||
          b->op == BinOp::BitXor || b->op == BinOp::Shl || b->op == BinOp::Shr) {
        Type lt = knownTypeOf(b->lhs.get());
        Type rt = knownTypeOf(b->rhs.get());
        bool lok = lt.kind != Type::Kind::Unknown && lt.isNumeric();
        bool rok = rt.kind != Type::Kind::Unknown && rt.isNumeric();
        if (lok != rok) {
          // coleta params da subárvore desconhecida
          std::set<std::string> sub;
          collectLambdaParams(lok ? b->rhs.get() : b->lhs.get(), paramIdx,
                              shadow, sub);
          const Type& t = lok ? lt : rt;
          for (auto& nm : sub) {
            auto it = paramIdx.find(nm);
            if (it != paramIdx.end()) out[it->second].push_back(t);
          }
        }
      }
      break;
    }
    case ExprKind::Unary: {
      auto u = static_cast<UnaryExpr*>(e);
      lambdaConstraints(u->operand.get(), e, paramIdx, shadow, out);
      break;
    }
    case ExprKind::Ternary: {
      auto t = static_cast<TernaryExpr*>(e);
      lambdaConstraints(t->cond.get(), e, paramIdx, shadow, out);
      lambdaConstraints(t->thenExpr.get(), e, paramIdx, shadow, out);
      lambdaConstraints(t->elseExpr.get(), e, paramIdx, shadow, out);
      break;
    }
    case ExprKind::Call: {
      auto c = static_cast<CallExpr*>(e);
      lambdaConstraints(c->callee.get(), e, paramIdx, shadow, out);
      for (auto& a : c->args) lambdaConstraints(a.get(), e, paramIdx, shadow, out);
      break;
    }
    case ExprKind::Assign: {
      auto a = static_cast<AssignExpr*>(e);
      lambdaConstraints(a->target.get(), e, paramIdx, shadow, out);
      lambdaConstraints(a->value.get(), e, paramIdx, shadow, out);
      break;
    }
    case ExprKind::Index: {
      auto ix = static_cast<IndexExpr*>(e);
      lambdaConstraints(ix->object.get(), e, paramIdx, shadow, out);
      lambdaConstraints(ix->index.get(), e, paramIdx, shadow, out);
      break;
    }
    case ExprKind::Member: {
      auto m = static_cast<MemberExpr*>(e);
      lambdaConstraints(m->object.get(), e, paramIdx, shadow, out);
      break;
    }
    case ExprKind::OptMember: {
      auto m = static_cast<OptMemberExpr*>(e);
      lambdaConstraints(m->object.get(), e, paramIdx, shadow, out);
      break;
    }
    case ExprKind::Cast: {
      auto c = static_cast<CastExpr*>(e);
      lambdaConstraints(c->operand.get(), e, paramIdx, shadow, out);
      break;
    }
    default: break; // demais formas não restringem (falha segura: pede anotação)
  }
}

void Semantic::lambdaConstraintsStmt(Stmt* s,
                                     const std::map<std::string, size_t>& paramIdx,
                                     const std::set<std::string>& shadow,
                                     std::vector<std::vector<Type>>& out) {
  if (!s) return;
  switch (s->kind) {
    case StmtKind::Block: {
      auto b = static_cast<BlockStmt*>(s);
      for (auto& st : b->stmts) lambdaConstraintsStmt(st.get(), paramIdx, shadow, out);
      break;
    }
    case StmtKind::ExprStmt: {
      auto st = static_cast<ExprStmt*>(s);
      lambdaConstraints(st->expr.get(), nullptr, paramIdx, shadow, out);
      break;
    }
    case StmtKind::Return: {
      auto st = static_cast<ReturnStmt*>(s);
      if (st->value) lambdaConstraints(st->value.get(), nullptr, paramIdx, shadow, out);
      break;
    }
    case StmtKind::VarDecl: {
      auto st = static_cast<StmtVarDecl*>(s);
      for (auto& d : st->decls) {
        if (!d->init) continue;
        // `int y = x;` restringe x ao tipo declarado (var não restringe)
        if (!d->isVar && d->type.kind != Type::Kind::Unknown &&
            d->init->kind == ExprKind::Ident) {
          auto id = static_cast<IdentExpr*>(d->init.get());
          if (!shadow.count(id->name)) {
            auto it = paramIdx.find(id->name);
            if (it != paramIdx.end()) out[it->second].push_back(d->type);
          }
        }
        lambdaConstraints(d->init.get(), nullptr, paramIdx, shadow, out);
      }
      break;
    }
    case StmtKind::If: {
      auto st = static_cast<IfStmt*>(s);
      lambdaConstraints(st->cond.get(), nullptr, paramIdx, shadow, out);
      lambdaConstraintsStmt(st->thenBranch.get(), paramIdx, shadow, out);
      lambdaConstraintsStmt(st->elseBranch.get(), paramIdx, shadow, out);
      break;
    }
    case StmtKind::While: {
      auto st = static_cast<WhileStmt*>(s);
      lambdaConstraints(st->cond.get(), nullptr, paramIdx, shadow, out);
      lambdaConstraintsStmt(st->body.get(), paramIdx, shadow, out);
      break;
    }
    case StmtKind::DoWhile: {
      auto st = static_cast<DoWhileStmt*>(s);
      lambdaConstraintsStmt(st->body.get(), paramIdx, shadow, out);
      lambdaConstraints(st->cond.get(), nullptr, paramIdx, shadow, out);
      break;
    }
    case StmtKind::For: {
      auto st = static_cast<ForStmt*>(s);
      lambdaConstraintsStmt(st->init.get(), paramIdx, shadow, out);
      if (st->cond) lambdaConstraints(st->cond.get(), nullptr, paramIdx, shadow, out);
      if (st->step) lambdaConstraints(st->step.get(), nullptr, paramIdx, shadow, out);
      lambdaConstraintsStmt(st->body.get(), paramIdx, shadow, out);
      break;
    }
    default: break; // demais stmts não restringem (falha segura)
  }
}

Type Semantic::checkLambda(LambdaExpr* l, const Type& expected) {
  // tipo contextual: `func<R, P...>` no VarDecl/argumento/retorno
  bool hasCtx = expected.kind == Type::Kind::Func;
  std::vector<Type> ctxParams;
  Type ctxRet;
  bool ctxHasRet = false;
  if (hasCtx) {
    if (l->params.size() != expected.genericArgs.size()) {
      error(l->line, "lambda tem " + std::to_string(l->params.size()) +
                         " parâmetro(s), o contexto espera " +
                         std::to_string(expected.genericArgs.size()));
    }
    for (auto& p : expected.genericArgs) ctxParams.push_back(resolveType(p, l->line));
    if (expected.elem) {
      ctxRet = resolveType(*expected.elem, l->line);
      ctxHasRet = true;
    }
  }
  size_t n = l->params.size();
  std::vector<Type> ptypes(n);
  for (size_t i = 0; i < n; i++) {
    if (l->params[i].hasType) {
      ptypes[i] = resolveType(l->params[i].type, l->params[i].line);
      if (hasCtx && (!isAssignable(ctxParams[i], ptypes[i]) ||
                     !isAssignable(ptypes[i], ctxParams[i]))) {
        error(l->params[i].line, "parâmetro '" + l->params[i].name + "' anotado como '" +
                                     lambdaTypeName(ptypes[i]) + "' conflita com o contexto '" +
                                     lambdaTypeName(ctxParams[i]) + "'");
      }
    } else if (hasCtx) {
      ptypes[i] = ctxParams[i];
    }
  }
  // inferência dos não-anotados (pré-pass sintático; falha segura → pede anotação)
  {
    std::map<std::string, size_t> pidx;
    for (size_t i = 0; i < n; i++)
      if (ptypes[i].kind == Type::Kind::Unknown) pidx[l->params[i].name] = i;
    if (!pidx.empty()) {
      std::vector<std::vector<Type>> constr(n);
      std::set<std::string> shadow;
      lambdaConstraintsStmt(l->body.get(), pidx, shadow, constr);
      for (auto& [name, i] : pidx) {
        std::vector<Type> cs;
        for (auto& c : constr[i])
          if (c.kind != Type::Kind::Unknown) cs.push_back(c);
        if (cs.empty()) {
          error(l->params[i].line, "não foi possível inferir o tipo do parâmetro '" +
                                       name + "'; anote-o: (" +
                                       lambdaTypeName(Type::makeInt(32)) + " " + name + ") => ...");
        }
        // conflitos numéricos resolvem pelo mais largo (ex.: int de `a+1` e
        // float propagado → float); demais conflitos são erro
        Type best = cs[0];
        for (size_t k = 1; k < cs.size(); k++) {
          if (cs[k] == best) continue;
          if (isNumericBinary(best, cs[k])) {
            best = commonNumeric(best, cs[k]);
          } else {
            error(l->params[i].line, "tipos conflitantes para o parâmetro '" + name +
                                         "' ('" + lambdaTypeName(best) + "' vs '" +
                                         lambdaTypeName(cs[k]) + "')");
          }
        }
        ptypes[i] = best;
      }
    }
  }
  // declara parâmetros em escopo próprio e checa o corpo (payload pattern)
  pushScope();
  for (size_t i = 0; i < n; i++)
    declareVar(l->params[i].name, ptypes[i], StoragePolicy::Stack, false,
               l->params[i].line, SymbolKind::Param);
  size_t savedBase = lambdaBaseScope_;
  Type* prevRet = lambdaReturn_;
  bool prevSet = lambdaReturnSet_;
  bool prevHad = lambdaHadReturn_;
  auto* prevSink = lambdaCaptureSink_;
  int prevDepth = inLambdaDepth_;
  std::set<std::string> outBefore = outAssigned_;
  lambdaBaseScope_ = scopes_.size() - 1; // escopo dos params no topo; captura = abaixo
  Type payload;
  lambdaReturn_ = &payload;
  lambdaReturnSet_ = false;
  lambdaHadReturn_ = false;
  lambdaCaptureSink_ = &l->captures;
  inLambdaDepth_++;
  checkBlock(l->body.get(), Type::makeVoid());
  bool bodyHadReturn = lambdaHadReturn_;
  Type bodyRet = payload;
  bool bodyRetSet = lambdaReturnSet_;
  outAssigned_ = outBefore;
  inLambdaDepth_ = prevDepth;
  lambdaCaptureSink_ = prevSink;
  lambdaReturn_ = prevRet;
  lambdaReturnSet_ = prevSet;
  lambdaHadReturn_ = prevHad;
  lambdaBaseScope_ = savedBase;
  popScope();
  Type ret;
  if (hasCtx && ctxHasRet) {
    if (!bodyHadReturn) {
      if (ctxRet.kind != Type::Kind::Void)
        error(l->line, "corpo da lambda sem 'return' (contexto espera '" +
                           lambdaTypeName(ctxRet) + "')");
      ret = ctxRet;
    } else {
      if (!isAssignable(ctxRet, bodyRet))
        error(l->line, "retorno da lambda ('" + lambdaTypeName(bodyRet) +
                           "') incompatível com o contexto ('" +
                           lambdaTypeName(ctxRet) + "')");
      ret = ctxRet;
    }
  } else if (!bodyRetSet) {
    ret = Type::makeVoid();
  } else {
    ret = bodyRet;
  }
  l->returnType = ret;
  l->hasReturnType = true;
  for (size_t i = 0; i < n; i++) l->params[i].type = ptypes[i]; // canônico p/ HIR
  Type ft = Type::makeFunc(ret, ptypes);
  l->exprType = ft;
  return ft;
}

Type Semantic::checkCall(CallExpr* c) {
  // v0.95 (lambdas): callee é valor `func` — `f(args)` ou `((x) => ...)(args)`.
  // Ident resolvido p/ local/parâmetro de tipo func, ou lambda direta.
  if (c->callee->kind == ExprKind::Lambda) {
    Type ct = checkExpr(c->callee.get());
    if (ct.kind != Type::Kind::Func)
      error(c->line, "callee inválido para chamada");
    return checkFuncCall(c, ct);
  }
  if (c->callee->kind == ExprKind::Ident) {
    auto id0 = static_cast<IdentExpr*>(c->callee.get());
    SymbolInfo si0;
    Type t0;
    size_t fs0 = 0;
    if (lookup(id0->name, si0, t0, nullptr, nullptr, &fs0) &&
        (si0.kind == SymbolKind::LocalVar || si0.kind == SymbolKind::Param) &&
        t0.kind == Type::Kind::Func) {
      checkExpr(c->callee.get());
      return checkFuncCall(c, t0);
    }
  }
// print builtin
  if (c->callee->kind == ExprKind::Ident) {
    auto ident = static_cast<IdentExpr*>(c->callee.get());
    // B11: `list<T>(n)` — construtor de list com capacidade inicial
    if (ident->name == "list" && !c->genericArgs.empty()) {
      if (c->args.size() != 1) {
        error(c->line, "list<T>(n) espera exatamente 1 argumento (capacidade)");
      }
      Type argT = checkExpr(c->args[0].get());
      if (!argT.isNumeric()) {
        error(c->line, "argumento de list<T>(n) deve ser numérico");
      }
      // marca como built-in para o codegen
      c->isListNew = true;
      c->listElemType = c->genericArgs[0];
      c->exprType = Type::makeList(c->genericArgs[0]);
      c->genericArgs.clear();
      return c->exprType;
    }
    if (ident->name == "print" || ident->name == "println") {
      c->isPrint = true;
      c->exprType = Type::makeVoid();
      for (auto& a : c->args) {
        Type t = checkExpr(a.get());
        if (t.kind == Type::Kind::Void) error(a->line, "print n�o aceita void");
        if (t.kind == Type::Kind::Class) error(a->line, "print de classe n�o implementado no M1");
        if (t.kind == Type::Kind::Enum && isRichEnum(t.name))
          error(a->line, "print de enum com dados n�o suportado; use match");
      }
      if (ident->name == "println") {
        // M5 (v0.36.0): println = print + '\n' � o literal � anexado aqui
        // (nenhuma mudan�a nos backends; a concatena��o com coer��o aplica-se
        // aos argumentos normalmente)
        auto nl = std::make_unique<StringLitExpr>();
        nl->value = "\n";
        nl->line = c->line;
        nl->exprType = Type::makeString();
        c->args.push_back(std::move(nl));
      }
      return c->exprType;
    }
    if (ident->name == "addr_of") {
      // FFI v2: addr_of(x) — endereço do lvalue x como `ptr` (void*) para
      // interop com C/Vulkan (passar buffers/structs p/ extern). Aceita
      // variável local/global/parâmetro, campo e elemento de array.
      if (c->args.size() != 1)
        error(c->line, "addr_of() espera exatamente 1 argumento");
      checkExpr(c->args[0].get());
      requireLvalue(c->args[0].get(), "'addr_of'");
      c->isAddrOf = true;
      c->exprType = Type::makeRawPtr();
      return c->exprType;
    }
    if (ident->name == "toString" || ident->name == "__hphl_to_str") {
      // M5: convers�o escalar ? string (usada pela coer��o do '+' e pelo
      // builtin p�blico toString(x)); int/uint/float/bool/char
      if (c->args.size() != 1)
        error(c->line, ident->name + "() espera exatamente 1 argumento");
      Type at = checkExpr(c->args[0].get());
      if (at.kind == Type::Kind::String)
        error(c->args[0]->line, ident->name + "() de string � redundante (� string)");
      if (!(at.isInteger() || at.kind == Type::Kind::Float ||
            at.kind == Type::Kind::Bool || at.kind == Type::Kind::Char ||
            at.kind == Type::Kind::UInt))
        error(c->args[0]->line, ident->name + "() aceita apenas int, uint, float, "
                                "bool ou char (M5)");
      c->isToStr = true;
      c->exprType = Type::makeString();
      return c->exprType;
    }
    if (ident->name == "clock_ns") {
      // builtin (v0.22.9): relógio de alta resolução em nanossegundos (QPC)
      if (!c->args.empty()) error(c->line, "clock_ns() nao recebe argumentos");
      c->isClockNs = true;
      c->exprType = Type::makeInt(64);
      return c->exprType;
    }
    if (ident->name == "sqrt") {
      // M11-bench: sqrt(x) — sqrtsd no x64, libm no LLVM
      if (c->args.size() != 1)
        error(c->line, "sqrt(x) espera exatamente 1 argumento");
      Type at = checkExpr(c->args[0].get());
      if (!at.isNumeric())
        error(c->line, "sqrt exige número");
      c->isSqrt = true;
      c->exprType = Type::makeFloat(64);
      return c->exprType;
    }
    if (ident->name == "arena_reset") {
      // M11.9 Safety (spec §3): recicla a frame-arena INTEIRA — todos os
      // objetos 'arena' da função ficam dangling; acessos posteriores são
      // rejeitados como USE AFTER FREE
      if (!c->args.empty())
        error(c->line, "arena_reset() não recebe argumentos");
      if (arenaLocals_.empty())
        error(c->line, "arena_reset(): nenhum local 'arena' declarado nesta função");
      c->isArenaReset = true;
      c->exprType = Type::makeVoid();
      arenaFreed_ = arenaLocals_;
      return c->exprType;
    }
    // M12.0: registro declarativo da biblioteca padrão (global) — resolve
    // por nome/arity na tabela; overloads resolvidos pelos tipos dos args
    {
      const auto& tbl = stdBuiltinTable();
      bool nameInTable = false;
      for (auto& b : tbl)
        if (ident->name == b.name && b.params.size() == c->args.size()) {
          nameInTable = true;
          break;
        }
      if (nameInTable) {
        std::vector<SBType> kinds;
        for (auto& a : c->args) {
          Type at = checkExpr(a.get());
          if (at.isInteger() || at.kind == Type::Kind::Enum)
            kinds.push_back(SBType::Int);
          else if (at.kind == Type::Kind::Float)
            kinds.push_back(SBType::Float);
          else if (at.kind == Type::Kind::String)
            kinds.push_back(SBType::Str);
          else if (at.kind == Type::Kind::Ptr) // FFI v2: ponteiro opaco
            kinds.push_back(SBType::Ptr);
          else if (at.kind == Type::Kind::Bool)
            kinds.push_back(SBType::Bool);
          else if (at.kind == Type::Kind::List)
            kinds.push_back(SBType::List);
          else
            kinds.push_back(SBType::Int); // desconhecido: trata como int
        }
        int bi = findStdBuiltin(ident->name, c->args.size(), kinds);
        if (bi < 0)
          error(c->line, "biblioteca padrão: nenhum overload de '" +
                             ident->name + "' aceita estes tipos de argumento");
        const StdBuiltin& sb = stdBuiltinTable()[(size_t)bi];
        c->stdBuiltin = bi;
        c->stdSymbol = sb.symbol;
        switch (sb.ret) {
          case SBType::Int: c->exprType = Type::makeInt(32); break;
          case SBType::Float: c->exprType = Type::makeFloat(64); break;
          case SBType::Str: c->exprType = Type::makeString(); break;
          case SBType::Bool: c->exprType = Type::makeBool(); break;
          case SBType::List:
            c->exprType = Type::makeList(Type::makeString());
            break;
          case SBType::ListInt:
            c->exprType = Type::makeList(Type::makeInt(32));
            break;
          case SBType::Void:
            break;
          case SBType::Ptr: c->exprType = Type::makeInt(64); break; // FFI v2: handle opaco (compat)
        }
        return c->exprType;
      }
    }
    // v0.46: metadados de tipo (spec §40) — exigem `reflect Nome;` e são
    // DOBRADOS em compilação (custo zero no executável final)
    if (ident->name == "SizeOf" || ident->name == "FieldCount" ||
        ident->name == "HasField" || ident->name == "FieldOffset") {
      if (c->args.empty() || c->args[0]->kind != ExprKind::Ident)
        error(c->line, ident->name + "() espera o NOME do tipo "
                       "(ex.: " + ident->name + "(Player))");
      std::string tname = static_cast<IdentExpr*>(c->args[0].get())->name;
      std::string canon = canonicalType(tname, c->line);
      auto cit = classes_.find(canon);
      if (cit == classes_.end())
        error(c->line, "'" + tname + "' não é classe/struct conhecida");
      if (!cit->second.decl->reflected)
        error(c->line, "'" + tname + "' não está refletido — use 'reflect " +
                           tname + ";' para habilitar metadados");
      ClassInfo& rc = cit->second;
      layoutClass(rc);
      long long val = 0;
      if (ident->name == "SizeOf") {
        if (rc.isTemplate) error(c->line, "'specialize'/'uso concreto' antes: " +
                                              tname + " é genérica");
        val = rc.size;
      } else if (ident->name == "FieldCount") {
        val = (long long)rc.fields.size();
      } else {
        // HasField / FieldOffset: 2º arg = nome do campo (string literal)
        if (c->args.size() != 2 || c->args[1]->kind != ExprKind::StringLit)
          error(c->line, ident->name + "() espera (" + tname +
                             ", \"nomeDoCampo\")");
        std::string fname =
            static_cast<StringLitExpr*>(c->args[1].get())->value;
        ClassInfo* fo = findFieldOwner(rc, fname);
        if (!fo) {
          if (ident->name == "HasField") {
            val = 0;
          } else {
            error(c->line, "campo '" + fname + "' não existe em '" +
                               tname + "'");
          }
        } else {
          val = ident->name == "HasField"
                    ? 1
                    : (long long)fo->fields.at(fname).second;
        }
      }
      for (size_t i = 1; i < c->args.size(); i++) checkExpr(c->args[i].get());
      c->folded = true;
      c->foldValue = val;
      c->exprType = Type::makeInt(32);
      return c->exprType;
    }
    if (ident->name == "__hphl_str_eq" || ident->name == "__hphl_str_cmp") {
      // builtin de strings (v0.25.0, usado pelos helpers de derive): igualdade
      // de conteúdo e ordem lexicográfica; null é tratado como "" e null==null
      if (c->args.size() != 2)
        error(c->line, ident->name + "() espera exatamente 2 strings");
      for (auto& a : c->args) {
        Type at = checkExpr(a.get());
        if (at.kind != Type::Kind::String)
          error(a->line, ident->name + "() só aceita strings");
      }
      c->isStrEq = ident->name == "__hphl_str_eq";
      c->isStrCmp = ident->name == "__hphl_str_cmp";
      c->exprType = c->isStrEq ? Type::makeBool() : Type::makeInt(64);
      return c->exprType;
    }
    if (ident->name == "assert" || ident->name == "panic") {
      error(ident->line, "'" + ident->name + "' deve ser usado como instrução da linguagem, não como chamada de função");
    }
    // chamada de função global (possivelmente genérica — monomorfização)
    FunctionDecl* fn = nullptr;
    auto git = genericFuncs_.find(ident->name);
    if (git != genericFuncs_.end()) {
      FunctionDecl* tmpl = nullptr;
      for (auto* cand : git->second)
        if (cand->params.size() == c->args.size()) { tmpl = cand; break; }
      if (!tmpl)
        error(c->line, "função genérica '" + ident->name + "' não tem versão com " +
                           std::to_string(c->args.size()) + " argumento(s)");
      std::vector<Type> own;
      if (!c->genericArgs.empty()) {
        for (auto& ga : c->genericArgs) {
          own.push_back(resolveType(ga, c->line));
        }
        fn = genericFunctionInstance(tmpl, own, c->line);
      } else {
        own = inferTypeArgs(tmpl, c->args, c->line);
        fn = genericFunctionInstance(tmpl, own, c->line);
      }
    } else {
      fn = findFunction(ident->name, c->args.size(), c->line);
      if (fn && !c->genericArgs.empty())
        error(c->line, "'" + ident->name + "' não é genérica (remova <...>)");
    }
    if (!fn) error(c->line, "função desconhecida '" + ident->name + "'");
    c->resolved = fn;
    if (fn->isDeprecated && !isDeprecationSuppressed()) {
      std::string msg = "'" + fn->name + "' is deprecated";
      if (!fn->deprecatedReason.empty()) msg += ": " + fn->deprecatedReason;
      warn(c->line, msg);
    }
    for (size_t i = 0; i < c->args.size(); i++) {
      // expected = tipo do parâmetro (permite inferir Some/Ok sem declarar o tipo)
      Type pt = resolveType(fn->params[i]->type, fn->params[i]->line);
      // argumento `out` é ESCRITA (não lê): o Ident do parâmetro 'out' direto
      // não precisa estar atribuído e passa a estar depois da chamada
      std::string outArg = fn->params[i]->byOut ? outDirectName(c->args[i].get()) : "";
      outWriteName_ = outArg;
      checkExpr(c->args[i].get(), pt);
      outWriteName_.clear();
      // checkExpr antes de requireLvalue: resolve flags (enum const, arr.Length,
      // propriedade etc.) que determinam se o argumento é lvalue real
      if (fn->params[i]->byRef || fn->params[i]->byOut)
        requireLvalue(c->args[i].get(),
                      std::string(fn->params[i]->byOut ? "'out'" : "'ref'"));
      if (!outArg.empty()) outMarkAssigned(outArg);
    }
    c->exprType = resolveType(fn->returnType, c->line);
    // v0.46: função `compiletime` com argumentos literais inteiros → dobra
    if (fn->isCompiletime) {
      std::vector<long long> ctArgs;
      bool allConst = true;
      for (auto& a : c->args) {
        if (a->kind == ExprKind::IntLit)
          ctArgs.push_back(static_cast<IntLitExpr*>(a.get())->value);
        else {
          allConst = false;
          break;
        }
      }
      if (allConst && fn->returnType.kind == Type::Kind::Int &&
          fn->params.size() == ctArgs.size()) {
        auto v = evalCompiletime(fn, ctArgs, 0);
        if (v) {
          c->folded = true;
          c->foldValue = *v;
        }
      }
    }
    if (fn->isAsync) {
      // função async: a chamada devolve task<T> e o corpo roda numa thread
      // (site de spawn no codegen); `await F()` / `t.Wait()` esperam
      c->isAsyncCall = true;
      c->exprType = Type::makeTask(c->exprType);
    }
    return c->exprType;
  }

  if (c->callee->kind == ExprKind::OptMember) {
    // `e?.metodo(args)` (spec §12): executa o método só se e != null;
    // resultado: referência (null-merged), Option<T> (None) ou void (skip)
    auto om = static_cast<OptMemberExpr*>(c->callee.get());
    Type objType = checkExpr(om->object.get());
    if (objType.kind != Type::Kind::Class)
      error(om->line, "'?.' exige referência de classe (nullable)");
    auto& ci = classes_[objType.name];
    FunctionDecl* m = findMethod(ci, om->member, c->args.size());
    if (!m)
      error(c->line, "método '" + om->member + "' não encontrado em '" +
                         objType.name + "'");
    {
      // v0.46: visibilidade private/protected
      auto mo = classes_.find(m->ownerClass);
      if (mo != classes_.end() && !canAccessMember(mo->second, m->access))
        error(c->line, "método '" + om->member + "' de '" + objType.name +
                           "' não é acessível aqui (" +
                           (m->access == Access::Private ? "privado" : "protegido") +
                           ")");
    }
    om->isField = false;
    om->resolved = m;
    if (m->isDeprecated && !isDeprecationSuppressed()) {
      std::string msg = "'" + m->name + "' is deprecated";
      if (!m->deprecatedReason.empty()) msg += ": " + m->deprecatedReason;
      warn(c->line, msg);
    }
    if (m->params.size() != c->args.size())
      error(c->line, "métodos com parâmetros opcionais ainda não são suportados com '?.'");
    for (size_t i = 0; i < c->args.size(); i++) {
      Type pt = resolveType(m->params[i]->type, m->params[i]->line);
      std::string outArg = m->params[i]->byOut ? outDirectName(c->args[i].get()) : "";
      outWriteName_ = outArg;
      checkExpr(c->args[i].get(), pt);
      outWriteName_.clear();
      if (m->params[i]->byRef || m->params[i]->byOut)
        requireLvalue(c->args[i].get(),
                      std::string(m->params[i]->byOut ? "'out'" : "'ref'"));
      if (!outArg.empty()) outMarkAssigned(outArg);
    }
    Type rt = resolveType(m->returnType, c->line);
    if (rt.kind == Type::Kind::Void)
      c->exprType = Type::makeVoid();
    else if (rt.kind == Type::Kind::Class || rt.kind == Type::Kind::String)
      c->exprType = rt; // referência null-merged
    else
      c->exprType = Type::makeOption(rt);
    return c->exprType;
  }

  if (c->callee->kind == ExprKind::Member) {
    auto mem = static_cast<MemberExpr*>(c->callee.get());
    // M10 (v0.44): `base.Metodo(...)` — resolve na BASE da classe corrente
    // (estático, sem dispatch virtual); o receiver implícito é `this`
    if (mem->isBaseCall) {
      if (!currentFunction_ || currentFunction_->ownerClass.empty())
        error(c->line, "'base.' só pode ser usado dentro de métodos de classe");
      std::string owner = currentFunction_->ownerClass;
      auto cit = classes_.find(owner);
      if (cit != classes_.end() && cit->second.base.empty())
        error(c->line, "'" + owner + "' não possui classe base");
      FunctionDecl* m = nullptr;
      if (cit != classes_.end()) {
        auto bit = classes_.find(cit->second.base);
        if (bit != classes_.end())
          m = findMethod(bit->second, mem->member, c->args.size(), c->line);
      }
      if (!m)
        error(c->line, "método '" + mem->member + "' não encontrado na base de '" +
                           owner + "'");
      {
        // v0.46: `base.Metodo()` respeita a visibilidade na base (privado
        // da base continua inacessível; protegido é permitido na derivada)
        auto mo = classes_.find(m->ownerClass);
        if (mo != classes_.end() && !canAccessMember(mo->second, m->access))
          error(c->line, "método '" + mem->member + "' da base não é acessível aqui (" +
                             (m->access == Access::Private ? "privado" : "protegido") +
                             ")");
      }
      c->resolved = m;
      if (m->isDeprecated && !isDeprecationSuppressed()) {
        std::string msg = "'" + m->name + "' is deprecated";
        if (!m->deprecatedReason.empty()) msg += ": " + m->deprecatedReason;
        warn(c->line, msg);
      }
      c->isBaseCall = true;
      for (auto& a : c->args) checkExpr(a.get());
      c->exprType = resolveType(m->returnType, c->line);
      return c->exprType;
    }
    Type objType = checkExpr(mem->object.get());
    // M11.7: chamada de método em possivelmente-null
    checkNullDeref(mem->object.get(), objType, c->line);
    // M11.9: método em objeto de arena resetada
    checkArenaUaf(mem->object.get(), c->line);
    // M11.8: TOCTOU — método em objeto com leitura sob lock vencida
    noteLockAccess(mem->object.get(), c->line);
    if (objType.kind == Type::Kind::Module) {
      FunctionDecl* f = moduleFunction(objType.name, mem->member, c->args.size(), c->line);
      if (!f) {
        if (objType.name == "std" || objType.name.rfind("std.", 0) == 0) {
          std::vector<SBType> kinds;
          for (auto& a : c->args) {
            Type at = checkExpr(a.get());
            if (at.isInteger()) kinds.push_back(SBType::Int);
            else if (at.kind == Type::Kind::Float) kinds.push_back(SBType::Float);
            else if (at.kind == Type::Kind::String) kinds.push_back(SBType::Str);
            else if (at.kind == Type::Kind::Ptr) kinds.push_back(SBType::Ptr); // FFI v2
            else if (at.kind == Type::Kind::Bool) kinds.push_back(SBType::Bool);
            else if (at.kind == Type::Kind::List) kinds.push_back(SBType::List);
            else kinds.push_back(SBType::Int);
          }
          int bi = findStdBuiltin(mem->member, c->args.size(), kinds);
          if (bi >= 0) {
            const StdBuiltin& sb = stdBuiltinTable()[(size_t)bi];
            c->stdBuiltin = bi;
            c->stdSymbol = sb.symbol;
            switch (sb.ret) {
              case SBType::Int: c->exprType = Type::makeInt(32); break;
              case SBType::Float: c->exprType = Type::makeFloat(64); break;
              case SBType::Str: c->exprType = Type::makeString(); break;
              case SBType::Bool: c->exprType = Type::makeBool(); break;
              case SBType::List: c->exprType = Type::makeList(Type::makeString()); break;
              case SBType::ListInt: c->exprType = Type::makeList(Type::makeInt(32)); break;
              case SBType::Void: break;
              case SBType::Ptr: c->exprType = Type::makeInt(64); break;
            }
            return c->exprType;
          }
        }
        error(c->line, "função '" + mem->member + "' não existe no módulo '" +
                           objType.name + "'");
      }
      c->resolved = f;
      if (f->isDeprecated && !isDeprecationSuppressed()) {
        std::string msg = "'" + f->name + "' is deprecated";
        if (!f->deprecatedReason.empty()) msg += ": " + f->deprecatedReason;
        warn(c->line, msg);
      }
      c->isModuleCall = true;
      for (size_t i = 0; i < c->args.size(); i++) {
        std::string outArg = f->params[i]->byOut ? outDirectName(c->args[i].get()) : "";
        outWriteName_ = outArg;
        checkExpr(c->args[i].get());
        outWriteName_.clear();
        if (f->params[i]->byRef || f->params[i]->byOut)
          requireLvalue(c->args[i].get(),
                        std::string(f->params[i]->byOut ? "'out'" : "'ref'"));
        if (!outArg.empty()) outMarkAssigned(outArg);
      }
      c->exprType = resolveType(f->returnType, c->line);
      return c->exprType;
    }
    if (objType.kind == Type::Kind::List) {
      if (mem->member != "Add" || c->args.size() != 1) {
        error(c->line, "list possui apenas 'Add(x)' como método");
      }
      Type et = checkExpr(c->args[0].get(), *objType.elem);
      if (!isAssignable(*objType.elem, et)) {
        error(c->line, "tipo do argumento de Add incompatível com o elemento do list");
      }
      c->isListAdd = true;
      c->exprType = Type::makeVoid();
      return c->exprType;
    }
    if (objType.kind == Type::Kind::Map) {
      if (mem->member == "Put") {
        if (c->args.size() != 2) error(c->line, "map.Put exige 2 argumentos (chave, valor)");
        Type kt = checkExpr(c->args[0].get(), *objType.elem);
        Type vt = checkExpr(c->args[1].get(), *objType.elem2);
        if (!isAssignable(*objType.elem, kt)) error(c->line, "tipo da chave incompatível com o map");
        if (!isAssignable(*objType.elem2, vt)) error(c->line, "tipo do valor incompatível com o map");
        c->isMapPut = true;
        c->exprType = Type::makeVoid();
        return c->exprType;
      } else if (mem->member == "Get") {
        if (c->args.size() != 1) error(c->line, "map.Get exige 1 argumento (chave)");
        Type kt = checkExpr(c->args[0].get(), *objType.elem);
        if (!isAssignable(*objType.elem, kt)) error(c->line, "tipo da chave incompatível com o map");
        c->isMapGet = true;
        c->exprType = *objType.elem2;
        return c->exprType;
      } else if (mem->member == "Contains") {
        if (c->args.size() != 1) error(c->line, "map.Contains exige 1 argumento (chave)");
        Type kt = checkExpr(c->args[0].get(), *objType.elem);
        if (!isAssignable(*objType.elem, kt)) error(c->line, "tipo da chave incompatível com o map");
        c->isMapContains = true;
        c->exprType = Type::makeBool();
        return c->exprType;
      } else if (mem->member == "Remove") {
        if (c->args.size() != 1) error(c->line, "map.Remove exige 1 argumento (chave)");
        Type kt = checkExpr(c->args[0].get(), *objType.elem);
        if (!isAssignable(*objType.elem, kt)) error(c->line, "tipo da chave incompatível com o map");
        c->isMapRemove = true;
        c->exprType = Type::makeBool();
        return c->exprType;
      } else if (mem->member == "Clear") {
        if (!c->args.empty()) error(c->line, "map.Clear não aceita argumentos");
        c->isMapClear = true;
        c->exprType = Type::makeVoid();
        return c->exprType;
      } else {
        error(c->line, "map não possui método '" + mem->member + "' (use Put, Get, Contains, Remove, Clear, Length)");
      }
    }
    if (objType.kind == Type::Kind::Task) {
      if (mem->member == "Wait") {
        if (!c->args.empty()) {
          error(c->line, "task.Wait() não aceita argumentos");
        }
        c->isWait = true;
        c->exprType = objType.elem ? *objType.elem : Type::makeVoid();
        return c->exprType;
      }
      if (mem->member == "Cancel") {
        if (!c->args.empty()) {
          error(c->line, "task.Cancel() não aceita argumentos");
        }
        c->isTaskCancel = true;
        c->exprType = Type::makeVoid();
        return c->exprType;
      }
      error(c->line, "task possui apenas 'Wait()' e 'Cancel()' como métodos");
    }
    if (objType.kind == Type::Kind::Channel) {
      // `ch.Send(x)` / `ch.Receive()` (spec §10, M2): runtime, não é método
      if (mem->member == "Send") {
        if (c->args.size() != 1) {
          error(c->line, "channel.Send(x) espera exatamente 1 argumento");
        }
        Type et = checkExpr(c->args[0].get(), *objType.elem);
        if (!isAssignable(*objType.elem, et)) {
          error(c->line, "tipo do argumento de Send incompatível com o elemento do channel");
        }
        c->isChannelSend = true;
        c->exprType = Type::makeVoid();
        return c->exprType;
      }
      if (mem->member == "Receive") {
        if (!c->args.empty()) {
          error(c->line, "channel.Receive() não aceita argumentos");
        }
        c->isChannelReceive = true;
        c->exprType = objType.elem ? *objType.elem : Type::makeVoid();
        return c->exprType;
      }
      error(c->line, "channel possui apenas 'Send(x)' e 'Receive()' como métodos");
    }
    if (objType.kind == Type::Kind::Mutex) {
      // `m.Lock()` / `m.Unlock()` (v0.24.0): exclusão mútua por handle de runtime
      if (mem->member == "Lock") c->primOp = CallExpr::PrimOp::MutexLock;
      else if (mem->member == "Unlock") c->primOp = CallExpr::PrimOp::MutexUnlock;
      else error(c->line, "mutex possui apenas 'Lock()' e 'Unlock()' como métodos");
      if (!c->args.empty())
        error(c->line, "método de mutex não aceita argumentos");
      c->exprType = Type::makeVoid();
      return c->exprType;
    }
    if (objType.kind == Type::Kind::Semaphore) {
      // `s.Wait()` (decrementa; bloqueia em 0) / `s.Signal()` (incrementa)
      if (mem->member == "Wait") c->primOp = CallExpr::PrimOp::SemaphoreWait;
      else if (mem->member == "Signal") c->primOp = CallExpr::PrimOp::SemaphoreSignal;
      else error(c->line, "semaphore possui apenas 'Wait()' e 'Signal()' como métodos");
      if (!c->args.empty())
        error(c->line, "método de semaphore não aceita argumentos");
      c->exprType = Type::makeVoid();
      return c->exprType;
    }
    if (objType.kind == Type::Kind::Event) {
      // `e.Set()` (acende), `e.Reset()` (apaga), `e.Wait()` (bloqueia até aceso)
      if (mem->member == "Wait") c->primOp = CallExpr::PrimOp::EventWait;
      else if (mem->member == "Set") c->primOp = CallExpr::PrimOp::EventSet;
      else if (mem->member == "Reset") c->primOp = CallExpr::PrimOp::EventReset;
      else error(c->line, "event possui apenas 'Wait()', 'Set()' e 'Reset()' como métodos");
      if (!c->args.empty())
        error(c->line, "método de event não aceita argumentos");
      c->exprType = Type::makeVoid();
      return c->exprType;
    }
    if (objType.kind == Type::Kind::Barrier) {
      // `b.Wait()` (bloqueia até N participantes chegarem; depois libera todos)
      if (mem->member == "Wait") c->primOp = CallExpr::PrimOp::BarrierWait;
      else error(c->line, "barrier possui apenas 'Wait()' como método");
      if (!c->args.empty())
        error(c->line, "método de barrier não aceita argumentos");
      c->exprType = Type::makeVoid();
      return c->exprType;
    }
    if (objType.kind == Type::Kind::Option) {
      if (mem->member == "is_some" || mem->member == "IsSome") {
        if (!c->args.empty()) error(c->line, "Option.is_some() não aceita argumentos");
        c->isOptionIsSome = true;
        c->exprType = Type::makeBool();
        return c->exprType;
      }
      if (mem->member == "is_none" || mem->member == "IsNone") {
        if (!c->args.empty()) error(c->line, "Option.is_none() não aceita argumentos");
        c->isOptionIsNone = true;
        c->exprType = Type::makeBool();
        return c->exprType;
      }
      if (mem->member == "unwrap") {
        if (!c->args.empty()) error(c->line, "Option.unwrap() não aceita argumentos");
        c->isOptionUnwrap = true;
        c->exprType = objType.elem ? *objType.elem : Type::makeVoid();
        return c->exprType;
      }
      if (mem->member == "unwrap_or") {
        if (c->args.size() != 1) error(c->line, "Option.unwrap_or(default) exige 1 argumento");
        Type expectedElem = objType.elem ? *objType.elem : Type::makeVoid();
        Type dt = checkExpr(c->args[0].get(), expectedElem);
        if (!isAssignable(expectedElem, dt))
          error(c->args[0]->line, "tipo do valor padrão incompatível com o tipo da Option");
        c->isOptionUnwrapOr = true;
        c->exprType = expectedElem;
        return c->exprType;
      }
      error(c->line, "Option possui apenas 'is_some()', 'is_none()', 'unwrap()' e 'unwrap_or(default)' como métodos");
    }
    if (objType.kind == Type::Kind::Result) {
      if (mem->member == "is_ok" || mem->member == "IsOk") {
        if (!c->args.empty()) error(c->line, "Result.is_ok() não aceita argumentos");
        c->isResultIsOk = true;
        c->exprType = Type::makeBool();
        return c->exprType;
      }
      if (mem->member == "is_err" || mem->member == "IsErr" ||
          mem->member == "is_error" || mem->member == "IsError") {
        if (!c->args.empty()) error(c->line, "Result.is_err() não aceita argumentos");
        c->isResultIsErr = true;
        c->exprType = Type::makeBool();
        return c->exprType;
      }
      if (mem->member == "unwrap") {
        if (!c->args.empty()) error(c->line, "Result.unwrap() não aceita argumentos");
        c->isResultUnwrap = true;
        c->exprType = objType.elem ? *objType.elem : Type::makeVoid();
        return c->exprType;
      }
      if (mem->member == "unwrap_err" || mem->member == "UnwrapErr") {
        if (!c->args.empty()) error(c->line, "Result.unwrap_err() não aceita argumentos");
        c->isResultUnwrapErr = true;
        c->exprType = objType.elem2 ? *objType.elem2 : Type::makeVoid();
        return c->exprType;
      }
      if (mem->member == "unwrap_or") {
        if (c->args.size() != 1) error(c->line, "Result.unwrap_or(default) exige 1 argumento");
        if (objType.elem && objType.elem->kind == Type::Kind::Void)
          error(c->line, "unwrap_or não é suportado para Result<void, E>");
        Type expectedElem = objType.elem ? *objType.elem : Type::makeVoid();
        Type dt = checkExpr(c->args[0].get(), expectedElem);
        if (!isAssignable(expectedElem, dt))
          error(c->args[0]->line, "tipo do valor padrão incompatível com o tipo do Result");
        c->isResultUnwrapOr = true;
        c->exprType = expectedElem;
        return c->exprType;
      }
      error(c->line, "Result possui apenas 'is_ok()', 'is_err()', 'unwrap()', 'unwrap_err()' e 'unwrap_or(default)' como métodos");
    }
    if (objType.kind == Type::Kind::Enum) {
      // `NetworkState.Connected(3)` / `Mod.State.Loading(path, n)`: constrói célula
      auto* en = enums_[objType.name];
      int idx = findEnumEntry(en, mem->member);
      if (idx < 0) {
        // `e.Serialize()` �?? enum com derive Serializable �?? inteiro (tag)
        if (mem->member == "Serialize" && c->args.empty() &&
            derived(objType.name, "Serializable")) {
          c->isSerialize = true;
          c->serializeEnum = objType.name;
          c->exprType = Type::makeInt(64);
          return c->exprType;
        }
        // `Enum.FromInt(n)` �?? enum simples �?? Option<Enum> (Some/None)
        if (mem->member == "FromInt" && c->args.size() == 1) {
          Type at = checkExpr(c->args[0].get());
          if (!at.isInteger())
            error(c->args[0]->line,
                  "'FromInt' exige argumento inteiro (índice da variante)");
          c->isFromInt = true;
          c->fromIntEnum = objType.name;
          c->exprType = Type::makeOption(objType);
          return c->exprType;
        }
        error(c->line, "variante '" + mem->member + "' não existe no enum '" +
                           objType.name + "'");
      }
      auto& entry = en->entries[idx];
      if (!entry.hasPayload) {
        error(c->line, "variante '" + entry.name + "' não tem dados; use " +
                           objType.name + "." + entry.name + " sem parênteses");
      }
      if (entry.params.size() != c->args.size())
        error(c->line, "variante '" + entry.name + "' espera " +
                           std::to_string(entry.params.size()) + " argumento(s), veio " +
                           std::to_string(c->args.size()));
      for (size_t i = 0; i < c->args.size(); i++) {
        Type t = checkExpr(c->args[i].get());
        if (!isAssignable(entry.params[i].type, t))
          error(c->args[i]->line, "argumento " + std::to_string(i + 1) +
                                      " da variante '" + entry.name +
                                      "' incompatível com o payload");
      }
      c->isEnumCtor = true;
      c->enumCtorEnum = objType.name;
      c->enumCtorIndex = idx;
      c->exprType = objType;
      return c->exprType;
    }
    if (objType.kind != Type::Kind::Class) {
      error(c->line, "chamada de método em tipo não-classe");
    }
    auto& ci = classes_[objType.name];
    FunctionDecl* m = findMethod(ci, mem->member, c->args.size(), c->line, &c->genericArgs);
    if (!m && ci.isInstance && !c->genericArgs.empty()) {
      // método genérico com tipos explícitos: `obj.M<T>(...)`
      auto& tci = classes_[ci.templateFrom];
      std::vector<Type> own;
      for (auto& ga : c->genericArgs) own.push_back(resolveType(ga, c->line));
      for (auto* cand : tci.methods) {
        if (cand->name == mem->member && cand->params.size() == c->args.size() &&
            cand->typeParams.size()) {
          m = cand;
          break;
        }
      }
      if (m) {
        std::vector<std::pair<std::string, Type>> cb;
        for (size_t i = 0; i < ci.typeParamNames.size(); i++)
          cb.push_back({ci.typeParamNames[i], ci.typeArgs[i]});
        m = methodInstance(m, objType.name, cb, own, c->line);
      }
    }
    if (!m) {
      error(c->line, "método '" + mem->member + "' não encontrado em '" + objType.name + "'");
    }
    {
      // v0.46: visibilidade private/protected do método chamado
      auto mo = classes_.find(m->ownerClass);
      if (mo != classes_.end() && !canAccessMember(mo->second, m->access))
        error(c->line, "método '" + mem->member + "' de '" + objType.name +
                           "' não é acessível aqui (" +
                           (m->access == Access::Private ? "privado" : "protegido") +
                           ")");
    }
    c->resolved = m;
    if (m->isDeprecated && !isDeprecationSuppressed()) {
      std::string msg = "'" + m->name + "' is deprecated";
      if (!m->deprecatedReason.empty()) msg += ": " + m->deprecatedReason;
      warn(c->line, msg);
    }
    // `actor` (v0.23.0): chamada EXTERNA ao estado próprio → serializada por
    // spinlock por objeto no runtime. Métodos do PRÓPRIO actor (this) sobre
    // `this` já estão sob o lock da chamada externa — sem re-lock (deadlock).
    // Métodos static não tocam o estado → chamada direta.
    if (ci.decl->isActor && !m->isStatic) {
      bool insideSame =
          currentFunction_ && currentFunction_->ownerClass == objType.name &&
          mem->object->kind == ExprKind::Ident &&
          static_cast<IdentExpr*>(mem->object.get())->name == "this";
      if (!insideSame) c->isActorCall = true;
    }
    for (size_t i = 0; i < c->args.size(); i++) {
      std::string outArg = m->params[i]->byOut ? outDirectName(c->args[i].get()) : "";
      outWriteName_ = outArg;
      checkExpr(c->args[i].get());
      outWriteName_.clear();
      if (m->params[i]->byRef || m->params[i]->byOut)
        requireLvalue(c->args[i].get(),
                      std::string(m->params[i]->byOut ? "'out'" : "'ref'"));
      if (!outArg.empty()) outMarkAssigned(outArg);
    }
    c->exprType = resolveType(m->returnType, c->line);
    // A3: método async devolve task<T> (mesma regra de função async)
    if (m->isAsync) {
      c->isAsyncCall = true;
      c->exprType = Type::makeTask(c->exprType);
    }
    return c->exprType;
  }
  error(c->line, "callee inválido");
}

Type Semantic::checkAssign(AssignExpr* a) {
if (a->target->kind != ExprKind::Ident && a->target->kind != ExprKind::Member &&
      a->target->kind != ExprKind::Index) {
    error(a->line, "alvo de atribuição deve ser variável, campo ou elemento de array");
  }
  // M10.4 Safety (spec §3): MOVE entre referências politizadas — `b = a`
  // transfere a posse; ler `a` depois vira USE AFTER MOVE, e o codegen
  // remove o slot da origem da limpeza (evita DOUBLE FREE por aliasing).
  // A marcação acontece DEPOIS de checar o valor (ler `a` no próprio move
  // é legítimo).
  // atribuição definida para 'out': `r = v` puro NÃO lê o alvo (é escrita);
  // compostas (`r += v`) e alvos de cadeia (`r.f = v`, `r[i] = v`) leem o
  // alvo/prefixo antes de escrever → nenhuma supressão
  outWriteName_ = (a->op == AssignOp::Plain) ? outDirectName(a->target.get()) : "";
  inAssignLhs_ = true;
  Type targetType = checkExpr(a->target.get());
  inAssignLhs_ = false;
  outWriteName_.clear();
  bool targetAtomic = false;
  if (a->target->kind == ExprKind::Ident)
    targetAtomic = static_cast<IdentExpr*>(a->target.get())->symbol.atomic;
  else if (a->target->kind == ExprKind::Member)
    targetAtomic = static_cast<MemberExpr*>(a->target.get())->fieldAtomic;
  if (targetAtomic) {
    if (a->op == AssignOp::Mul || a->op == AssignOp::Div || a->op == AssignOp::Mod)
      error(a->line, "atomic suporta apenas '=', '+=' e '-=' (para as demais, use 'lock')");
  }
  // propriedade (spec §19): leitura exige get; escrita exige set; composta
  // ainda não é suportada (transforme em `x = x op v`)
  FunctionDecl* pSetter = nullptr;
  std::string pName;
  bool pTarget = false;
  if (a->target->kind == ExprKind::Member &&
      static_cast<MemberExpr*>(a->target.get())->isProperty) {
    pTarget = true;
    pSetter = static_cast<MemberExpr*>(a->target.get())->propSet;
    pName = static_cast<MemberExpr*>(a->target.get())->member;
  } else if (a->target->kind == ExprKind::Ident &&
             static_cast<IdentExpr*>(a->target.get())->isProperty) {
    pTarget = true;
    pSetter = static_cast<IdentExpr*>(a->target.get())->propSet;
    pName = static_cast<IdentExpr*>(a->target.get())->name;
  }
  if (pTarget) {
    if (!pSetter)
      error(a->line, "propriedade '" + pName + "' não possui 'set' (somente leitura)");
    if (a->op != AssignOp::Plain)
      error(a->line, "atribuição composta a propriedade não suportada; use '" +
                         pName + " = " + pName + " ...'");
    Type vt = checkExpr(a->value.get(), targetType);
    if (!isAssignable(targetType, vt))
      error(a->line, "atribuição de tipo incompatível à propriedade");
a->exprType = targetType;
    return a->exprType;
  }
  // escrita direta no parâmetro 'out' (qualquer forma de `=`): está atribuído
  if (a->target->kind == ExprKind::Ident) {
    auto sy = static_cast<IdentExpr*>(a->target.get())->symbol;
    if (isOutParam(sy)) outMarkAssigned(sy.name);
  }

  // M10.4 Safety/Concurrency (spec §3/§10): THREAD RACE — escrita em
  // variável GLOBAL dentro de spawn/parallel é corrida de dados, a menos que
  // a global seja `shared` (intenção explícita), `threadlocal` (sem
  // compartilhamento) ou `atomic` (RMW sincronizado)
  // M11.1: lógica extraída para checkGlobalRaceWrite — reutilizada por
  // ++/--, que antes escapavam da checagem (eram UnOp, não AssignExpr)
  checkGlobalRaceWrite(a->line, a->target.get());
  if (a->op == AssignOp::Plain) {
    Type vt = checkExpr(a->value.get(), targetType);
    // M10.4 Safety (spec §3): MOVE entre referências politizadas — marcado
    // DEPOIS de checar o valor (ler a origem no próprio move é legítimo)
    if (a->target->kind == ExprKind::Ident && a->value->kind == ExprKind::Ident) {
      auto* tgt = static_cast<IdentExpr*>(a->target.get());
      auto* src = static_cast<IdentExpr*>(a->value.get());
      if (tgt->name != src->name && isRefPolicyLocal(tgt->name) &&
          isRefPolicyLocal(src->name)) {
        movedVars_.insert(src->name);
        movedVars_.erase(tgt->name); // destino recebe posse nova e válida
      }
    }
    // M11.7: estado possivelmente-null do alvo (`= null` marca; `= novo` limpa)
    if (a->target->kind == ExprKind::Ident) {
      auto* tgt = static_cast<IdentExpr*>(a->target.get());
      nullNoteAssign(tgt->name, targetType, a->value.get());
    }
    // B9: cópia de array agora suportada via codegen (loop de elementos)
    /* if (targetType.kind == Type::Kind::Array) {
      error(a->line, "cópia de array não implementada no Milestone 2 (atribua por elementos)");
    } */
    // B10: atribuição de list agora suportada via codegen (free old + assign new)
    // Permite `list<T> = {e1, e2, e3}` (array literal → list) quando os
    // elementos do array são compatíveis com o tipo da list.
    bool listLiteralOk = false;
    if (targetType.kind == Type::Kind::List && vt.kind == Type::Kind::Array &&
        a->value->kind == ExprKind::ArrayLit && targetType.elem) {
      auto* al = static_cast<ArrayLitExpr*>(a->value.get());
      for (auto& el : al->elements) {
        if (!isAssignable(*targetType.elem, el->exprType)) break;
      }
      listLiteralOk = true;
    }
    if (!listLiteralOk && !isAssignable(targetType, vt)) {
      error(a->line, "atribuição de tipo incompatível");
    }
    a->exprType = targetType;
    return targetType;
  }
  // operações compostas
  Type vt = checkExpr(a->value.get());
  if (!isNumericBinary(targetType, vt)) error(a->line, "atribuição composta exige números");
  a->exprType = targetType;
  return targetType;
}

// A2 (interface como tipo): verifica se a classe `clsCanon` implementa a
// interface `ifaceMangled`, seguindo a cadeia de base. `ifaceMangled` pode ser
// o nome canônico simples (interface não-genérica) ou o nome monomorfizado
// (ex.: "main.Repo[i32]"). Compara os type args resolvidos de cada interface
// implementada — templates nus (sem instância) nunca casam.
bool Semantic::implementsInterface(const std::string& clsCanon,
                                   const std::string& ifaceMangled) const {
  auto fit = classes_.find(ifaceMangled);
  if (fit != classes_.end() && fit->second.isTemplate && !fit->second.isInstance)
    return false; // referência a template nu: tipo incompleto, não casa
  std::string cur = clsCanon;
  for (int guard = 0; guard < 64; guard++) {
    auto it = classes_.find(cur);
    if (it == classes_.end()) return false;
    const ClassDecl* d = it->second.decl;
    auto& ifArgsList = it->second.interfaceTypeArgs.empty() ? d->interfaceTypeArgs : it->second.interfaceTypeArgs;
    for (size_t i = 0; i < d->interfaces.size(); i++) {
      std::string m = d->interfaces[i];
      if (i < ifArgsList.size() && !ifArgsList[i].empty())
        m = mangleClassName(m, ifArgsList[i]);
      if (m == ifaceMangled) return true;
      auto mit = classes_.find(m);
      auto tit = classes_.find(ifaceMangled);
      if (mit != classes_.end() && tit != classes_.end() &&
          mit->second.isInstance && tit->second.isInstance &&
          mit->second.templateFrom == tit->second.templateFrom) {
        auto tmplIt = classes_.find(mit->second.templateFrom);
        if (tmplIt != classes_.end() &&
            mit->second.typeArgs.size() == tit->second.typeArgs.size()) {
          bool varOk = true;
          for (size_t k = 0; k < mit->second.typeArgs.size(); k++) {
            Variance v = k < tmplIt->second.decl->typeParams.size()
                             ? tmplIt->second.decl->typeParams[k].variance
                             : Variance::Invariant;
            if (v == Variance::Covariant) {
              if (!isAssignable(tit->second.typeArgs[k], mit->second.typeArgs[k])) {
                varOk = false; break;
              }
            } else if (v == Variance::Contravariant) {
              if (!isAssignable(mit->second.typeArgs[k], tit->second.typeArgs[k])) {
                varOk = false; break;
              }
            } else {
              if (!(mit->second.typeArgs[k] == tit->second.typeArgs[k])) {
                varOk = false; break;
              }
            }
          }
          if (varOk) return true;
        }
      }
    }
    if (it->second.base.empty()) return false;
    cur = it->second.base;
  }
  return false;
}

// Nome de tipo primitivo → Type (para `new int[10]`, `new int(0)`, etc.)
static Type primitiveTypeFromName(const std::string& name) {
  if (name == "int") return Type::makeInt(32);
  if (name == "float") return Type::makeFloat(32);
  if (name == "double") return Type::makeFloat(64);
  if (name == "bool") return Type::makeBool();
  if (name == "char") return Type::makeChar();
  if (name == "string") return Type::makeString();
  if (name == "u8") return Type::makeUInt(8);
  if (name == "u16") return Type::makeUInt(16);
  if (name == "u32") return Type::makeUInt(32);
  if (name == "u64") return Type::makeUInt(64);
  return Type::makeVoid(); // não é primitivo
}

void Semantic::checkNew(NewExpr* n) {
  // re-checagem do inicializador (`var x = new ...` é analisado 2x)
  if (n->resolved) return;
  if (n->className == "list") {
    // `new list<T>()` / `new list<T>(cap)` — builtin do runtime (bug 1.4)
    n->isList = true;
    n->resolved = true;
    if (n->genericArgs.size() != 1) {
      error(n->line, "list<T> exige 1 argumento de tipo (o tipo do elemento)");
      n->exprType = Type::makeList(Type::makeVoid());
      return;
    }
    // canonicaliza o elemento (ex.: `Caixa<int>`/`Repo<int>` cru vira
    // `main.Caixa[i32]`); sem isso `list<X<Y>> = new list<X<Y>>()` falhava
    // por desigualdade de nomes cru vs. canônico
    n->exprType = Type::makeList(resolveType(n->genericArgs[0], n->line));
    for (auto& a : n->args) checkExpr(a.get());
    if (n->args.size() > 1)
      error(n->line, "new list<T>(cap) aceita no máximo 1 argumento (a capacidade)");
    return;
  }
  if (n->className == "map") {
    // `new map<K,V>()` — builtin do runtime
    n->isList = false;
    n->resolved = true;
    if (n->genericArgs.size() != 2) {
      error(n->line, "map<K,V> exige 2 argumentos de tipo (chave e valor)");
      n->exprType = Type::makeMap(Type::makeVoid(), Type::makeVoid());
      return;
    }
    // idem list<T> acima: canonicaliza chave e valor
    n->exprType = Type::makeMap(resolveType(n->genericArgs[0], n->line),
                                resolveType(n->genericArgs[1], n->line));
    for (auto& a : n->args) checkExpr(a.get());
    return;
  }
  if (n->isArrayNew) {
    // `new int[1000]` / `new Foo[10]` — array alocado na heap (bug 1.7)
    n->resolved = true;
    Type et = primitiveTypeFromName(n->className);
    if (et.kind == Type::Kind::Void) {
      et = resolveType(Type::makeClass(n->className), n->line);
      if (et.kind == Type::Kind::Class && et.name == n->className &&
          !classes_.count(et.name))
        error(n->line, "tipo desconhecido '" + n->className + "' no new de array");
    }
    n->exprType = Type::makeArray(et, n->arraySize);
    return;
  }
  if (n->className == "channel") {
    // `new channel<T>(cap)` (spec §10, M2): builtin do runtime — channel é
    // keyword, então o nome nunca colide com uma classe do usuário
    n->isChannel = true;
    n->resolved = true;
    if (n->genericArgs.size() != 1) {
      error(n->line, "channel<T> exige 1 argumento de tipo (o tipo do payload)");
      n->exprType = Type::makeChannel(Type::makeVoid());
      return;
    }
    // M11.3: canonicaliza o payload — o parse pode deixar o nome da classe
    // sem o prefixo de módulo (`Msg` vs canônico `main.Msg`), o que quebrava
    // a comparação de tipos no Send
    {
      Type pe = n->genericArgs[0];
      if (pe.kind == Type::Kind::Class && !classes_.count(pe.name)) {
        for (auto& kv : classes_) {
          const std::string& key = kv.first;
          if (key.size() > pe.name.size() &&
              key.compare(key.size() - pe.name.size(), pe.name.size(),
                          pe.name) == 0 &&
              key[key.size() - pe.name.size() - 1] == '.') {
            pe.name = key;
            break;
          }
        }
      }
      n->genericArgs[0] = pe;
      n->exprType = Type::makeChannel(pe);
    }
    // M11.3 Safety (spec §3/§10): CHANNEL SENDABLE — o payload do canal
    // atravessa threads; classe/coleção não-sendable não pode ser transportada
    if (!isSendableType(n->genericArgs[0])) {
      std::string tyName =
          n->genericArgs[0].kind == Type::Kind::Class
              ? n->genericArgs[0].name
              : typeKey(n->genericArgs[0]);
      error(n->line, "CHANNEL SENDABLE: canal transporta '" + tyName +
                         "' entre threads — use um tipo escalar/string ou " +
                         "derive 'Sendable' na declaração da classe");
    }
    if (n->args.size() > 1)
      error(n->line, "new channel<T>(cap) aceita no máximo 1 argumento (a capacidade)");
    long long cap = 16;
    if (n->args.size() == 1) {
      auto lit = dynamic_cast<IntLitExpr*>(n->args[0].get());
      if (!lit || lit->value <= 0)
        error(n->line, "capacidade do channel deve ser um literal inteiro > 0");
      else
        cap = lit->value;
    }
    n->channelCapacity = cap;
    return;
  }
  Type pt = primitiveTypeFromName(n->className);
  if (pt.kind != Type::Kind::Void) {
    // `new int(0)` / `new double(1.5)` / `new string("x")` — aloca um
    // primitivo na heap (bug 1.7). Retorna o tipo primitivo (valor).
    n->resolved = true;
    n->exprType = pt;
    for (auto& a : n->args) checkExpr(a.get());
    return;
  }
  Type t;
  if (!n->genericArgs.empty()) {
    // `new Caixa<int>(...)` �?? resolve o template com os tipos explícitos
    Type clsT = Type::makeClass(n->className);
    clsT.genericArgs = n->genericArgs;
    t = resolveType(clsT, n->line);
  } else {
    t = resolveType(Type::makeClass(n->className), n->line);
  }
  if (t.kind != Type::Kind::Class) {
    error(n->line, "'" + n->className + "' é enum, não classe");
  }
  n->className = t.name; // canônico (instância, quando genérica)
  auto it = classes_.find(n->className);
  ClassInfo& ci = it->second;
  if (ci.decl->isDeprecated && !isDeprecationSuppressed()) {
    std::string msg = "'" + ci.decl->name + "' is deprecated";
    if (!ci.decl->deprecatedReason.empty()) msg += ": " + ci.decl->deprecatedReason;
    warn(n->line, msg);
  }
  if (ci.decl->isInterface)
    error(n->line, "interface '" + n->className + "' não pode ser instanciada com new");
  if (ci.isTemplate)
    error(n->line, "'" + ci.decl->name + "' é classe genérica: use 'new " +
                       ci.decl->name + "<Tipo>(...)' com os argumentos de tipo");
  // construtores de instância genérica: instancia cada ctor do template
  if (ci.ctors.empty() && ci.isInstance) {
    auto& tci = classes_[ci.templateFrom];
    if (!tci.ctors.empty()) {
      std::vector<std::pair<std::string, Type>> cb;
      for (size_t i = 0; i < ci.typeParamNames.size(); i++)
        cb.push_back({ci.typeParamNames[i], ci.typeArgs[i]});
      for (auto* tc : tci.ctors)
        ci.ctors.push_back(methodInstance(tc, n->className, cb, {}, n->line));
    }
  }
  bool named = false;
  for (auto& nm : n->argNames)
    if (!nm.empty()) named = true;
  if (named)
    for (auto& nm : n->argNames)
      if (nm.empty()) error(n->line, "misturou argumentos nomeados e posicionais em '" +
                                         n->className + "'");
  // overload de construtores: seleciona pela aridade (primeiro que casa, como
  // em findMethod); nomeados exigem tamanho exato
  FunctionDecl* sel = nullptr;
  for (auto* c : ci.ctors) {
    if (named) {
      if (c->params.size() == n->args.size()) { sel = c; break; }
    } else if (arityMatches(c, n->args.size())) { sel = c; break; }
  }
  if (named && !sel)
    error(n->line, "'" + n->className + "' não possui construtor para argumentos nomeados");
  if (!named && sel) {
    n->ctor = sel;
    if (sel->isDeprecated && !isDeprecationSuppressed()) {
      std::string msg = "'" + n->className + "' constructor is deprecated";
      if (!sel->deprecatedReason.empty()) msg += ": " + sel->deprecatedReason;
      warn(n->line, msg);
    }
  } else if (!named) {
    if (ci.ctors.empty()) {
      if (!n->args.empty()) {
        error(n->line, "'" + n->className + "' não possui construtor com argumentos");
      }
    } else {
      std::string ar;
      for (auto* c : ci.ctors) {
        if (!ar.empty()) ar += ", ";
        ar += std::to_string(requiredParams(c)) + " a " + std::to_string(c->params.size());
      }
      error(n->line, "construtor de '" + n->className + "' não aceita " +
                         std::to_string(n->args.size()) + " argumento(s) (aridades: " + ar + ")");
    }
  }
  if (named) {
    // construtor nomeado (spec §12): `LoadError(mensagem: "x", codigo: 12)`
    n->ctor = sel;
    if (sel->isDeprecated && !isDeprecationSuppressed()) {
      std::string msg = "'" + n->className + "' constructor is deprecated";
      if (!sel->deprecatedReason.empty()) msg += ": " + sel->deprecatedReason;
      warn(n->line, msg);
    }
    std::vector<std::unique_ptr<Expr>> ordered(n->args.size());
    std::set<std::string> used;
    for (size_t i = 0; i < n->args.size(); i++) {
      Type pt = resolveType(sel->params[i]->type, sel->params[i]->line);
      auto& nme = n->argNames[i];
      if (used.count(nme)) {
        error(n->line, "argumento nomeado '" + nme + "' repetido em '" + n->className + "'");
      }
      used.insert(nme);
      bool found = false;
      for (size_t j = 0; j < sel->params.size(); j++) {
        if (sel->params[j]->name != nme) continue;
        found = true;
        checkExpr(n->args[i].get(), resolveType(sel->params[j]->type,
                                                sel->params[j]->line));
        ordered[j] = std::move(n->args[i]);
        break;
      }
      if (!found)
        error(n->line, "parâmetro '" + nme + "' não existe no construtor de '" +
                           n->className + "'");
    }
    n->args = std::move(ordered);
  } else {
    for (auto& a : n->args) checkExpr(a.get());
  }
  n->resolved = true;
}

int Semantic::findEnumEntry(const EnumDecl* en, const std::string& path) const {
  std::string last = path;
  size_t dot = path.find_last_of('.');
  if (dot != std::string::npos) last = path.substr(dot + 1);
  for (size_t i = 0; i < en->entries.size(); i++)
    if (en->entries[i].name == last) return (int)i;
  return -1;
}

// ---------------------------------------------------------------------------
// match / pattern matching
// ---------------------------------------------------------------------------
// Checa um padrão ANINHADO contra um tipo esperado, declarando bindings no
// escopo do braço. Usada na raiz do match e dentro de payloads/campos/elementos.
void Semantic::checkNestedPattern(Pattern* p, const Type& want, int line) {
  switch (p->kind) {
    case Pattern::K::Wildcard:
      return;
    case Pattern::K::Const: {
      if (want.kind == Type::Kind::Enum) {
        // `Position(x: Dia.TERCA)` �?? resolve a variante para o valor
        int idx = findEnumEntry(enums_[want.name], p->path);
        if (idx < 0)
          error(line, "variante '" + p->path + "' não pertence ao enum '" + want.name + "'");
        if (enums_[want.name]->entries[idx].hasPayload)
          error(line, "variante com dados '" + p->path + "' exige padrão '(...)'");
        p->constValue = enums_[want.name]->entries[idx].value;
        return;
      }
      if (want.isOption() || want.isResult()) {
        // variante sem dados como constante: `None`, e `Ok`/`Err` de
        // Result<void, E> (não há o que desestruturar)
        if (want.isOption() && p->path == "None") {
          p->constValue = 0;
          return;
        }
        if (want.isResult() && (p->path == "Ok" || p->path == "Err")) {
          const Type* pay =
              p->path == "Ok" ? want.elem.get() : want.elem2.get();
          if (pay->kind != Type::Kind::Void)
            error(line, "padrão '" + p->path + "' exige '(...)' (variante com dados)");
          p->constValue = p->path == "Ok" ? 0 : 1;
          return;
        }
        error(line, "padrão '" + p->path + "' exige '(...)' (variante com dados)");
        return;
      }
      if (!want.isInteger() && want.kind != Type::Kind::Char)
        error(line, "padrao constante exige inteiro ou char");
      return;
    }
    case Pattern::K::StrConst:
      if (want.kind != Type::Kind::String)
        error(line, "padr�o de string exige sujeito string");
      return;
    case Pattern::K::Range:
      if (!want.isInteger() && want.kind != Type::Kind::Char)
        error(line, "faixa no padrao exige inteiro ou char");
      return;
    case Pattern::K::Bind: {
      if (scopes_.back().symbols.count(p->bindName))
        error(line, "binding '" + p->bindName + "' repetido no braço");
      p->bindType = want;
      declareVar(p->bindName, want, StoragePolicy::Stack, false, line);
      return;
    }
    case Pattern::K::Variant: {
      if (want.kind == Type::Kind::Enum) {
        auto* en = enums_[want.name];
        int idx = findEnumEntry(en, p->path);
        if (idx < 0)
          error(line, "variante '" + p->path + "' não pertence ao enum '" + want.name + "'");
        auto& entry = en->entries[idx];
        if (!entry.hasPayload)
          error(line, "variante sem dados '" + entry.name + "' não usa padrão '(...)'; use " +
                         want.name + "." + entry.name);
        if (entry.params.size() != p->subs.size())
          error(line, "variante '" + entry.name + "' espera " +
                         std::to_string(entry.params.size()) + " padrão(ões), veio " +
                         std::to_string(p->subs.size()));
        p->constValue = entry.value; // tag
        for (size_t i = 0; i < p->subs.size(); i++)
          checkNestedPattern(p->subs[i].get(), entry.params[i].type, line);
        return;
      }
      if (want.isOption() || want.isResult()) {
        if (p->path == "None" && want.isOption()) {
          if (!p->subs.empty())
            error(line, "'None' não aceita padrão '(...)'");
          p->constValue = 0;
          return;
        }
        if (p->path == "Some" && want.isOption()) {
          if (p->subs.size() != 1)
            error(line, "'Some(...)' espera 1 padrão");
          p->constValue = 1;
          checkNestedPattern(p->subs[0].get(), *want.elem, line);
          return;
        }
        if (p->path == "Ok" && want.isResult()) {
          bool voidPay = want.elem->kind == Type::Kind::Void;
          if (voidPay ? !p->subs.empty() : p->subs.size() != 1)
            error(line, voidPay ? "'Ok' de Result<void, E> não recebe '(..)'"
                                : "'Ok(...)' espera 1 padrão");
          p->constValue = 0;
          if (!voidPay) checkNestedPattern(p->subs[0].get(), *want.elem, line);
          return;
        }
        if (p->path == "Err" && want.isResult()) {
          bool isVoidPay = want.elem2->kind == Type::Kind::Void;
          if (isVoidPay && !p->subs.empty())
            error(line, "'Err' de Result<void, E> não recebe '(..)'");
          if (!isVoidPay && p->subs.size() != 1) error(line, "'Err(...)' espera 1 padrão");
          p->constValue = 1;
          if (!isVoidPay) checkNestedPattern(p->subs[0].get(), *want.elem2, line);
          return;
        }
        error(line, "padrão de variante '" + p->path + "' não pertence ao sujeito " +
                       (want.isOption() ? "Option" : "Result"));
      }
      error(line, "padrão de variante '" + p->path + "' exige sujeito enum, Option ou Result");
      return;
    }
    case Pattern::K::Struct: {
      if (want.kind != Type::Kind::Class || !classes_[want.name].decl->isStruct)
        error(line, "padrão de struct '" + p->path + "' exige sujeito struct");
      auto& ci = classes_[want.name];
      if (p->subNames.size() != p->subs.size()) {
        // inalcançável (parser mantém paralelos); defesa apenas
        error(line, "padrão de struct malformado");
      }
      for (size_t i = 0; i < p->subs.size(); i++) {
        Type ft = typeOfMember(ci, p->subNames[i], nullptr);
        if (ft.kind == Type::Kind::Void)
          error(line, "campo '" + p->subNames[i] + "' não existe em '" + want.name + "'");
        checkNestedPattern(p->subs[i].get(), ft, line);
      }
      return;
    }
    case Pattern::K::List: {
      if (want.kind == Type::Kind::List) {
        if (p->hasRest) {
          if (scopes_.back().symbols.count(p->restName))
            error(line, "binding '.." + p->restName + "' repetido no braço");
          p->restType = want;
          declareVar(p->restName, want, StoragePolicy::Stack, false, line);
        }
        for (auto& sub : p->subs)
          checkNestedPattern(sub.get(), *want.elem, line);
        return;
      }
      if (want.kind == Type::Kind::Array) {
        if (p->hasRest)
          error(line, "'..resto' só vale para list (arrays têm tamanho fixo)");
        if (p->subs.size() != (size_t)want.arraySize)
          error(line, "padrão de array '" + std::to_string(want.arraySize) +
                         "' deve ter exatamente " + std::to_string(want.arraySize) +
                         " elemento(s)");
        for (auto& sub : p->subs)
          checkNestedPattern(sub.get(), *want.elem, line);
        return;
      }
      error(line, "padrão de lista exige sujeito list ou array");
      return;
    }
  }
}

Type Semantic::checkMatch(MatchExpr* m, const Type& expected) {
  Type st = checkExpr(m->subject.get());
  bool isStructSubj = st.kind == Type::Kind::Class && classes_[st.name].decl->isStruct;
  bool isListSubj = st.kind == Type::Kind::List;
  bool isArraySubj = st.kind == Type::Kind::Array;
  if (!st.isInteger() && st.kind != Type::Kind::Enum && !st.isOption() && !st.isResult() &&
      !isStructSubj && !isListSubj && !isArraySubj && st.kind != Type::Kind::String &&
      st.kind != Type::Kind::Char)
    error(m->line,
          "match exige sujeito inteiro, char, string, enum, Option, Result, struct, list ou array");

  bool isEnum = st.kind == Type::Kind::Enum;
  bool isOpt = st.isOption();
  bool isRes = st.isResult();
  EnumDecl* en = isEnum ? enums_[st.name] : nullptr;

  std::set<long long> covered;      // tags cobertas (Const/Variant)
  std::set<long long> coveredPlain; // tags cobertas por braço SEM guarda
  bool hasWildcard = false;
  bool hasListAll = false;          // braço que casa QUALQUER list (wildcard ou `[..r]`)
  bool hasListRest = false;         // algum braço de list com `..resto`
  bool hasListEmpty = false;        // braço `[]`
  bool hasArrayAll = false;         // braço que casa o array inteiro (tamanho exato ou `_`)
  bool anyYield = false, anyBlock = false;
  Type yieldType;

  for (auto& arm : m->arms) {
    pushScope();
    // binding do sujeito: `x @ padrão`
    if (arm.hasSubjectBind) {
      arm.subjectBindType = st;
      if (arm.subjectBind != "_") {
        if (scopes_.back().symbols.count(arm.subjectBind))
          error(arm.line, "binding '" + arm.subjectBind + "' repetido no braço");
        declareVar(arm.subjectBind, st, StoragePolicy::Stack, false, arm.line);
      }
    }
    Pattern* p = arm.pattern.get();
    checkNestedPattern(p, st, arm.line);
    if (p->kind == Pattern::K::Wildcard) {
      hasWildcard = true;
      if (isListSubj) hasListAll = true;
      if (isArraySubj) hasArrayAll = true;
    } else if (p->kind == Pattern::K::Const || p->kind == Pattern::K::Variant) {
      if (isEnum || isOpt || isRes) {
        covered.insert(p->constValue);
        if (!arm.guard) {
          // duplicidade só conta para variantes sem sub-patterns (ex. `None`,
          // `Dia.TERCA`); `Some(...)` difere pelo payload
          if (p->subs.empty() && coveredPlain.count(p->constValue))
            error(arm.line,
                  "padrão duplicado no match (braço anterior sem guarda)");
          coveredPlain.insert(p->constValue);
        }
      }
    } else if (p->kind == Pattern::K::List) {
      if (isListSubj) {
        if (p->hasRest) {
          if (p->subs.empty() && !arm.guard) hasListAll = true;
          hasListRest = true;
        } else if (p->subs.empty()) {
          hasListEmpty = true;
        }
      }
      if (isArraySubj && !p->hasRest &&
          p->subs.size() == (size_t)st.arraySize && !arm.guard)
        hasArrayAll = true;
    }

    // guarda: `when <cond>`
    if (arm.guard) {
      Type gt = checkExpr(arm.guard.get());
      if (gt.kind != Type::Kind::Bool)
        error(arm.guard->line, "guarda 'when' deve ser bool");
    }

    // corpo
    if (arm.yield) {
      anyYield = true;
      Type yt = checkExpr(arm.yield.get(), expected);
      if (yt.kind == Type::Kind::Void)
        error(arm.yield->line, "braço '=>' de match não pode ser void");
      if (!yieldType.isUnknown() && yieldType != yt && !isAssignable(yieldType, yt) &&
          !isAssignable(yt, yieldType) && !isNumericBinary(yieldType, yt))
        error(arm.yield->line, "tipos dos braços do match diferem");
      if (yieldType.isUnknown()) yieldType = yt;
      arm.yieldType = yieldType;
    } else {
      anyBlock = true;
      for (auto& s : arm.body) checkStatement(s.get(), Type::makeVoid());
    }
    popScope();
  }

  if (anyYield && anyBlock)
    error(m->line, "braços do match misturam '=> expr' e bloco '{ ... }'");

  // exaustividade
  if (isEnum) {
    for (auto& e : en->entries)
      if (!covered.count(e.value) && !hasWildcard)
        error(m->line, "match não exaustivo: variante '" + e.name +
                           "' não coberta (adicione '_')");
  } else if (isOpt || isRes) {
    // Option: None(0) + Some(1); Result: Ok(0) + Err(1)
    const char* names[2] = {};
    if (isOpt) { names[0] = "None"; names[1] = "Some"; }
    else { names[0] = "Ok"; names[1] = "Err"; }
    for (int tag = 0; tag <= 1; tag++)
      if (!covered.count(tag) && !hasWildcard)
        error(m->line, "match não exaustivo: '" + std::string(names[tag]) +
                           "' não coberto (adicione '_')");
  } else if (isListSubj) {
    // `[]` + braço com `..resto` cobre 0 e �?� prefixo; `_` ou `[..r]` cobre tudo
    if (!hasWildcard && !hasListAll && !(hasListEmpty && hasListRest))
      error(m->line, "match sobre list exige '_'/'[..resto]' ou '[]' + braço '..resto'");
  } else if (isArraySubj) {
    if (!hasWildcard && !hasArrayAll)
      error(m->line, "match sobre array exige um padr�o do tamanho exato ou '_'");
  } else if (st.kind == Type::Kind::String) {
    // strings: cobertura total nao e verificavel — exige '_' final
    if (!hasWildcard)
      error(m->line, "match sobre string exige um padrao '_' final");
  } else if (st.kind == Type::Kind::Char) {
    if (!hasWildcard)
      error(m->line, "match sobre char exige um padrão '_' final");
  } else {
    if (!hasWildcard)
      error(m->line, "match sobre inteiro exige um padrão '_' final");
  }

  m->exprType = anyYield ? yieldType : Type::makeVoid();
  return m->exprType;
}

// ---------------------------------------------------------------------------
// Option/Result (spec seção 12)
// ---------------------------------------------------------------------------
// `Some(x)` / `None` / `Ok(x)` / `Err(e)` �?? células [tag][payload]; o TIPO vem
// do contexto (inicializador, return, argumento; `var x = Some(5)` exige tipo).
// `Result<void, E>`: `Ok`/`Ok()` são válidos sem payload.
// `e?.campo` �?? acesso condicional a referência de classe (spec §12). A forma de
// método (`e?.M()`) é tratada em checkCall (CallExpr acima do OptMemberExpr).
Type Semantic::checkOptMember(OptMemberExpr* om) {
  Type objType = checkExpr(om->object.get());
  if (objType.kind != Type::Kind::Class)
    error(om->line, "'?.' exige referência de classe (nullable); '" +
                        objType.name + "' não é classe");
  auto& ci = classes_[objType.name];
  Type ft = typeOfMember(ci, om->member, &om->fieldOffset);
  if (ft.kind == Type::Kind::Void)
    error(om->line, "'" + om->member + "' não é campo de '" + objType.name +
                        "' (se for método, use '(..)')");
  om->isField = true;
  if (ft.kind == Type::Kind::Class || ft.kind == Type::Kind::String)
    om->exprType = ft; // referência null-merged
  else
    om->exprType = Type::makeOption(ft);
  return om->exprType;
}

Type Semantic::checkOptIndex(OptIndexExpr* oi) {
  Type ot = checkExpr(oi->object.get());
  if (ot.kind != Type::Kind::Array && ot.kind != Type::Kind::List && ot.kind != Type::Kind::String) {
    error(oi->line, "'?[' exige array, list ou string");
  }
  Type it = checkExpr(oi->index.get());
  if (!it.isInteger()) {
    error(oi->line, "índice deve ser inteiro");
  }
  Type elemT = (ot.kind == Type::Kind::String) ? Type::makeChar() : *ot.elem;
  if (elemT.kind == Type::Kind::Class || elemT.kind == Type::Kind::String) {
    oi->exprType = elemT;
  } else {
    oi->exprType = Type::makeOption(elemT);
  }
  return oi->exprType;
}

Type Semantic::checkOptCtor(OptCtorExpr* o, const Type& expected) {
  if (o->variant == "None") {
    if (expected.isUnknown())
      error(o->line, "'None' sem tipo esperado; declare o tipo (Option<T>)");
    if (!expected.isOption())
      error(o->line, "'None' exige alvo Option<T>");
    o->exprType = expected;
    return expected;
  }
  if (o->variant == "Some") {
    if (expected.isUnknown())
      error(o->line, "'Some(x)' sem tipo esperado; declare o tipo (Option<T>)");
    if (!expected.isOption())
      error(o->line, "'Some(x)' exige alvo Option<T>");
    if (!o->arg)
      error(o->line, "'Some' exige '(...)' com o payload");
    Type it = checkExpr(o->arg.get(), *expected.elem);
    if (!isAssignable(*expected.elem, it))
      error(o->arg->line, "payload de 'Some' incompatível com Option<T>");
    o->exprType = expected;
    return expected;
  }
  // Ok / Err
  if (expected.isUnknown() || !expected.isResult())
    error(o->line, "'" + o->variant + "(x)' exige alvo Result<T, E>");
  const Type* want = o->variant == "Ok" ? expected.elem.get() : expected.elem2.get();
  if (want->kind == Type::Kind::Void) {
    // Result<void, E>: Ok/Err usados como sinal, sem payload
    if (o->arg)
      error(o->arg->line, "'" + o->variant + "' de Result<void, E> não recebe payload");
  } else {
    if (!o->arg)
      error(o->line, "'" + o->variant + "(x)' exige '(...)' com o payload");
    Type it = checkExpr(o->arg.get(), *want);
    if (!isAssignable(*want, it))
      error(o->arg->line, "payload de '" + o->variant + "' incompatível com Result<T, E>");
  }
  o->exprType = expected;
  return expected;
}

// `x?` �?? desembrulha Option/Result; no None/Err, o valor é devolvido como
// retorno da função (que precisa ser Option/Result da mesma forma).
Type Semantic::checkTry(TryExpr* t) {
  Type ot = checkExpr(t->operand.get());
  if (!ot.isOption() && !ot.isResult())
    error(t->line, "'?' só se aplica a Option ou Result");
  if (!currentFunction_ || currentFunction_->returnType.kind == Type::Kind::Void)
    error(t->line, "'?' exige função retornando Option ou Result");
  Type ret = resolveType(currentFunction_->returnType, currentFunction_->line);
  if (ot.isOption()) {
    if (!ret.isOption())
      error(t->line, "'?' sobre Option exige função que retorna Option");
    if (!isAssignable(*ret.elem, *ot.elem))
      error(t->line, "Option interna diverge do retorno: '" + ret.name + "'");
  } else {
    if (!ret.isResult())
      error(t->line, "'?' sobre Result exige função que retorna Result");
    if (!isAssignable(*ret.elem, *ot.elem) || !isAssignable(*ret.elem2, *ot.elem2))
      error(t->line, "Result diverge do retorno da função");
  }
  t->exprType = *ot.elem;
  return t->exprType;
}

// `a ?? padrao` �?? valor do Some(a) ou o padrão
Type Semantic::checkCoalesce(CoalesceExpr* c) {
  Type lt = checkExpr(c->lhs.get());
  Type rt = checkExpr(c->rhs.get());
  if (lt.isOption()) {
    if (!isAssignable(*lt.elem, rt))
      error(c->rhs->line, "lado direito do ?? deve ser do tipo do valor da Option");
    c->exprType = *lt.elem;
    return c->exprType;
  }
  if (lt.isResult()) {
    if (lt.elem && lt.elem->kind == Type::Kind::Void)
      error(c->lhs->line, "operador ?? não é suportado para Result<void, E>");
    if (!isAssignable(*lt.elem, rt))
      error(c->rhs->line, "lado direito do ?? deve ser do tipo do valor do Result");
    c->exprType = *lt.elem;
    return c->exprType;
  }
  // Bug 1.9: `tail ?? firstNo` com referência nulável (classe/string/list/...)
  // — o `??` também desambigua ponteiros nuláveis, não só Option<T>.
  bool nullableRef = lt.kind == Type::Kind::Class || lt.kind == Type::Kind::String ||
                     lt.kind == Type::Kind::List || lt.kind == Type::Kind::Map ||
                     lt.kind == Type::Kind::Task || lt.kind == Type::Kind::Channel;
  if (nullableRef) {
    if (!isAssignable(lt, rt))
      error(c->rhs->line, "lado direito do ?? deve ser compatível com o esquerdo");
    c->exprType = lt;
    return c->exprType;
  }
  error(c->lhs->line, "lado esquerdo do ?? deve ser Option<T>, Result<T, E> ou referência nulável");
  c->exprType = rt;
  return c->exprType;
}

void Semantic::checkArrayInit(Expr* init, const Type& target) {
  auto* al = static_cast<ArrayLitExpr*>(init);
  if (target.kind != Type::Kind::Array) {
    error(init->line, "inicializador de array para tipo não-array");
  }
  if ((int)al->elements.size() != target.arraySize) {
    error(init->line, "array espera " + std::to_string(target.arraySize) + " elemento(s), "
                      "obteve " + std::to_string(al->elements.size()));
  }
  for (auto& el : al->elements) {
    if (el->kind == ExprKind::ArrayLit) {
      checkArrayInit(el.get(), *target.elem); // sub-array (multidimensional)
    } else {
      Type et = checkExpr(el.get());
      if (!isAssignable(*target.elem, et)) {
        error(el->line, "elemento de tipo incompatível no array");
      }
    }
  }
  init->exprType = target;
}

PropInfo* Semantic::findProperty(ClassInfo& ci, const std::string& name) {
  auto it = ci.properties.find(name);
  if (it != ci.properties.end()) return &it->second;
  if (!ci.base.empty()) {
    auto bit = classes_.find(ci.base);
    if (bit != classes_.end()) return findProperty(bit->second, name);
  }
  return nullptr;
}

Type Semantic::typeOfMember(ClassInfo& ci, const std::string& member, int* offsetOut) {
  auto it = ci.fields.find(member);
  if (it != ci.fields.end()) {
    if (offsetOut) *offsetOut = it->second.second;
    return it->second.first;
  }
  // base
  if (!ci.base.empty()) {
    auto bit = classes_.find(ci.base);
    if (bit != classes_.end()) {
      Type t = typeOfMember(bit->second, member, offsetOut);
      if (t.kind != Type::Kind::Void) return t;
    }
  }
  return Type::makeVoid();
}

bool Semantic::isFieldAtomic(ClassInfo& ci, const std::string& member) {
  auto it = ci.fieldAtomic.find(member);
  if (it != ci.fieldAtomic.end()) return it->second;
  if (!ci.base.empty()) {
    auto bit = classes_.find(ci.base);
    if (bit != classes_.end()) return isFieldAtomic(bit->second, member);
  }
  return false;
}

// M10.4 Safety: variável local com política de ponteiro e tipo por
// referência (class/list/map/array) — candidata a move/double-free
bool Semantic::isRefPolicyLocal(const std::string& name) {
  SymbolInfo si;
  Type t;
  StoragePolicy pol;
  if (!lookup(name, si, t, &pol)) return false;
  if (si.kind != SymbolKind::LocalVar) return false;
  if (pol != StoragePolicy::Heap && pol != StoragePolicy::Arena &&
      pol != StoragePolicy::Pool && pol != StoragePolicy::Shared)
    return false;
  return t.kind == Type::Kind::Class || t.kind == Type::Kind::List ||
         t.kind == Type::Kind::Map || t.kind == Type::Kind::Array;
}

// Classe onde o campo foi DECLARADO (percorre a cadeia de bases).
ClassInfo* Semantic::findFieldOwner(ClassInfo& ci, const std::string& name) {
  ClassInfo* cur = &ci;
  for (int guard = 0; guard < 64 && cur != nullptr; guard++) {
    auto it = cur->fields.find(name);
    if (it != cur->fields.end()) return cur;
    if (cur->base.empty()) break;
    auto bit = classes_.find(cur->base);
    if (bit == classes_.end()) break;
    cur = &bit->second;
  }
  return nullptr;
}

// private: só dentro da própria classe (mesma ClassDecl — instâncias
// genéricas compartilham o decl do template). protected: também nas derivadas.
// Funções sintetizadas pelo compilador (derive Equatable etc.) acessam tudo.
// M11.1 Safety (spec §3/§10): THREAD RACE — escrita em variável GLOBAL
// dentro de spawn/parallel é corrida de dados, a menos que a global seja
// `shared` (intenção explícita), `threadlocal` (sem compartilhamento) ou
// `atomic` (RMW sincronizado). Chamado por checkAssign (`=` e compostas) e
// pelo caminho de ++/--, que antes escapavam da checagem.
void Semantic::checkGlobalRaceWrite(int line, const Expr* target) {
  if (inSpawnDepth_ <= 0) return;
  StoragePolicy pol = StoragePolicy::Auto;
  bool isAtomic = false;
  bool isGlobalWrite = false;
  std::string gname;
  if (target->kind == ExprKind::Ident) {
    auto* id = static_cast<const IdentExpr*>(target);
    if (id->symbol.kind == SymbolKind::GlobalVar) {
      Type t2;
      SymbolInfo siCopy = id->symbol;
      if (lookup(id->name, siCopy, t2, &pol)) {
        isGlobalWrite = true;
        isAtomic = id->symbol.atomic;
        gname = id->name;
      }
    }
  } else if (target->kind == ExprKind::Member) {
    auto* m = static_cast<const MemberExpr*>(target);
    if (m->isGlobalRef && m->resolvedGlobal) {
      isGlobalWrite = true;
      pol = m->resolvedGlobal->storage;
      isAtomic = m->fieldAtomic;
      gname = m->resolvedGlobal->name;
    }
  }
  if (isGlobalWrite && pol != StoragePolicy::Shared &&
      pol != StoragePolicy::ThreadLocal && !isAtomic)
    error(line, "THREAD RACE: escrita na global '" + gname +
                       "' dentro de 'spawn'/'parallel' sem sincronização — "
                       "declare-a como 'shared " + gname + ";' (ou "
                       "'threadlocal'/'atomic')");
}

bool Semantic::canAccessMember(const ClassInfo& ownerCi, Access acc) {
  if (acc == Access::Public) return true;
  if (acc == Access::Internal)
    return ownerCi.decl->moduleName == curModule_ ||
           ownerCi.decl->filePath == curFile_;
  if (!currentFunction_) return false;
  if (currentFunction_->isSynthetic) return true;
  if (!currentFunction_->isMethod) return false;
  auto it = classes_.find(currentFunction_->ownerClass);
  if (it == classes_.end()) return false;
  const ClassInfo* cur = &it->second;
  if (acc == Access::Private) return cur->decl == ownerCi.decl;
  // protected: classe corrente ou qualquer ancestral dela
  for (int guard = 0; guard < 64 && cur != nullptr; guard++) {
    if (cur->decl == ownerCi.decl) return true;
    if (cur->base.empty()) break;
    auto bit = classes_.find(cur->base);
    if (bit == classes_.end()) break;
    cur = &bit->second;
  }
  return false;
}

// ---------------------------------------------------------------------------
// M10.1c (v0.46): funções `compiletime` (spec §32) — interpretador mínimo do
// subconjunto puro: locais int, aritmética/comparação/bitwise, unários,
// ternário, if/else, atribuição simples/composta e chamadas aninhadas a
// compiletime. Fora do subconjunto → nullopt (a chamada vira código normal).
// ---------------------------------------------------------------------------

std::optional<long long>
Semantic::evalCtExpr(Expr* e, std::map<std::string, long long>& env,
                     FunctionDecl* fn, int depth) {
  switch (e->kind) {
    case ExprKind::IntLit:
      return static_cast<IntLitExpr*>(e)->value;
    case ExprKind::Ident: {
      auto it = env.find(static_cast<IdentExpr*>(e)->name);
      if (it != env.end()) return it->second;
      return std::nullopt;
    }
    case ExprKind::Binary: {
      auto* b = static_cast<BinaryExpr*>(e);
      auto l = evalCtExpr(b->lhs.get(), env, fn, depth);
      auto r = evalCtExpr(b->rhs.get(), env, fn, depth);
      if (!l || !r) return std::nullopt;
      switch (b->op) {
        case BinOp::Add: return *l + *r;
        case BinOp::Sub: return *l - *r;
        case BinOp::Mul: return *l * *r;
        case BinOp::Div:
          if (*r == 0) return std::nullopt;
          return *l / *r;
        case BinOp::Mod:
          if (*r == 0) return std::nullopt;
          return *l % *r;
        case BinOp::Eq: return (long long)(*l == *r);
        case BinOp::Ne: return (long long)(*l != *r);
        case BinOp::Lt: return (long long)(*l < *r);
        case BinOp::Gt: return (long long)(*l > *r);
        case BinOp::Le: return (long long)(*l <= *r);
        case BinOp::Ge: return (long long)(*l >= *r);
        case BinOp::And: return (long long)(*l != 0 && *r != 0);
        case BinOp::Or: return (long long)(*l != 0 || *r != 0);
        case BinOp::BitAnd: return *l & *r;
        case BinOp::BitOr: return *l | *r;
        case BinOp::BitXor: return *l ^ *r;
        case BinOp::Shl: return *l << *r;
        case BinOp::Shr: return *l >> *r;
      }
      return std::nullopt;
    }
    case ExprKind::Unary: {
      auto* u = static_cast<UnaryExpr*>(e);
      switch (u->op) {
        case UnOp::Neg: {
          auto v = evalCtExpr(u->operand.get(), env, fn, depth);
          if (!v) return std::nullopt;
          return -*v;
        }
        case UnOp::Not: {
          auto v = evalCtExpr(u->operand.get(), env, fn, depth);
          if (!v) return std::nullopt;
          return (long long)(*v == 0);
        }
        case UnOp::BitNot: {
          auto v = evalCtExpr(u->operand.get(), env, fn, depth);
          if (!v) return std::nullopt;
          return ~*v;
        }
        default:
          return std::nullopt; // ++/-- têm efeito; não dobramos
      }
    }
    case ExprKind::Ternary: {
      auto* t = static_cast<TernaryExpr*>(e);
      auto c = evalCtExpr(t->cond.get(), env, fn, depth);
      if (!c) return std::nullopt;
      return evalCtExpr((*c != 0 ? t->thenExpr : t->elseExpr).get(), env, fn,
                        depth);
    }
    case ExprKind::Call: {
      auto* c = static_cast<CallExpr*>(e);
      if (!c->resolved || !c->resolved->isCompiletime) return std::nullopt;
      std::vector<long long> args;
      for (auto& a : c->args) {
        auto v = evalCtExpr(a.get(), env, fn, depth);
        if (!v) return std::nullopt;
        args.push_back(*v);
      }
      return evalCompiletime(c->resolved, args, depth + 1);
    }
    default:
      return std::nullopt;
  }
}

bool Semantic::evalCtBlock(BlockStmt* b, std::map<std::string, long long>& env,
                           std::optional<long long>* result, FunctionDecl* fn,
                           int depth) {
  for (auto& s : b->stmts) {
    switch (s->kind) {
      case StmtKind::VarDecl: {
        auto* vd = static_cast<StmtVarDecl*>(s.get());
        for (auto& dcl : vd->decls) {
          if (!dcl->init) return false; // sem inicializador não avaliamos
          auto v = evalCtExpr(dcl->init.get(), env, fn, depth);
          if (!v) return false;
          env[dcl->name] = *v;
        }
        break;
      }
      case StmtKind::Return: {
        auto* r = static_cast<ReturnStmt*>(s.get());
        if (!r->value) {
          *result = 0;
          return true;
        }
        auto v = evalCtExpr(r->value.get(), env, fn, depth);
        if (!v) return false;
        *result = *v;
        return true;
      }
      case StmtKind::If: {
        auto* i = static_cast<IfStmt*>(s.get());
        auto c = evalCtExpr(i->cond.get(), env, fn, depth);
        if (!c) return false;
        if (*c != 0) {
          if (i->thenBranch->kind == StmtKind::Block) {
            if (!evalCtBlock(static_cast<BlockStmt*>(i->thenBranch.get()), env,
                             result, fn, depth))
              return false;
            // v0.46: o bloco interno pode ter retornado — não continuar
            if (result->has_value()) return true;
          } else if (i->thenBranch->kind == StmtKind::Return) {
            auto* r = static_cast<ReturnStmt*>(i->thenBranch.get());
            auto v = r->value ? evalCtExpr(r->value.get(), env, fn, depth)
                              : std::optional<long long>(0);
            if (!v) return false;
            *result = *v;
            return true;
          } else {
            return false;
          }
        } else if (i->elseBranch) {
          if (i->elseBranch->kind == StmtKind::Block) {
            if (!evalCtBlock(static_cast<BlockStmt*>(i->elseBranch.get()), env,
                             result, fn, depth))
              return false;
            if (result->has_value()) return true;
          } else if (i->elseBranch->kind == StmtKind::Return) {
            auto* r = static_cast<ReturnStmt*>(i->elseBranch.get());
            auto v = r->value ? evalCtExpr(r->value.get(), env, fn, depth)
                              : std::optional<long long>(0);
            if (!v) return false;
            *result = *v;
            return true;
          } else {
            return false;
          }
        }
        break;
      }
      case StmtKind::ExprStmt: {
        auto* es = static_cast<ExprStmt*>(s.get());
        if (es->expr->kind != ExprKind::Assign) return false;
        auto* a = static_cast<AssignExpr*>(es->expr.get());
        if (a->target->kind != ExprKind::Ident) return false;
        auto v = evalCtExpr(a->value.get(), env, fn, depth);
        if (!v) return false;
        auto it = env.find(static_cast<IdentExpr*>(a->target.get())->name);
        if (it == env.end()) return false;
        switch (a->op) {
          case AssignOp::Plain: it->second = *v; break;
          case AssignOp::Add: it->second += *v; break;
          case AssignOp::Sub: it->second -= *v; break;
          case AssignOp::Mul: it->second *= *v; break;
          case AssignOp::Div:
            if (*v == 0) return false;
            it->second /= *v;
            break;
          case AssignOp::Mod:
            if (*v == 0) return false;
            it->second %= *v;
            break;
        }
        break;
      }
      case StmtKind::Block:
        if (!evalCtBlock(static_cast<BlockStmt*>(s.get()), env, result, fn,
                         depth))
          return false;
        if (result->has_value()) return true; // return dentro do bloco aninhado
        break;
      default:
        return false; // laços/match/try fora do MVP de compilação
    }
  }
  return true; // fim do bloco sem return
}

std::optional<long long> Semantic::evalCompiletime(
    FunctionDecl* fn, const std::vector<long long>& args, int depth) {
  if (depth > 64 || !fn->body) return std::nullopt; // recursão demais/sem corpo
  std::map<std::string, long long> env;
  for (size_t i = 0; i < fn->params.size() && i < args.size(); i++)
    env[fn->params[i]->name] = args[i];
  std::optional<long long> res;
  if (!evalCtBlock(fn->body.get(), env, &res, fn, depth)) return std::nullopt;
  if (!res) return std::nullopt; // terminou sem return
  return res;
}

void Semantic::validateAtomicDecl(int line, const Type& t, bool atomic) {
  if (!atomic) return;
  if (!t.isInteger())
    error(line, "'atomic' é válido apenas para tipos inteiros (int, u8..u64, char)");
}

FunctionDecl* Semantic::findMethod(ClassInfo& ci, const std::string& name, size_t argc,
                                   int line, const std::vector<Type>* explicitArgs) {
  if (ci.isInstance) {
    for (auto* m : ci.methods) {
      if (m->name == name && arityMatches(m, argc)) {
        if (explicitArgs && !explicitArgs->empty() && m->typeParams.empty())
          error(line, "'" + name + "' não é genérico (remova <...>)");
        return m;
      }
    }
    // instância de classe genérica: procura no template e instancia o método
    auto& tci = classes_[ci.templateFrom];
    FunctionDecl* fnd = nullptr;
    for (auto* m : tci.methods)
      if (m->name == name && arityMatches(m, argc)) { fnd = m; break; }
    if (!fnd) {
      if (!ci.base.empty()) {
        auto bit = classes_.find(ci.base);
        if (bit != classes_.end()) return findMethod(bit->second, name, argc, line, explicitArgs);
      }
      return nullptr;
    }
    std::vector<std::pair<std::string, Type>> cb;
    for (size_t i = 0; i < ci.typeParamNames.size(); i++)
      cb.push_back({ci.typeParamNames[i], ci.typeArgs[i]});
    if (fnd->typeParams.empty()) {
      if (explicitArgs && !explicitArgs->empty())
        error(line, "'" + name + "' não é genérico (remova <...>)");
      return methodInstance(fnd, mangleClassName(ci.templateFrom, ci.typeArgs), cb, {}, line);
    }
    // método genérico: exige tipos explícitos (M1 não infere métodos)
    if (!explicitArgs || explicitArgs->empty()) {
      std::string tps;
      for (auto& tp : fnd->typeParams) { if (!tps.empty()) tps += ","; tps += tp.name; }
      error(line, "método genérico '" + name + "' exige os tipos: <" + tps + ">");
    }
    std::vector<Type> own;
    for (auto& ga : *explicitArgs) own.push_back(resolveType(ga, line));
    return methodInstance(fnd, mangleClassName(ci.templateFrom, ci.typeArgs), cb, own, line);
  }
  for (auto* m : ci.methods) {
    if (m->name == name && arityMatches(m, argc)) {
      if (explicitArgs && !explicitArgs->empty() && m->typeParams.empty())
        error(line, "'" + name + "' não é genérico (remova <...>)");
      return m;
    }
  }
  if (!ci.base.empty()) {
    auto bit = classes_.find(ci.base);
    if (bit != classes_.end()) return findMethod(bit->second, name, argc, line, explicitArgs);
  }
  return nullptr;
}

FunctionDecl* Semantic::findFunction(const std::string& name, size_t argc, int line) {
  std::vector<FunctionDecl*> cands;
  for (auto& fi : functions_) {
    auto* fn = fi.decl;
    if (fn->name == name && !fn->isMethod && arityMatches(fn, argc))
      cands.push_back(fn);
  }
  std::vector<FunctionDecl*> vis;
  for (auto* fn : cands) {
    if (visibleFrom(fn->moduleName, fn->filePath, fn->access)) vis.push_back(fn);
  }
  if (vis.empty()) {
    if (!cands.empty())
      error(line, "função '" + name + "' é internal (não visível fora de seu módulo)");
    return nullptr;
  }
  if (vis.size() > 1) {
    std::string mods;
    for (auto* fn : vis) mods += " '" + fn->moduleName + "'";
    error(line, "função '" + name + "' é ambígua (definida em" + mods +
                    "); use nome qualificado (ex.: 'Mod." + name + "')");
  }
  return vis[0];
}

bool Semantic::arityMatches(const FunctionDecl* fn, size_t argc) const {
  if (argc > fn->params.size()) return false;
  size_t required = fn->params.size();
  for (size_t i = 0; i < fn->params.size(); i++) {
    if (fn->params[i]->defaultVal) { required = i; break; }
  }
  return argc >= required;
}

size_t Semantic::requiredParams(const FunctionDecl* fn) const {
  for (size_t i = 0; i < fn->params.size(); i++)
    if (fn->params[i]->defaultVal) return i;
  return fn->params.size();
}

void Semantic::requireLvalue(Expr* e, const std::string& what) {
  bool ok = false;
  switch (e->kind) {
    case ExprKind::Ident: {
      auto* id = static_cast<IdentExpr*>(e);
      ok = !id->isProperty &&
           (id->symbol.kind == SymbolKind::LocalVar ||
            id->symbol.kind == SymbolKind::Param ||
            id->symbol.kind == SymbolKind::Field ||
            id->symbol.kind == SymbolKind::GlobalVar);
      break;
    }
    case ExprKind::Member: {
      auto* m = static_cast<MemberExpr*>(e);
      ok = !m->isProperty && !m->isEnumConst && !m->isArrayLength &&
           !m->isListLength && !m->isModuleTypeRef && !m->isEnumCtor;
      break;
    }
    case ExprKind::Index:
      ok = true;
      break;
    default:
      break;
  }
  if (!ok)
    error(e->line, "argumento " + what + " precisa ser uma variável, campo ou "
                   "elemento de array (lvalue)");
  if (ok && e->kind == ExprKind::Ident && static_cast<IdentExpr*>(e)->symbol.atomic)
    error(e->line, "não é possível passar endereço de variável 'atomic' em " + what +
                   " (quebraria a atomicidade)");
  if (ok && e->kind == ExprKind::Member && static_cast<MemberExpr*>(e)->fieldAtomic)
    error(e->line, "não é possível passar endereço de campo 'atomic' em " + what +
                       " (quebraria a atomicidade)");
}

// ---------------------------------------------------------------------------
// Atribuição definida para 'out' (SPEC §5.2): ler um 'out' antes de atribuí-lo
// em TODOS os caminhos é erro — o callee não pode enxergar o valor antigo do
// lvalue do chamador. O conjunto outAssigned_ acompanha o fluxo (if/else
// interseção, loops conservadores — o corpo pode não rodar, try/catch: o corpo
// pode lançar, lock: throw escapa).
// ---------------------------------------------------------------------------
bool Semantic::isOutParam(const SymbolInfo& si) const {
  if (si.kind != SymbolKind::Param || si.slotIndex < 0) return false;
  if (!currentFunction_) return false;
  return (size_t)si.slotIndex < currentFunction_->params.size() &&
         currentFunction_->params[si.slotIndex]->byOut;
}

std::string Semantic::outDirectName(Expr* arg) {
  if (!arg || arg->kind != ExprKind::Ident) return "";
  SymbolInfo si;
  Type t;
  if (!lookup(static_cast<IdentExpr*>(arg)->name, si, t)) return "";
  return isOutParam(si) ? si.name : "";
}

void Semantic::outReadCheck(int line, const std::string& name) {
  if (!outAssigned_.count(name))
    error(line, "leitura do parâmetro 'out' '" + name +
                "' antes de atribuição definida (atribua '" + name +
                "' antes de ler)");
}

void Semantic::outMarkAssigned(const std::string& name) {
  outAssigned_.insert(name);
}

std::set<std::string> Semantic::outIntersect(const std::set<std::string>& a,
                                             const std::set<std::string>& b) const {
  std::set<std::string> r;
  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                        std::inserter(r, r.begin()));
  return r;
}

bool Semantic::isAssignable(const Type& target, const Type& value) const {
  if (target.kind == Type::Kind::Void && value.kind == Type::Kind::Void)
    return true; // Result<void, E>: payload interno de Ok é void
  if (value.kind == Type::Kind::Void) {
    // null
    return target.isPointer() || target.kind == Type::Kind::String ||
           target.kind == Type::Kind::List || target.kind == Type::Kind::Map;
  }
  if (target == value) return true;
  // M10 (v0.44): subsumption — derivada é atribuível à base (referência)
  if (target.kind == Type::Kind::Class && value.kind == Type::Kind::Class &&
      target.name != value.name) {
    auto tit = classes_.find(target.name);
    auto vit = classes_.find(value.name);
    if (tit != classes_.end() && vit != classes_.end() &&
        tit->second.decl->isInterface && vit->second.decl->isInterface &&
        tit->second.isInstance && vit->second.isInstance &&
        tit->second.templateFrom == vit->second.templateFrom) {
      auto tmplIt = classes_.find(tit->second.templateFrom);
      if (tmplIt != classes_.end() &&
          tit->second.typeArgs.size() == vit->second.typeArgs.size()) {
        bool allMatch = true;
        for (size_t k = 0; k < tit->second.typeArgs.size(); k++) {
          Variance v = k < tmplIt->second.decl->typeParams.size()
                           ? tmplIt->second.decl->typeParams[k].variance
                           : Variance::Invariant;
          if (v == Variance::Covariant) {
            if (!isAssignable(tit->second.typeArgs[k], vit->second.typeArgs[k])) {
              allMatch = false; break;
            }
          } else if (v == Variance::Contravariant) {
            if (!isAssignable(vit->second.typeArgs[k], tit->second.typeArgs[k])) {
              allMatch = false; break;
            }
          } else {
            if (!(tit->second.typeArgs[k] == vit->second.typeArgs[k])) {
              allMatch = false; break;
            }
          }
        }
        if (allMatch) return true;
      }
    }
    if (tit != classes_.end() && tit->second.decl->isInterface) {
      // A2 (interface como tipo): referência de classe (com vtable para
      // dispatch) que a implementa; struct não tem vptr — rejeitado aqui
      // (erro genérico de incompatibilidade no chamador).
      if (vit == classes_.end() || vit->second.decl->isStruct) return false;
      return implementsInterface(value.name, target.name);
    }
    return derivesFrom(value.name, target.name);
  }
  // M10.1b: tupla atribuível quando mesmo arity e cada elemento compatível
  if (target.kind == Type::Kind::Tuple && value.kind == Type::Kind::Tuple) {
    if (target.tupleElems.size() != value.tupleElems.size()) return false;
    for (size_t i = 0; i < target.tupleElems.size(); i++)
      if (!isAssignable(target.tupleElems[i], value.tupleElems[i])) return false;
    return true;
  }
  // Option<T>/Result<T,E>: atribuível quando os tipos internos são compatíveis
  if (target.isOption() && value.isOption())
    return isAssignable(*target.elem, *value.elem);
  if (target.isResult() && value.isResult())
    return isAssignable(*target.elem, *value.elem) &&
           isAssignable(*target.elem2, *value.elem2);
  if (target.kind == Type::Kind::Task && value.kind == Type::Kind::Task) {
    if (!target.elem && !value.elem) return true;
    if (!target.elem || !value.elem) return false;
    return isAssignable(*target.elem, *value.elem);
  }
  if (target.kind == Type::Kind::Channel && value.kind == Type::Kind::Channel) {
    if (!target.elem && !value.elem) return true;
    if (!target.elem || !value.elem) return false;
    return isAssignable(*target.elem, *value.elem);
  }
  // v0.95 (lambdas): `func` com retorno covariante e parâmetros contravariantes
  if (target.kind == Type::Kind::Func && value.kind == Type::Kind::Func) {
    if (!target.elem && !value.elem) {
      // ambos void: compara params abaixo
    } else if (!target.elem || !value.elem) {
      return false;
    } else if (!isAssignable(*target.elem, *value.elem)) {
      return false;
    }
    if (target.genericArgs.size() != value.genericArgs.size()) return false;
    for (size_t i = 0; i < target.genericArgs.size(); i++)
      if (!isAssignable(value.genericArgs[i], target.genericArgs[i]))
        return false;
    return true;
  }
  // literais inteiros (bits == 0) cabem em qualquer alvo numérico
  // FFI v2: literal 0 também cabe em `ptr` (NULL estilo C)
  if (value.kind == Type::Kind::Int && value.bits == 0) {
    return target.isNumeric() || target.kind == Type::Kind::Enum ||
           target.kind == Type::Kind::Ptr;
  }
  // numérico �?? numérico (M1: ampliações permitidas, casting explícito para estreitar)
  if (target.isNumeric() && value.isNumeric()) {
    if (target.kind == Type::Kind::Float && value.kind != Type::Kind::Float) return true;
    if (target.kind == Type::Kind::Float && value.kind == Type::Kind::Float) {
      return target.bits >= value.bits;
    }
    if (value.kind == Type::Kind::Float) return false; // float ?? int precisa cast
    if (target.kind == Type::Kind::UInt && value.kind == Type::Kind::Int) return false;
    return target.bits >= value.bits;
  }
  if (target.kind == Type::Kind::Bool && value.kind == Type::Kind::Bool) return true;
  if (target.kind == Type::Kind::Char && value.kind == Type::Kind::Char) return target.bits >= value.bits;
  // FFI v2 (ponteiros p/ interop C/Vulkan): `ptr` é opaco de 64 bits.
  // - ptr <- int/uint (handles vindos de extern int, ex.: HWND como int)
  // - ptr <- string (o slot string guarda char*; copia o ponteiro cru)
  // - int64/uint64 <- ptr (guardar endereço p/ impressão ou repasse)
  // - ptr <- literal 0 (NULL estilo C; `null` já funciona via isPointer)
  // Aritmética em ptr continua rejeitada (não é numérico).
  if (target.kind == Type::Kind::Ptr && value.isInteger()) return true;
  if (target.kind == Type::Kind::Ptr && value.kind == Type::Kind::String) return true;
  if ((target.kind == Type::Kind::Int || target.kind == Type::Kind::UInt) &&
      target.bits >= 64 && value.kind == Type::Kind::Ptr) return true;
  return false;
}

bool Semantic::isNumericBinary(const Type& a, const Type& b) {
  return a.isNumeric() && b.isNumeric();
}

Type Semantic::commonNumeric(const Type& a, const Type& b) {
  // regra simples: o mais "forte" vence (float > int, bits maiores vencem)
  auto rank = [](const Type& t) {
    if (t.kind == Type::Kind::Float) return 100000 + t.bits;
    if (t.kind == Type::Kind::Int || t.kind == Type::Kind::UInt) return t.bits;
    return 0;
  };
  Type r = rank(a) >= rank(b) ? a : b;
  if (r.kind == Type::Kind::Int || r.kind == Type::Kind::UInt || r.kind == Type::Kind::Float) {
    // overflow policy vem do operando esquerdo; 'promote' amplia para 64 bits
    if (r.isInteger() && a.policy != OverflowPolicy::Default) {
      r.policy = a.policy;
      if (r.policy == OverflowPolicy::Promote) r.bits = 64;
    }
    return r;
  }
  return Type::makeInt(64);
}

// ---------------------------------------------------------------------------
// Genéricos: monomorfização (implementação das instâncias)
// ---------------------------------------------------------------------------
std::string Semantic::typeKey(const Type& t) const {
  switch (t.kind) {
    case Type::Kind::Int: return "i" + std::to_string(t.bits);
    case Type::Kind::UInt: return "u" + std::to_string(t.bits);
    case Type::Kind::Float: return "f" + std::to_string(t.bits);
    case Type::Kind::Bool: return "b" + (t.bits != 8 ? std::to_string(t.bits) : "");
    case Type::Kind::Char: return "c" + (t.bits != 8 ? std::to_string(t.bits) : "");
    case Type::Kind::String: return "s" + (t.bits > 0 ? std::to_string(t.bits) : "");
    case Type::Kind::Void: return "v";
    case Type::Kind::Class:
      return "C" + t.name + (t.genericArgs.empty() ? "" : "(" + argsKey(t.genericArgs) + ")");
    case Type::Kind::Enum: return "E:" + t.name;
    case Type::Kind::Array: return "[" + typeKey(*t.elem) + "x" + std::to_string(t.arraySize) + "]";
    case Type::Kind::List: return "l(" + typeKey(*t.elem) + ")";
    case Type::Kind::Option: return "o(" + typeKey(*t.elem) + ")";
    case Type::Kind::Task: return "k(" + (t.elem ? typeKey(*t.elem) : std::string("v")) + ")";
    case Type::Kind::Channel: return "ch(" + (t.elem ? typeKey(*t.elem) : std::string("v")) + ")";
    case Type::Kind::Mutex: return "mutex";
    case Type::Kind::Semaphore: return "semaphore";
    case Type::Kind::Event: return "event";
    case Type::Kind::Barrier: return "barrier";
    case Type::Kind::Result: return "r(" + typeKey(*t.elem) + "," + typeKey(*t.elem2) + ")";
    case Type::Kind::Func: {
      // v0.95 (lambdas): `func<R, P...>` → "f(ret,params...)"
      std::string s = "f(" + (t.elem ? typeKey(*t.elem) : std::string("v")) + ")";
      if (!t.genericArgs.empty()) s += "(" + argsKey(t.genericArgs) + ")";
      return s;
    }
    case Type::Kind::TypeVar: return "T:" + t.name;
    case Type::Kind::LitValue: return "lit" + std::to_string(t.arraySize); // M10
    case Type::Kind::Module: return "M:" + t.name;
    case Type::Kind::Unknown: return "?";
  }
  return "?";
}

std::string Semantic::argsKey(const std::vector<Type>& args) const {
  std::string s;
  for (size_t i = 0; i < args.size(); i++) {
    if (i) s += ",";
    s += typeKey(args[i]);
  }
  return s;
}

std::string Semantic::mangleClassName(const std::string& canon,
                                      const std::vector<Type>& args) const {
  std::string m = canon;
  for (auto& a : args) m += "[" + typeKey(a) + "]";
  return labelSafe(m);
}

// cria (ou reutiliza) a instância 'templateCanon[args]' da classe genérica
ClassInfo& Semantic::instantiateClassInfo(const std::string& templateCanon,
                                          const std::vector<Type>& args, int line) {
std::string mg = mangleClassName(templateCanon, args);
  auto it = classes_.find(mg);
  if (it != classes_.end()) return it->second;
  auto& t = classes_[templateCanon];
  if (!t.isTemplate)
    error(line, "'" + t.decl->name + "' não é classe genérica");
  if (args.size() != t.typeParamNames.size())
    error(line, "'" + t.decl->name + "' espera " + std::to_string(t.typeParamNames.size()) +
                    " argumento(s) de tipo, veio " + std::to_string(args.size()));
  for (size_t i = 0; i < t.decl->typeParams.size() && i < args.size(); i++) {
    auto& tp = t.decl->typeParams[i];
    for (auto& c : tp.constraints) {
      if (!satisfiesConstraint(args[i], c))
        error(line, "constraint '" + c + "' violada pelo argumento de tipo '" +
                        tp.name + "='" + typeKey(args[i]) + "'");
    }
  }
  ClassInfo ci;
  ci.decl = t.decl;
  ci.isInstance = true;
  ci.templateFrom = templateCanon;
  ci.typeParamNames = t.typeParamNames;
  ci.typeArgs = args;
  ci.base = t.base;
  // M10 (v0.45): instância tem vptr/base prefixo como classe comum
  ci.hasVptr = !ci.decl->isStruct;
  Subst subst;
  for (size_t i = 0; i < args.size(); i++) subst[t.typeParamNames[i]] = args[i];
  int offset = ci.hasVptr ? 8 : 0;
  for (auto& [fname, ft] : t.fields) {
    if (!subst.empty()) {
      Type st = substType(ft.first, subst);
      ci.fields[fname] = {st, offset};
      offset += typeSize(st);
    } else {
      ci.fields[fname] = {ft.first, offset};
      offset += typeSize(ft.first);
    }
    ci.fieldAtomic[fname] = t.fieldAtomic.count(fname) ? t.fieldAtomic[fname] : false;
    ci.fieldAccess[fname] = t.fieldAccess.count(fname) ? t.fieldAccess[fname]
                                                       : Access::Public;
  }
  ci.interfaceTypeArgs.clear();
  auto& parentIfArgs = t.interfaceTypeArgs.empty() ? t.decl->interfaceTypeArgs : t.interfaceTypeArgs;
  for (auto& va : parentIfArgs) {
    std::vector<Type> sub;
    for (auto& a : va) sub.push_back(substType(a, subst));
    ci.interfaceTypeArgs.push_back(std::move(sub));
  }
  ci.size = offset == 0 ? 8 : offset;
  ci.layoutDone = true;
  ci.derives = t.derives;
  classes_[mg] = std::move(ci);
  auto& inst = classes_[mg];
  std::vector<std::pair<std::string, Type>> cb;
  for (size_t i = 0; i < inst.typeParamNames.size(); i++)
    cb.push_back({inst.typeParamNames[i], inst.typeArgs[i]});
  for (auto* tm : t.methods) {
    if (tm->typeParams.empty()) {
      auto* im = methodInstance(tm, mg, cb, {}, line);
      inst.methods.push_back(im);
    }
  }
  if (!inst.derives.empty()) synthDerivedHelpers(inst, true);
  return inst;
}

// ---------------------------------------------------------------------------
// v0.25.0: derive de traços em struct/class (Equatable/Comparable/Hashable/
// Cloneable) — sintetiza funções livres equ_/cmp_/hash_/clone_<canon> e, para
// classes não-genéricas, também os métodos Equals/CompareTo/Hash/Clone. As
// instâncias de genéricos (toInstances=true) ganham apenas os helpers livres
// (operadores ==, !=, <, <=, >, >= funcionam; métodos ficam para o M2+).
// ---------------------------------------------------------------------------
void Semantic::synthDerivedHelpers(ClassInfo& ci, bool toInstances) {
  const std::string canon = ci.isInstance ? mangleClassName(ci.decl->name, ci.typeArgs)
                                          : ci.decl->name;
  const int line = ci.decl->line;
  bool hasEqu = false, hasCmp = false, hasHash = false, hasClone = false;
  for (auto& tr : ci.derives) {
    if (tr == "Equatable") hasEqu = true;
    else if (tr == "Comparable") hasCmp = true;
    else if (tr == "Hashable") hasHash = true;
    else if (tr == "Cloneable") hasClone = true;
    // M11.3: marcador puro — não sintetiza nada, só habilita a capacidade
    // `sendable` (isSendableType/canais/constraints)
    else if (tr == "Sendable") {}
    else error(line, "traço '" + tr + "' não existe para struct/class "
                     "(disponíveis: Equatable, Comparable, Hashable, Cloneable)");
  }

  // builders de AST para os corpos sintetizados
  auto evec1 = [](std::unique_ptr<Expr> a) {
    std::vector<std::unique_ptr<Expr>> v; v.push_back(std::move(a)); return v; };
  auto evec2 = [](std::unique_ptr<Expr> a, std::unique_ptr<Expr> b) {
    std::vector<std::unique_ptr<Expr>> v; v.push_back(std::move(a)); v.push_back(std::move(b)); return v; };
  auto svec1 = [](std::unique_ptr<Stmt> a) {
    std::vector<std::unique_ptr<Stmt>> v; v.push_back(std::move(a)); return v; };

  auto ident = [](std::string n) {
    auto e = std::make_unique<IdentExpr>(); e->name = std::move(n); return e; };
  auto this_ = []() { return std::make_unique<ThisExpr>(); };
  auto memb = [](std::unique_ptr<Expr> o, std::string m) {
    auto e = std::make_unique<MemberExpr>(); e->object = std::move(o);
    e->member = std::move(m); return e; };
  auto call = [](std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Expr>> a) {
    auto e = std::make_unique<CallExpr>(); e->callee = std::move(c);
    e->args = std::move(a); return e; };
  auto callName = [&](const std::string& name, std::vector<std::unique_ptr<Expr>> a) {
    return call(ident(name), std::move(a)); };
  auto ilit = [](long long v) {
    auto e = std::make_unique<IntLitExpr>(); e->value = v; return e; };
  auto blit = [](bool v) {
    auto e = std::make_unique<BoolLitExpr>(); e->value = v; return e; };
  auto bin = [](BinOp op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r) {
    auto e = std::make_unique<BinaryExpr>(); e->op = op; e->lhs = std::move(l);
    e->rhs = std::move(r); return e; };
  auto unot = [](std::unique_ptr<Expr> o) {
    auto e = std::make_unique<UnaryExpr>(); e->op = UnOp::Not;
    e->operand = std::move(o); return e; };
  auto castI64 = [](std::unique_ptr<Expr> o) {
    auto e = std::make_unique<CastExpr>(); e->target = Type::makeInt(64);
    e->operand = std::move(o); return e; };
  auto ret = [](std::unique_ptr<Expr> v) {
    auto s = std::make_unique<ReturnStmt>(); s->value = std::move(v); return s; };
  auto exst = [](std::unique_ptr<Expr> e) {
    auto s = std::make_unique<ExprStmt>(); s->expr = std::move(e); return s; };
  auto ifs = [](std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Stmt>> then) {
    auto s = std::make_unique<IfStmt>(); s->cond = std::move(c);
    auto blk = std::make_unique<BlockStmt>();
    for (auto& x : then) blk->stmts.push_back(std::move(x));
    s->thenBranch = std::move(blk);
    return s; };
  auto block = [](std::vector<std::unique_ptr<Stmt>> ss) {
    auto b = std::make_unique<BlockStmt>();
    for (auto& s : ss) b->stmts.push_back(std::move(s));
    return b; };
  auto mkParam = [canon, line](std::string n) {
    auto p = std::make_unique<Param>(); p->name = std::move(n);
    p->line = line; p->type = Type::makeClass(canon); return p; };
  auto mkFn = [&](std::string name, Type rt) {
    auto fn = std::make_unique<FunctionDecl>();
    fn->name = std::move(name);
    fn->returnType = std::move(rt);
    fn->hasReturnType = true;
    fn->isSynthetic = true; // v0.46: helper de derive acessa membros privados
    fn->moduleName = ci.decl->moduleName;
    fn->filePath = ci.decl->filePath;
    fn->line = line;
    return fn; };
  auto reg = [&](std::unique_ptr<FunctionDecl> fn) {
    FunctionInfo fi; fi.decl = fn.get(); fi.moduleName = ci.decl->moduleName;
    functions_.push_back(fi);
    if (toInstances) instancesPending_.push_back(fn.get());
    return fn.release(); };
  auto regMethod = [&](std::unique_ptr<FunctionDecl> fn) {
    fn->isMethod = true;
    fn->isStatic = false;
    fn->ownerClass = canon;
    FunctionInfo fi; fi.decl = fn.get(); fi.moduleName = ci.decl->moduleName;
    functions_.push_back(fi);
    return fn.release(); };

  if (!toInstances) {
    for (auto* m : ci.methods)
      if (m->name == "Equals" || m->name == "CompareTo" || m->name == "Hash" ||
          m->name == "Clone")
        error(m->line, "'" + ci.decl->name + "' já declara o método '" + m->name +
                       "', que conflita com os traços do derive");
  }

  auto checkFieldKinds = [&](const char* trait, Type ft, int fline,
                             bool allowHeapRef, bool allowString) {
    if (ft.kind == Type::Kind::List || ft.kind == Type::Kind::Array)
      error(fline, "derive " + std::string(trait) + " não suporta campo do tipo list/array");
    if (ft.kind == Type::Kind::Class) {
      auto ic = classes_.find(ft.name);
      if (ic == classes_.end()) return;
      if (!ic->second.decl->isStruct) {
        if (!allowHeapRef)
          error(fline, "derive " + std::string(trait) + " não suporta campo de "
                       "classe (referência) '" + ft.name + "' — use struct ou "
                       "remova o campo");
      } else if (ic->second.equFn == nullptr && trait[0] != 'c' /* clone */) {
        // struct por valor: exige o mesmo traço (os helpers encadeiam)
        if (trait[0] == 'E' && !derived(ft.name, "Equatable"))
          error(fline, "campo '" + ft.name + "' é struct sem derive Equatable "
                       "(derive Equatable na struct também)");
        if (trait[0] == 'C' && !(derived(ft.name, "Comparable") || ft.name == canon))
          error(fline, "campo '" + ft.name + "' é struct sem derive Comparable "
                       "(derive Comparable na struct também)");
      }
    }
  };

  // ---- Equatable: equ_<canon>(a, b) -> bool (campo a campo)
  if (hasEqu) {
    std::vector<std::unique_ptr<Stmt>> body;
    for (auto& f : ci.decl->fields) {
      auto fit = ci.fields.find(f->name);
      if (fit == ci.fields.end()) continue;
      Type ft = fit->second.first;
      checkFieldKinds("Equatable", ft, f->line, false, true);
      std::unique_ptr<Expr> neq;
      if (ft.kind == Type::Kind::Class &&
          classes_.count(ft.name) && classes_[ft.name].decl->isStruct)
        neq = unot(callName("equ_" + ft.name,
                            evec2(memb(ident("a"), f->name), memb(ident("b"), f->name))));
      else if (ft.kind == Type::Kind::String)
        neq = unot(callName("__hphl_str_eq",
                            evec2(memb(ident("a"), f->name), memb(ident("b"), f->name))));
      else
        neq = bin(BinOp::Ne, memb(ident("a"), f->name), memb(ident("b"), f->name));
      body.push_back(ifs(std::move(neq), svec1(ret(blit(false)))));
    }
    body.push_back(ret(blit(true)));
    auto fn = mkFn("equ_" + canon, Type::makeBool());
    fn->params.push_back(mkParam("a"));
    fn->params.push_back(mkParam("b"));
    fn->body = block(std::move(body));
    ci.equFn = reg(std::move(fn));
  }

  // ---- Comparable: cmp_<canon>(a, b) -> int<64> (-1/0/1)
  if (hasCmp) {
    std::vector<std::unique_ptr<Stmt>> body;
    int ki = 0;
    for (auto& f : ci.decl->fields) {
      auto fit = ci.fields.find(f->name);
      if (fit == ci.fields.end()) continue;
      Type ft = fit->second.first;
      checkFieldKinds("Comparable", ft, f->line, false, true);
      if (ft.kind == Type::Kind::Class &&
          classes_.count(ft.name) && classes_[ft.name].decl->isStruct) {
        std::string kn = "k" + std::to_string(ki++);
        auto vd = std::make_unique<VarDecl>();
        vd->name = kn; vd->type = Type::makeInt(64); vd->hasType = true;
        vd->init = callName("cmp_" + ft.name,
                            evec2(memb(ident("a"), f->name), memb(ident("b"), f->name)));
        auto svd = std::make_unique<StmtVarDecl>(); svd->decls.push_back(std::move(vd));
        body.push_back(std::move(svd));
        body.push_back(ifs(bin(BinOp::Lt, ident(kn), ilit(0)), svec1(ret(ilit(-1)))));
        body.push_back(ifs(bin(BinOp::Gt, ident(kn), ilit(0)), svec1(ret(ilit(1)))));
      } else if (ft.kind == Type::Kind::String) {
        std::string kn = "k" + std::to_string(ki++);
        auto vd = std::make_unique<VarDecl>();
        vd->name = kn; vd->type = Type::makeInt(64); vd->hasType = true;
        vd->init = callName("__hphl_str_cmp",
                            evec2(memb(ident("a"), f->name), memb(ident("b"), f->name)));
        auto svd = std::make_unique<StmtVarDecl>(); svd->decls.push_back(std::move(vd));
        body.push_back(std::move(svd));
        body.push_back(ifs(bin(BinOp::Lt, ident(kn), ilit(0)), svec1(ret(ilit(-1)))));
        body.push_back(ifs(bin(BinOp::Gt, ident(kn), ilit(0)), svec1(ret(ilit(1)))));
      } else {
        body.push_back(ifs(bin(BinOp::Lt, memb(ident("a"), f->name),
                               memb(ident("b"), f->name)), svec1(ret(ilit(-1)))));
        body.push_back(ifs(bin(BinOp::Gt, memb(ident("a"), f->name),
                               memb(ident("b"), f->name)), svec1(ret(ilit(1)))));
      }
    }
    body.push_back(ret(ilit(0)));
    auto fn = mkFn("cmp_" + canon, Type::makeInt(64));
    fn->params.push_back(mkParam("a"));
    fn->params.push_back(mkParam("b"));
    fn->body = block(std::move(body));
    ci.cmpFn = reg(std::move(fn));
  }

  // ---- Hashable: hash_<canon>(a) -> int<64> (FNV-1a sobre os campos por
  // valor; string/classes não contribuem no Milestone 2 — documentado)
  if (hasHash) {
    // 0x811c9dc5 equivalentes? — base FNV 64: FNV_offset_basis
    std::unique_ptr<Expr> acc;
    for (auto& f : ci.decl->fields) {
      auto fit = ci.fields.find(f->name);
      if (fit == ci.fields.end()) continue;
      Type ft = fit->second.first;
      checkFieldKinds("Hashable", ft, f->line, true, true);
      if (ft.kind == Type::Kind::Class || ft.kind == Type::Kind::String ||
          ft.kind == Type::Kind::Float)
        continue; // referências/strings/pontos flutuantes não contribuem (M2)
      std::unique_ptr<Expr> v = memb(ident("a"), f->name);
      if (ft.kind == Type::Kind::Enum)
        v = castI64(std::move(v));
      if (!acc)
        acc = std::move(v);
      else
        acc = bin(BinOp::BitXor, std::move(acc), std::move(v));
      acc = bin(BinOp::Mul, std::move(acc), ilit(1099511628211LL));
    }
    if (!acc) acc = ilit(0);
    auto fn = mkFn("hash_" + canon, Type::makeInt(64));
    fn->params.push_back(mkParam("a"));
    fn->body = block(svec1(ret(std::move(acc))));
    ci.hashFn = reg(std::move(fn));
  }

  // ---- Cloneable: clone_<canon>(a) -> <canon> (cópia rasa: referências e
  // strings compartilham; struct deixa de ser furada por cópia campo a campo)
  if (hasClone) {
    bool isStruct = ci.decl->isStruct && !ci.isInstance;
    // instâncias de struct genérica também são por valor
    if (ci.isInstance && !ci.decl->isStruct) isStruct = false;
    std::vector<std::unique_ptr<Stmt>> body;
    {
      auto vd = std::make_unique<VarDecl>();
      vd->name = "n";
      vd->hasType = true;
      vd->type = Type::makeClass(canon);
      if (isStruct) {
        vd->init = ident("a"); // struct: cópia por valor da origem
      } else {
        auto ne = std::make_unique<NewExpr>();
        ne->className = canon;
        vd->init = std::move(ne);
      }
      auto svd = std::make_unique<StmtVarDecl>(); svd->decls.push_back(std::move(vd));
      body.push_back(std::move(svd));
    }
    for (auto& f : ci.decl->fields) {
      auto fit = ci.fields.find(f->name);
      if (fit == ci.fields.end()) continue;
      auto as = std::make_unique<AssignExpr>();
      as->op = AssignOp::Plain;
      as->target = memb(ident("n"), f->name);
      as->value = memb(ident("a"), f->name);
      body.push_back(exst(std::move(as)));
    }
    body.push_back(ret(ident("n")));
    auto fn = mkFn("clone_" + canon, Type::makeClass(canon));
    fn->params.push_back(mkParam("a"));
    fn->body = block(std::move(body));
    ci.cloneFn = reg(std::move(fn));
  }

  // ---- métodos por convenção (só em classes não-genéricas)
  if (!toInstances) {
    if (hasEqu) {
      auto fn = mkFn("Equals", Type::makeBool());
      fn->params.push_back(mkParam("other"));
      fn->body = block(svec1(ret(callName("equ_" + canon, evec2(this_(), ident("other"))))));
      ci.methods.push_back(regMethod(std::move(fn)));
    }
    if (hasCmp) {
      auto fn = mkFn("CompareTo", Type::makeInt(64));
      fn->params.push_back(mkParam("other"));
      fn->body = block(svec1(ret(callName("cmp_" + canon, evec2(this_(), ident("other"))))));
      ci.methods.push_back(regMethod(std::move(fn)));
    }
    if (hasHash) {
      auto fn = mkFn("Hash", Type::makeInt(64));
      fn->body = block(svec1(ret(callName("hash_" + canon, evec1(this_())))));
      ci.methods.push_back(regMethod(std::move(fn)));
    }
    if (hasClone) {
      auto fn = mkFn("Clone", Type::makeClass(canon));
      fn->body = block(svec1(ret(callName("clone_" + canon, evec1(this_())))));
      ci.methods.push_back(regMethod(std::move(fn)));
    }
  }
}

// função global genérica: cria a instância com 'ownArgs' e agenda a checagem
FunctionDecl* Semantic::genericFunctionInstance(FunctionDecl* tmpl,
                                                const std::vector<Type>& ownArgs, int line) {
  if (ownArgs.size() != tmpl->typeParams.size())
    error(line, "'" + tmpl->name + "' espera " + std::to_string(tmpl->typeParams.size()) +
                    " argumento(s) de tipo, veio " + std::to_string(ownArgs.size()));
  checkConstraints(tmpl, ownArgs, line);
  std::string key = tmpl->name + "[" + argsKey(ownArgs) + "]";
  auto it = genericInstances_.find(key);
  if (it != genericInstances_.end()) return it->second;
  Subst subst;
  for (size_t i = 0; i < tmpl->typeParams.size(); i++) subst[tmpl->typeParams[i].name] = ownArgs[i];
  auto clone = cloneFunction(*tmpl, subst);
  FunctionDecl* p = clone.get();
  p->ownerClass.clear();
  p->isMethod = false;
  p->name = tmpl->name + labelSafe("[" + argsKey(ownArgs) + "]"); // label único por instância
  genericInstances_[key] = p;
  instancesKeep_.push_back(std::move(clone));
  instancesPending_.push_back(p);
  return p;
}

// método de classe genérica (ou genérico �?? ownArgs não-vazio): a instância do
// método herda os args da classe (classBinding: nome do param �?? tipo)
FunctionDecl* Semantic::methodInstance(FunctionDecl* tmpl, const std::string& ownerMangled,
                                       const std::vector<std::pair<std::string, Type>>& classBinding,
                                       const std::vector<Type>& ownArgs, int line) {
  if (ownArgs.size() != tmpl->typeParams.size()) {
    std::string tps;
    for (auto& tp : tmpl->typeParams) { if (!tps.empty()) tps += ","; tps += tp.name; }
    error(line, "método '" + tmpl->name + "' espera " +
                    std::to_string(tmpl->typeParams.size()) +
                    " argumento(s) de tipo (" + tps + "), veio " +
                    std::to_string(ownArgs.size()));
  }
  checkConstraints(tmpl, ownArgs, line);
  std::string key = tmpl->name + "@" + ownerMangled + "#";
  for (auto& [cname, ct] : classBinding) key += cname + "=" + typeKey(ct) + ";";
  key += "|" + argsKey(ownArgs);
  auto it = genericInstances_.find(key);
  if (it != genericInstances_.end()) return it->second;
  Subst subst;
  for (auto& [name, t] : classBinding) subst[name] = t;
  for (size_t i = 0; i < tmpl->typeParams.size(); i++)
    subst[tmpl->typeParams[i].name] = ownArgs[i];
  auto n = cloneFunction(*tmpl, subst);
  FunctionDecl* p = n.get();
  p->ownerClass = ownerMangled;
  p->isMethod = true;
  if (!ownArgs.empty())
    p->name = tmpl->name + labelSafe("[" + argsKey(ownArgs) + "]"); // label único
  genericInstances_[key] = p;
  instancesKeep_.push_back(std::move(n));
  instancesPending_.push_back(p);
  return p;
}

// inferência: unifica os tipos dos argumentos com os parâmetros (T �?? concreto)
std::vector<Type> Semantic::inferTypeArgs(FunctionDecl* tmpl,
                                          const std::vector<std::unique_ptr<Expr>>& args,
                                          int line) {
  if (args.size() != tmpl->params.size())
    error(line, "esperava " + std::to_string(tmpl->params.size()) +
                    " argumento(s) em '" + tmpl->name + "', veio " +
                    std::to_string(args.size()));
  std::map<std::string, Type> binds;
  for (size_t i = 0; i < args.size(); i++) {
    Type at = args[i]->exprType;
    if (at.kind == Type::Kind::Unknown) {
      // Some(...)/Ok(...) dependem do tipo esperado �?? não dão para inferir sozinhos
      if (args[i]->kind == ExprKind::OptCtor)
        error(line, "não foi possível inferir os tipos de '" + tmpl->name +
                    "' (Some/Ok sem tipo): informe-os explicitamente, ex.: '" +
                    tmpl->name + "<int>(...)'");
      at = checkExpr(args[i].get());
    }
    std::function<void(const Type&, const Type&)> unify =
        [&](const Type& pt, const Type& av) {
          if (pt.isTypeVar()) {
            auto it = binds.find(pt.name);
            if (it == binds.end()) binds[pt.name] = av;
            else if (!(it->second == av))
              error(line, "inferência inconsistente para '" + pt.name + "'");
          } else if (pt.kind == Type::Kind::Class && av.kind == Type::Kind::Class) {
            size_t n = std::min(pt.genericArgs.size(), av.genericArgs.size());
            for (size_t k = 0; k < n; k++) unify(pt.genericArgs[k], av.genericArgs[k]);
          } else if (pt.kind == Type::Kind::List && av.kind == Type::Kind::List) {
            unify(*pt.elem, *av.elem);
          } else if (pt.kind == Type::Kind::Option && av.isOption()) {
            unify(*pt.elem, *av.elem);
          } else if (pt.kind == Type::Kind::Result && av.isResult()) {
            unify(*pt.elem, *av.elem);
            unify(*pt.elem2, *av.elem2);
          }
        };
    unify(tmpl->params[i]->type, at);
  }
  std::vector<Type> out;
  for (auto& tp : tmpl->typeParams) {
    auto it = binds.find(tp.name);
    if (it == binds.end())
      error(line, "não foi possível inferir o tipo paramétrico '" + tp.name +
                      "' �?? informe os argumentos de tipo explicitamente: '" +
                      tmpl->name + "<...>(...)'");
    out.push_back(it->second);
  }
  return out;
}

// ---------------------------------------------------------------------------
// M10 (v0.44): polimorfismo — subsumption, slots virtuais e override
// ---------------------------------------------------------------------------

bool Semantic::derivesFrom(const std::string& derivedCanon,
                           const std::string& baseCanon) const {
  if (derivedCanon == baseCanon) return true;
  std::string cur = derivedCanon;
  for (int guard = 0; guard < 64; guard++) {
    auto it = classes_.find(cur);
    if (it == classes_.end() || it->second.base.empty()) return false;
    cur = it->second.base;
    if (cur == baseCanon) return true;
  }
  return false;
}

const ClassInfo* Semantic::classInfo(const std::string& canon) const {
  auto it = classes_.find(canon);
  return it == classes_.end() ? nullptr : &it->second;
}

int Semantic::vslotOf(const std::string& name, size_t arity) const {
  auto it = vslots_.find(name + "/" + std::to_string(arity));
  return it == vslots_.end() ? -1 : it->second;
}

bool Semantic::slotOverridden(int slot) const {
  auto it = vslotDeclCount_.find(slot);
  return it != vslotDeclCount_.end() && it->second >= 2;
}

// implementação do slot visível a partir de `startCanon` (subindo na cadeia)
FunctionDecl* Semantic::findOverrideIn(const std::string& startCanon, int slot) const {
  std::string key;
  for (auto& [k, s] : vslots_)
    if (s == slot) { key = k; break; }
  if (key.empty()) return nullptr;
  size_t slash = key.rfind('/');
  std::string name = key.substr(0, slash);
  size_t arity = (size_t)std::stoll(key.substr(slash + 1));
  std::string cur = startCanon;
  for (int guard = 0; guard < 64; guard++) {
    auto it = classes_.find(cur);
    if (it == classes_.end()) return nullptr;
    for (auto* m : it->second.methods)
      if (!m->isConstructor && !m->isStatic && m->name == name &&
          m->params.size() == arity)
        return m;
    if (it->second.base.empty()) return nullptr;
    cur = it->second.base;
  }
  return nullptr;
}

// M10 (v0.44): layout recursivo — base primeiro (prefixo compatível para
// subsumption) e vptr como prefixo da raiz (offset 0); registra slots virtuais
void Semantic::layoutClass(ClassInfo& ci) {
  if (ci.layoutDone) return;
  if (ci.inLayout) {
    error(ci.decl->line, "herança circular detectada ao calcular o layout de '" + ci.decl->name + "'");
  }
  ci.inLayout = true;
  std::string m0 = curModule_, f0 = curFile_;
  curModule_ = ci.decl->moduleName;
  curFile_ = ci.decl->filePath;
  int offset = 0;
  ci.hasVptr = !ci.decl->isStruct;
  if (!ci.decl->isStruct) {
    if (!ci.base.empty()) {
      auto bit = classes_.find(ci.base);
      if (bit != classes_.end()) {
        layoutClass(bit->second);
        offset = bit->second.size;
      }
    } else {
      offset = 8; // vptr da raiz
    }
  }
  for (auto& f : ci.decl->fields) {
    Type ft = resolveType(f->type, f->line);
    // M17: list<T> e map<K,V> como campos de classe agora suportados
    if (isPrimitiveSyncType(ft.kind)) {
      error(f->line, "primitiva de sincronização não pode ser campo de classe: "
                     "declare a variável em escopo de função (primitivas de sincronização exigem inicialização local pelo runtime)");
    }
    validateAtomicDecl(f->line, ft, f->atomic);
    // M10 (v0.45): dimensão simbólica só existe em templates — placeholder
    // (a instância recalcula o layout com o valor concreto)
    if (ft.kind == Type::Kind::Array && ft.arraySize == -1) {
      if (!ci.isTemplate)
        error(f->line, "dimensão simbólica de array fora de classe genérica");
      offset += 8; // placeholder: a instância refaz o layout
      if (ci.fields.count(f->name)) error(f->line, "campo '" + f->name + "' duplicado");
      ci.fields[f->name] = {ft, offset - 8};
      ci.fieldAtomic[f->name] = f->atomic;
      ci.fieldAccess[f->name] = f->access;
      continue;
    }
    int size = typeSize(ft); // M2: arrays ocupam elem*N bytes (8-alinhado)
    offset += size;
    if (ci.fields.count(f->name)) error(f->line, "campo '" + f->name + "' duplicado");
    ci.fields[f->name] = {ft, offset - size};
    ci.fieldAtomic[f->name] = f->atomic;
    ci.fieldAccess[f->name] = f->access;
  }
  ci.size = offset == 0 ? 8 : offset;
  if (ci.decl->isStruct && ci.size < 8) ci.size = 8;
  if (!ci.decl->isStruct) {
    for (auto& m : ci.decl->methods) {
      if (m->isConstructor || m->isStatic) continue;
      std::string key = m->name + "/" + std::to_string(m->params.size());
      if (vslots_.find(key) == vslots_.end())
        vslots_[key] = (int)vslots_.size();
      vslotDeclCount_[vslots_[key]]++;
    }
  }
  curModule_ = m0;
  curFile_ = f0;
  ci.inLayout = false;
  ci.layoutDone = true;
}

// M11.3 Safety (spec §3/§10): tipo pode atravessar threads com segurança?
// escalares/enum/string sim; classe só com `derive Sendable` (marcador);
// list/map herdam do elemento. Usado por constraints de generics e pela
// checagem de CHANNEL SENDABLE na criação de canais.
bool Semantic::isSendableType(const Type& t) {
  if (t.isNumeric() || t.kind == Type::Kind::Bool || t.kind == Type::Kind::Char ||
      t.kind == Type::Kind::Enum || t.kind == Type::Kind::String)
    return true;
  if (t.kind == Type::Kind::Class) {
    // o nome no Type pode vir sem o prefixo de módulo (canônico é "mod.Nome")
    if (derived(t.name, "Sendable")) return true;
    for (auto& kv : classes_) {
      const std::string& key = kv.first;
      if (key.size() > t.name.size() &&
          key.compare(key.size() - t.name.size(), t.name.size(), t.name) == 0 &&
          key[key.size() - t.name.size() - 1] == '.')
        return std::find(kv.second.derives.begin(), kv.second.derives.end(),
                         "Sendable") != kv.second.derives.end();
    }
    return false;
  }
  if ((t.kind == Type::Kind::List || t.kind == Type::Kind::Map) && t.elem) {
    // list<T>: elem é o payload; map<K,V>: elem=chave, elem2=valor
    if (!isSendableType(*t.elem)) return false;
    if (t.kind == Type::Kind::Map)
      return t.elem2 ? isSendableType(*t.elem2) : true;
    return true;
  }
  if (t.isOption()) {
    return !t.elem || isSendableType(*t.elem);
  }
  if (t.isResult()) {
    bool okSend = !t.elem || isSendableType(*t.elem);
    bool errSend = !t.elem2 || isSendableType(*t.elem2);
    return okSend && errSend;
  }
  return false;
}

// ---------------------------------------------------------------------------
// M11.7 Safety (spec §3): NULL ANALYSIS — rastreia locais Class/String que
// podem estar null (`= null`, cópia de possivelmente-null, resultado de `?.`)
// e rejeita acesso direto a campo/método sem checagem. O estado faz merge
// por ramos no `if` (mesmo modelo do movedVars_) e o ternário respeita a
// polaridade da condição (`s == null ? a : s` analisa cada braço certo).
// ---------------------------------------------------------------------------

// atualiza o estado de 'name' (Class/String) após atribuição/inicialização
void Semantic::nullNoteAssign(const std::string& name, const Type& t,
                              const Expr* value) {
  if (t.kind != Type::Kind::Class && t.kind != Type::Kind::String) return;
  if (!value) {
    // declaração sem init: classe nasce calloc'd (nunca null)
    maybeNull_.erase(name);
    return;
  }
  if (value->kind == ExprKind::NullLit) {
    maybeNull_.insert(name);
    return;
  }
  if (value->kind == ExprKind::Ident && maybeNull_.count(
          static_cast<const IdentExpr*>(value)->name)) {
    maybeNull_.insert(name);
    return;
  }
  // `?.` e `?[]` produzem possivelmente-null; `??` tem default (não-null)
  if (value->kind == ExprKind::OptMember || value->kind == ExprKind::OptIndex) {
    maybeNull_.insert(name);
    return;
  }
  maybeNull_.erase(name);
}

// 0 = não é comparação com null; 1 = `x != null`; 2 = `x == null`
int Semantic::nullCondPolarity(const Expr* cond, std::string& name) {
  if (!cond || cond->kind != ExprKind::Binary) return 0;
  auto* b = static_cast<const BinaryExpr*>(cond);
  if (b->op != BinOp::Eq && b->op != BinOp::Ne) return 0;
  const Expr* identSide = nullptr;
  if (b->rhs && b->rhs->kind == ExprKind::NullLit &&
      b->lhs->kind == ExprKind::Ident)
    identSide = b->lhs.get();
  else if (b->lhs && b->lhs->kind == ExprKind::NullLit &&
           b->rhs->kind == ExprKind::Ident)
    identSide = b->rhs.get();
  else
    return 0;
  name = static_cast<const IdentExpr*>(identSide)->name;
  return b->op == BinOp::Ne ? 1 : 2;
}

// acesso direto a campo/método em possivelmente-null → erro
void Semantic::checkNullDeref(const Expr* obj, const Type& objType, int line) {
  if (obj->kind != ExprKind::Ident) return;
  if (objType.kind != Type::Kind::Class && objType.kind != Type::Kind::String)
    return;
  auto* id = static_cast<const IdentExpr*>(obj);
  if (!maybeNull_.count(id->name)) return;
  error(line, "NULL DEREF: '" + id->name +
                  "' pode ser null aqui — cheque antes ('" + id->name +
                  " != null'), use acesso condicional '?.' ou forneça um "
                  "default com coalescência");
}

// M11.8 Safety (spec §3/§10): LOCK DISCIPLINE / TOCTOU — acesso a campo/
// método de objeto cuja leitura protegida por lock VENCEU (bloco saiu) é
// time-of-check-to-time-of-use: o valor pode ter mudado. Re-embrulhe em
// `lock (obj)` ou copie para um local DENTRO do bloco.
void Semantic::noteLockAccess(const Expr* obj, int line) {
  if (obj->kind != ExprKind::Ident) return;
  auto* id = static_cast<const IdentExpr*>(obj);
  if (inLockDepth_ > 0 && !lockObjName_.empty() && id->name == lockObjName_) {
    lockBodyRead_ = true; // leitura sincronizada: válida e marca vencimento
    return;
  }
  if (inLockDepth_ == 0 && staleAfterLock_.count(id->name))
    error(line, "LOCK TOCTOU: '" + id->name +
                    "' foi lido sob lock que já terminou — o valor pode ter " +
                    "mudado desde então; acesse dentro de um novo " +
                    "'lock (" + id->name + ")' ou copie para um local dentro " +
                    "do bloco");
}

// M11.9 Safety (spec §3): USE AFTER FREE — acesso a local 'arena' depois de
// arena_reset(): o bloco inteiro foi devolvido e o próximo alloc recicla.
void Semantic::checkArenaUaf(const Expr* obj, int line) {
  if (obj->kind != ExprKind::Ident) return;
  auto* id = static_cast<const IdentExpr*>(obj);
  if (!arenaFreed_.count(id->name)) return;
  error(line, "USE AFTER FREE: '" + id->name +
                  "' vivia em arena resetada por arena_reset() — o bloco " +
                  "inteiro ficou inválido; declare um novo objeto 'arena'");
}

bool Semantic::satisfiesConstraint(const Type& arg, const std::string& constraint) {
  if (constraint == "class") {
    if (arg.kind == Type::Kind::Class) {
      auto it = classes_.find(arg.name);
      if (it != classes_.end() && it->second.decl->isStruct) return false;
      return true;
    }
    return arg.kind == Type::Kind::String || arg.kind == Type::Kind::Enum ||
           arg.kind == Type::Kind::List || arg.kind == Type::Kind::Map ||
           arg.kind == Type::Kind::Func || arg.kind == Type::Kind::Task ||
           arg.kind == Type::Kind::Channel;
  }
  if (constraint == "struct") {
    if (arg.kind == Type::Kind::Class) {
      auto it = classes_.find(arg.name);
      return it != classes_.end() && it->second.decl->isStruct;
    }
    return arg.isNumeric() || arg.kind == Type::Kind::Bool ||
           arg.kind == Type::Kind::Char || arg.kind == Type::Kind::Enum;
  }
  if (constraint == "new()") {
    if (arg.kind != Type::Kind::Class) return false;
    auto it = classes_.find(arg.name);
    if (it == classes_.end()) return false;
    if (it->second.decl->isInterface) return false;
    if (it->second.ctors.empty()) return true;
    for (auto* c : it->second.ctors) {
      if (c->params.empty()) return true;
    }
    return false;
  }
  // M10.1b: capacidades e `supports <op>`
  bool isScalar = arg.isNumeric() || arg.kind == Type::Kind::Bool ||
                  arg.kind == Type::Kind::Char || arg.kind == Type::Kind::Enum;
  if (constraint == "immutable")
    return isScalar; // mutáveis: string/class/list/map
  if (constraint == "sendable" || constraint == "shareable" ||
      constraint == "thread_safe")
    return isSendableType(arg); // M11.3: inclui classes com derive Sendable
  if (constraint.rfind("supports ", 0) == 0) {
    std::string op = constraint.substr(9);
    switch (arg.kind) {
      case Type::Kind::Int:
      case Type::Kind::UInt:
        return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
               op == "==" || op == "!=" || op == "<" || op == ">" ||
               op == "<=" || op == ">=";
      case Type::Kind::Float:
        return op == "+" || op == "-" || op == "*" || op == "/" ||
               op == "==" || op == "!=" || op == "<" || op == ">" ||
               op == "<=" || op == ">=";
      case Type::Kind::Bool:
        return op == "==" || op == "!=";
      case Type::Kind::Char:
        return op == "==" || op == "!=" || op == "<" || op == ">" ||
               op == "<=" || op == ">=";
      case Type::Kind::String:
        return op == "+" || op == "==" || op == "!=";
      default:
        // classes: ==/!= via derive Equatable
        if (arg.kind == Type::Kind::Class) {
          auto it = classes_.find(arg.name);
          if (it != classes_.end()) {
            for (auto& d : it->second.decl->derives)
              if ((op == "==" || op == "!=") && (d == "Equatable" || d == "IEquatable")) return true;
            if (op == "==" || op == "!=") return false;
          }
        }
        return false;
    }
  }

  // Trait constraints: Comparable/IComparable, Equatable/IEquatable, Hashable/IHashable, Cloneable/ICloneable
  if (constraint == "Comparable" || constraint == "IComparable") {
    if (arg.isNumeric() || arg.kind == Type::Kind::Char || arg.kind == Type::Kind::String) return true;
    if (arg.kind == Type::Kind::Enum) {
      auto it = enums_.find(arg.name);
      return it != enums_.end() &&
             (std::find(it->second->derives.begin(), it->second->derives.end(), "Comparable") != it->second->derives.end() ||
              std::find(it->second->derives.begin(), it->second->derives.end(), "IComparable") != it->second->derives.end());
    }
    if (arg.kind == Type::Kind::Class) {
      auto it = classes_.find(arg.name);
      if (it != classes_.end()) {
        for (auto& d : it->second.decl->derives)
          if (d == "Comparable" || d == "IComparable") return true;
        std::string canonC = (curModule_.empty() ? "main" : curModule_) + ".Comparable";
        std::string canonIC = (curModule_.empty() ? "main" : curModule_) + ".IComparable";
        if (implementsInterface(arg.name, "Comparable") || implementsInterface(arg.name, "IComparable") ||
            implementsInterface(arg.name, canonC) || implementsInterface(arg.name, canonIC))
          return true;
      }
      return false;
    }
    return false;
  }
  if (constraint == "Equatable" || constraint == "IEquatable") {
    if (arg.isNumeric() || arg.kind == Type::Kind::Bool || arg.kind == Type::Kind::Char || arg.kind == Type::Kind::String) return true;
    if (arg.kind == Type::Kind::Enum) return true;
    if (arg.kind == Type::Kind::Class) {
      auto it = classes_.find(arg.name);
      if (it != classes_.end()) {
        for (auto& d : it->second.decl->derives)
          if (d == "Equatable" || d == "IEquatable") return true;
        std::string canonE = (curModule_.empty() ? "main" : curModule_) + ".Equatable";
        std::string canonIE = (curModule_.empty() ? "main" : curModule_) + ".IEquatable";
        if (implementsInterface(arg.name, "Equatable") || implementsInterface(arg.name, "IEquatable") ||
            implementsInterface(arg.name, canonE) || implementsInterface(arg.name, canonIE))
          return true;
      }
      return false;
    }
    return false;
  }
  if (constraint == "Hashable" || constraint == "IHashable") {
    if (arg.isNumeric() || arg.kind == Type::Kind::Bool || arg.kind == Type::Kind::Char || arg.kind == Type::Kind::String) return true;
    if (arg.kind == Type::Kind::Class) {
      auto it = classes_.find(arg.name);
      if (it != classes_.end()) {
        for (auto& d : it->second.decl->derives)
          if (d == "Hashable" || d == "IHashable") return true;
        std::string canonH = (curModule_.empty() ? "main" : curModule_) + ".Hashable";
        std::string canonIH = (curModule_.empty() ? "main" : curModule_) + ".IHashable";
        if (implementsInterface(arg.name, "Hashable") || implementsInterface(arg.name, "IHashable") ||
            implementsInterface(arg.name, canonH) || implementsInterface(arg.name, canonIH))
          return true;
      }
      return false;
    }
    return false;
  }
  if (constraint == "Cloneable" || constraint == "ICloneable") {
    if (isScalar || arg.kind == Type::Kind::String) return true;
    if (arg.kind == Type::Kind::Class) {
      auto it = classes_.find(arg.name);
      if (it != classes_.end()) {
        for (auto& d : it->second.decl->derives)
          if (d == "Cloneable" || d == "ICloneable") return true;
        std::string canonCl = (curModule_.empty() ? "main" : curModule_) + ".Cloneable";
        std::string canonICl = (curModule_.empty() ? "main" : curModule_) + ".ICloneable";
        if (implementsInterface(arg.name, "Cloneable") || implementsInterface(arg.name, "ICloneable") ||
            implementsInterface(arg.name, canonCl) || implementsInterface(arg.name, canonICl))
          return true;
      }
      return false;
    }
    return false;
  }

  // constraint por nome: classe base, enum com derive, ou deriva de traço/interface
  if (arg.kind == Type::Kind::Enum) {
    auto it = enums_.find(arg.name);
    return it != enums_.end() &&
           std::find(it->second->derives.begin(), it->second->derives.end(), constraint) !=
               it->second->derives.end();
  }
  if (arg.kind == Type::Kind::Class) {
    auto it = classes_.find(arg.name);
    if (it == classes_.end()) return false;
    std::string canonC = constraint.find('.') == std::string::npos
                             ? (curModule_.empty() ? "main" : curModule_) + "." + constraint
                             : constraint;
    if (it->second.decl->name == constraint || arg.name == constraint || arg.name == canonC)
      return true;
    if (derivesFrom(arg.name, constraint) || derivesFrom(arg.name, canonC))
      return true;
    if (implementsInterface(arg.name, constraint) || implementsInterface(arg.name, canonC))
      return true;
    for (auto& d : it->second.decl->derives)
      if (d == constraint) return true;
    return false;
  }
  return false;
}

void Semantic::checkConstraints(FunctionDecl* tmpl, const std::vector<Type>& args, int line) {
  for (size_t i = 0; i < tmpl->typeParams.size(); i++) {
    auto& tp = tmpl->typeParams[i];
    if (tp.constraints.empty()) continue;
    for (auto& c : tp.constraints) {
      if (!satisfiesConstraint(args[i], c))
        error(line, "constraint '" + c + "' violada pelo argumento de tipo '" +
                        tmpl->typeParams[i].name + "='" + typeKey(args[i]) + "'");
    }
  }
}

void Semantic::analyze() {
  registerDeclarations();

  // M10 (v0.46): `specialize T;` — instanciação manual: resolve o tipo
  // (valida aridade/args) e força a monomorfização dos métodos da instância
  // mesmo sem uso posterior; erros de constraint/aridade saem aqui
  for (auto& d : prog_->decls) {
    if (d->kind != DeclKind::Specialize) continue;
    auto* sd = static_cast<SpecializeDecl*>(d.get());
    Type t = resolveType(sd->type, sd->line);
    if (t.kind != Type::Kind::Class)
      error(sd->line, "'specialize' exige classe genérica");
    auto& ci = classes_[t.name];
    if (!ci.isInstance)
      error(sd->line, "'specialize' exige argumentos de tipo "
                      "(ex.: 'specialize Buffer<int, 64>;')");
    auto& tci = classes_[ci.templateFrom];
    std::vector<std::pair<std::string, Type>> cb;
    for (size_t i = 0; i < ci.typeParamNames.size(); i++)
      cb.push_back({ci.typeParamNames[i], ci.typeArgs[i]});
    for (auto* tm : tci.methods)
      methodInstance(tm, t.name, cb, {}, sd->line);
  }

  // M10.1c (v0.46): `reflect T;` — marca a classe como refletida (spec §40);
  // os metadados são consultáveis em compilação (custo zero em runtime)
  for (auto& d : prog_->decls) {
    if (d->kind != DeclKind::Reflect) continue;
    auto* rd = static_cast<ReflectDecl*>(d.get());
    std::string canon = canonicalType(rd->typeName, rd->line);
    if (!classes_.count(canon))
      error(rd->line, "'reflect' exige classe/struct conhecida ('" +
                          rd->typeName + "' não encontrada)");
    classes_[canon].decl->reflected = true;
  }

  checkFunctions();

  // instâncias de genéricos criadas durante a checagem são analisadas por sua
  // vez (cada check pode criar novas instâncias �?? laço até atingir ponto fixo)
  size_t i = 0;
  while (i < instancesPending_.size()) {
    FunctionDecl* fn = instancesPending_[i++];
    if (instancesChecked_.count(fn)) continue;
    instancesChecked_.insert(fn);
    checkFunction(fn);
  }
  // instâncias emitem código como as funções normais
  for (auto* p : instancesPending_) {
    FunctionInfo fi;
    fi.decl = p;
    fi.moduleName = p->moduleName;
    functions_.push_back(fi);
  }
  // HIR (M3): assinaturas + corpos desaçucarados das funções do programa fonte
  // (instâncias de genéricos e funções derivadas não entram no HIR — o codegen
  // as emite da AST)
  loweringToHir(hirProgram, prog_, *this);
}

void Semantic::optimizeHir() {
  hirOptimize(hirProgram);
}

} // namespace hphl
