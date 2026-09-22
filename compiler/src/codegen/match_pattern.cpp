// Auto-generated from codegen.cpp
#include "codegen.h"
#include <cstdio>
#include <functional>
#include "stdlib/stdbuiltins.h"
#include <cstdint>
#include <cstring>

/* M27: hphl_set_class_desc â€” called at startup to register class bitmaps. */
extern "C" int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);

// ConstruÃ§Ã£o de variante: cÃ©lula na heap `[tag][p0][p1]...` (slots de 8 bytes);
// o valor do enum rico Ã© o ponteiro da cÃ©lula (igual a uma referÃªncia de classe).

namespace hphl {
void Codegen::genEnumCtor(const std::string& enumCanon, int entryIndex,
                          const std::vector<Expr*>& args) {
  long long tag = 0;
  const auto& en = *sem_.enums().at(enumCanon);
  if (entryIndex >= 0 && entryIndex < (int)en.entries.size())
    tag = en.entries[entryIndex].value;
  long long bytes = 8 + 8 * (long long)args.size();
  int h = nextSlot_++; // slot temporÃ¡rio com o handle da cÃ©lula
  genCallCalloc(bytes);
  emitText("movq %rax, " + slotRef(h));
  emitText("movq $" + std::to_string(tag) + ", %rax");
  emitText("movq " + slotRef(h) + ", %rcx");
  emitText("movq %rax, (%rcx)");
  for (size_t i = 0; i < args.size(); i++) {
    genExpr(args[i]);
    emitText("movq " + slotRef(h) + ", %rcx");
    if (isFloatType(args[i]->exprType)) {
      emitText("movsd %xmm0, " + std::to_string(8 * (i + 1)) + "(%rcx)");
    } else {
      emitText("movq %rax, " + std::to_string(8 * (i + 1)) + "(%rcx)");
    }
  }
  emitText("movq " + slotRef(h) + ", %rax");
}

void Codegen::genFromInt(const std::string& enumCanon, Expr* arg) {
  EnumDecl* en = sem_.enums().at(enumCanon);
  genExpr(arg);
  int vSlot = nextSlot_++;
  emitText("movq %rax, " + slotRef(vSlot));
  genCallCalloc(16);
  int hSlot = nextSlot_++;
  emitText("movq %rax, " + slotRef(hSlot));
  std::string someL = newLabel("fromint_some");
  std::string endL = newLabel("fromint_end");
  emitText("movq " + slotRef(vSlot) + ", %rax");
  for (auto& e : en->entries) {
    emitText("cmpq $" + std::to_string(e.value) + ", %rax");
    emitText("je " + someL);
  }
  emitText("movq $0, %rax"); // tag None
  emitText("movq " + slotRef(hSlot) + ", %rcx");
  emitText("movq %rax, (%rcx)");
  emitText("movq " + slotRef(vSlot) + ", %rax");
  emitText("movq %rax, 8(%rcx)");
  emitText("jmp " + endL);
  emitText(someL + ":");
  emitText("movq $1, %rax"); // tag Some
  emitText("movq " + slotRef(hSlot) + ", %rcx");
  emitText("movq %rax, (%rcx)");
  emitText("movq " + slotRef(vSlot) + ", %rax");
  emitText("movq %rax, 8(%rcx)");
  emitText(endL + ":");
  emitText("movq " + slotRef(hSlot) + ", %rax");
}

void Codegen::genMatch(MatchExpr* m) {
  const Type& subjType = m->subject->exprType;
  int subjSlot = nextSlot_++; // sujeito avaliado uma Ãºnica vez
  genExpr(m->subject.get());
  emitText("movq %rax, " + slotRef(subjSlot));

  std::string endL = newLabel("match_end");
  for (auto& arm : m->arms) {
    std::string nextL = newLabel("match_next");
    std::string bodyL = newLabel("match_arm");
    scopes_.emplace_back();
    // binding do sujeito: `nome @ padrÃ£o` (para array: guarda a base)
    if (arm.hasSubjectBind && arm.subjectBind != "_") {
      Local l;
      l.type = arm.subjectBindType;
      l.policy = (arm.subjectBindType.kind == Type::Kind::Array)
                     ? StoragePolicy::Heap
                     : StoragePolicy::Stack;
      l.slot = nextSlot_++;
      scopes_.back()[arm.subjectBind] = l;
      emitText("movq " + slotRef(subjSlot) + ", %rax");
      emitText("movq %rax, " + slotRef(l.slot));
    }
    // padrÃ£o (wildcard nÃ£o emite nada); falha salta para nextL
    genPatternAt(arm.pattern.get(), subjType, slotRef(subjSlot), nextL);
    // guarda `when`: se falsa, cai para o prÃ³ximo braÃ§o
    if (arm.guard) {
      genCondTrue(arm.guard.get(), bodyL);
      emitText("jmp " + nextL);
    } else {
      emitText("jmp " + bodyL);
    }
    emitText(bodyL + ":");
    if (arm.yield) {
      genExpr(arm.yield.get());
    } else {
      for (auto& s : arm.body) genStmt(s.get());
    }
    emitText("jmp " + endL);
    emitText(nextL + ":");
    scopes_.pop_back();
  }
  // inalcanÃ§Ã¡vel (match exaustivo); em caso de guardas que falharam, panic
  emitRuntimeCall("hphl_match_fail");
  emitText(endL + ":");
}

void Codegen::genPatternAt(Pattern* p, const Type& t, const std::string& addrExpr,
                           const std::string& nextL) {
  switch (p->kind) {
    case Pattern::K::Wildcard:
      return;
    case Pattern::K::Const:
      // sujeito escalar: [addr] jÃ¡ Ã© o valor; sujeito cÃ©lula (enum rico/
      // Option/Result): [addr] Ã© o ponteiro da cÃ©lula â€” compara a tag
      emitText("movq " + addrExpr + ", %rax");
      if ((t.kind == Type::Kind::Enum && sem_.isRichEnum(t.name)) ||
          t.isOption() || t.isResult())
        emitText("movq (%rax), %rax");
      emitText("cmpq $" + std::to_string(p->constValue) + ", %rax");
      emitText("jne " + nextL);
      return;
    case Pattern::K::StrConst: {
      // sujeito string: handle (char*) em [addr]; compara conte�do via
      // hphl_str_eq(rcx=sujeito, rdx=literal) �?" mesma conven��o do `==`
      std::string lbl = internString(p->strValue);
      emitText("movq " + addrExpr + ", %rcx");
      emitText("leaq " + lbl + "(%rip), %rdx");
      emitRuntimeCall("hphl_str_eq");
      emitText("testq %rax, %rax");
      emitText("je " + nextL);
      return;
    }
    case Pattern::K::Range:
      emitText("movq " + addrExpr + ", %rax");
      emitText("cmpq $" + std::to_string(p->rangeLo) + ", %rax");
      emitText("jl " + nextL);
      emitText("cmpq $" + std::to_string(p->rangeHi) + ", %rax");
      emitText("jg " + nextL);
      return;
    case Pattern::K::Bind: {
      Local l;
      l.type = p->bindType;
      l.policy = (p->bindType.kind == Type::Kind::Array) ? StoragePolicy::Heap
                                                         : StoragePolicy::Stack;
      l.slot = nextSlot_++;
      scopes_.back()[p->bindName] = l;
      emitText("movq " + addrExpr + ", %rax");
      emitText("movq %rax, " + slotRef(l.slot));
      return;
    }
    case Pattern::K::Variant: {
      // o valor do sujeito Ã© o ponteiro da cÃ©lula; tag em (%cÃ©lula)
      emitText("movq " + addrExpr + ", %rax");
      emitText("movq (%rax), %rdx");
      emitText("cmpq $" + std::to_string(p->constValue) + ", %rdx");
      emitText("jne " + nextL);
      int cellSlot = nextSlot_++;
      emitText("movq %rax, " + slotRef(cellSlot));
      for (size_t i = 0; i < p->subs.size(); i++) {
        Type subType;
        if (t.isOption()) {
          subType = *t.elem;
        } else if (t.isResult()) {
          subType = i == 0 ? *t.elem : *t.elem2;
        } else {
          auto* en = sem_.enums().at(t.name);
          subType = en->entries[p->constValue].params[i].type;
        }
        emitText("movq " + slotRef(cellSlot) + ", %r10");
        genPatternAt(p->subs[i].get(), subType,
                     std::to_string(8 * (i + 1)) + "(%r10)", nextL);
      }
      return;
    }
    case Pattern::K::Struct: {
      // o valor do sujeito Ã© o ponteiro da struct; campos em +offset
      const auto& ci = sem_.classes().at(t.name);
      emitText("movq " + addrExpr + ", %rax");
      int sSlot = nextSlot_++;
      emitText("movq %rax, " + slotRef(sSlot));
      for (size_t i = 0; i < p->subs.size(); i++) {
        Type ft = ci.fields.at(p->subNames[i]).first;
        long long off = fieldOffset(t.name, p->subNames[i]);
        emitText("movq " + slotRef(sSlot) + ", %r10");
        genPatternAt(p->subs[i].get(), ft,
                     off > 0 ? (std::to_string(off) + "(%r10)") : "(%r10)", nextL);
      }
      return;
    }
    case Pattern::K::List: {
      if (t.kind == Type::Kind::Array) {
        // array: val do sujeito = base; elementos em base + i*stride
        int stride = typeSize(*t.elem);
        emitText("movq " + addrExpr + ", %rax");
        int baseSlot = nextSlot_++;
        emitText("movq %rax, " + slotRef(baseSlot));
        for (size_t i = 0; i < p->subs.size(); i++) {
          emitText("movq " + slotRef(baseSlot) + ", %r10");
          long long off = (long long)stride * (long long)i;
          genPatternAt(p->subs[i].get(), *t.elem,
                       off > 0 ? (std::to_string(off) + "(%r10)") : "(%r10)",
                       nextL);
        }
        return;
      }
      // list: val o sujeito Ã© o ponteiro do objeto
      int need = (int)p->subs.size();
      emitText("movq " + addrExpr + ", %rax");
      int listSlot = nextSlot_++;
      emitText("movq %rax, " + slotRef(listSlot));
      emitText("movq %rax, %rcx");
      emitRuntimeCall("hphl_list_len");
      int lenSlot = nextSlot_++;
      emitText("movq %rax, " + slotRef(lenSlot));
      if (p->hasRest) {
        if (need > 0) {
          emitText("cmpq $" + std::to_string(need) + ", %rax");
          emitText("jl " + nextL);
        }
      } else {
        emitText("cmpq $" + std::to_string(need) + ", %rax");
        emitText("jne " + nextL);
      }
      int stride = typeSize(*t.elem);
      emitText("movq " + slotRef(listSlot) + ", %rcx");
      emitRuntimeCall("hphl_list_data");
      int baseSlot = nextSlot_++;
      emitText("movq %rax, " + slotRef(baseSlot));
      for (size_t i = 0; i < p->subs.size(); i++) {
        emitText("movq " + slotRef(baseSlot) + ", %r10");
        long long off = (long long)stride * (long long)i;
        genPatternAt(p->subs[i].get(), *t.elem,
                     off > 0 ? (std::to_string(off) + "(%r10)") : "(%r10)",
                     nextL);
      }
      if (p->hasRest) {
        Local l;
        l.type = p->restType;
        l.policy = StoragePolicy::Stack;
        l.slot = nextSlot_++;
        scopes_.back()[p->restName] = l;
        listSlots_.push_back(l.slot);
        emitText("movq " + slotRef(listSlot) + ", %rcx");
        emitText("movq $" + std::to_string(need) + ", %rdx");
        emitText("movq " + slotRef(lenSlot) + ", %r8");
        emitText("subq $" + std::to_string(need) + ", %r8");
        emitText("movq $" + std::to_string(stride) + ", %r9");
        emitRuntimeCall("hphl_list_slice");
        emitText("movq %rax, " + slotRef(l.slot));
      }
      return;
    }
  }
}




} // namespace hphl