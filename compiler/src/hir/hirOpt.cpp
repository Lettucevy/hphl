#include "hirOpt.h"

#include <cmath>
#include <cstdint>
#include <set>

namespace hphl {
namespace {

// ---------------------------------------------------------------------------
// helpers de constante
// ---------------------------------------------------------------------------

bool asInt(const HirExpr* e, long long& out, bool& isUnsigned) {
  if (!e || e->kind != HirExprKind::IntLit) return false;
  auto* l = static_cast<const HirIntLit*>(e);
  out = l->value;
  isUnsigned = l->isUnsigned;
  return true;
}

bool asFloat(const HirExpr* e, double& out) {
  if (!e || e->kind != HirExprKind::FloatLit) return false;
  out = static_cast<const HirFloatLit*>(e)->value;
  return true;
}

bool asBool(const HirExpr* e, bool& out) {
  if (!e || e->kind != HirExprKind::BoolLit) return false;
  out = static_cast<const HirBoolLit*>(e)->value;
  return true;
}

// constrói o literal resultante do folding (preserva o tipo do nó original)
std::unique_ptr<HirExpr> mkInt(long long v, bool isUnsigned, const Type& t) {
  auto n = std::make_unique<HirIntLit>();
  n->value = v;
  n->isUnsigned = isUnsigned;
  n->type = t;
  return n;
}

std::unique_ptr<HirExpr> mkFloat(double v, const Type& t) {
  auto n = std::make_unique<HirFloatLit>();
  n->value = v;
  n->type = t;
  return n;
}

std::unique_ptr<HirExpr> mkBool(bool v, const Type& t) {
  auto n = std::make_unique<HirBoolLit>();
  n->value = v;
  n->type = t;
  return n;
}

// ---------------------------------------------------------------------------
// pureza de expressão (para DCE: só remover inicializadores sem efeito)
// ---------------------------------------------------------------------------

bool pureExpr(const HirExpr* e) {
  if (!e) return true;
  switch (e->kind) {
    case HirExprKind::IntLit:
    case HirExprKind::FloatLit:
    case HirExprKind::StringLit:
    case HirExprKind::CharLit:
    case HirExprKind::BoolLit:
    case HirExprKind::NullLit:
    case HirExprKind::This:
      return true;
    case HirExprKind::Var: {
      const HirVar* v = static_cast<const HirVar*>(e);
      return !v->isProperty; // leitura de local/global/campo é pura
    }
    case HirExprKind::Member: {
      const HirMember* m = static_cast<const HirMember*>(e);
      if (m->isProperty || m->isListLength || m->isWait || m->isTaskCancelled ||
          m->isEnumCtor || m->isModuleTypeRef ||
          m->isResultValue || m->isResultError || m->isOptionValue)
        return false; // chamadas/alocações implícitas
      return pureExpr(m->object.get());
    }
    case HirExprKind::Binary: {
      const HirBinary* b = static_cast<const HirBinary*>(e);
      if (b->derivedEq || b->derivedCmp) return false; // comparadores derivados = chamadas
      return pureExpr(b->lhs.get()) && pureExpr(b->rhs.get());
    }
    case HirExprKind::Unary: {
      const HirUnary* u = static_cast<const HirUnary*>(e);
      if (u->op == UnOp::PreInc || u->op == UnOp::PreDec || u->op == UnOp::PostInc ||
          u->op == UnOp::PostDec)
        return false;
      return pureExpr(u->operand.get());
    }
    case HirExprKind::Cast:
      return pureExpr(static_cast<const HirCast*>(e)->operand.get());
    case HirExprKind::LoadAt:
    case HirExprKind::CellTag:
      return true; // leituras de célula/struct
    default:
      // B1: Call, New, Index (bounds check), ArrayLit (alloc), Assign,
      // OptCtor (alloc), Spawn, Await, HirSpawnExpr, List.Add, Node.ctor —
      // todos são impuros (podem alocar, chamar runtime, ou ter side effects)
      return false;
  }
}

// ---------------------------------------------------------------------------
// constant folding de expressões (recursivo; devolve replacement ou nullptr)
// ---------------------------------------------------------------------------

// folda a operação binária quando os dois operandos já são constantes
std::unique_ptr<HirExpr> tryFoldBinary(HirBinary* b) {
  if (b->derivedEq || b->derivedCmp) return nullptr;
  long long a = 0;
  bool isU = false;
  bool hasIntA = asInt(b->lhs.get(), a, isU);
  long long ib = 0;
  bool ibU = false;
  bool hasIntB = asInt(b->rhs.get(), ib, ibU);
  double fa = 0.0, fb = 0.0;
  bool hasFloatA = asFloat(b->lhs.get(), fa);
  bool hasFloatB = asFloat(b->rhs.get(), fb);
  const Type& ty = b->type;

  // int × int com mesmo sinal
  if (hasIntA && hasIntB && ibU == isU) {
    bool isCmp = false;
    bool r = false;
    long long res = 0;
    bool valid = true;
    switch (b->op) {
      case BinOp::Add: case BinOp::Sub: case BinOp::Mul: {
        __int128 acc = 0;
        if (b->op == BinOp::Add) acc = (__int128)a + ib;
        else if (b->op == BinOp::Sub) acc = (__int128)a - ib;
        else acc = (__int128)a * ib;
        if (isU) valid = acc >= 0 && acc <= (__int128)UINT64_MAX;
        else valid = acc >= (__int128)INT64_MIN && acc <= (__int128)INT64_MAX;
        res = (long long)acc;
        break;
      }
      case BinOp::Div:
      case BinOp::Mod:
        if (ib == 0 || (a == INT64_MIN && ib == -1)) { valid = false; break; }
        if (b->op == BinOp::Div) res = a / ib;
        else res = a % ib;
        break;
      case BinOp::BitAnd: res = a & ib; break;
      case BinOp::BitOr:  res = a | ib; break;
      case BinOp::BitXor: res = a ^ ib; break;
      case BinOp::Shl:
      case BinOp::Shr:
        if (ib < 0 || ib >= 64) { valid = false; break; }
        if (b->op == BinOp::Shl) res = (long long)((unsigned long long)a << ib);
        else res = (long long)((unsigned long long)a >> ib);
        break;
      case BinOp::Eq: isCmp = true; r = a == ib; break;
      case BinOp::Ne: isCmp = true; r = a != ib; break;
      case BinOp::Lt: isCmp = true; r = a < ib; break;
      case BinOp::Gt: isCmp = true; r = a > ib; break;
      case BinOp::Le: isCmp = true; r = a <= ib; break;
      case BinOp::Ge: isCmp = true; r = a >= ib; break;
      case BinOp::And: isCmp = true; r = (a != 0) && (ib != 0); break;
      case BinOp::Or:  isCmp = true; r = (a != 0) || (ib != 0); break;
      default: valid = false; break;
    }
    if (valid) {
      if (isCmp) return mkBool(r, ty);
      return mkInt(res, isU, ty);
    }
    return nullptr;
  }

  // float envolvido (int×float, float×int, float×float)
  if ((hasFloatA || hasFloatB) && (hasIntA || hasFloatA) && (hasIntB || hasFloatB)) {
    double fA = hasFloatA ? fa : (double)a;
    double fB = hasFloatB ? fb : (double)ib;
    bool isCmp = false;
    bool r = false;
    double res = 0.0;
    bool valid = true;
    switch (b->op) {
      case BinOp::Add: res = fA + fB; break;
      case BinOp::Sub: res = fA - fB; break;
      case BinOp::Mul: res = fA * fB; break;
      case BinOp::Div: if (fB == 0.0) { valid = false; break; } res = fA / fB; break;
      case BinOp::Eq: isCmp = true; r = fA == fB; break;
      case BinOp::Ne: isCmp = true; r = fA != fB; break;
      case BinOp::Lt: isCmp = true; r = fA < fB; break;
      case BinOp::Gt: isCmp = true; r = fA > fB; break;
      case BinOp::Le: isCmp = true; r = fA <= fB; break;
      case BinOp::Ge: isCmp = true; r = fA >= fB; break;
      default: valid = false; break;
    }
    if (valid) {
      if (isCmp) return mkBool(r, ty);
      return mkFloat(res, ty);
    }
    return nullptr;
  }

  // bool × bool (and/or/eq/ne)
  bool ba = false, bb = false;
  if (asBool(b->lhs.get(), ba) && asBool(b->rhs.get(), bb)) {
    bool r = false;
    switch (b->op) {
      case BinOp::And: r = ba && bb; break;
      case BinOp::Or:  r = ba || bb; break;
      case BinOp::Eq:  r = ba == bb; break;
      case BinOp::Ne:  r = ba != bb; break;
      default: return nullptr;
    }
    return mkBool(r, ty);
  }
  return nullptr;
}

std::unique_ptr<HirExpr> tryFoldUnary(HirUnary* u) {
  const Type& ty = u->type;
  long long a = 0;
  bool isU = false;
  if (asInt(u->operand.get(), a, isU)) {
    switch (u->op) {
      case UnOp::Neg:
        if (isU) return nullptr; // negação de UInt: mantém
        return mkInt(-a, false, ty);
      case UnOp::BitNot:
        return mkInt((long long)(~((unsigned long long)a)), isU, ty);
      case UnOp::Not:
        return mkBool(a == 0, ty);
      default:
        return nullptr; // inc/dec: efeito colateral
    }
  }
  double f = 0.0;
  if (asFloat(u->operand.get(), f) && u->op == UnOp::Neg) return mkFloat(-f, ty);
  bool bv = false;
  if (asBool(u->operand.get(), bv) && u->op == UnOp::Not) return mkBool(!bv, ty);
  return nullptr;
}

std::unique_ptr<HirExpr> tryFoldCast(HirCast* c) {
  const Type& ty = c->type;
  long long a = 0;
  bool isU = false;
  if (asInt(c->operand.get(), a, isU)) {
    switch (c->target.kind) {
      case Type::Kind::Int:  return mkInt(a, false, ty);
      case Type::Kind::UInt: return mkInt(a, true, ty);
      case Type::Kind::Float: return mkFloat((double)a, ty);
      case Type::Kind::Bool: return mkBool(a != 0, ty);
      default: return nullptr;
    }
  }
  double f = 0.0;
  if (asFloat(c->operand.get(), f)) {
    if (c->target.kind == Type::Kind::Int || c->target.kind == Type::Kind::UInt)
      return mkInt((long long)f, c->target.kind == Type::Kind::UInt, ty);
    if (c->target.kind == Type::Kind::Float) return mkFloat(f, ty);
    if (c->target.kind == Type::Kind::Bool) return mkBool(f != 0.0, ty);
    return nullptr;
  }
  bool bv = false;
  if (asBool(c->operand.get(), bv)) {
    if (c->target.kind == Type::Kind::Int || c->target.kind == Type::Kind::UInt)
      return mkInt(bv ? 1 : 0, c->target.kind == Type::Kind::UInt, ty);
  }
  return nullptr;
}

// declarado abaixo (dead branch); o folding de corpos de task precisa dele
void simplifyList(std::vector<std::unique_ptr<HirStmt>>& stmts, bool& changed);

// recursão nos filhos + folding do próprio nó (substitui o nó quando folda)
void foldExpr(std::unique_ptr<HirExpr>& e) {
  if (!e) return;
  switch (e->kind) {
    case HirExprKind::Binary: {
      auto* b = static_cast<HirBinary*>(e.get());
      foldExpr(b->lhs);
      foldExpr(b->rhs);
      if (auto repl = tryFoldBinary(b)) e = std::move(repl);
      break;
    }
    case HirExprKind::Unary: {
      auto* u = static_cast<HirUnary*>(e.get());
      foldExpr(u->operand);
      if (auto repl = tryFoldUnary(u)) e = std::move(repl);
      break;
    }
    case HirExprKind::Cast: {
      auto* c = static_cast<HirCast*>(e.get());
      foldExpr(c->operand);
      if (auto repl = tryFoldCast(c)) e = std::move(repl);
      break;
    }
    case HirExprKind::Assign: {
      auto* a = static_cast<HirAssign*>(e.get());
      foldExpr(a->value);
      foldExpr(a->target); // cuidado: não dobra o TARGET (lvalue) — só filhos
      break;
    }
    case HirExprKind::Call: {
      auto* c = static_cast<HirCall*>(e.get());
      for (auto& a : c->args) foldExpr(a);
      break;
    }
    case HirExprKind::New: {
      auto* n = static_cast<HirNew*>(e.get());
      for (auto& a : n->args) foldExpr(a);
      break;
    }
    case HirExprKind::ArrayLit: {
      auto* al = static_cast<HirArrayLit*>(e.get());
      for (auto& el : al->elements) foldExpr(el);
      break;
    }
    case HirExprKind::Index: {
      auto* ix = static_cast<HirIndex*>(e.get());
      foldExpr(ix->object);
      foldExpr(ix->index);
      break;
    }
    case HirExprKind::Member: {
      auto* m = static_cast<HirMember*>(e.get());
      foldExpr(m->object);
      break;
    }
    case HirExprKind::CellTag:
    case HirExprKind::LoadAt: {
      auto* la = static_cast<HirLoadAt*>(e.get());
      foldExpr(la->subject);
      break;
    }
    case HirExprKind::OptCtor: {
      auto* o = static_cast<HirOptCtor*>(e.get());
      foldExpr(o->arg);
      break;
    }
    case HirExprKind::Await: {
      auto* aw = static_cast<HirAwaitExpr*>(e.get());
      foldExpr(aw->operand);
      break;
    }
    case HirExprKind::Spawn: {
      // corpo da task (HirSpawnExpr): folda/simplifica como um bloco
      auto* sp = static_cast<HirSpawnExpr*>(e.get());
      bool ch = false;
      simplifyList(sp->body->stmts, ch);
      break;
    }
    default:
      break; // literais, Var, This
  }
}

// ---------------------------------------------------------------------------
// dead branch: remove if/while/do-while/for com condição constante
// ---------------------------------------------------------------------------

bool stmtCondIsTrue(const HirExpr* e, bool& isConst, bool& value) {
  isConst = false;
  if (e && e->kind == HirExprKind::BoolLit) {
    isConst = true;
    value = static_cast<const HirBoolLit*>(e)->value;
    return true;
  }
  if (e && e->kind == HirExprKind::IntLit) {
    // condição inteira (ex.: `while (1)`)
    isConst = true;
    value = (static_cast<const HirIntLit*>(e)->value != 0);
    return true;
  }
  return false;
}

// devolve o replacement do stmt (nullptr = remover)
std::unique_ptr<HirStmt> simplifyStmt(std::unique_ptr<HirStmt> s, bool& changed);

// processa uma lista de stmts in-place (remove/simplifica e desce nos blocos)
void simplifyList(std::vector<std::unique_ptr<HirStmt>>& stmts, bool& changed) {
  std::vector<std::unique_ptr<HirStmt>> out;
  out.reserve(stmts.size());
  for (auto& s : stmts) {
    std::unique_ptr<HirStmt> kept = simplifyStmt(std::move(s), changed);
    if (kept) out.push_back(std::move(kept));
  }
  stmts = std::move(out);
}

std::unique_ptr<HirStmt> simplifyStmt(std::unique_ptr<HirStmt> s, bool& changed) {
  if (!s) return nullptr;
  switch (s->kind) {
    case HirStmtKind::If: {
      auto* i = static_cast<HirIf*>(s.get());
      foldExpr(i->cond);
      bool isConst = false, value = false;
      stmtCondIsTrue(i->cond.get(), isConst, value);
      if (isConst) {
        changed = true;
        if (value) {
          // then sempre executa: o bloco vira o stmt
          return std::move(i->thenBranch);
        }
        // else (se houver) sempre executa
        if (i->elseBranch) return std::move(i->elseBranch);
        return nullptr;
      }
      simplifyList(i->thenBranch->stmts, changed);
      if (i->elseBranch) {
        if (i->elseBranch->kind == HirStmtKind::Block) {
          simplifyList(static_cast<HirBlock*>(i->elseBranch.get())->stmts, changed);
        } else {
          std::unique_ptr<HirStmt> nb = simplifyStmt(std::move(i->elseBranch), changed);
          i->elseBranch = std::move(nb);
        }
      }
      break;
    }
    case HirStmtKind::While: {
      auto* w = static_cast<HirWhile*>(s.get());
      foldExpr(w->cond);
      bool isConst = false, value = false;
      stmtCondIsTrue(w->cond.get(), isConst, value);
      if (isConst && !value) {
        changed = true;
        return nullptr; // while (false): nunca executa
      }
      simplifyList(w->body->stmts, changed);
      break;
    }
    case HirStmtKind::DoWhile: {
      auto* d = static_cast<HirDoWhile*>(s.get());
      foldExpr(d->cond);
      simplifyList(d->body->stmts, changed);
      bool isConst = false, value = false;
      stmtCondIsTrue(d->cond.get(), isConst, value);
      if (isConst && !value) {
        // do-while: o corpo roda UMA vez
        changed = true;
        return std::move(d->body);
      }
      break;
    }
    case HirStmtKind::For: {
      auto* f = static_cast<HirFor*>(s.get());
      foldExpr(f->cond);
      bool isConst = false, value = false;
      stmtCondIsTrue(f->cond.get(), isConst, value);
      if (isConst && !value) {
        // for (init; false; step): só o init executa
        changed = true;
        return std::move(f->init);
      }
      simplifyList(f->body->stmts, changed);
      break;
    }
    case HirStmtKind::Block:
      simplifyList(static_cast<HirBlock*>(s.get())->stmts, changed);
      break;
    case HirStmtKind::VarDecl: {
      auto* vd = static_cast<HirVarDecl*>(s.get());
      foldExpr(vd->init);
      break;
    }
    case HirStmtKind::ExprStmt: {
      auto* es = static_cast<HirExprStmt*>(s.get());
      foldExpr(es->expr);
      break;
    }
    case HirStmtKind::Return: {
      auto* r = static_cast<HirReturn*>(s.get());
      foldExpr(r->value);
      break;
    }
    case HirStmtKind::Assert: {
      auto* a = static_cast<HirAssert*>(s.get());
      foldExpr(a->cond);
      break;
    }
    case HirStmtKind::Panic: {
      auto* p = static_cast<HirPanic*>(s.get());
      foldExpr(p->message);
      break;
    }
    case HirStmtKind::Throw: {
      auto* t = static_cast<HirThrow*>(s.get());
      foldExpr(t->value);
      break;
    }
    case HirStmtKind::Try: {
      auto* t = static_cast<HirTry*>(s.get());
      simplifyList(t->body->stmts, changed);
      if (t->catchBody) simplifyList(t->catchBody->stmts, changed);
      break;
    }
    case HirStmtKind::Lock: {
      auto* l = static_cast<HirLock*>(s.get());
      foldExpr(l->target);
      simplifyList(l->body->stmts, changed);
      break;
    }
    case HirStmtKind::Spawn: {
      auto* sp = static_cast<HirSpawn*>(s.get());
      simplifyList(sp->body->stmts, changed);
      break;
    }
    case HirStmtKind::Parallel: {
      auto* p = static_cast<HirParallel*>(s.get());
      for (auto& part : p->parts) simplifyList(part->stmts, changed);
      break;
    }
    case HirStmtKind::ParallelForeach: {
      auto* pf = static_cast<HirParallelForeach*>(s.get());
      foldExpr(pf->collection);
      simplifyList(pf->body->stmts, changed);
      break;
    }
    default:
      break; // Break/Continue
  }
  return s;
}

// ---------------------------------------------------------------------------
// DCE de locais: remove declarações não usadas sem efeito colateral
// ---------------------------------------------------------------------------

void collectNames(const HirStmt* s, std::set<std::string>& used);

void collectNamesExpr(const HirExpr* e, std::set<std::string>& used) {
  if (!e) return;
  switch (e->kind) {
    case HirExprKind::Var: {
      const HirVar* v = static_cast<const HirVar*>(e);
      if (!v->name.empty()) used.insert(v->name);
      break;
    }
    case HirExprKind::Binary: {
      const HirBinary* b = static_cast<const HirBinary*>(e);
      collectNamesExpr(b->lhs.get(), used);
      collectNamesExpr(b->rhs.get(), used);
      break;
    }
    case HirExprKind::Unary:
      collectNamesExpr(static_cast<const HirUnary*>(e)->operand.get(), used);
      break;
    case HirExprKind::Cast:
      collectNamesExpr(static_cast<const HirCast*>(e)->operand.get(), used);
      break;
    case HirExprKind::Assign: {
      const HirAssign* a = static_cast<const HirAssign*>(e);
      collectNamesExpr(a->target.get(), used);
      collectNamesExpr(a->value.get(), used);
      break;
    }
    case HirExprKind::Call: {
      const HirCall* c = static_cast<const HirCall*>(e);
      for (auto& a : c->args) collectNamesExpr(a.get(), used);
      break;
    }
    case HirExprKind::New: {
      const HirNew* n = static_cast<const HirNew*>(e);
      for (auto& a : n->args) collectNamesExpr(a.get(), used);
      break;
    }
    case HirExprKind::ArrayLit: {
      const HirArrayLit* al = static_cast<const HirArrayLit*>(e);
      for (auto& el : al->elements) collectNamesExpr(el.get(), used);
      break;
    }
    case HirExprKind::Index: {
      const HirIndex* ix = static_cast<const HirIndex*>(e);
      collectNamesExpr(ix->object.get(), used);
      collectNamesExpr(ix->index.get(), used);
      break;
    }
    case HirExprKind::Member:
      collectNamesExpr(static_cast<const HirMember*>(e)->object.get(), used);
      break;
    case HirExprKind::Spawn: {
      const HirSpawnExpr* sp = static_cast<const HirSpawnExpr*>(e);
      collectNames(sp->body.get(), used); // corpo captura locais do caller
      break;
    }
    case HirExprKind::Lambda: {
      const HirLambda* l = static_cast<const HirLambda*>(e);
      for (auto& cap : l->captures)
        if (!cap.first.empty()) used.insert(cap.first);
      break;
    }
    case HirExprKind::CellTag:
    case HirExprKind::LoadAt:
      collectNamesExpr(static_cast<const HirLoadAt*>(e)->subject.get(), used);
      break;
    case HirExprKind::OptCtor:
      collectNamesExpr(static_cast<const HirOptCtor*>(e)->arg.get(), used);
      break;
    case HirExprKind::Await:
      collectNamesExpr(static_cast<const HirAwaitExpr*>(e)->operand.get(), used);
      break;
    default:
      break;
  }
}

void collectNames(const HirStmt* s, std::set<std::string>& used) {
  if (!s) return;
  switch (s->kind) {
    case HirStmtKind::Block:
      for (auto& st : static_cast<const HirBlock*>(s)->stmts) collectNames(st.get(), used);
      break;
    case HirStmtKind::VarDecl: {
      const HirVarDecl* vd = static_cast<const HirVarDecl*>(s);
      collectNamesExpr(vd->init.get(), used); // definição em si NÃO conta
      break;
    }
    case HirStmtKind::ExprStmt:
      collectNamesExpr(static_cast<const HirExprStmt*>(s)->expr.get(), used);
      break;
    case HirStmtKind::Return:
      collectNamesExpr(static_cast<const HirReturn*>(s)->value.get(), used);
      break;
    case HirStmtKind::If: {
      const HirIf* i = static_cast<const HirIf*>(s);
      collectNamesExpr(i->cond.get(), used);
      collectNames(i->thenBranch.get(), used);
      collectNames(i->elseBranch.get(), used);
      break;
    }
    case HirStmtKind::While: {
      const HirWhile* w = static_cast<const HirWhile*>(s);
      collectNamesExpr(w->cond.get(), used);
      collectNames(w->body.get(), used);
      break;
    }
    case HirStmtKind::DoWhile: {
      const HirDoWhile* d = static_cast<const HirDoWhile*>(s);
      collectNamesExpr(d->cond.get(), used);
      collectNames(d->body.get(), used);
      break;
    }
    case HirStmtKind::For: {
      const HirFor* f = static_cast<const HirFor*>(s);
      collectNames(f->init.get(), used);
      collectNamesExpr(f->cond.get(), used);
      collectNamesExpr(f->step.get(), used);
      collectNames(f->body.get(), used);
      break;
    }
    case HirStmtKind::Panic:
      collectNamesExpr(static_cast<const HirPanic*>(s)->message.get(), used);
      break;
    case HirStmtKind::Assert:
      collectNamesExpr(static_cast<const HirAssert*>(s)->cond.get(), used);
      break;
    case HirStmtKind::Throw:
      collectNamesExpr(static_cast<const HirThrow*>(s)->value.get(), used);
      break;
    case HirStmtKind::Try: {
      const HirTry* t = static_cast<const HirTry*>(s);
      collectNames(t->body.get(), used);
      if (t->catchBody) collectNames(t->catchBody.get(), used);
      break;
    }
    case HirStmtKind::Lock: {
      const HirLock* l = static_cast<const HirLock*>(s);
      collectNamesExpr(l->target.get(), used);
      collectNames(l->body.get(), used);
      break;
    }
    case HirStmtKind::Spawn: {
      const HirSpawn* sp = static_cast<const HirSpawn*>(s);
      collectNames(sp->body.get(), used);
      break;
    }
    case HirStmtKind::Parallel: {
      const HirParallel* p = static_cast<const HirParallel*>(s);
      for (auto& part : p->parts) collectNames(part.get(), used);
      break;
    }
    case HirStmtKind::ParallelForeach: {
      const HirParallelForeach* pf = static_cast<const HirParallelForeach*>(s);
      collectNamesExpr(pf->collection.get(), used);
      collectNames(pf->body.get(), used);
      break;
    }
    default:
      break;
  }
}

bool dceLocal(const HirVarDecl* vd, const std::set<std::string>& used) {
  if (vd->atomic) return false;
  // tipos com criação implícita no runtime: nunca remover
  if (vd->type.kind == Type::Kind::List || vd->type.kind == Type::Kind::Mutex ||
      vd->type.kind == Type::Kind::Semaphore || vd->type.kind == Type::Kind::Event ||
      vd->type.kind == Type::Kind::Barrier)
    return false;
  if (used.count(vd->name)) return false;
  return pureExpr(vd->init.get());
}

void dceBlock(std::vector<std::unique_ptr<HirStmt>>& stmts, const std::set<std::string>& used) {
  std::vector<std::unique_ptr<HirStmt>> out;
  out.reserve(stmts.size());
  for (auto& s : stmts) {
    if (s->kind == HirStmtKind::VarDecl) {
      const HirVarDecl* vd = static_cast<const HirVarDecl*>(s.get());
      if (dceLocal(vd, used)) continue; // remove
    }
    out.push_back(std::move(s));
  }
  stmts = std::move(out);
}

void dceStmt(HirStmt* s, const std::set<std::string>& used) {
  if (!s) return;
  switch (s->kind) {
    case HirStmtKind::Block: {
      auto* b = static_cast<HirBlock*>(s);
      dceBlock(b->stmts, used);
      for (auto& st : b->stmts) dceStmt(st.get(), used);
      break;
    }
    case HirStmtKind::If: {
      auto* i = static_cast<HirIf*>(s);
      dceStmt(i->thenBranch.get(), used);
      dceStmt(i->elseBranch.get(), used);
      break;
    }
    case HirStmtKind::While: {
      auto* w = static_cast<HirWhile*>(s);
      dceStmt(w->body.get(), used);
      break;
    }
    case HirStmtKind::DoWhile: {
      auto* d = static_cast<HirDoWhile*>(s);
      dceStmt(d->body.get(), used);
      break;
    }
    case HirStmtKind::For: {
      auto* f = static_cast<HirFor*>(s);
      dceStmt(f->init.get(), used);
      dceStmt(f->body.get(), used);
      break;
    }
    case HirStmtKind::Try: {
      auto* t = static_cast<HirTry*>(s);
      dceStmt(t->body.get(), used);
      if (t->catchBody) dceStmt(t->catchBody.get(), used);
      break;
    }
    case HirStmtKind::Lock: {
      auto* l = static_cast<HirLock*>(s);
      dceStmt(l->body.get(), used);
      break;
    }
    case HirStmtKind::Spawn: {
      auto* sp = static_cast<HirSpawn*>(s);
      dceStmt(sp->body.get(), used);
      break;
    }
    case HirStmtKind::Parallel: {
      auto* p = static_cast<HirParallel*>(s);
      for (auto& part : p->parts) dceStmt(part.get(), used);
      break;
    }
    case HirStmtKind::ParallelForeach: {
      auto* pf = static_cast<HirParallelForeach*>(s);
      dceStmt(pf->body.get(), used);
      break;
    }
    default:
      break;
  }
}

} // namespace

// ---------------------------------------------------------------------------
// API pública
// ---------------------------------------------------------------------------

void hirOptimize(HirProgram& hir) {
  for (auto& fn : hir.functions) {
    if (!fn.body) continue;

    // 1+2: folding + dead branch até o ponto fixo (poucas iterações bastam)
    bool changed = true;
    int rounds = 0;
    while (changed && rounds < 4) {
      changed = false;
      simplifyList(fn.body->stmts, changed);
      rounds++;
    }

    // 3: DCE de locais não usados (nomes usados em toda a função)
    std::set<std::string> used;
    collectNames(fn.body.get(), used);
    dceStmt(fn.body.get(), used);
  }
}

} // namespace hphl