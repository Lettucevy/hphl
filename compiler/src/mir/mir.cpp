// mir.cpp — M10.3 (v0.48): lowering HIR → MIR + impressora textual
#include "mir.h"
#include <algorithm>
#include <set>
#include <functional>
#include <sstream>

namespace hphl {

namespace {

struct MirLower {
  int tempCounter = 0;
  int blockCounter = 0;

  MirFunction* fn = nullptr;
  MirBlock* cur = nullptr;
  std::vector<std::pair<std::string, std::string>>* loops = nullptr; // (break, continue)

  std::string newTemp() { return "%t" + std::to_string(tempCounter++); }
  std::string newBlockLabel(const char* base) {
    return std::string(base) + std::to_string(blockCounter++);
  }

  void emit(MirInst i) { cur->insts.push_back(std::move(i)); }

  bool curTerminated() const {
    if (!cur || cur->insts.empty()) return false;
    MirOp op = cur->insts.back().op;
    return op == MirOp::Ret || op == MirOp::Br || op == MirOp::CondBr;
  }

  void startBlock(const std::string& label) {
    // fecha o bloco corrente com Br implícito se não terminou
    if (cur && !curTerminated()) {
      MirInst br;
      br.op = MirOp::Br;
      br.a = label;
      emit(std::move(br));
    }
    fn->blocks.push_back({label, {}});
    cur = &fn->blocks.back();
  }

  void emitOpaque(const std::string& what) {
    MirInst i;
    i.op = MirOp::Opaque;
    i.text = what;
    emit(std::move(i));
  }

