// stdbuiltins.h — M12.0 (v0.54.0): registro declarativo da biblioteca
// padrão. Cada entrada descreve nome HP-HL, assinatura e símbolo do
// runtime C — semantic resolve, codegen x64 e irgen emitem a chamada
// genericamente, sem código por função.
#ifndef HPHL_STDBUILTINS_H
#define HPHL_STDBUILTINS_H

#include <string>
#include <vector>

namespace hphl {

// List = list<string> (split/read_lines/args); ListInt = list<int>
// (builtins numéricos como img_*). Params aceitam qualquer List; o ret
// tipa o elemento para atribuição direta a list<int>.
enum class SBType { Int, Float, Str, Bool, List, ListInt, Void, Ptr };

struct StdBuiltin {
  const char* name;             // nome na linguagem (global)
  std::vector<SBType> params;   // assinatura (overload = outra entrada)
  SBType ret;
  const char* symbol;           // símbolo do runtime C
};

const std::vector<StdBuiltin>& stdBuiltinTable();

// melhor entrada para name/argc/kinds: -1 se nenhuma; empate resolvido por
// score de matches exatos (overload abs(int) vs abs(double))
int findStdBuiltin(const std::string& name, size_t argc,
                   const std::vector<SBType>& argKinds);

} // namespace hphl

#endif
