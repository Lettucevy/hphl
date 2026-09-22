// mir.h — M10.3 (v0.48): MIR (Mid-level IR) mínimo
//
// Representação intermediária de nível médio entre o HIR e os backends:
// blocos básicos + código de três endereços, alvo-neutro. É a base para o
// SSA do Milestone Safety (M10.4) e para o cache incremental (spec §14).
//
// MVP (--dump-mir): lowering TOTAL do HIR com fallback `Opaque` para os
// constructos ainda não modelados — nunca lança, nunca falha.
#ifndef HPHL_MIR_H
#define HPHL_MIR_H

#include "hir/hir.h"
#include <string>
#include <vector>

namespace hphl {

enum class MirOp {
  Imm,     // dst = imediato inteiro (imm)
  FImm,    // dst = imediato float (text)
  SImm,    // dst = literal string (text)
  Local,   // dst = nome de variável/local/parâmetro (a = nome)
  Bin,     // dst = a OP b      (text = operador: + - * / % & | ^ << >>)
  Cmp,     // dst = a CMP b     (text = == != < <= > >=; resultado 0/1)
  Unary,   // dst = OP a        (text = - ~ !)
  Call,    // dst(?) = call text(args)
  Store,   // a = dst           (a = nome do local; dst = valor)
  Ret,     // ret dst(?)
  Br,      // br a              (a = label)
  CondBr,  // br cond ? a : b   (dst = condição; a/b = labels)
  Phi,     // dst = φ(args)     (SSA: args por predecessor)
  Opaque,  // constructo não modelado no MVP (text descreve)
};

struct MirInst {
  MirOp op = MirOp::Opaque;
  std::string dst; // destino (%tN) ou condição do CondBr
  std::string a;   // operando/label/nome
  std::string b;   // segundo operando/label
  std::string text;// operador/callee/payload/opaque
  long long imm = 0;
  std::vector<std::string> args; // Call: operandos dos argumentos
};

struct MirBlock {
  std::string label;
  std::vector<MirInst> insts;
};

struct MirFunction {
  std::string name;
  Type returnType;
  bool hasReturnType = false;
  std::vector<std::pair<std::string, Type>> params;
  bool isMethod = false;
  std::string ownerClass;
  std::vector<MirBlock> blocks;
};

struct MirProgram {
  std::vector<MirFunction> functions;
};

// lowering total do HIR → MIR (nunca lança; constructos não modelados viram
// instruções Opaque preservando a estrutura de blocos)
MirProgram loweringToMir(const HirProgram& hir);

// M10.4: construção de SSA sobre o MIR — phis nas fronteiras de dominância
// (Cooper-Harvey-Kennedy) + renomeação pela árvore de dominadores
MirProgram mirToSSA(const MirProgram& mir);

// representação textual (debug/`--dump-mir`)
std::string mirToString(const MirProgram& mir);

} // namespace hphl

#endif
