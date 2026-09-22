/*
 * hphlc — Compilador HP-HL (High-Performance High-Level Language)
 *
 * Uso:
 *   hphlc <arquivo.hphl> [opções]
 *
 * Opções principais:
 *   -o <saida>     caminho do executável (padrão: <arquivo>.exe)
 *   --backend      backend de codegen: x64 (padrão), llvm (otimizado), ir (textual)
 *   -O0..-O3       nível de otimização no backend llvm (padrão: -O0)
 *   --run          compila, linka e executa o programa diretamente
 *   --lsp          inicia o servidor Language Server Protocol (LSP)
 *   --dap          inicia o servidor Debug Adapter Protocol (DAP)
 *   -h, --help     mostra ajuda completa
 *
 * Pipeline: lexer → parser → semântica → codegen (x64 nativo / LLVM IR) → linker.
 */
#include "codegen/codegen.h"
#include "mir/mir.h"
#include "dap/dap.h"
#include "irgen/irgen.h"
#include "json/json.h"
#include "frontend/lexer.h"
#include "loader/loader.h"
#include "lsp/lsp.h"
#include "llvmapi.h"
#include "frontend/parser.h"
#include "loader/pkgfetch.h"
#include "messages/message_loader.h"
#include "semantic/semantic.h"
#include "version.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <functional>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct Options {
  std::string input;
  std::string output;
  std::string gcc;
  std::string backend;  // "ir" para LLVM IR textual, "llvm" para LLVM API
  int optLevel = 0;     // Milestone 4: 0 = sem otimização; 1..3 = -O1..-O3
  bool keepAsm = false;
  bool noLink = false;
  bool run = false;
  bool dumpHir = false;
  bool dumpMir = false; // M10.3: imprime o MIR (blocos + three-address) e sai
  bool ssa = false;     // M10.4: com --dump-mir, constrói a forma SSA
  bool lsp = false;
  bool dap = false;     // Milestone 7: servidor DAP (debug adapter)
  bool debug = false;   // Milestone 7: instrumentação do debugger (x64)
  std::string target;   // M10.5: triple de cross-compilation (--target)
  std::string project;  // M13.4: arquivo .hpproj (build system mínimo)
  bool incremental = false; // M26 14.1: cache hash-based incremental
  int jobs = 0;            // M26 14.5: -1=auto, 0=default(auto), N>0=limite de paralelismo
  std::string targetOs;    // M26 14.5: --target-os windows|linux|macos (para conditional)
  std::string language = "en";   // M30: language for compiler messages (en|ptbr|es, default en)
  std::string buildTask;   // M26 14.5: --task <nome> (compila só esta task)
  std::string package;  // M14.2: diretório com package.hpkg (package manager)
  bool frozenLockfile = false; // M17 Fase 3: --frozen-lockfile
  std::string fetchCmd; // M17 fetch: "name@version" (subcomando fetch)
  bool installCmd = false; // M17 install: subcomando install (lê package.hpkg do cwd)
  bool publishCmd = false; // M17 publish: subcomando publish [dir] [--out <registry_dir>]
  std::string publishDir;
  std::string publishOut;
  std::vector<std::string> depDirs;
  std::vector<std::string> extraSources;
  bool help = false;
  bool showVersion = false; // --version / -V
};

void usage() {
  std::cout
      << "HP-HL Compiler (hphlc) v" << HPHL_VERSION << "\n"
      << "Usage: hphlc <file.hphl> [options]\n\n"
      << "Options:\n"
      << "  -o <path>                Output executable or object path (default: <file>.exe)\n"
      << "  --backend <x64|llvm|ir>  Code generation backend (default: x64)\n"
      << "  -O0, -O1, -O2, -O3       Optimization level for LLVM backend (default: -O0)\n"
      << "  --run                    Compile, link and execute the program immediately\n"
      << "  --keep-asm               Keep intermediate assembly (.s) or IR (.ll) file\n"
      << "  --no-link                Generate assembly/object without invoking linker\n"
      << "  --target <triple>        Cross-compilation target triple (e.g. wasm32-unknown-unknown,\n"
      << "                           aarch64-linux-gnu, aarch64-pc-windows-msvc)\n"
      << "  --lsp                    Run in Language Server Protocol (LSP) mode via stdio\n"
      << "  --dap                    Run in Debug Adapter Protocol (DAP) mode via stdio\n"
      << "  --debug                  Enable debug symbol instrumentation (x64 backend)\n"
      << "  --dump-hir               Print High-Level Intermediate Representation and exit\n"
      << "  --dump-mir               Print Mid-Level Intermediate Representation and exit\n"
      << "  --project <file.hpproj>  Build project using .hpproj manifest\n"
      << "  --incremental            Enable hash-based incremental compilation cache\n"
      << "  --jobs <N>               Parallel build jobs (0 = automatic)\n"
      << "  --task <name>            Compile specific task in project manifest\n"
      << "  --language <en|ptbr|es>  Language for compiler diagnostics and messages (default: en)\n"
      << "  -v, --version            Display version information\n"
      << "  -h, --help               Display this help message\n";
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool writeFile(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << content;
  return out.good();
}

bool fileExists(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return in.good();
}

std::string formatCompileError(const hphl::CompileError& e, const std::string& src, const std::string& filename) {
  std::ostringstream out;
  // e.message já contém "arquivo:linha:coluna: erro de sintaxe: ..." (parser.cpp:51), então não duplicar
  // Se e.message já começa com filename, usar direto; senão prefixar
  if (e.message.rfind(filename, 0) == 0) out << e.message << "\n";
  else out << filename << ":" << e.line << ":" << e.column << ": " << e.message << "\n";
  // extrai linha do src
  std::istringstream iss(src);
  std::string line;
  int cur = 1;
  while (std::getline(iss, line)) {
    if (cur == e.line) {
      // remove \r
      if (!line.empty() && line.back() == '\r') line.pop_back();
      out << line << "\n";
      int col = e.column > 0 ? e.column - 1 : 0;
      if (col > (int)line.size()) col = line.size();
      out << std::string(col, ' ') << "^\n";
      break;
    }
    cur++;
  }
  return out.str();
}

int levenshtein(const std::string& a, const std::string& b) {
  size_t n = a.size(), m = b.size();
  std::vector<std::vector<int>> dp(n+1, std::vector<int>(m+1, 0));
  for (size_t i=0;i<=n;i++) dp[i][0]=i;
  for (size_t j=0;j<=m;j++) dp[0][j]=j;
  for (size_t i=1;i<=n;i++) for (size_t j=1;j<=m;j++) {
    int cost = (a[i-1]==b[j-1])?0:1;
    dp[i][j]=std::min({dp[i-1][j]+1, dp[i][j-1]+1, dp[i-1][j-1]+cost});
  }
  return dp[n][m];
}
std::string didYouMean(const std::string& target, const std::vector<std::string>& cands) {
  std::string best; int bestDist=100;
  for (auto& c: cands) {
    int d = levenshtein(target, c);
    if (d < bestDist && d <= 3 && d < (int)target.size()) { bestDist=d; best=c; }
  }
  if (!best.empty()) return " (did you mean '" + best + "'?)";
  return "";
}

// modificação mais recente (para invalidar o cache do runtime pré-compilado)
long long fileMtime(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return -1;
  return (long long)st.st_mtime;
}

std::string dirOf(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return ".";
  return path.substr(0, pos);
}

// ---------------------------------------------------------------------------
// Módulos e import
// ---------------------------------------------------------------------------
// O ImportLoader (main + imports transitivos num único Program) vive em
// loader.h/loader.cpp (também usado pelo servidor LSP com overlay in-memory).

// diretório do próprio executável (independente do cwd)
std::string exeDir() {
#ifdef _WIN32
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n > 0) {
    std::string p(buf, n);
    size_t pos = p.find_last_of("/\\");
    if (pos != std::string::npos) return p.substr(0, pos);
  }
#endif
  return "";
}

