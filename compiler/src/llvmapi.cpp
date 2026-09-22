// ===========================================================================
// llvmapi.cpp — Backend LLVM API real (Milestone 3, etapa final)
//
// Usa a C API do LLVM (libLLVM-22, MSYS2 ucrt64) para compilar o IR textual
// do Irgen: parse, verificação, (Milestone 4) otimização via pass manager e
// emissão do object file nativo do host — sem depender de um clang/llc externo
// no pipeline. O link final (runtime.c) fica com gcc, como nos demais backends.
// ===========================================================================

#include "llvmapi.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/ErrorHandling.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
// C++ API para o pass manager: o LLVMRunPasses (C API) crasha (0xC0000005)
// no libLLVM-22 do MSYS2 com QUALQUER pipeline; o PassBuilder C++ funciona
// (símbolos exportados pelo DLL — verificado com repro mínimo em 2026-08-16)
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/OptimizationLevel.h"
#include <map>
#include <string>

namespace hphl {

// Milestone 4: roda o pipeline default<O1..O3> do pass manager C++ do LLVM
static bool optimizeModule(LLVMModuleRef mod, int optLevel, std::string& err) {
  try {
    llvm::Module* lm = llvm::unwrap(mod);
    llvm::PassBuilder pb;
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    llvm::OptimizationLevel level =
        (optLevel >= 3)  ? llvm::OptimizationLevel::O3
        : (optLevel == 2) ? llvm::OptimizationLevel::O2
                          : llvm::OptimizationLevel::O1;
    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(*lm, mam);
  } catch (const std::exception& e) {
    err = "passes falharam: " + std::string(e.what());
    return false;
  } catch (...) {
    err = "passes falharam: exceção desconhecida";
    return false;
  }
  return true;
}

bool llvmEmitObjectFile(const std::string& irText, const std::string& objPath,
                        int optLevel, std::string& err,
                        const std::string& targetTriple) {
  // inicializa o target nativo (x86-64) — na C API do LLVM 22 os inits
  // por-target ficam atrás das macros LLVM_NATIVE_TARGET*
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();
  if (!targetTriple.empty()) {
    // M10.5: alvos de cross-compilation compilados dentro do libLLVM-22
    // (llvm-config --targets-built confirma AArch64 e WebAssembly); a ordem
    // importa: TargetInfo -> Target -> TargetMC -> AsmPrinter
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyAsmPrinter();
  }

  LLVMContextRef ctx = LLVMContextCreate();
  if (!ctx) {
    err = "LLVMContextCreate falhou";
    return false;
  }

  // o buffer é consumido pelo parse
  LLVMMemoryBufferRef buf =
      LLVMCreateMemoryBufferWithMemoryRangeCopy(irText.data(), irText.size(),
                                                "hphl.ll");
  if (!buf) {
    err = "falha ao criar memory buffer do IR";
    LLVMContextDispose(ctx);
    return false;
  }

  LLVMModuleRef mod = nullptr;
  char* msg = nullptr;
  if (LLVMParseIRInContext2(ctx, buf, &mod, &msg)) {
    err = msg ? msg : "falha ao parsear o IR";
    if (msg) LLVMDisposeMessage(msg);
    LLVMContextDispose(ctx);
    return false;
  }

  if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &msg)) {
    err = msg ? msg : "IR inválido";
    if (msg) LLVMDisposeMessage(msg);
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    return false;
  }

  // M10.5: triple do host ou de cross-compilation (--target); a normalização
  // cobre formas curtas como 'aarch64-linux-gnu' e 'wasm32'
  char* triple = targetTriple.empty() ? LLVMGetDefaultTargetTriple()
                                      : LLVMNormalizeTargetTriple(
                                            targetTriple.c_str());
  if (!triple) {
    err = "triple inválida: '" + targetTriple + "'";
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    return false;
  }
  LLVMSetTarget(mod, triple);

  LLVMTargetRef target = nullptr;
  if (LLVMGetTargetFromTriple(triple, &target, &msg)) {
    err = std::string(msg ? msg : "target não encontrado para a triple") +
          " ('" + triple + "')";
    if (msg) LLVMDisposeMessage(msg);
    LLVMDisposeMessage(triple);
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    return false;
  }

  // M10.5: em alvos WASM, marca toda função DEFINIDA com o atributo
  // 'wasm-export-name' — o AsmPrinter grava o flag WASM_SYMBOL_EXPORTED na
  // tabela de símbolos do objeto e o wasm-ld promove essas entradas à seção
  // EXPORT no link (tornando-as chamáveis do host Node/navegador)
  std::string tripleStr = triple;
  bool isWasm = tripleStr.rfind("wasm32", 0) == 0 ||
                tripleStr.rfind("wasm64", 0) == 0;
  if (isWasm) {
    llvm::Module* lm = llvm::unwrap(mod);
    // Item v0.95 (WASM no browser): exporta nomes LISOS (sem sufixo de
    // aridade `.N`) quando não há ambiguidade — o host JS chama `SnakeInit()`
    // em vez de `SnakeInit.0`. Com overloads reais (mesmo prefixo), mantém o
    // nome mangled para não colidir no link.
    auto stripArity = [](llvm::StringRef n) -> std::string {
      size_t dot = n.rfind('.');
      if (dot == llvm::StringRef::npos || dot + 1 >= n.size()) return n.str();
      for (size_t i = dot + 1; i < n.size(); i++)
        if (n[i] < '0' || n[i] > '9') return n.str();
      return n.substr(0, dot).str();
    };
    std::map<std::string, int> plainCount;
    for (llvm::Function& f : *lm) {
      if (!f.isDeclaration() && !f.getName().empty() && f.getName() != "main")
        plainCount[stripArity(f.getName())]++;
    }
    for (llvm::Function& f : *lm) {
      if (f.isDeclaration() || f.getName().empty() || f.getName() == "main")
        continue;
      std::string plain = stripArity(f.getName());
      const std::string& exportAs =
          (plainCount[plain] == 1) ? plain : f.getName().str();
      f.addFnAttr("wasm-export-name", exportAs);
    }
  }

  // M22 5.2 SSA per-function: pipeline O1..O3 via PassBuilder promove
  // allocas para SSA (mem2reg/SROA). CodeGenLevelNone apenas em -O0 mantém
  // frame pointers para hphl_exc_begin (captura RBP). Com O1+ o codegen usa
  // Aggressive/Default/Less e aloca registradores — SSA values permanecem
  // em registradores LLVM sem derramar para stack. Contrato de exceção
  // preserva callee-saved (rbx/rsi/rdi/r12-15/xmm6-15) no HphlExc.
  LLVMCodeGenOptLevel codegenLevel = (optLevel >= 3)  ? LLVMCodeGenLevelAggressive
                                   : (optLevel == 2) ? LLVMCodeGenLevelDefault
                                   : (optLevel == 1) ? LLVMCodeGenLevelLess
                                                     : LLVMCodeGenLevelNone;
  LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
      target, triple, "", "", codegenLevel, LLVMRelocDefault,
      LLVMCodeModelDefault);
  if (!tm) {
    err = std::string("LLVMCreateTargetMachine falhou para a triple '") +
          triple + "'";
    LLVMDisposeMessage(triple);
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    return false;
  }
  // M10.5: o data layout do alvo é aplicado ao módulo (essencial p/ wasm32,
  // onde i64 tem layout diferente do host x86-64)
  char* dl = LLVMCopyStringRepOfTargetData(LLVMCreateTargetDataLayout(tm));
  LLVMSetDataLayout(mod, dl);
  LLVMDisposeMessage(dl);

  // Milestone 4: otimização via pass manager do LLVM (-O1/-O2/-O3)
  if (optLevel > 0) {
    if (!optimizeModule(mod, optLevel, err)) {
      LLVMDisposeTargetMachine(tm);
      LLVMDisposeMessage(triple);
      LLVMDisposeModule(mod);
      LLVMContextDispose(ctx);
      return false;
    }
    // re-verifica: os passes não podem quebrar o módulo
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &msg)) {
      err = msg ? msg : "IR inválido após otimização";
      if (msg) LLVMDisposeMessage(msg);
      LLVMDisposeTargetMachine(tm);
      LLVMDisposeMessage(triple);
      LLVMDisposeModule(mod);
      LLVMContextDispose(ctx);
      return false;
    }
  }

  bool ok = true;
  if (LLVMTargetMachineEmitToFile(tm, mod, objPath.c_str(), LLVMObjectFile,
                                  &msg)) {
    err = msg ? msg : "falha ao emitir o object file";
    if (msg) LLVMDisposeMessage(msg);
    ok = false;
  }

  LLVMDisposeMessage(triple);
  LLVMDisposeTargetMachine(tm);
  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
  return ok;
}

} // namespace hphl