  // ------------------------------------------------------------------
  // expressões: devolvem o operand-string (imediato/%tN/nome)
  // ------------------------------------------------------------------
  std::string lowerExpr(const HirExpr* e) {
    if (!e) return "";
    switch (e->kind) {
      case HirExprKind::IntLit:
        return std::to_string(static_cast<const HirIntLit*>(e)->value);
      case HirExprKind::BoolLit:
        return std::to_string(
            (long long)(static_cast<const HirBoolLit*>(e)->value ? 1 : 0));
      case HirExprKind::CharLit:
        return std::to_string((long long)static_cast<const HirCharLit*>(e)->value);
      case HirExprKind::FloatLit: {
        std::string t = newTemp();
        MirInst i;
        i.op = MirOp::FImm;
        i.dst = t;
        i.text = std::to_string(static_cast<const HirFloatLit*>(e)->value);
        emit(std::move(i));
        return t;
      }
      case HirExprKind::StringLit: {
        std::string t = newTemp();
        MirInst i;
        i.op = MirOp::SImm;
        i.dst = t;
        i.text = "\"" + static_cast<const HirStringLit*>(e)->value + "\"";
        emit(std::move(i));
        return t;
      }
      case HirExprKind::NullLit: {
        std::string t = newTemp();
        MirInst i;
        i.op = MirOp::Imm;
        i.dst = t;
        i.imm = 0;
        emit(std::move(i));
        return t;
      }
      case HirExprKind::Var: {
        auto* v = static_cast<const HirVar*>(e);
        if (v->isConst) return std::to_string(v->constValue); // enum const
        if (v->isGlobal && !v->label.empty()) return "g:" + v->label;
        return "%" + v->name; // local/parâmetro
      }
      case HirExprKind::This: {
        std::string t = newTemp();
        MirInst i;
        i.op = MirOp::Local;
        i.dst = t;
        i.a = "%this";
        emit(std::move(i));
        return t;
      }
      case HirExprKind::Binary: {
        auto* b = static_cast<const HirBinary*>(e);
        if (b->op == BinOp::And || b->op == BinOp::Or)
          return lowerShortCircuit(b);
        static const char* arith[] = {"+", "-", "*", "/", "%"};
        static const char* cmps[] = {"==", "!=", "<", "<=", ">", ">="};
        static const char* bits[] = {"&", "|", "^", "<<", ">>"};
        const char* opText = nullptr;
        bool isCmp = false;
        switch (b->op) {
          case BinOp::Add: opText = arith[0]; break;
          case BinOp::Sub: opText = arith[1]; break;
          case BinOp::Mul: opText = arith[2]; break;
          case BinOp::Div: opText = arith[3]; break;
          case BinOp::Mod: opText = arith[4]; break;
          case BinOp::Eq: opText = cmps[0]; isCmp = true; break;
          case BinOp::Ne: opText = cmps[1]; isCmp = true; break;
          case BinOp::Lt: opText = cmps[2]; isCmp = true; break;
          case BinOp::Gt: opText = cmps[4]; isCmp = true; break;
          case BinOp::Le: opText = cmps[3]; isCmp = true; break;
          case BinOp::Ge: opText = cmps[5]; isCmp = true; break;
          case BinOp::BitAnd: opText = bits[0]; break;
          case BinOp::BitOr: opText = bits[1]; break;
          case BinOp::BitXor: opText = bits[2]; break;
          case BinOp::Shl: opText = bits[3]; break;
          case BinOp::Shr: opText = bits[4]; break;
          default: break;
        }
        std::string l = lowerExpr(b->lhs.get());
        std::string r = lowerExpr(b->rhs.get());
        std::string t = newTemp();
        MirInst i;
        i.op = isCmp ? MirOp::Cmp : MirOp::Bin;
        i.dst = t;
        i.a = l;
        i.b = r;
        i.text = opText ? opText : "?";
        emit(std::move(i));
        return t;
      }
      case HirExprKind::Unary: {
        auto* u = static_cast<const HirUnary*>(e);
        const char* opText =
            u->op == UnOp::Neg ? "-" : u->op == UnOp::Not ? "!" : "~";
        std::string v = lowerExpr(u->operand.get());
        std::string t = newTemp();
        MirInst i;
        i.op = MirOp::Unary;
        i.dst = t;
        i.a = v;
        i.text = opText;
        emit(std::move(i));
        return t;
      }
      case HirExprKind::Assign: {
        auto* a = static_cast<const HirAssign*>(e);
        // MVP: atribuição a variável local simples
        if (a->target->kind == HirExprKind::Var &&
            !static_cast<const HirVar*>(a->target.get())->isGlobal) {
          std::string name = static_cast<const HirVar*>(a->target.get())->name;
          std::string val = lowerExpr(a->value.get());
          if (a->op != AssignOp::Plain) {
            std::string curV = newTemp();
            MirInst ld;
            ld.op = MirOp::Local;
            ld.dst = curV;
            ld.a = "%" + name;
            emit(std::move(ld));
            std::string combined = newTemp();
            MirInst bin;
            bin.op = MirOp::Bin;
            bin.dst = combined;
            bin.a = curV;
            bin.b = val;
            bin.text = a->op == AssignOp::Add ? "+"
                       : a->op == AssignOp::Sub ? "-"
                       : a->op == AssignOp::Mul ? "*"
                       : a->op == AssignOp::Div ? "/"
                                                : "%";
            emit(std::move(bin));
            val = combined;
          }
          MirInst st;
          st.op = MirOp::Store;
          st.a = name;
          st.dst = val;
          emit(std::move(st));
          return val;
        }
        emitOpaque("assign complexo");
        return "";
      }
      case HirExprKind::Call: {
        auto* c = static_cast<const HirCall*>(e);
        if (c->folded) return std::to_string(c->foldValue);
        std::string callee;
        switch (c->kind) {
          case HirCallKind::Runtime:
          case HirCallKind::ListAdd:
          case HirCallKind::MapPut:
          case HirCallKind::MapGet:
          case HirCallKind::MapContains:
          case HirCallKind::MapRemove:
          case HirCallKind::MapClear:
          case HirCallKind::MapLen:
            callee = c->runtimeName.empty() ? "runtime" : c->runtimeName;
            break;
          case HirCallKind::Print:
            callee = "builtin.print";
            break;
          case HirCallKind::ClockNs:
            callee = "clock_ns";
            break;
          default:
            callee = c->resolved
                         ? (c->resolved->ownerClass.empty()
                                ? c->resolved->name
                                : c->resolved->ownerClass + "." +
                                      c->resolved->name)
                         : "call?";
            break;
        }
        MirInst i;
        i.op = MirOp::Call;
        i.text = callee;
        if (e->type.kind != Type::Kind::Void && e->type.kind != Type::Kind::Unknown)
          i.dst = newTemp();
        for (auto& a : c->args) i.args.push_back(lowerExpr(a.get()));
        std::string dst = i.dst;
        emit(std::move(i));
        return dst;
      }
      default:
        emitOpaque("expr kind " + std::to_string((int)e->kind));
        return "";
    }
  }