std::string baseOf(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  std::string name = pos == std::string::npos ? path : path.substr(pos + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

std::string findGcc(const std::string& explicitPath) {
  if (!explicitPath.empty() && fileExists(explicitPath)) return explicitPath;
  // padrA�es comuns do MSYS2/ucrt64
  const char* candidates[] = {
      "C:\\msys64\\ucrt64\\bin\\gcc.exe",
      "C:\\msys64\\mingw64\\bin\\gcc.exe",
      nullptr,
  };
  for (int i = 0; candidates[i]; i++) {
    if (fileExists(candidates[i])) return candidates[i];
  }
  return "gcc"; // confia no PATH
}

// M10.5: linker WebAssembly para o passo --target wasm* (lld do LLVM)
// L5 (Sprint 5): descoberta cross-platform via `where` (Windows) / `which` (Unix)
// ou diretorios comuns do LLVM/MSYS2.
std::string findWasmLd() {
  const char* candidates[] = {
      "C:\\Program Files\\LLVM\\bin\\wasm-ld.exe",
      "C:\\Program Files (x86)\\LLVM\\bin\\wasm-ld.exe",
      "C:\\msys64\\ucrt64\\bin\\wasm-ld.exe",
      "/usr/lib/llvm-22/bin/wasm-ld",
      "/usr/bin/wasm-ld",
      "/opt/homebrew/opt/llvm@22/bin/wasm-ld",
      "/usr/local/opt/llvm/bin/wasm-ld",
      nullptr,
  };
  for (int i = 0; candidates[i]; i++) {
    if (fileExists(candidates[i])) return candidates[i];
  }
  // Tenta `where wasm-ld` (Windows) / `which wasm-ld` (Unix)
  FILE* pipe = nullptr;
#if defined(_WIN32)
  pipe = _popen("where wasm-ld 2>NUL", "r");
#else
  pipe = popen("which wasm-ld 2>/dev/null", "r");
#endif
  if (pipe) {
    char buf[512] = {0};
    if (fgets(buf, sizeof(buf), pipe)) {
      // remove \r\n final
      size_t n = strlen(buf);
      while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
      if (n > 0) {
#if defined(_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return std::string(buf);
      }
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
  }
  return "wasm-ld"; // fallback: confia no PATH
}

std::string findRuntime() {
  // runtime.c pode estar ao lado do executável do compilador, em HPHL_EXE_DIR ou no cwd.
  // Para uma instalação com layout "compiler/bin/hphlc.exe + compiler/src/runtime/main.c", o
  // executável também pode ficar na raiz do projeto (copiado), então tenta
  // <dir>/compiler/src/runtime.c em cada base.
  std::vector<std::string> dirs;
  if (const char* exe = std::getenv("HPHL_EXE_DIR")) {
    dirs.push_back(exe);
    dirs.push_back(dirOf(exe));
    dirs.push_back(dirOf(dirOf(exe)));
  }
  std::string self = exeDir();
  if (!self.empty()) {
    dirs.push_back(self);
    dirs.push_back(dirOf(self));
    dirs.push_back(dirOf(dirOf(self)));
  }
  if (const char* home = std::getenv("HPHL_HOME")) {
    dirs.push_back(home);
    dirs.push_back(std::string(home) + hphl::pathSep() + "compiler");
  }
  dirs.push_back(".");
  dirs.push_back("..");
  for (const auto& dir : dirs) {
    std::string p1 = dir + hphl::pathSep() + "src" + hphl::pathSep() + "runtime" + hphl::pathSep() + "main.c";
    std::string p2 = dir + hphl::pathSep() + "compiler" + hphl::pathSep() + "src" + hphl::pathSep() + "runtime" + hphl::pathSep() + "main.c";
    std::string p3 = dir + hphl::pathSep() + "runtime.c";
    std::string p4 = dir + hphl::pathSep() + "src" + hphl::pathSep() + "runtime.c";
    std::string p5 = dir + hphl::pathSep() + "compiler" + hphl::pathSep() + "src" + hphl::pathSep() + "runtime.c";
    std::string p6 = dir + hphl::pathSep() + "runtime" + hphl::pathSep() + "main.c";
    if (fileExists(p1)) return p1;
    if (fileExists(p2)) return p2;
    if (fileExists(p3)) return p3;
    if (fileExists(p4)) return p4;
    if (fileExists(p5)) return p5;
    if (fileExists(p6)) return p6;
  }
  return "";
}

int run(const std::string& cmd);

// runtime.o pré-compilado: a parte cara do link (recompilar o runtime.c com
// todo o <windows.h> leva ~1.5s de cada 2.4s do ciclo). O objeto é colocado ao
// lado do runtime.c e invalidado por mtime (se runtime.c ou o gcc mudarem).
std::string ensureRuntimeObj(const std::string& gcc, const std::string& runtime) {
  if (runtime.empty()) return "";
  std::string obj = runtime.substr(0, runtime.size() - 2) + ".o"; // runtime.c → runtime.o
  long long mObj = fileMtime(obj);
  std::string runtimeDir = dirOf(runtime);
  std::vector<std::string> runtimeDeps = {
      runtimeDir + hphl::pathSep() + "runtime_api.h",
      runtimeDir + hphl::pathSep() + "runtime.h",
      runtimeDir + hphl::pathSep() + "core" + hphl::pathSep() + "core.c",
      runtimeDir + hphl::pathSep() + "strings" + hphl::pathSep() + "strings.c",
      runtimeDir + hphl::pathSep() + "concurrency" + hphl::pathSep() + "concurrency.c",
      runtimeDir + hphl::pathSep() + "collections" + hphl::pathSep() + "collections.c",
      runtimeDir + hphl::pathSep() + "debug" + hphl::pathSep() + "debug.c",
      runtimeDir + hphl::pathSep() + "alloc" + hphl::pathSep() + "alloc.c",
      runtimeDir + hphl::pathSep() + "gc" + hphl::pathSep() + "gc.c",
      runtimeDir + hphl::pathSep() + "img" + hphl::pathSep() + "img.c",
      runtimeDir + hphl::pathSep() + "net" + hphl::pathSep() + "socket.c",
      runtimeDir + hphl::pathSep() + "mem" + hphl::pathSep() + "mem.c",
  };
  bool runtimeChanged = mObj < 0 || fileMtime(runtime) > mObj || fileMtime(gcc) > mObj;
  for (const auto& dep : runtimeDeps) runtimeChanged = runtimeChanged || fileMtime(dep) > mObj;
  if (runtimeChanged) {
    std::string cmd = "\"" + gcc + "\" -O2 -c \"" + runtime + "\" -o \"" + obj + "\"";
    if (run(cmd) != 0) {
      std::cerr << "hphlc: " << hphl::messages().get("cli_precompile_runtime_failed", {obj}) << "\n";
      return "";
    }
    // recompilou: o objeto agora existe (cancelar o remove no final)
    return obj;
  }
  return obj;
}

bool parseArgs(int argc, char** argv, Options& opts) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      opts.help = true;
    } else if (a == "-V" || a == "--version") {
      opts.showVersion = true;
    } else if (a == "-o") {
      if (i + 1 >= argc) return false;
      opts.output = argv[++i];
    } else if (a == "--keep-asm") {
      opts.keepAsm = true;
    } else if (a == "--gcc") {
      if (i + 1 >= argc) return false;
      opts.gcc = argv[++i];
    } else if (a == "--no-link") {
      opts.noLink = true;
    } else if (a == "--run") {
      opts.run = true;
    } else if (a == "--dump-hir") {
      opts.dumpHir = true;
    } else if (a == "--dump-mir") {
      opts.dumpMir = true;
    } else if (a == "--ssa") {
      opts.ssa = true;
    } else if (a == "--lsp") {
      opts.lsp = true;
    } else if (a == "--dap") {
      opts.dap = true;
    } else if (a == "--debug") {
      opts.debug = true;
    } else if (a == "--backend") {
      if (i + 1 >= argc) return false;
      opts.backend = argv[++i];
    } else if (a == "--target") {
      // M10.5: triple de cross-compilation (backend llvm)
      if (i + 1 >= argc) return false;
      opts.target = argv[++i];
    } else if (a == "--project") {
      if (i + 1 >= argc) return false;
      opts.project = argv[++i];
    } else if (a == "--incremental") {
      opts.incremental = true;
    } else if (a == "--jobs") {
      if (i + 1 >= argc) return false;
      opts.jobs = std::atoi(argv[++i]);
      if (opts.jobs < 0) opts.jobs = 0;
    } else if (a == "--task") {
      if (i + 1 >= argc) return false;
      opts.buildTask = argv[++i];
    } else if (a == "--target-os") {
      if (i + 1 >= argc) return false;
      opts.targetOs = argv[++i];
    } else if (a == "--language") {
      if (i + 1 >= argc) return false;
      opts.language = argv[++i];
    } else if (a == "--package") {
      if (i + 1 >= argc) return false;
      opts.package = argv[++i];
    } else if (a.size() >= 2 && a[0] == '-' && a[1] == 'O' &&
               a.size() <= 3 && a[2] >= '0' && a[2] <= '3') {
      opts.optLevel = a[2] - '0';
    } else if (a == "--frozen-lockfile") {
      opts.frozenLockfile = true;
    } else if (a == "fetch") {
      // M17: subcomando fetch <pkg>[@<version>]
      if (i + 1 >= argc) { std::cerr << "hphlc: " << hphl::messages().get("cli_fetch_requires_arg") << "\n"; return false; }
      opts.fetchCmd = argv[++i];
    } else if (a == "install") {
      // M17: subcomando install
      opts.installCmd = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        opts.package = argv[++i];
      }
    } else if (a == "publish") {
      // M17: subcomando publish [dir] [--out <registry_dir>]
      opts.publishCmd = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        opts.publishDir = argv[++i];
      }
    } else if (a == "--out") {
      if (i + 1 >= argc) { std::cerr << "hphlc: " << hphl::messages().get("cli_out_requires_dir") << "\n"; return false; }
      opts.publishOut = argv[++i];
    } else if (!a.empty() && a[0] == '-') {
      return false;
    } else {
      if (!opts.input.empty()) return false;
      opts.input = a;
    }
  }
  return true;
}

