#pragma once

#include "hir.h"

namespace hphl {

// ---------------------------------------------------------------------------
// Otimizações do HIR (Milestone 4, Fase 2)
//
// Rodam entre o lowering e o codegen, portanto beneficiam os TRÊS backends
// (x64, IR textual e LLVM API):
//
//   1. constant folding  — aritmética/comparação/unário/cast de literais
//      Int/Float/Bool (conservador: sem overflow, sem divisão por zero,
//      sem deslocamento fora de faixa);
//   2. dead branch       — if/while/do-while/for com condição constante
//      conhecida após o folding;
//   3. DCE de locais     — remoção de declarações locais sem uso cujo
//      inicializador não tem efeito colateral (não mexe em listas,
//      primitivas de sincronização, atômicos etc.).
// ---------------------------------------------------------------------------
void hirOptimize(HirProgram& hir);

} // namespace hphl