  // and/or com curto-circuito via blocos (padrão memória; SSA vem no M10.4)
  std::string lowerShortCircuit(const HirBinary* b) {
    std::string result = newTemp(); // temp que recebe lhs/rhs
    std::string rhsLabel = newBlockLabel("sc.rhs");
    std::string joinLabel = newBlockLabel("sc.join");
    std::string lhs = lowerExpr(b->lhs.get());
    MirInst cb;
    cb.op = MirOp::CondBr;
    cb.dst = lhs;
    if (b->op == BinOp::And) {
      cb.a = rhsLabel;
      cb.b = joinLabel;
    } else {
      cb.a = joinLabel;
      cb.b = rhsLabel;
    }
    emit(std::move(cb));

    startBlock(rhsLabel);
    std::string r = lowerExpr(b->rhs.get());
    MirInst st;
    st.op = MirOp::Store;
    st.a = result.substr(1); // Store usa nome sem %
    st.dst = r;
    emit(std::move(st));
    MirInst br;
    br.op = MirOp::Br;
    br.a = joinLabel;
    emit(std::move(br));

    startBlock(joinLabel);
    std::string t = newTemp();
    MirInst ld;
    ld.op = MirOp::Local;
    ld.dst = t;
    ld.a = result;
    emit(std::move(ld));
    return t;
  }

