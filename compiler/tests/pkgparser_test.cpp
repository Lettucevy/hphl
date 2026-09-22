// ============================================================================
// pkgparser_test.cpp — smoke test do parser TOML hphl.pkg.toml
// Compilado junto com o hphlc para validar que o parser funciona.
// ============================================================================

#include "loader/pkgparser.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace hphl {
namespace {

// Cria arquivo temporario com conteudo, retorna path
std::string writeTemp(const std::string& name, const std::string& content) {
  std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") +
                     "/" + name;
  std::ofstream f(path, std::ios::binary);
  f << content;
  return path;
}

#define EXPECT(cond, msg) do { \
  if (!(cond)) { std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); failed++; } \
  else { passed++; } \
} while (0)

int failed = 0;
int passed = 0;

void testSimple() {
  std::string path = writeTemp("hpkg_test1.toml",
    "# comentario\n"
    "name = \"demo\"\n"
    "version = \"1.0.0\"\n"
    "release = true\n"
    "deps = [\"a\", \"b\", \"c\"]\n"
    "[package]\n"
    "entry = \"main.hphl\"\n");
  PkgDocument doc;
  std::string err;
  bool ok = loadPkgToml(path, doc, err);
  EXPECT(ok, "loadPkgToml success");
  EXPECT(err.empty(), "no error");
  EXPECT(doc.str("name") == "demo", "name == demo");
  {
    const PkgValue* v = doc.get("version");
    EXPECT(v && v->kind == PkgValue::Kind::String && v->str == "1.0.0",
           "version == 1.0.0 (got kind=" + std::to_string(v ? (int)v->kind : -1) +
           " str='" + (v ? v->str : "(null)") + "')");
  }
  const PkgValue* rel = doc.get("release");
  EXPECT(rel && rel->kind == PkgValue::Kind::Bool && rel->boolean, "release == true");
  auto deps = doc.list("deps");
  EXPECT(deps.size() == 3 && deps[0] == "a" && deps[2] == "c", "deps[0..2]");
  const PkgSection* sec = doc.section("package");
  EXPECT(sec != nullptr, "section package exists");
  const PkgValue* ev = sec ? sec->get("entry") : nullptr;
  EXPECT(ev && ev->kind == PkgValue::Kind::String && ev->str == "main.hphl",
         "entry == main.hphl");
}

void testNumbers() {
  std::string path = writeTemp("hpkg_test2.toml",
    "intval = 42\n"
    "neg = -7\n"
    "doubleval = 3.14\n"
    "exp = 1.5e2\n"
    "empty_list = []\n");
  PkgDocument doc;
  std::string err;
  bool ok = loadPkgToml(path, doc, err);
  EXPECT(ok, "loadPkgToml success");
  const PkgValue* v = doc.get("intval");
  EXPECT(v && v->kind == PkgValue::Kind::Int && v->num == 42, "intval = 42");
  v = doc.get("neg");
  EXPECT(v && v->kind == PkgValue::Kind::Int && v->num == -7, "neg = -7");
  v = doc.get("doubleval");
  EXPECT(v && v->kind == PkgValue::Kind::Double && v->dbl == 3.14, "doubleval = 3.14");
  v = doc.get("exp");
  EXPECT(v && v->kind == PkgValue::Kind::Double && v->dbl == 150.0, "exp = 150.0");
  auto lst = doc.list("empty_list");
  EXPECT(lst.empty(), "empty_list vazia");
}

void testMultipleSections() {
  std::string path = writeTemp("hpkg_test3.toml",
    "top = \"raiz\"\n"
    "[a]\n"
    "x = 1\n"
    "[b]\n"
    "y = \"two\"\n"
    "z = [\"foo\", \"bar\"]\n");
  PkgDocument doc;
  std::string err;
  bool ok = loadPkgToml(path, doc, err);
  EXPECT(ok, "loadPkgToml success");
  EXPECT(doc.str("top") == "raiz", "top");
  EXPECT(doc.strSec("a", "x") == "", "a.x eh int (nao string)");  // a.x = 1
  const PkgValue* aXv = doc.section("a") ? doc.section("a")->get("x") : nullptr;
  EXPECT(aXv && aXv->kind == PkgValue::Kind::Int && aXv->num == 1, "a.x == 1 (int)");
  EXPECT(doc.strSec("b", "y") == "two", "b.y");
  auto zb = doc.listSec("b", "z");
  EXPECT(zb.size() == 2 && zb[0] == "foo" && zb[1] == "bar", "b.z[0..1]");
}

}  // namespace
}  // namespace hphl

int main() {
  hphl::testSimple();
  hphl::testNumbers();
  hphl::testMultipleSections();

  // M_RV1 H6: smoke test com exemplo real (examples/proj_demo/hphl.pkg.toml)
  hphl::PkgDocument doc;
  std::string err;
  std::string projPath = "C:/Projetos/HPHL/examples/proj_demo/hphl.pkg.toml";
  bool ok = hphl::loadPkgToml(projPath, doc, err);
  if (!ok) { std::fprintf(stderr, "FAIL: loadPkgToml exemplo real: %s\n", err.c_str()); failed++; }
  else { passed++; }
  if (ok) {
    if (doc.str("name") != "demo_pkg") { std::fprintf(stderr, "FAIL: name\n"); failed++; } else passed++;
    if (doc.str("version") != "0.1.0") { std::fprintf(stderr, "FAIL: version\n"); failed++; } else passed++;
    if (doc.str("entry") != "demo.hphl") { std::fprintf(stderr, "FAIL: entry\n"); failed++; } else passed++;
    if (doc.str("backend") != "auto") { std::fprintf(stderr, "FAIL: backend\n"); failed++; } else passed++;
    auto deps = doc.list("sources");
    if (deps.size() != 2) { std::fprintf(stderr, "FAIL: sources size\n"); failed++; } else passed++;
    if (doc.strSec("dependencies", "hphl_stdlib") != "^1.0") { std::fprintf(stderr, "FAIL: dep stdlib\n"); failed++; } else passed++;
    if (doc.strSec("tasks", "app") != "") { std::fprintf(stderr, "FAIL: app eh objeto nao string\n"); failed++; } else passed++;
  }

  std::printf("\nhphlc_pkgparser_test: %d passed, %d failed\n", passed, failed);
  return failed > 0 ? 1 : 0;
}
