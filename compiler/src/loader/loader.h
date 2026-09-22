#pragma once
#include "ast/ast.h"
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hphl {

// separador de caminho por plataforma
inline std::string pathSep() {
#ifdef _WIN32
  return "\\";
#else
  return "/";
#endif
}

// chave canônica de arquivo (case-insensitive no Windows; normaliza separadores e ..)
inline std::string canonicalPath(const std::string& p) {
  std::string r;
  try {
    r = std::filesystem::path(p).lexically_normal().string();
  } catch (...) {
    r = p;
  }
#ifdef _WIN32
  for (auto& c : r) {
    if (c == '/') c = '\\';
    c = (char)std::tolower((unsigned char)c);
  }
#else
  for (auto& c : r)
    if (c == '\\') c = '/';
#endif
  return r;
}

// M_RV1 2.17: verifica se o módulo é um módulo virtual da biblioteca padrão
inline bool isStdModule(const std::string& name) {
  return name == "std" || (name.rfind("std.", 0) == 0);
}

// Carrega o arquivo principal e todos os `import` transitivos num único
// Program (dependências primeiro). `readOverride`, se fornecido, é consultado
// antes do disco (sobreposição in-memory — usada pelo servidor LSP).
// Resolução: import Nome; → <dir>/Nome.hphl (ou <dir>/A/B.hphl p/ 'A.B');
// import "cam.hphl"; → caminho relativo ao arquivo que importa.
// Diretórios de busca: o do arquivo que importa + o do arquivo principal.
class ImportLoader {
public:
  using ReadOverride = std::function<std::optional<std::string>(const std::string&)>;

  ImportLoader(const std::string& mainFile, ReadOverride override = nullptr)
      : mainFile_(mainFile), override_(std::move(override)) {}

  void load(const std::string& path, Program& out);

  void addSearchDir(const std::string& dir) { searchDirs_.push_back(dir); }

  // arquivos efetivamente carregados (caminhos originais, na ordem de carga)
  const std::vector<std::string>& loadedPaths() const { return loadedPaths_; }

private:
  std::string mainFile_;
  ReadOverride override_;
  std::vector<std::string> searchDirs_;
  std::set<std::string> loaded_;         // canônicos já carregados
  std::vector<std::string> loadedPaths_; // ordem de carga
  std::map<std::string, std::string> fileModule_; // caminho canônico → módulo

  static bool isAbsPath(const std::string& p);
  std::string resolveImport(const ImportDecl& imp, const std::string& from);
  void loadRec(const std::string& path, Program& out, std::vector<std::string> chain);
};

} // namespace hphl