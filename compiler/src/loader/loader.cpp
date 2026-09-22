#include "loader.h"
#include "pkgfetch.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "messages/message_loader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace hphl {

namespace {

std::string readFileRaw(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool fileExists(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return in.good();
}

std::string dirOf(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return ".";
  return path.substr(0, pos);
}

} // namespace

bool ImportLoader::isAbsPath(const std::string& p) {
  return p.find(':') != std::string::npos ||
         (!p.empty() && (p[0] == '/' || p[0] == '\\'));
}

std::string ImportLoader::resolveImport(const ImportDecl& imp, const std::string& from) {
  if (imp.isPath) {
    std::string full = isAbsPath(imp.path)
                           ? imp.path
                           : dirOf(from) + pathSep() + imp.path;
    if (fileExists(full)) return full;
    throw CompileError{0, 0,
                       "hphlc: " + hphl::messages().get("loader_import_file_not_found", {imp.path, dirOf(from)})};
  }
  // M_RV1 2.17: módulos da biblioteca padrão (ex.: std, std.collections, std.io, std.net)
  if (isStdModule(imp.name)) {
    return "<std:" + imp.name + ">";
  }
  // nomes candidatos: 'Mod' → Mod.hphl; 'A.B' → A\B.hphl também
  std::vector<std::string> names;
  std::string sub = imp.name;
  for (auto& c : sub)
    if (c == '.') c = '\\';
  names.push_back(imp.name + ".hphl");
  names.push_back(sub + ".hphl");

  std::vector<std::string> dirs;
  std::string d1 = dirOf(from);
  dirs.push_back(d1);
  std::string d2 = dirOf(mainFile_);
  if (canonicalPath(d2) != canonicalPath(d1)) dirs.push_back(d2);
  for (const auto& sd : searchDirs_) {
    if (std::find(dirs.begin(), dirs.end(), sd) == dirs.end()) {
      dirs.push_back(sd);
    }
  }

  for (const auto& dir : dirs) {
    for (const auto& n : names) {
      std::string p = dir + pathSep() + n;
      if (fileExists(p)) return p;
    }
    // Suporte a pacotes com lib.hphl ou entry especificado no manifesto
    std::string toml = dir + pathSep() + "hphl.pkg.toml";
    std::string hpkg = dir + pathSep() + "package.hpkg";
    if (fileExists(toml) || fileExists(hpkg)) {
      PackageManifest m = loadManifest(dir);
      if (m.valid && m.name == imp.name) {
        if (!m.entry.empty()) {
          std::string ep = dir + pathSep() + m.entry;
          if (fileExists(ep)) return ep;
        }
        std::string libp = dir + pathSep() + "lib.hphl";
        if (fileExists(libp)) return libp;
      }
    }
  }
  std::string searched;
  for (const auto& dir : dirs) searched += " '" + dir + "'";
  throw CompileError{0, 0,
                     "hphlc: " + hphl::messages().get("loader_module_not_found", {imp.name, searched})};
}

void ImportLoader::loadRec(const std::string& path, Program& out,
                           std::vector<std::string> chain) {
  std::string key = canonicalPath(path);
  if (std::find(chain.begin(), chain.end(), key) != chain.end())
    throw CompileError{0, 0, "hphlc: " + hphl::messages().get("loader_cyclic_import", {path})};
  if (loaded_.count(key)) return;
  chain.push_back(key);

  // M_RV1 2.17: módulo virtual da stdlib
  if (path.rfind("<std:", 0) == 0 && path.back() == '>') {
    std::string modName = path.substr(5, path.size() - 6);
    bool seen = false;
    for (auto& m : out.modules) {
      if (m.first == modName) { seen = true; break; }
    }
    if (!seen) out.modules.push_back({modName, "<std>"});
    fileModule_[key] = modName;
    loaded_.insert(key);
    loadedPaths_.push_back(path);
    return;
  }

  std::string src;
  bool haveOverride = false;
  if (override_) {
    auto ov = override_(path);
    if (ov) {
      src = *ov;
      haveOverride = true;
    }
  }
  if (!haveOverride) src = readFileRaw(path);
  if (src.empty() && !haveOverride)
    throw CompileError{0, 0, "hphlc: " + hphl::messages().get("cli_cannot_read_file", {path})};

  Lexer lexer(src, path);
  std::vector<Token> tokens = lexer.tokenize();
  Parser parser(std::move(tokens), path);
  std::unique_ptr<Program> prog = parser.parseProgram();

  // um arquivo = uma unidade de módulo; registra o nome declarado
  std::string modName = prog->moduleName;
  if (modName == "main" && canonicalPath(path) != canonicalPath(mainFile_)) {
    throw CompileError{0, 0,
                       "hphlc: " + hphl::messages().get("loader_imported_file_no_module", {path})};
  }
  if (modName != "main") {
    for (auto& m : out.modules) {
      if (m.first == modName) {
        throw CompileError{0, 0,
                           "hphlc: " + hphl::messages().get("loader_module_duplicate_files", {modName, m.second, path})};
      }
    }
    out.modules.push_back({modName, path});
  }
  fileModule_[key] = modName;

  // dependências declaradas (`module X depends on A, B;`) acompanham o módulo
  for (auto& dep : prog->moduleDeps) {
    bool seen = false;
    for (auto& e : out.moduleDeps)
      if (e.first == dep.first) { seen = true; break; }
    if (!seen) out.moduleDeps.push_back(dep);
  }

  // carimba origem em todas as declarações (visibilidade/qualificação)
  std::function<void(const std::vector<std::unique_ptr<Decl>>&, const std::string&)> stampModule =
      [&](const std::vector<std::unique_ptr<Decl>>& decls, const std::string& curMod) {
        for (auto& d : decls) {
          if (d->kind == DeclKind::Module) {
            auto* mod = static_cast<ModuleDecl*>(d.get());
            stampModule(mod->decls, mod->name);
          } else {
            d->moduleName = curMod;
            d->filePath = path;
          }
        }
      };
  stampModule(prog->decls, modName);

  // dependências primeiro (imports transitivos), depois as declarações próprias
  for (auto& d : prog->decls) {
    if (d->kind == DeclKind::Import) {
      auto* imp = static_cast<ImportDecl*>(d.get());
      std::string rp = resolveImport(*imp, path);
      loadRec(rp, out, chain);
      if (!imp->alias.empty()) {
        auto it = fileModule_.find(canonicalPath(rp));
        if (it != fileModule_.end())
          out.aliases.push_back({imp->alias, it->second});
      }
    }
  }
  for (auto& d : prog->decls) {
    if (d->kind != DeclKind::Import)
      out.decls.push_back(std::move(d));
  }
  loaded_.insert(key);
  loadedPaths_.push_back(path);
}

void ImportLoader::load(const std::string& path, Program& out) {
  loadRec(path, out, {});
}

} // namespace hphl