  // ------------------------------------------------------------------
  // statements
  // ------------------------------------------------------------------
  void lowerStmt(const HirStmt* s) {
    if (!s) return;
    switch (s->kind) {
      case HirStmtKind::Block:
        for (auto& st : static_cast<const HirBlock*>(s)->stmts)
          lowerStmt(st.get());
        break;
      case HirStmtKind::VarDecl: {
        auto* vd = static_cast<const HirVarDecl*>(s);
        if (vd->init) {
          std::string v = lowerExpr(vd->init.get());
          MirInst st;
          st.op = MirOp::Store;
          st.a = vd->name;
          st.dst = v;
          emit(std::move(st));
        }
        break;
      }
      case HirStmtKind::ExprStmt:
        lowerExpr(static_cast<const HirExprStmt*>(s)->expr.get());
        break;
      case HirStmtKind::Return: {
        auto* r = static_cast<const HirReturn*>(s);
        MirInst i;
        i.op = MirOp::Ret;
        if (r->value) i.dst = lowerExpr(r->value.get());
        emit(std::move(i));
        break;
      }
      case HirStmtKind::If: {
        auto* i = static_cast<const HirIf*>(s);
        std::string cond = lowerExpr(i->cond.get());
        std::string thenL = newBlockLabel("if.then");
        std::string elseL = newBlockLabel("if.else");
        std::string endL = newBlockLabel("if.end");
        bool hasElse = i->elseBranch != nullptr;
        MirInst cb;
        cb.op = MirOp::CondBr;
        cb.dst = cond;
        cb.a = thenL;
        cb.b = hasElse ? elseL : endL;
        emit(std::move(cb));
        startBlock(thenL);
        lowerStmt(i->thenBranch.get());
        if (!curTerminated()) {
          MirInst br;
          br.op = MirOp::Br;
          br.a = endL;
          emit(std::move(br));
        }
        if (hasElse) {
          startBlock(elseL);
          lowerStmt(i->elseBranch.get()); // cobre else-if chain também
          if (!curTerminated()) {
            MirInst br;
            br.op = MirOp::Br;
            br.a = endL;
            emit(std::move(br));
          }
        }
        startBlock(endL);
        break;
      }
      case HirStmtKind::While: {
        auto* w = static_cast<const HirWhile*>(s);
        std::string condL = newBlockLabel("while.cond");
        std::string bodyL = newBlockLabel("while.body");
        std::string endL = newBlockLabel("while.end");
        MirInst br0;
        br0.op = MirOp::Br;
        br0.a = condL;
        emit(std::move(br0));
        startBlock(condL);
        std::string cond = lowerExpr(w->cond.get());
        MirInst cb;
        cb.op = MirOp::CondBr;
        cb.dst = cond;
        cb.a = bodyL;
        cb.b = endL;
        emit(std::move(cb));
        startBlock(bodyL);
        loops->push_back({endL, condL});
        lowerStmt(w->body.get());
        loops->pop_back();
        MirInst br;
        br.op = MirOp::Br;
        br.a = condL;
        emit(std::move(br));
        startBlock(endL);
        break;
      }
      case HirStmtKind::For: {
        auto* f = static_cast<const HirFor*>(s);
        if (f->init) lowerStmt(f->init.get());
        std::string condL = newBlockLabel("for.cond");
        std::string bodyL = newBlockLabel("for.body");
        std::string stepL = newBlockLabel("for.step");
        std::string endL = newBlockLabel("for.end");
        MirInst br0;
        br0.op = MirOp::Br;
        br0.a = condL;
        emit(std::move(br0));
        startBlock(condL);
        if (f->cond) {
          std::string cond = lowerExpr(f->cond.get());
          MirInst cb;
          cb.op = MirOp::CondBr;
          cb.dst = cond;
          cb.a = bodyL;
          cb.b = endL;
          emit(std::move(cb));
        }
        startBlock(bodyL);
        loops->push_back({endL, stepL});
        lowerStmt(f->body.get());
        loops->pop_back();
        startBlock(stepL);
        if (f->step) lowerExpr(f->step.get());
        MirInst br;
        br.op = MirOp::Br;
        br.a = condL;
        emit(std::move(br));
        startBlock(endL);
        break;
      }
      case HirStmtKind::Break:
        if (!loops->empty()) {
          MirInst br;
          br.op = MirOp::Br;
          br.a = loops->back().first;
          emit(std::move(br));
          startBlock(newBlockLabel("after.br"));
        }
        break;
      case HirStmtKind::Continue:
        if (!loops->empty()) {
          MirInst br;
          br.op = MirOp::Br;
          br.a = loops->back().second;
          emit(std::move(br));
          startBlock(newBlockLabel("after.cont"));
        }
        break;
      default:
        emitOpaque("stmt kind " + std::to_string((int)s->kind));
        break;
    }
  }
};

} // namespace

MirProgram loweringToMir(const HirProgram& hir) {
  MirProgram out;
  MirLower low;
  std::vector<std::pair<std::string, std::string>> loopStack;
  low.loops = &loopStack;
  for (const auto& hf : hir.functions) {
    if (!hf.body) continue;
    out.functions.push_back({});
    MirFunction& mf = out.functions.back();
    mf.name = hf.name;
    mf.returnType = hf.returnType;
    mf.hasReturnType = hf.hasReturnType;
    mf.params = hf.params;
    mf.isMethod = hf.isMethod;
    mf.ownerClass = hf.ownerClass;

    low.fn = &mf;
    low.tempCounter = 0;
    low.blockCounter = 0;
    low.fn->blocks.push_back({"b0", {}});
    low.cur = &low.fn->blocks.back();
    low.lowerStmt(hf.body.get());
    // fecha com ret implícito se o corpo terminou em aberto
    if (!low.curTerminated()) {
      MirInst r;
      r.op = MirOp::Ret;
      low.emit(std::move(r));
    }
  }
  return out;
}

std::string mirToString(const MirProgram& mir) {
  std::ostringstream o;
  for (const auto& f : mir.functions) {
    o << "func " << f.name << "(";
    for (size_t i = 0; i < f.params.size(); i++) {
      if (i) o << ", ";
      o << "%" << f.params[i].first;
    }
    o << ")";
    o << " {\n";
    for (const auto& blk : f.blocks) {
      o << "  " << blk.label << ":\n";
      for (const auto& i : blk.insts) {
        o << "    ";
        switch (i.op) {
          case MirOp::Imm:
            o << i.dst << " = " << i.imm << "\n";
            break;
          case MirOp::FImm:
            o << i.dst << " = f:" << i.text << "\n";
            break;
          case MirOp::SImm:
            o << i.dst << " = s:" << i.text << "\n";
            break;
          case MirOp::Local:
            o << i.dst << " = " << i.a << "\n";
            break;
          case MirOp::Bin:
          case MirOp::Cmp:
            o << i.dst << " = " << i.a << " " << i.text << " " << i.b << "\n";
            break;
          case MirOp::Unary:
            o << i.dst << " = " << i.text << i.a << "\n";
            break;
          case MirOp::Call: {
            o << i.dst << (i.dst.empty() ? "" : " = ") << "call " << i.text
              << "(";
            for (size_t k = 0; k < i.args.size(); k++) {
              if (k) o << ", ";
              o << i.args[k];
            }
            o << ")\n";
            break;
          }
          case MirOp::Store:
            o << i.a << " = " << i.dst << "\n";
            break;
          case MirOp::Ret:
            o << "ret" << (i.dst.empty() ? "" : " " + i.dst) << "\n";
            break;
          case MirOp::Br:
            o << "br " << i.a << "\n";
            break;
          case MirOp::CondBr:
            o << "br " << i.dst << " ? " << i.a << " : " << i.b << "\n";
            break;
          case MirOp::Phi: {
            o << i.dst << " = φ(";
            for (size_t k = 0; k < i.args.size(); k++) {
              if (k) o << ", ";
              o << i.args[k];
            }
            o << ")\n";
            break;
          }
          case MirOp::Opaque:
            o << "opaque /* " << i.text << " */\n";
            break;
        }
      }
    }
    o << "}\n";
  }
  return o.str();
}



// ---------------------------------------------------------------------------
// M10.4: construção de SSA sobre o MIR
//
// 1. CFG (Br/CondBr) → dominadores iterativos → árvore de dominadores
// 2. phis nas fronteiras de dominância (Cooper-Harvey-Kennedy) para toda
//    variável multi-def
// 3. renomeação pela árvore com pilha por variável (pop ao sair do bloco)
// ---------------------------------------------------------------------------

namespace {

struct SsaBuilder {
  const MirProgram* src = nullptr;
  MirProgram out;
  int n = 0;                                   // nº de blocos da função corrente
  std::vector<std::vector<int>> preds_, succs_;
  std::vector<int> idom_;
  std::vector<std::vector<int>> kids_;         // árvore de dominadores
  std::vector<std::vector<int>> df_;           // fronteiras de dominância