int run(const std::string& cmd) {
#ifdef _WIN32
  // cmd /S /C: evita o strip de aspas do cmd.exe quando o comando
  // começa com '"' (caminho com espaços no gcc)
  std::string full = "cmd /S /C \"" + cmd + "\"";
  int rc = std::system(full.c_str());
  // no Windows, system() retorna o código como exit code (>=0) ou -1
  if (rc == -1) return -1;
  return rc;
#else
  int rc = std::system(cmd.c_str());
  if (rc == -1) return -1;
  return WEXITSTATUS(rc);
#endif
}

#ifdef _WIN32
static void ensureOutputFileUnlocked(const std::string& outPath) {
  // Se o arquivo de saída existe mas não pode ser aberto para escrita,
  // significa que uma instância anterior do .exe ainda está rodando no Windows.
  FILE* f = fopen(outPath.c_str(), "ab");
  if (f) {
    fclose(f);
    return;
  }
  // Encerra o processo pelo nome de arquivo para liberar o lock de escrita do gcc/ld
  size_t slash = outPath.find_last_of("/\\");
  std::string exeName = (slash != std::string::npos) ? outPath.substr(slash + 1) : outPath;
  std::string killCmd = "taskkill /F /IM \"" + exeName + "\" >nul 2>&1";
  std::system(killCmd.c_str());
  Sleep(150);
}
#else
static void ensureOutputFileUnlocked(const std::string&) {}
#endif

// ---------------------------------------------------------------------------
// M26 14.5 — Build system avançado (tasks/deps/parallel/conditional)
// ---------------------------------------------------------------------------

// Compila uma fonte .hphl para .o usando opts (copy). Retorna 0 OK, !=0 erro.
// `errOut` recebe uma mensagem de erro se falhar (vazio se OK).
int compileSourceToObj(const std::string& src, const Options& optsIn,
                       const std::string& objPath, std::string& errOut) {
  errOut.clear();
  Options opts = optsIn;
  if (opts.backend == "auto") {
    if (opts.debug) opts.backend.clear();
    else { opts.backend = "llvm"; if (opts.optLevel==0) opts.optLevel=2; }
  }
  std::string srcText = readFile(src);
  if (srcText.empty()) { errOut = hphl::messages().get("cli_empty_or_unreadable_source", {src}); std::cerr << errOut << "\n"; return 1; }
  hphl::Program prog;
  hphl::ImportLoader loader(src);
  try { loader.load(src, prog); } catch (const hphl::CompileError& e) { errOut = formatCompileError(e, srcText, src); std::cerr << errOut << "\n"; return 1; }
  hphl::Semantic sem(&prog, src);
  try { sem.analyze(); sem.optimizeHir(); } catch (const hphl::CompileError& e) { errOut = formatCompileError(e, srcText, src); std::cerr << errOut << "\n"; return 1; }
  if (opts.backend == "llvm" || opts.backend == "ir") {
    hphl::Irgen irgen(sem, src, opts.debug);
    std::string irText = irgen.generate();
    std::string lerr;
    if (!hphl::llvmEmitObjectFile(irText, objPath, opts.optLevel, lerr, opts.target)) {
      errOut = "backend LLVM: " + lerr;
      return 1;
    }
    return 0;
  }
  hphl::Codegen cg(sem, src, opts.debug);
  std::string asmText = cg.generate();
  std::string sep = hphl::pathSep();
  std::string asmPath = dirOf(objPath) + sep + baseOf(src) + ".tmp.s";
  if (!writeFile(asmPath, asmText)) { errOut = hphl::messages().get("cli_cannot_write_file", {asmPath}); return 1; }
  std::string gcc = findGcc(opts.gcc);
  int opt = opts.optLevel >= 2 ? 2 : (opts.optLevel == 1 ? 1 : 0);
  std::string cmd = "\"" + gcc + "\" -O" + std::to_string(opt) + " -c \"" + asmPath + "\" -o \"" + objPath + "\"";
  int rc = run(cmd);
  if (!opts.keepAsm) std::remove(asmPath.c_str());
  if (rc != 0) { errOut = hphl::messages().get("cli_assemble_failed", {src}); return rc; }
  return 0;
}

// Avalia uma expressão de condição simples (M26 14.5):
//   target == "x86_64"        → opts.target contém "x86_64"
//   os == "windows"           → targetOs == "windows" (default do host)
//   feature("debug")          → opts.debug == true
//   optimization == "release" → optLevel >= 2
// Sem `==`, `!=`, `!`, `&&`, `||`, etc. — apenas comparação de igualdade e
// presença de feature. Vazio ou ausente → true.
bool evalCondition(const std::string& cond, const Options& opts) {
  if (cond.empty()) return true;
  // !expr (negação)
  if (cond[0] == '!') return !evalCondition(cond.substr(1), opts);
  // target == "x86_64" / "wasm32" / etc.
  size_t eq = cond.find("==");
  if (eq != std::string::npos) {
    std::string lhs = cond.substr(0, eq);
    std::string rhs = cond.substr(eq + 2);
    // trim
    while (!lhs.empty() && (lhs.back() == ' ' || lhs.back() == '\t')) lhs.pop_back();
    while (!rhs.empty() && (rhs[0] == ' ' || rhs[0] == '\t')) rhs = rhs.substr(1);
    // strip aspas
    if (rhs.size() >= 2 && rhs.front() == '"' && rhs.back() == '"') rhs = rhs.substr(1, rhs.size() - 2);
    while (!lhs.empty() && (lhs.back() == ' ' || lhs.back() == '\t')) lhs.pop_back();
    while (!lhs.empty() && lhs[0] == ' ') lhs = lhs.substr(1);
    if (lhs == "target") return opts.target.find(rhs) != std::string::npos;
    if (lhs == "os") {
      std::string os = opts.targetOs;
      if (os.empty()) {
#ifdef _WIN32
        os = "windows";
#else
        os = "linux";
#endif
      }
      return os == rhs;
    }
    if (lhs == "optimization") {
      std::string level = opts.optLevel >= 2 ? "release" : (opts.optLevel >= 1 ? "debug" : "none");
      return level == rhs;
    }
    if (lhs == "backend") return opts.backend == rhs;
    return false;
  }
  // feature("name")
  if (cond.rfind("feature(", 0) == 0 && cond.back() == ')') {
    std::string name = cond.substr(8, cond.size() - 9);
    if (name == "debug") return opts.debug;
    if (name == "incremental") return opts.incremental;
    if (name == "wasm") return opts.target.rfind("wasm", 0) == 0;
    return false;
  }
  return false;
}

// Esquema intermediário: cada task lida do JSON.
struct BuildTask {
  std::string name;
  std::vector<std::string> sources;   // caminhos absolutos
  std::vector<std::string> deps;      // nomes de outras tasks
  std::string optsStr;                // "opts" (ex: "-O2 --backend llvm")
  std::string when;                   // "when": "target == \"x86_64\""
  std::vector<std::string> objs;      // preenchido após compilar
  std::string objPattern;             // padrão para nomear objs (padrão: build/<task>/<base>.o)
  bool isEntry = false;               // true para a task que tem "output" no root
  std::vector<std::string> externLibs; // FFI: libs de `extern "lib"` acumuladas
};

