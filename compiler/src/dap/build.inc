// ---------------------------------------------------------------------------
// Infra de compilação (espelha o main.cpp — M7 v1; refactor do driver fica
// para depois do milestone)
// ---------------------------------------------------------------------------
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

// FFI v2: diretórios -L extras p/ libs nativas (espelha main.cpp).
// HPHL_LIBDIR (dirs ';'/':') + $VULKAN_SDK/Lib p/ 'vulkan-1'.
static std::string dapNativeLibDirFlags(const std::vector<std::string>& libs) {
  std::string flags;
  auto addDir = [&](const std::string& d) {
    if (d.empty()) return;
    // sem trailing '\'/'/': '...tools\"' quebraria o parsing MSVCRT (\" = quote literal)
    size_t n = d.size();
    while (n > 1 && (d[n - 1] == '\\' || d[n - 1] == '/')) n--;
    flags += " -L\"" + d.substr(0, n) + "\"";
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

std::string baseOf(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  std::string name = pos == std::string::npos ? path : path.substr(pos + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

std::string exeDir() {
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n > 0) {
    std::string p(buf, n);
    size_t pos = p.find_last_of("/\\");
    if (pos != std::string::npos) return p.substr(0, pos);
  }
  return "";
}

std::string pathSep() {
#ifdef _WIN32
  return "\\";
#else
  return "/";
#endif
}

std::string findGcc(const std::string& explicitPath) {
  if (!explicitPath.empty() && fileExists(explicitPath)) return explicitPath;
  const char* candidates[] = {
      "C:\\msys64\\ucrt64\\bin\\gcc.exe",
      "C:\\msys64\\mingw64\\bin\\gcc.exe",
      nullptr,
  };
  for (int i = 0; candidates[i]; i++)
    if (fileExists(candidates[i])) return candidates[i];
  return "gcc";
}

std::string findRuntime() {
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
    dirs.push_back(std::string(home) + pathSep() + "compiler");
  }
  dirs.push_back(".");
  dirs.push_back("..");
  for (const auto& dir : dirs) {
    std::string p1 = dir + pathSep() + "src" + pathSep() + "runtime" + pathSep() + "main.c";
    std::string p2 = dir + pathSep() + "compiler" + pathSep() + "src" + pathSep() + "runtime" + pathSep() + "main.c";
    std::string p3 = dir + pathSep() + "runtime.c";
    std::string p4 = dir + pathSep() + "src" + pathSep() + "runtime.c";
    std::string p5 = dir + pathSep() + "compiler" + pathSep() + "src" + pathSep() + "runtime.c";
    std::string p6 = dir + pathSep() + "runtime" + pathSep() + "main.c";
    if (fileExists(p1)) return p1;
    if (fileExists(p2)) return p2;
    if (fileExists(p3)) return p3;
    if (fileExists(p4)) return p4;
    if (fileExists(p5)) return p5;
    if (fileExists(p6)) return p6;
  }
  return "";
}

int run(const std::string& cmd) {
#ifdef _WIN32
  std::string full = "cmd /S /C \"" + cmd + "\"";
  int rc = std::system(full.c_str());
  if (rc == -1) return -1;
  return rc;
#else
  int rc = std::system(cmd.c_str());
  if (rc == -1) return -1;
  return WEXITSTATUS(rc);
#endif
}

int runCapture(const std::string& cmd, std::string& output) {
  output.clear();
#ifdef _WIN32
  std::string full = "cmd /S /C \"" + cmd + " 2>&1\"";
  FILE* p = _popen(full.c_str(), "r");
  if (!p) return -1;
  char buf[256];
  while (fgets(buf, sizeof(buf), p)) output += buf;
  return _pclose(p);
#else
  std::string full = cmd + " 2>&1";
  FILE* p = popen(full.c_str(), "r");
  if (!p) return -1;
  char buf[256];
  while (fgets(buf, sizeof(buf), p)) output += buf;
  int rc = pclose(p);
  return WEXITSTATUS(rc);
#endif
}

std::string ensureRuntimeObj(const std::string& gcc, const std::string& runtime) {
  if (runtime.empty()) return "";
  std::string obj = runtime.substr(0, runtime.size() - 2) + ".o";
  long long mObj = fileMtime(obj);
  std::string runtimeDir = dirOf(runtime);
  std::vector<std::string> runtimeDeps = {
      runtimeDir + pathSep() + "runtime_api.h",
      runtimeDir + pathSep() + "runtime.h",
      runtimeDir + pathSep() + "core" + pathSep() + "core.c",
      runtimeDir + pathSep() + "strings" + pathSep() + "strings.c",
      runtimeDir + pathSep() + "concurrency" + pathSep() + "concurrency.c",
      runtimeDir + pathSep() + "collections" + pathSep() + "collections.c",
      runtimeDir + pathSep() + "debug" + pathSep() + "debug.c",
      runtimeDir + pathSep() + "alloc" + pathSep() + "alloc.c",
      runtimeDir + pathSep() + "gc" + pathSep() + "gc.c",
      runtimeDir + pathSep() + "img" + pathSep() + "img.c",
      runtimeDir + pathSep() + "net" + pathSep() + "socket.c",
      runtimeDir + pathSep() + "mem" + pathSep() + "mem.c",
  };
  bool runtimeChanged = mObj < 0 || fileMtime(runtime) > mObj || fileMtime(gcc) > mObj;
  for (const auto& dep : runtimeDeps) runtimeChanged = runtimeChanged || fileMtime(dep) > mObj;
  if (runtimeChanged) {
    std::string cmd = "\"" + gcc + "\" -O2 -c \"" + runtime + "\" -o \"" + obj + "\"";
    if (run(cmd) != 0) return "";
    return obj;
  }
  return obj;
}

// enum simples exportado ao adapter (F2.2): nome canônico + variantes com valor
struct EnumDbg {
  std::string name;
  std::vector<std::pair<std::string, long long>> entries;
};

// compila `input` com --debug no backend x64; o exe vai para `outExe`
bool compileForDebug(const std::string& input, const std::string& outExe,
                     std::string& err,
                     std::vector<EnumDbg>* enumsOut = nullptr) {
  try {
    Program prog;
    ImportLoader loader(input);
    loader.load(input, prog);

    Semantic semantic(&prog, input);
    semantic.analyze();
    semantic.optimizeHir();

    bool hasEntry = false;
    for (auto& d : prog.decls) {
      if (d->kind == DeclKind::Function &&
          static_cast<FunctionDecl*>(d.get())->isEntryPoint) {
        hasEntry = true;
        break;
      }
    }
    if (!hasEntry) {
      err = input + ": erro: nenhum ponto de entrada 'Main' encontrado\n";
      return false;
    }

    std::string sep = pathSep();
    std::string asmPath = dirOf(outExe) + sep + baseOf(input) + ".tmp.s";

    if (enumsOut) {
      enumsOut->clear();
      for (const auto& kv : semantic.enums()) {
        const EnumDecl* en = kv.second;
        if (en->anyPayload) continue; // enum rico: célula [tag][p0..], fora do v1
        EnumDbg e;
        e.name = kv.first;
        for (const EnumEntry& entry : en->entries)
          e.entries.emplace_back(entry.name, entry.value);
        enumsOut->push_back(std::move(e));
      }
    }

    Codegen codegen(semantic, input, /*debug=*/true);
    std::string asmText = codegen.generate();
    if (!writeFile(asmPath, asmText)) {
      err = "não foi possível gravar '" + asmPath + "'\n";
      return false;
    }

#ifdef _WIN32
    // Se o executável anterior estiver em execução (bloqueado), encerra o processo órfão
    std::string baseExe = baseOf(outExe) + ".exe";
    std::string killCmd = "taskkill /F /IM \"" + baseExe + "\" >nul 2>&1";
    std::system(killCmd.c_str());
#endif

    std::string gcc = findGcc("");
    std::string runtime = findRuntime();
    std::string cmd = "\"" + gcc + "\" -O2 -Wl,--stack,8388608 -o \"" + outExe + "\" \"" + asmPath + "\"";
    std::string runtimeObj = ensureRuntimeObj(gcc, runtime);
    if (!runtimeObj.empty()) cmd += " \"" + runtimeObj + "\"";
    else if (!runtime.empty()) cmd += " \"" + runtime + "\"";
    cmd += " -lws2_32 -lwsock32 -lbcrypt -lz";
    cmd += dapNativeLibDirFlags(semantic.externLibs());
    for (auto& lib : semantic.externLibs()) cmd += " -l" + lib;
    std::string gccOutput;
    int rc = runCapture(cmd, gccOutput);
    std::remove(asmPath.c_str());
    if (rc != 0) {
      err = "falha ao montar/linkar (gcc retornou " + std::to_string(rc) + "):\n" + gccOutput;
      return false;
    }
    return true;
  } catch (const CompileError& e) {
    err = e.message;
    return false;
  }
}