  static bool isVar(const std::string& s) { return s.size() >= 2 && s[0] == '%'; }

  void buildCFG(const MirFunction& f) {
    std::map<std::string, int> idx;
    for (int i = 0; i < (int)f.blocks.size(); i++) idx[f.blocks[i].label] = i;
    n = (int)f.blocks.size();
    preds_.assign(n, {});
    succs_.assign(n, {});
    if (n == 0) return;
    for (int i = 0; i < n; i++) {
      const auto& insts = f.blocks[i].insts;
      if (insts.empty()) continue;
      const MirInst& last = insts.back();
      auto add = [&](const std::string& lbl) {
        auto it = idx.find(lbl);
        if (it != idx.end()) {
          succs_[i].push_back(it->second);
          preds_[it->second].push_back(i);
        }
      };
      if (last.op == MirOp::Br) add(last.a);
      else if (last.op == MirOp::CondBr) {
        add(last.a);
        add(last.b);
      }
    }
  }

  void dominators() {
    idom_.assign(n, -1);
    if (n == 0) return;
    idom_[0] = 0;
    bool changed = true;
    while (changed) {
      changed = false;
      for (int b = 1; b < n; b++) {
        int cand = -1;
        for (int p : preds_[b]) {
          if (idom_[p] == -1 || p == b) continue;
          if (cand == -1) {
            cand = p;
          } else {
            int f1 = p, f2 = cand;
            while (f1 != f2) {
              while (f1 > f2) f1 = idom_[f1];
              while (f2 > f1) f2 = idom_[f2];
            }
            cand = f1;
          }
        }
        if (cand != -1 && idom_[b] != cand) {
          idom_[b] = cand;
          changed = true;
        }
      }
    }
    kids_.assign(n, {});
    for (int b = 1; b < n; b++)
      if (idom_[b] >= 0 && idom_[b] != b) kids_[idom_[b]].push_back(b);
  }