// Faz parse do .hpproj em tasks. Se o JSON tem só {sources, output, opts}
// (formato antigo), cria uma task implícita "main" com as sources.
bool parseBuildTasks(const hphl::JsonValue& proj,
                     const std::string& projDir,
                     const Options& /*cliOpts*/,
                     std::vector<BuildTask>& tasks,
                     std::string& errOut) {
  std::string sep = hphl::pathSep();
  std::string projOpts = proj["opts"].asString("");
  std::string outPath = proj["output"].asString("a.exe");
  // resolve output
  if (outPath.find(':') == std::string::npos && outPath.find('/') == std::string::npos && outPath.find('\\') == std::string::npos) {
    outPath = projDir + sep + outPath;
  } else if (outPath.rfind("./", 0) == 0 || outPath.rfind(".\\", 0) == 0) {
    outPath = projDir + sep + outPath.substr(2);
  }
  auto tasksVal = proj["tasks"];
  if (tasksVal.isObject() && tasksVal.memberCount() > 0) {
    // schema novo: tasks com deps
    for (size_t i = 0; i < tasksVal.memberCount(); i++) {
      const auto& kv = tasksVal.memberAt(i);
      const std::string& tname = kv.first;
      const hphl::JsonValue& tv = kv.second;
      if (!tv.isObject()) { errOut = hphl::messages().get("cli_task_must_be_object", {tname}); return false; }
      BuildTask t;
      t.name = tname;
      t.optsStr = tv["opts"].asString("");
      t.when = tv["when"].asString("");
      t.isEntry = (tname == proj["entry_task"].asString(""));
      auto srcs = tv["sources"];
      if (!srcs.isArray() || srcs.size() == 0) {
        errOut = hphl::messages().get("cli_task_sources_non_empty", {tname});
        return false;
      }
      for (size_t j = 0; j < srcs.size(); j++) {
        std::string s = srcs.at(j).asString("");
        if (s.empty()) continue;
        if (s.find(':') == std::string::npos && s[0] != '/' && s[0] != '\\') {
          if (s.rfind("./", 0) == 0 || s.rfind(".\\", 0) == 0) s = s.substr(2);
          s = projDir + sep + s;
        }
        t.sources.push_back(s);
      }
      auto deps = tv["deps"];
      if (deps.isArray()) {
        for (size_t j = 0; j < deps.size(); j++) {
          std::string d = deps.at(j).asString("");
          if (!d.empty()) t.deps.push_back(d);
        }
      }
      t.objPattern = tv["obj_dir"].asString(""); // vazio = build/<task>/
      tasks.push_back(std::move(t));
    }
    // marca a entry task
    std::string entryName = proj["entry_task"].asString("");
    if (entryName.empty()) {
      // heurística: task cujo nome bate com o basename do output
      std::string base = baseOf(outPath);
      for (auto& t : tasks) if (t.name == base) entryName = t.name;
      if (entryName.empty() && !tasks.empty()) entryName = tasks.back().name; // última = main
    }
    for (auto& t : tasks) if (t.name == entryName) t.isEntry = true;
    return true;
  }
  // schema antigo: cria task única "main" (preserva comportamento M13.4)
  auto srcs = proj["sources"];
  if (!srcs.isArray() || srcs.size() == 0) {
    errOut = hphl::messages().get("cli_project_sources_non_empty");
    return false;
  }
  BuildTask t;
  t.name = "main";
  t.optsStr = projOpts;
  t.isEntry = true;
  for (size_t j = 0; j < srcs.size(); j++) {
    std::string s = srcs.at(j).asString("");
    if (s.empty()) continue;
    if (s.find(':') == std::string::npos && s[0] != '/' && s[0] != '\\') {
      if (s.rfind("./", 0) == 0 || s.rfind(".\\", 0) == 0) s = s.substr(2);
      s = projDir + sep + s;
    }
    t.sources.push_back(s);
  }
  tasks.push_back(std::move(t));
  return true;
}

// Aplica opts string aos pOpts (parsing de "-O2 --backend llvm" etc.)
Options projectOptsToOptions(const Options& base, const std::string& optsStr) {
  Options p = base;
  if (optsStr.empty()) return p;
  std::istringstream iss(optsStr);
  std::vector<std::string> toks;
  std::string tok;
  while (iss >> tok) toks.push_back(tok);
  for (size_t i = 0; i < toks.size(); i++) {
    std::string a = toks[i];
    if (a.rfind("-O", 0) == 0 && a.size() <= 3 && a[2] >= '0' && a[2] <= '3') {
      p.optLevel = a[2] - '0';
    } else if (a == "--backend" && i + 1 < toks.size()) {
      p.backend = toks[++i];
    } else if (a == "--keep-asm") {
      p.keepAsm = true;
    } else if (a == "--debug") {
      p.debug = true;
    } else if (a == "--target" && i + 1 < toks.size()) {
      p.target = toks[++i];
    }
  }
  return p;
}

// Resolve ordem topológica de tasks (Kahn). Retorna false em ciclo.
bool topoSortTasks(const std::vector<BuildTask>& tasks, const std::string& onlyTask,
                   std::vector<std::string>& order, std::string& errOut) {
  std::map<std::string, size_t> idx;
  for (size_t i = 0; i < tasks.size(); i++) idx[tasks[i].name] = i;
  std::vector<int> indeg(tasks.size(), 0);
  std::vector<std::vector<size_t>> adj(tasks.size());
  std::set<std::string> knownDeps;
  for (size_t i = 0; i < tasks.size(); i++) {
    for (auto& d : tasks[i].deps) {
      if (idx.find(d) == idx.end()) { errOut = hphl::messages().get("cli_task_depends_missing", {tasks[i].name, d}); return false; }
      adj[idx[d]].push_back(i);
      indeg[i]++;
    }
  }
  // Filtra: se onlyTask foi pedido, mantém só ele e suas deps
  std::set<size_t> keep;
  if (!onlyTask.empty()) {
    if (idx.find(onlyTask) == idx.end()) { errOut = hphl::messages().get("cli_task_not_found", {onlyTask}); return false; }
    std::queue<size_t> q;
    q.push(idx[onlyTask]);
    keep.insert(idx[onlyTask]);
    while (!q.empty()) {
      size_t k = q.front(); q.pop();
      for (auto& d : tasks[k].deps) {
        size_t di = idx[d];
        if (keep.insert(di).second) q.push(di);
      }
    }
  } else {
    for (size_t i = 0; i < tasks.size(); i++) keep.insert(i);
  }
  // Kahn filtrado
  std::queue<size_t> q;
  for (size_t i = 0; i < tasks.size(); i++) {
    if (keep.count(i) && indeg[i] == 0) q.push(i);
  }
  while (!q.empty()) {
    size_t k = q.front(); q.pop();
    order.push_back(tasks[k].name);
    for (size_t n : adj[k]) {
      if (!keep.count(n)) continue;
      if (--indeg[n] == 0) q.push(n);
    }
  }
  if (order.size() != keep.size()) { errOut = hphl::messages().get("cli_cyclic_dependencies"); return false; }
  return true;
}

// Compila uma task inteira. Retorna 0 OK, !=0 erro.
int compileTask(BuildTask& t, const Options& baseOpts, const std::string& objRoot,
                const std::string& /*out*/, std::string& errOut) {
  errOut.clear();
  Options pOpts = projectOptsToOptions(baseOpts, t.optsStr);
  std::string sep = hphl::pathSep();
  std::string taskObjDir = objRoot + sep + t.name;
#ifdef _WIN32
  CreateDirectoryA(objRoot.c_str(), NULL);
  CreateDirectoryA(taskObjDir.c_str(), NULL);
#else
  mkdir(objRoot.c_str(), 0755);
  mkdir(taskObjDir.c_str(), 0755);
#endif
  t.objs.clear();
  for (auto& src : t.sources) {
    std::string srcText = readFile(src);
    if (srcText.empty()) { errOut = hphl::messages().get("cli_empty_or_unreadable_source", {src}); return 1; }
    hphl::Program prog;
    hphl::ImportLoader loader(src);
    try { loader.load(src, prog); } catch (const hphl::CompileError& e) { errOut = formatCompileError(e, srcText, src); return 1; }
    hphl::Semantic sem(&prog, src);
    try { sem.analyze(); sem.optimizeHir(); } catch (const hphl::CompileError& e) { errOut = formatCompileError(e, srcText, src); return 1; }
    for (auto& lib : sem.externLibs()) {
      if (std::find(t.externLibs.begin(), t.externLibs.end(), lib) == t.externLibs.end())
        t.externLibs.push_back(lib);
    }
    bool hasEntry = false;
    for (auto& d : prog.decls) {
      if (d->kind == hphl::DeclKind::Function && static_cast<hphl::FunctionDecl*>(d.get())->isEntryPoint) {
        hasEntry = true; break;
      }
    }
    if (!hasEntry) continue; // biblioteca: sem Main, só verifica
    std::string objPath = taskObjDir + sep + baseOf(src) + ".o";
    std::string srcErr;
    int rc = compileSourceToObj(src, pOpts, objPath, srcErr);
    if (rc != 0) {
      errOut = "[" + t.name + "] " + src + ": " + srcErr;
      std::cerr << errOut << "\n";
      return rc;
    }
    t.objs.push_back(objPath);
  }
  return 0;
}

