// ===========================================================================
// llvmapi.h — Backend LLVM API real (Milestone 3, etapa final)
// ===========================================================================
#pragma once

#include <string>

namespace hphl {

// Compila o IR textual do Irgen via a C API do LLVM (libLLVM-22):
// parse -> verify -> [otimização -O1..-O3 via pass manager (Milestone 4)] ->
// object file nativo do host. Retorna false + mensagem em err em caso de falha.
// optLevel: 0 = sem passes (igual ao clang -O0); 1..3 = default<O1..O3>.
//
// M10.5 (v0.51): targetTriple vazio = host nativo (x86-64); não-vazio =
// cross-compilation (ex.: 'aarch64-linux-gnu', 'wasm32-unknown-unknown').
// Os alvos AArch64/WebAssembly/X86 são inicializados explicitamente.
bool llvmEmitObjectFile(const std::string& irText, const std::string& objPath,
                        int optLevel, std::string& err,
                        const std::string& targetTriple = "");

} // namespace hphl