  void dominanceFrontiers() {
    df_.assign(n, {});
    for (int b = 0; b < n; b++) {
      if (preds_[b].size() < 2) continue;
      for (int p : preds_[b]) {
        int runner = p;
        while (runner >= 0 && runner != idom_[b]) {
          if (std::find(df_[runner].begin(), df_[runner].end(), b) ==
              df_[runner].end())
            df_[runner].push_back(b);
          runner = runner == 0 ? -1 : idom_[runner];
        }
      }
    }
  }

  MirProgram run() {
    for (const auto& f : src->functions) {
      out.functions.push_back({});
      MirFunction& mf = out.functions.back();
      mf.name = f.name;
      mf.returnType = f.returnType;
      mf.hasReturnType = f.hasReturnType;
      mf.params = f.params;
      mf.isMethod = f.isMethod;
      mf.ownerClass = f.ownerClass;

      buildCFG(f);
      if (n <= 1) {
        mf.blocks = f.blocks; // bloco único já está em SSA trivial
        continue;
      }
      dominators();
      dominanceFrontiers();

      mf.blocks = f.blocks;

      // defs por variável
      std::map<std::string, std::set<int>> defBlocks;
      for (int bi = 0; bi < n; bi++) {
        for (const auto& i : mf.blocks[bi].insts) {
          switch (i.op) {
            case MirOp::Store:
              defBlocks[i.a].insert(bi);
              break;
            case MirOp::Imm: case MirOp::FImm: case MirOp::SImm:
            case MirOp::Local: case MirOp::Bin: case MirOp::Cmp:
            case MirOp::Unary: case MirOp::Call:
              if (isVar(i.dst)) defBlocks[i.dst].insert(bi);
              break;
            default:
              break;
          }
        }
      }

      // inserção de phis nas fronteiras de dominância
      std::map<std::string, std::vector<std::pair<int, std::string>>> phiOf; // label -> (var)
      for (auto& [var, blocksSet] : defBlocks) {
        if (blocksSet.size() < 2) continue;
        std::set<int> work(blocksSet);
        std::set<int> hasPhi;
        while (!work.empty()) {
          int b = *work.begin();
          work.erase(work.begin());
          for (int d : df_[b]) {
            if (hasPhi.insert(d).second) {
              MirInst phi;
              phi.op = MirOp::Phi;
              phi.dst = var;
              phi.b = var; // preserva o nome ORIGINAL para a renomeação
              phi.text = "φ";
              mf.blocks[d].insts.insert(mf.blocks[d].insts.begin(), phi);
              if (defBlocks[var].insert(d).second) work.insert(d);
            }
          }
        }
      }

      buildCFG(mf); // estrutura não mudou, mas revalida índices
      dominators();

      // renomeação: BFS na árvore de dominadores (pais antes dos filhos),
      // pilha global de versões com pop após processar cada bloco
      std::map<std::string, int> counter;
      std::map<std::string, std::vector<std::string>> stack;
      auto top = [&](const std::string& operand) -> std::string {
        if (!isVar(operand)) return operand;
        std::string name = operand.substr(1);
        auto it = stack.find(name);
        if (it == stack.end() || it->second.empty())
          return "%" + name + ".0"; // leitura sem def visível (parâmetro etc.)
        return "%" + it->second.back();
      };

      std::vector<int> order;
      std::vector<bool> vis(n, false);
      std::vector<int> q{0};
      vis[0] = true;
      while (!q.empty()) {
        int u = q.back();
        q.pop_back();
        order.push_back(u);
        for (int c : kids_[u]) {
          if (!vis[c]) {
            vis[c] = true;
            q.push_back(c);
          }
        }
      }

      // DFS na árvore de dominadores: a pilha da variável permanece viva
      // enquanto os filhos são visitados (pop só após TODOS os filhos)
      std::function<void(int)> go = [&](int u) {
        auto& insts = mf.blocks[u].insts;
        size_t idx = 0;
        std::vector<std::string> mine;
        auto freshAt = [&](const std::string& v) -> std::string {
          std::string name = v.substr(1);
          int ver = counter[name]++;
          std::string vered = name + "." + std::to_string(ver);
          stack[name].push_back(vered);
          mine.push_back(name);
          return "%" + vered;
        };
        // phis primeiro: definem a versão da variável no início do bloco
        for (; idx < insts.size(); idx++) {
          MirInst& i = insts[idx];
          if (i.op != MirOp::Phi) break;
          i.dst = freshAt("%" + i.b); // b = nome original da variável
        }
        for (; idx < insts.size(); idx++) {
          MirInst& i = insts[idx];
          switch (i.op) {
            case MirOp::Store:
              // valor renomeado para versão corrente; o ALVO ganha versão
              // nova (definição)
              i.dst = top(i.dst);
              i.a = freshAt("%" + i.a).substr(1);
              break;
            case MirOp::Local:
              i.a = top(i.a);
              i.dst = freshAt(i.dst);
              break;
            case MirOp::Bin: case MirOp::Cmp:
              i.a = top(i.a);
              i.b = top(i.b);
              i.dst = freshAt(i.dst);
              break;
            case MirOp::Unary:
              i.a = top(i.a);
              i.dst = freshAt(i.dst);
              break;
            case MirOp::Call:
              for (auto& a : i.args) a = top(a);
              if (isVar(i.dst)) i.dst = freshAt(i.dst);
              break;
            case MirOp::Ret:
              if (!i.dst.empty()) i.dst = top(i.dst);
              break;
            default:
              break;
          }
        }
        // preenche os args dos phis dos sucessores vindos deste bloco
        // (usa o nome ORIGINAL do phi — em b — para achar a versão corrente)
        for (int s : succs_[u]) {
          for (MirInst& i : mf.blocks[s].insts) {
            if (i.op != MirOp::Phi) break;
            i.args.push_back(top("%" + i.b));
          }
        }
        for (int c : kids_[u]) go(c);
        // pop das versões criadas neste bloco — SÓ depois dos filhos
        for (auto rit = mine.rbegin(); rit != mine.rend(); ++rit) {
          auto it = stack.find(*rit);
          if (it != stack.end() && !it->second.empty()) it->second.pop_back();
        }
      };
      go(0);
    }
    return out;
  }
};

} // namespace

MirProgram mirToSSA(const MirProgram& mir) {
  SsaBuilder b;
  b.src = &mir;
  return b.run();
}

} // namespace hphl