// FFI v2: diretórios -L extras para bibliotecas nativas.
// - HPHL_LIBDIR: lista de diretórios separados por ';' (ou ':' no Unix)
// - VULKAN_SDK: auto-adiciona $VULKAN_SDK/Lib quando 'vulkan-1' é linkada
static std::string nativeLibDirFlags(const std::vector<std::string>& libs) {
  std::string flags;
  auto addDir = [&](const std::string& d) {
    if (d.empty()) return;
    // sem trailing '\'/'/': '...tools\"' quebraria o parsing MSVCRT (\" = quote literal)
    size_t n = d.size();
    while (n > 1 && (d[n - 1] == '\\' || d[n - 1] == '/')) n--;
    flags += " -L\"" + d.substr(0, n) + "\"";
  };
  auto fileExists = [](const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return f.good();
  };
  if (const char* e = std::getenv("HPHL_LIBDIR")) {
    std::string s = e;
#ifdef _WIN32
    const char* seps = ";"; // ':' quebraria "C:\..." no Windows
#else
    const char* seps = ";:";
#endif
    size_t i = 0;
    while (i <= s.size()) {
      size_t j = s.find_first_of(seps, i);
      if (j == std::string::npos) j = s.size();
      if (j > i) addDir(s.substr(i, j - i));
      i = j + 1;
    }
  }
  bool needVulkan = false;
  for (auto& l : libs)
    if (l == "vulkan-1") { needVulkan = true; break; }
  if (needVulkan) {
    if (const char* vs = std::getenv("VULKAN_SDK")) {
      std::string d = std::string(vs) + "/Lib";
      if (fileExists(d + "/vulkan-1.lib") || fileExists(d + "/libvulkan-1.dll.a"))
        addDir(d);
    }
  }
  return flags;
}

// Linka objetos em out. Para a task entry, gera executável. Para libs,
// gera .a (archive estática) — mas M26 14.5 foca em entry.
int linkObjects(const std::vector<std::string>& objs, const std::string& out,
                const std::string& gcc, const std::string& runtime, std::string& errOut,
                const std::vector<std::string>& extraLibs = {}) {
  errOut.clear();
  if (objs.empty()) { errOut = hphl::messages().get("cli_no_objects_to_link"); return 1; }
  std::string cmd = "\"" + gcc + "\" -O2 -Wl,--stack,8388608 -o \"" + out + "\"";
  for (auto& o : objs) cmd += " \"" + o + "\"";
  std::string ro = ensureRuntimeObj(gcc, runtime);
  if (!ro.empty()) cmd += " \"" + ro + "\"";
  else if (!runtime.empty()) cmd += " \"" + runtime + "\"";
#ifdef _WIN32
  cmd += " -lws2_32 -lwsock32 -lbcrypt -lz";
#endif
  cmd += nativeLibDirFlags(extraLibs);
  for (auto& lib : extraLibs) cmd += " -l" + lib;
  int rc = run(cmd);
  if (rc != 0) { errOut = hphl::messages().get("cli_linker_returned_code", {std::to_string(rc)}); return rc; }
  return 0;
}

// Pipeline completo de build project: detecta schema, compila tasks em
// paralelo (limitado por --jobs), linka entry. Retorna exit code.
int runBuildProject(const Options& cliOpts) {
  std::string projFile = cliOpts.project;
  std::string projText = readFile(projFile);
  if (projText.empty()) {
    std::cerr << "hphlc: " << hphl::messages().get("cli_cannot_read_project", {projFile}) << "\n";
    return 1;
  }
  hphl::JsonValue proj;
  try { proj = hphl::parseJson(projText); } catch (const std::exception& e) {
    std::cerr << "hphlc: " << hphl::messages().get("cli_project_invalid_json", {std::string(e.what())}) << "\n";
    return 1;
  }
  if (!proj.isObject()) { std::cerr << "hphlc: " << hphl::messages().get("cli_project_must_be_object") << "\n"; return 1; }
  std::string projDir = dirOf(projFile);
  std::string sep = hphl::pathSep();
  // parse tasks
  std::vector<BuildTask> tasks;
  std::string errOut;
  if (!parseBuildTasks(proj, projDir, cliOpts, tasks, errOut)) {
    std::cerr << "hphlc: " << errOut << "\n";
    return 1;
  }
  if (tasks.empty()) { std::cerr << "hphlc: " << hphl::messages().get("cli_project_no_tasks") << "\n"; return 1; }
  // output
  std::string outPath = proj["output"].asString("a.exe");
  if (outPath.find(':') == std::string::npos && outPath.find('/') == std::string::npos && outPath.find('\\') == std::string::npos) {
    outPath = projDir + sep + outPath;
  } else if (outPath.rfind("./", 0) == 0 || outPath.rfind(".\\", 0) == 0) {
    outPath = projDir + sep + outPath.substr(2);
  }
  // topológica
  std::vector<std::string> order;
  if (!topoSortTasks(tasks, cliOpts.buildTask, order, errOut)) {
    std::cerr << "hphlc: " << errOut << "\n";
    return 1;
  }
  // filtra por `when`
  for (auto& t : tasks) {
    if (!evalCondition(t.when, cliOpts)) {
      t.objs.clear(); // marca como "skipped" — sem objs, sem link
    }
  }
  // incremental check: se o output existe e é mais novo que tudo, skip total
  long long outMtime = fileMtime(outPath);
  long long maxSrc = fileMtime(projFile);
  for (auto& t : tasks) {
    // pula fontes de tasks que serão puladas por `when`
    if (!evalCondition(t.when, cliOpts)) continue;
    for (auto& s : t.sources) {
      long long m = fileMtime(s);
      if (m < 0) { std::cerr << "hphlc: " << hphl::messages().get("cli_project_source_not_found", {s}) << "\n"; return 1; }
      if (m > maxSrc) maxSrc = m;
    }
  }
  if (!cliOpts.buildTask.empty() && outMtime >= 0 && outMtime > maxSrc && !cliOpts.incremental) {
    std::cout << "hphlc: " << hphl::messages().get("cli_task_up_to_date", {cliOpts.buildTask}) << "\n";
    return 0;
  }
  if (cliOpts.buildTask.empty() && outMtime >= 0 && outMtime > maxSrc && !cliOpts.incremental) {
    std::cout << "hphlc: " << hphl::messages().get("cli_project_up_to_date", {projFile, outPath}) << "\n";
    return 0;
  }
  // paralelismo
  unsigned int maxJobs = (unsigned int)cliOpts.jobs;
  if (maxJobs == 0) {
    unsigned int hc = std::thread::hardware_concurrency();
    maxJobs = hc > 0 ? hc : 1;
  }
  // mapa nome→task
  std::map<std::string, size_t> idx;
  for (size_t i = 0; i < tasks.size(); i++) idx[tasks[i].name] = i;
  // compila cada task respeitando deps; ready queue
  std::map<std::string, std::thread> running;
  std::map<std::string, int> runningRc;
  std::mutex logMu;
  std::map<std::string, bool> done;
  std::map<std::string, std::string> errs;
  auto launchOne = [&](const std::string& name) {
    BuildTask& t = tasks[idx[name]];
    if (!evalCondition(t.when, cliOpts)) {
      // task pulada por condição
      std::lock_guard<std::mutex> lk(logMu);
      std::cout << "hphlc: " << hphl::messages().get("cli_task_skipped", {name, t.when}) << "\n";
      done[name] = true;
      return;
    }
    running[name] = std::thread([&, name]() {
      std::string e;
      BuildTask& taskRef = tasks[idx[name]];
      int rc = compileTask(taskRef, cliOpts, projDir + sep + "build", outPath, e);
      if (rc != 0) {
        std::lock_guard<std::mutex> lk(logMu);
        std::cerr << "hphlc: " << hphl::messages().get("cli_task_error", {name, e}) << "\n";
      }
      runningRc[name] = rc;
    });
  };
  // Kahn dinâmico: tarefas cujas deps estão todas em `done`
  auto allDepsDone = [&](const BuildTask& t) {
    for (auto& d : t.deps) if (!done.count(d)) return false;
    return true;
  };
  // join uma thread e remove
  auto joinOne = [&](std::map<std::string, std::thread>::iterator it) {
    std::string name = it->first;
    if (it->second.joinable()) it->second.join();
    int rc = runningRc.count(name) ? runningRc[name] : 1;
    if (rc != 0) {
      std::lock_guard<std::mutex> lk(logMu);
      std::cerr << "hphlc: " << hphl::messages().get("cli_task_failed", {name, std::to_string(rc)}) << "\n";
      return false;
    }
    std::lock_guard<std::mutex> lk(logMu);
    std::cout << "hphlc: " << hphl::messages().get("cli_task_ok", {name, std::to_string(tasks[idx[name]].objs.size())}) << "\n";
    done[name] = true;
    return true;
  };
  // inicial: tasks sem deps
  for (auto& n : order) {
    BuildTask& t = tasks[idx[n]];
    if (t.deps.empty() && running.size() < maxJobs) {
      launchOne(n);
    }
  }
  // loop principal
  while (!running.empty()) {
    // verifica se alguma thread terminou (joinable+join retorna rápido se já acabou)
    for (auto it = running.begin(); it != running.end(); ) {
      // poll rápido: 10ms
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      // tenta join com timeout — std::thread::join não tem timeout; usamos detach temporário?
      // truque: cria um future a partir de uma promise que a thread sinaliza
      // simplificação: usamos runningRc como flag de conclusão
      if (runningRc.count(it->first)) {
        if (!joinOne(it)) return 1;
        it = running.erase(it);
        // lança novos que ficaram ready
        for (auto& n2 : order) {
          if (running.count(n2) || done.count(n2)) continue;
          BuildTask& t2 = tasks[idx[n2]];
          if (allDepsDone(t2) && running.size() < maxJobs) {
            launchOne(n2);
          }
        }
      } else {
        ++it;
      }
    }
  }
  // coleta objs na ordem topológica e linka (entry)
  std::vector<std::string> allObjs;
  std::string entryName;
  for (auto& n : order) {
    BuildTask& t = tasks[idx[n]];
    for (auto& o : t.objs) allObjs.push_back(o);
    if (t.isEntry) entryName = n;
  }
  if (allObjs.empty()) { std::cerr << "hphlc: " << hphl::messages().get("cli_project_no_objects_generated") << "\n"; return 1; }
  if (entryName.empty()) entryName = order.back();
  // separa objs entry vs deps — o linker recebe tudo numa linha
  // (não tem archive estática ainda)
  std::string linkErr;
  std::string gcc = findGcc(cliOpts.gcc);
  std::string runtime = findRuntime();
  std::vector<std::string> allExternLibs;
  for (auto& n : order) {
    for (auto& lib : tasks[idx[n]].externLibs) {
      if (std::find(allExternLibs.begin(), allExternLibs.end(), lib) == allExternLibs.end())
        allExternLibs.push_back(lib);
    }
  }
  int lrc = linkObjects(allObjs, outPath, gcc, runtime, linkErr, allExternLibs);
  if (lrc != 0) {
    std::cerr << "hphlc: " << hphl::messages().get("cli_project_link_error", {entryName, outPath, linkErr}) << "\n";
    return lrc;
  }
  std::cout << "hphlc: " << hphl::messages().get("cli_project_built", {projFile, outPath, std::to_string(allObjs.size()), std::to_string(order.size()), std::to_string(maxJobs)}) << "\n";
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  // Early check for --language so all messages, including arg parse errors, use the requested language
  for (int i = 1; i < argc - 1; i++) {
    if (std::string(argv[i]) == "--language") {
      hphl::messages().setLocale(argv[i + 1]);
      break;
    }
  }

  Options opts;
  if (!parseArgs(argc, argv, opts)) {
    usage();
    return 1;
  }
  if (opts.showVersion) {
    std::cout << "hphlc " << HPHL_VERSION << "\n";
    return 0;
  }

  // M30 v0.89.0: inicializar loader de mensagens com locale especificado
  hphl::messages().setLocale(opts.language.empty() ? "en" : opts.language);

  // servidor LSP (M6) / DAP (M7): nunca toca no fluxo normal de compilação
  if (opts.lsp) return hphl::lspMain();
  if (opts.dap) return hphl::dapMain();
  // M17: subcomando fetch <pkg>[@<version>]
  if (!opts.fetchCmd.empty()) {
    hphl::FetchResult r = hphl::fetchPackage(opts.fetchCmd);
    if (!r.ok) {
      std::cerr << "hphlc: " << hphl::messages().get("cli_fetch_failed", {r.error}) << "\n";
      return 1;
    }
    std::cout << "hphlc: " << hphl::messages().get("cli_fetch_ok", {r.installPath, r.resolvedVersion}) << "\n";
    return 0;
  }
  // M17: subcomando install (lê package.hpkg ou hphl.pkg.toml, baixa deps, gera lockfile)
  if (opts.installCmd) {
    std::string cwd = opts.package.empty() ? "." : opts.package;
    hphl::ResolveResult rr = hphl::resolveDependencies(cwd, opts.frozenLockfile);
    if (!rr.ok) {
      std::cerr << "hphlc: " << hphl::messages().get("cli_install_failed", {rr.error}) << "\n";
      return 1;
    }
    if (!hphl::writeLockfile(cwd, rr.lockEntries)) {
      std::cerr << "hphlc: " << hphl::messages().get("cli_lockfile_fail") << "\n";
      return 1;
    }
    std::cout << "hphlc: " << hphl::messages().get("cli_install_ok", {std::to_string(rr.lockEntries.size())}) << "\n";
    return 0;
  }
  // M17: subcomando publish [dir] [--out <registry_dir>]
  if (opts.publishCmd) {
    std::string dir = opts.publishDir.empty() ? "." : opts.publishDir;
    std::string outReg = opts.publishOut;
    if (outReg.empty()) {
      const char* envReg = std::getenv("HPHL_REGISTRY");
      if (envReg && *envReg) outReg = envReg;
      else outReg = "registry";
    }
    if (outReg.rfind("file://", 0) == 0) {
      outReg = outReg.substr(7);
#ifdef _WIN32
      if (outReg.size() >= 3 && outReg[0] == '/' && std::isalpha((unsigned char)outReg[1]) && outReg[2] == ':') {
        outReg = outReg.substr(1);
      }
#endif
    }
    hphl::PublishResult pr = hphl::publishPackage(dir, outReg);
    if (!pr.ok) {
      std::cerr << "hphlc: " << hphl::messages().get("cli_publish_failed", {pr.error}) << "\n";
      return 1;
    }
    std::cout << "hphlc: " << hphl::messages().get("cli_published", {pr.name, pr.version, pr.tarballPath}) << "\n";
    std::cout << "  sha256: " << pr.sha256 << "\n";
    std::cout << "  index:  " << pr.indexPath << "\n";
    return 0;
  }
  if (!opts.project.empty()) {
    // M26 14.5: build system avançado — tasks, deps, parallel, conditional
    return runBuildProject(opts);
  }
  // M14.2: package manager (--package <dir>)
  if (!opts.package.empty()) {
    std::string sep2 = hphl::pathSep();
    hphl::PackageManifest manifest = hphl::loadManifest(opts.package);
    if (!manifest.valid) {
      std::cerr << "hphlc: " << manifest.error << "\n";
      return 1;
    }
    std::string pkgName = manifest.name.empty() ? "pkg" : manifest.name;
    std::string pkgVersion = manifest.version.empty() ? "0.0.0" : manifest.version;
    const auto& pkgSources = manifest.sources;
    const auto& pkgDeps = manifest.dependencies;

    std::cout << "hphlc: " << hphl::messages().get("cli_package_info", {pkgName, pkgVersion}) << "\n";

    // Resolver dependências (caminhos locais ou nomes em ~/.hphl/packages/)
    std::vector<std::string> allSources;
    std::vector<std::string> depDirs;
    std::vector<hphl::LockEntry> pkgLocks = hphl::readLockfile(opts.package);

    for (const auto& [rawDep, rawSpec] : pkgDeps) {
      std::string dep = rawDep;
      if (dep.empty()) continue;
      // Trata spec path:
      if (rawSpec.rfind("path:", 0) == 0) {
        std::string relPath = rawSpec.substr(5);
        std::string fullPath = opts.package + sep2 + relPath;
        depDirs.push_back(fullPath);
        hphl::PackageManifest sub = hphl::loadManifest(fullPath);
        for (const auto& ds : sub.sources) {
          if (!ds.empty()) allSources.push_back(fullPath + sep2 + ds);
        }
        continue;
      }
      // um pacote dependente é válido se contém hphl.pkg.toml ou package.hpkg
      auto findDepDir = [&](const std::string& base) -> std::string {
        std::string p = base + sep2 + dep;
        if (fileExists(p + sep2 + "hphl.pkg.toml") || fileExists(p + sep2 + "package.hpkg")) return p;
        for (const auto& le : pkgLocks) {
          if (le.name == dep && !le.version.empty()) {
            std::string pv = base + sep2 + dep + "@" + le.version;
            if (fileExists(pv + sep2 + "hphl.pkg.toml") || fileExists(pv + sep2 + "package.hpkg")) return pv;
          }
        }
        if (!rawSpec.empty() && rawSpec != "*") {
          std::string pv = base + sep2 + dep + "@" + rawSpec;
          if (fileExists(pv + sep2 + "hphl.pkg.toml") || fileExists(pv + sep2 + "package.hpkg")) return pv;
        }
        return "";
      };
      // busca: relativo ao pkgdir → relativo ao cwd → ~/.hphl/packages/<dep>/
      std::string depPath = findDepDir(opts.package);
      std::string tried = depPath;
      if (depPath.empty()) {
        depPath = findDepDir(".");
        tried += (tried.empty() ? "" : "; ") + opts.package + sep2 + dep;
      }
      if (depPath.empty()) {
        const char* homeEnv = getenv("USERPROFILE");
        if (!homeEnv) homeEnv = getenv("HOME");
        std::string home = homeEnv ? homeEnv : "";
        if (!home.empty()) {
          depPath = findDepDir(home + sep2 + ".hphl" + sep2 + "packages");
          tried += "; " + home + sep2 + ".hphl" + sep2 + "packages" + sep2 + dep;
        }
      }
      if (depPath.empty()) {
        std::cerr << "hphlc: " << hphl::messages().get("cli_dep_not_found", {dep}) << " (" << tried << ")\n";
        return 1;
      }
      depDirs.push_back(depPath);
      hphl::PackageManifest depManifest = hphl::loadManifest(depPath);
      for (const auto& ds : depManifest.sources) {
        if (!ds.empty()) allSources.push_back(depPath + sep2 + ds);
      }
    }

    // Fontes do pacote principal (o primeiro com Main é o entry)
    std::string entrySource;
    if (!manifest.entry.empty()) {
      entrySource = opts.package + sep2 + manifest.entry;
    }
    for (const auto& s : pkgSources) {
      if (!s.empty()) {
        std::string full = opts.package + sep2 + s;
        if (entrySource.empty()) entrySource = full;
        allSources.push_back(full);
      }
    }
    if (allSources.empty() || entrySource.empty()) {
      std::cerr << "hphlc: " << hphl::messages().get("cli_no_sources") << "\n";
      return 1;
    }
    if (opts.backend.empty() && !manifest.backend.empty() && manifest.backend != "auto") {
      opts.backend = manifest.backend;
    }

    // Compilar: o entry é o primeiro fonte do PACOTE PRINCIPAL
    opts.input = entrySource;
    // output padrão: <pkgdir>/<nome>.exe (em vez do cwd)
    if (opts.output.empty()) opts.output = opts.package + sep2 + pkgName + ".exe";
    opts.depDirs = depDirs;
    opts.extraSources = allSources;
    std::cout << "hphlc: sources: ";
    for (auto& s : allSources) std::cout << s << " ";
    std::cout << "\n";
  }
  if (opts.help || opts.input.empty()) {
    usage();
    return opts.input.empty() ? 1 : 0;
  }

  std::string src = readFile(opts.input);
  if (src.empty()) {
    std::cerr << "hphlc: " << hphl::messages().get("cli_cannot_read_file", {opts.input}) << "\n";
    return 1;
  }

  std::string sep = hphl::pathSep();
#ifdef _WIN32
  std::string outBase = opts.output.empty() ? baseOf(opts.input) + ".exe" : opts.output;
#else
  std::string outBase = opts.output.empty() ? baseOf(opts.input) : opts.output;
#endif
  std::string asmPath = (opts.noLink || opts.keepAsm)
                            ? baseOf(opts.input) + ".s"
                            : opts.input + ".tmp.s";
  // asm gerado na mesma pasta do executável (padrão de driver simples)
  if (!opts.output.empty()) {
    std::string ext = (opts.noLink || opts.keepAsm) ? ".s" : ".tmp.s";
    asmPath = dirOf(opts.output) + sep + baseOf(opts.input) + ext;
  }

  /* M26 14.1: incremental cache — hash-based, stored in build/cache/ */
  bool doIncrementalCache = opts.incremental;
  std::string cacheDir;
  std::string cacheFile;
  std::string srcHash;
  if (doIncrementalCache) {
    cacheDir = "build" + sep + "cache";
    srcHash = hphl::sha256Hex(src);
    cacheFile = cacheDir + sep + baseOf(opts.input) + ".json";
    /* create cache dir if needed */
#ifdef _WIN32
    CreateDirectoryA(cacheDir.c_str(), NULL);
#else
    mkdir(cacheDir.c_str(), 0755);
#endif
    /* check if cache is valid (hash matches + output exists) */
    long long outMtime = fileMtime(outBase);
    if (outMtime >= 0) {
      std::string cachedHash;
      std::ifstream cf(cacheFile, std::ios::binary);
      if (cf) {
        std::getline(cf, cachedHash);
        cf.close();
      }
      if (!cachedHash.empty() && cachedHash == srcHash) {
        std::cout << "hphlc: incremental: cache hit (hash " << srcHash.substr(0, 12) << "...), skipping compilation\n";
        if (opts.run) {
#ifdef _WIN32
          SetConsoleOutputCP(CP_UTF8);
#endif
          std::cout << "hphlc: " << hphl::messages().get("cli_executing", {outBase}) << "\n";
          int erc = run("\"" + outBase + "\"");
          return erc;
        }
        return 0;
      }
    }
  }

  try {
    // carrega o arquivo principal + imports transitivos num único Program
    hphl::Program prog;
    hphl::ImportLoader opts_loader(opts.input);
    for (const auto& dd : opts.depDirs) {
      opts_loader.addSearchDir(dd);
    }
    opts_loader.load(opts.input, prog);

    hphl::Semantic semantic(&prog, opts.input);
    semantic.analyze();
    // M4 Fase 2: folding + dead branch + DCE de locais no HIR (todos os
    // backends se beneficiam; --dump-hir mostra o HIR já otimizado)
    semantic.optimizeHir();

    if (opts.dumpHir) {
      std::cout << hphl::hirToString(semantic.hir()) << "\n";
      return 0;
    }

    // M10.3: MIR mínimo — blocos básicos + three-address (base p/ SSA)
    if (opts.dumpMir) {
      hphl::MirProgram mir = hphl::loweringToMir(semantic.hir());
      // M10.4: --ssa constrói a forma SSA (phis nas fronteiras de dominância)
      if (opts.ssa) mir = hphl::mirToSSA(mir);
      std::cout << hphl::mirToString(mir) << "\n";
      return 0;
    }

    // M10.5: --target só se aplica ao backend llvm
    if (!opts.target.empty()) {
      if (opts.backend.empty() || opts.backend == "x64") {
        opts.backend = "llvm";   // auto-infer LLVM for cross-targets
      } else if (opts.backend != "llvm") {
        std::cerr << "hphlc: --target exige --backend llvm (o backend x64 "
                     "emite assembly nativo do host)\n";
        return 1;
      }
    }

    // valida o ponto de entrada antes do codegen (senão o linker falha com
    // "undefined reference to WinMain" de forma confusa para arquivos-módulo)
    bool hasEntry = false;
    for (auto& d : prog.decls) {
      if (d->kind == hphl::DeclKind::Function &&
          static_cast<hphl::FunctionDecl*>(d.get())->isEntryPoint)
        hasEntry = true;
    }
      if (!hasEntry)
        throw hphl::CompileError{
            0, 0,
            opts.input + ": " + hphl::messages().get("semantic_error_prefix") + ": " +
                hphl::messages().get("cli_no_entry_point") + "\n"};

      if (opts.backend == "auto") {
        if (opts.debug) opts.backend.clear();
        else { opts.backend = "llvm"; if (opts.optLevel==0) opts.optLevel=2; }
      }
      if (opts.backend == "ir") {
        std::string irPath = dirOf(outBase) + sep + baseOf(opts.input) + ".ll";
        hphl::Irgen irgen(semantic, opts.input, opts.debug);
        std::string irText = irgen.generate();
        if (!writeFile(irPath, irText)) {
          std::cerr << "hphlc: " << hphl::messages().get("cli_cannot_write_file", {irPath}) << "\n";
          return 1;
        }
        std::cout << "hphlc: " << hphl::messages().get("cli_llvm_ir_generated", {irPath}) << "\n";
        return 0;
      }

      if (opts.backend == "llvm") {
        // Backend LLVM API real (Milestone 3): o IR textual do Irgen é
        // compilado via libLLVM (parse + verify + object file) — sem clang.
        hphl::Irgen irgen(semantic, opts.input, opts.debug);
        // M14.3: wasm32 não suporta o modelo CONTEXT (returns_twice/naked
        // asm) — usa EH por flag de propagação com arestas de CFG estáticas
        if (opts.target.rfind("wasm", 0) == 0) irgen.setWasmEh(true);
        std::string irText = irgen.generate();
        // M10.5: cross-compilation gera object file nomeado e não linka
        if (!opts.target.empty()) {
          bool wasmTarget = opts.target.rfind("wasm32", 0) == 0 ||
                            opts.target.rfind("wasm64", 0) == 0;
          std::string objPath = dirOf(outBase) + sep + baseOf(outBase) +
                                (wasmTarget ? ".tmp.o" : ".cross.o");
          std::string lerr;
          if (!hphl::llvmEmitObjectFile(irText, objPath, opts.optLevel, lerr,
                                        opts.target)) {
            std::cerr << "hphlc: backend LLVM (--target " << opts.target
                      << "): " << lerr << "\n";
            return 1;
          }
          if (!wasmTarget) {
            std::cout << "hphlc: object file '" << opts.target
                      << "' gerado em '" << objPath << "' (-O" << opts.optLevel
                      << "; link cruzado fora de escopo no M10.5)\n";
            return 0;
          }
          // WASM: linka com wasm-ld → módulo .wasm standalone executável no
          // Node/navegador (imports de runtime ficam pendentes p/ o host)
          std::string wasmPath = dirOf(outBase) + sep + baseOf(outBase) + ".wasm";
          std::string cmd = "\"" + findWasmLd() + "\" \"" + objPath +
                            "\" -o \"" + wasmPath +
                            "\" --no-entry --allow-undefined --export-table --export-if-defined=main";
          int rc = run(cmd);
          if (!getenv("HPHL_KEEP_OBJ")) std::remove(objPath.c_str());
          if (rc != 0) {
            std::cerr << "hphlc: " << hphl::messages().get("cli_wasm_link_failed", {std::to_string(rc)}) << "\n";
            return rc;
          }
          std::cout << "hphlc: " << hphl::messages().get("cli_wasm_generated", {wasmPath, std::to_string(opts.optLevel)}) << "\n";
          return 0;
        }
        std::string objPath = dirOf(outBase) + sep + baseOf(opts.input) + ".tmp.o";
        std::string lerr;
        if (!hphl::llvmEmitObjectFile(irText, objPath, opts.optLevel, lerr)) {
          std::cerr << "hphlc: backend LLVM: " << lerr << "\n";
          return 1;
        }
        std::cout << "hphlc: " << hphl::messages().get("cli_llvm_obj_generated", {objPath, std::to_string(opts.optLevel)}) << "\n";

        if (opts.noLink) return 0;

        ensureOutputFileUnlocked(outBase);
        std::string gcc = findGcc(opts.gcc);
        std::string runtime = findRuntime();
        std::string cmd = "\"" + gcc + "\" -O2 -o \"" + outBase + "\" \"" + objPath + "\"";
        std::string runtimeObj = ensureRuntimeObj(gcc, runtime);
        if (!runtimeObj.empty()) cmd += " \"" + runtimeObj + "\"";
        else if (!runtime.empty()) cmd += " \"" + runtime + "\"";
        cmd += " -lws2_32 -lwsock32 -lbcrypt -lz";
        cmd += nativeLibDirFlags(semantic.externLibs());
        for (auto& lib : semantic.externLibs()) cmd += " -l" + lib;
        int rc = run(cmd);
        if (rc != 0) {
          std::cerr << "hphlc: " << hphl::messages().get("cli_link_failed", {std::to_string(rc)}) << "\n";
          std::remove(objPath.c_str());
          return rc;
        }
        std::cout << "hphlc: " << hphl::messages().get("cli_exe_generated", {outBase}) << "\n";

        if (opts.run) {
#ifdef _WIN32
          SetConsoleOutputCP(CP_UTF8);
#endif
          std::cout << "hphlc: " << hphl::messages().get("cli_executing", {outBase}) << "\n";
          int erc = run("\"" + outBase + "\"");
          std::remove(objPath.c_str());
          return erc;
        }

        std::remove(objPath.c_str());
        return 0;
      }

     hphl::Codegen codegen(semantic, opts.input, opts.debug);
    std::string asmText = codegen.generate();

    if (!writeFile(asmPath, asmText)) {
      std::cerr << "hphlc: " << hphl::messages().get("cli_cannot_write_file", {asmPath}) << "\n";
      return 1;
    }
    std::cout << "hphlc: " << hphl::messages().get("cli_assembly_generated", {asmPath}) << "\n";

    if (opts.noLink) return 0;

    ensureOutputFileUnlocked(outBase);
    std::string gcc = findGcc(opts.gcc);
    std::string runtime = findRuntime();

    std::string cmd = "\"" + gcc + "\" -O2 -Wl,--stack,8388608 -o \"" + outBase + "\" \"" + asmPath + "\"";
    std::string runtimeObj = ensureRuntimeObj(gcc, runtime);
    if (!runtimeObj.empty()) cmd += " \"" + runtimeObj + "\"";
    else if (!runtime.empty()) cmd += " \"" + runtime + "\"";
    cmd += " -lws2_32 -lwsock32 -lbcrypt -lz";
    cmd += nativeLibDirFlags(semantic.externLibs());
    for (auto& lib : semantic.externLibs()) cmd += " -l" + lib;
    int rc = run(cmd);
    if (rc != 0) {
      std::cerr << "hphlc: " << hphl::messages().get("cli_link_failed", {std::to_string(rc)}) << "\n";
      if (!opts.keepAsm) std::remove(asmPath.c_str());
      return rc;
    }
    std::cout << "hphlc: " << hphl::messages().get("cli_exe_generated", {outBase}) << "\n";

    if (opts.run) {
      // console UTF-8 para a saída do programa (printf em UTF-8 no runtime.c)
#ifdef _WIN32
      SetConsoleOutputCP(CP_UTF8);
#endif
      std::cout << "hphlc: " << hphl::messages().get("cli_executing", {outBase}) << "\n";
      int erc = run("\"" + outBase + "\"");
      if (!opts.keepAsm) std::remove(asmPath.c_str());
      if (doIncrementalCache && !cacheFile.empty()) {
        std::ofstream cf(cacheFile, std::ios::binary);
        if (cf) { cf << srcHash << "\n"; cf.close(); }
      }
      return erc;
    }

    if (!opts.keepAsm) std::remove(asmPath.c_str());
    /* M26 14.1: update incremental cache with new source hash */
    if (doIncrementalCache && !cacheFile.empty()) {
      std::ofstream cf(cacheFile, std::ios::binary);
      if (cf) { cf << srcHash << "\n"; cf.close(); }
    }
    return 0;
  } catch (const hphl::CompileError& e) {
    std::cerr << formatCompileError(e, src, opts.input) << "\n";
    if (!opts.keepAsm) std::remove(asmPath.c_str());
    return 1;
  }
}
