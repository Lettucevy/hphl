// semver_test.cpp — M_RV1 H7: testes do parser/compare/resolve semver
#include "loader/pkgfetch.h"
#include <cstdio>
#include <cstring>

namespace hphl {
namespace {

int passed = 0;
int failed = 0;

#define EXPECT(cond, msg) do { \
  if (!(cond)) { std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); failed++; } \
  else { passed++; } \
} while (0)

void testParse() {
  SemVer s = parseSemVer("1.2.3");
  EXPECT(s.valid && s.major == 1 && s.minor == 2 && s.patch == 3 &&
         s.preRelease.empty() && s.build.empty(), "1.2.3 simples");

  s = parseSemVer("1.2.3-alpha");
  EXPECT(s.valid && s.preRelease == "alpha", "1.2.3-alpha");

  s = parseSemVer("1.2.3-alpha.1");
  EXPECT(s.valid && s.preRelease == "alpha.1", "1.2.3-alpha.1");

  s = parseSemVer("1.2.3-alpha+build.42");
  EXPECT(s.valid && s.preRelease == "alpha" && s.build == "build.42",
         "1.2.3-alpha+build.42");

  s = parseSemVer("0.0.0");
  EXPECT(s.valid && s.major == 0 && s.minor == 0 && s.patch == 0, "0.0.0");

  s = parseSemVer("999999999.999999999.999999999");
  EXPECT(s.valid, "numeros grandes");

  s = parseSemVer("");
  EXPECT(!s.valid, "string vazia invalida");
  s = parseSemVer("1");
  EXPECT(!s.valid, "1 sem . invalido");
  s = parseSemVer("1.2");
  EXPECT(!s.valid, "1.2 sem 3o numero invalido");
}

void testCompare() {
  EXPECT(compareSemVer(parseSemVer("1.0.0"), parseSemVer("1.0.0")) == 0,
         "1.0.0 == 1.0.0");
  EXPECT(compareSemVer(parseSemVer("1.0.0"), parseSemVer("2.0.0")) < 0,
         "1.0.0 < 2.0.0");
  EXPECT(compareSemVer(parseSemVer("2.0.0"), parseSemVer("1.0.0")) > 0,
         "2.0.0 > 1.0.0");
  EXPECT(compareSemVer(parseSemVer("1.0.0"), parseSemVer("1.1.0")) < 0,
         "1.0.0 < 1.1.0");
  EXPECT(compareSemVer(parseSemVer("1.0.0"), parseSemVer("1.0.1")) < 0,
         "1.0.0 < 1.0.1");
  // pre-release: ausente > presente
  EXPECT(compareSemVer(parseSemVer("1.0.0"), parseSemVer("1.0.0-alpha")) > 0,
         "1.0.0 > 1.0.0-alpha");
  EXPECT(compareSemVer(parseSemVer("1.0.0-alpha"), parseSemVer("1.0.0")) < 0,
         "1.0.0-alpha < 1.0.0");
  // build metadata ignorado
  EXPECT(compareSemVer(parseSemVer("1.0.0+a"), parseSemVer("1.0.0+b")) == 0,
         "build ignorado no compare");
  // semver 2.0 §11: pre-release identifier comparison
  // 1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta
  //   < 1.0.0-beta.2 < 1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0
  EXPECT(compareSemVer(parseSemVer("1.0.0-alpha"), parseSemVer("1.0.0-alpha.1")) < 0,
         "alpha < alpha.1");
  EXPECT(compareSemVer(parseSemVer("1.0.0-alpha.1"), parseSemVer("1.0.0-alpha.beta")) < 0,
         "alpha.1 < alpha.beta");
  EXPECT(compareSemVer(parseSemVer("1.0.0-alpha.beta"), parseSemVer("1.0.0-beta")) < 0,
         "alpha.beta < beta");
  EXPECT(compareSemVer(parseSemVer("1.0.0-beta"), parseSemVer("1.0.0-beta.2")) < 0,
         "beta < beta.2");
  EXPECT(compareSemVer(parseSemVer("1.0.0-beta.2"), parseSemVer("1.0.0-beta.11")) < 0,
         "beta.2 < beta.11 (numerico: 2 < 11)");
  EXPECT(compareSemVer(parseSemVer("1.0.0-beta.11"), parseSemVer("1.0.0-rc.1")) < 0,
         "beta.11 < rc.1");
  EXPECT(compareSemVer(parseSemVer("1.0.0-rc.1"), parseSemVer("1.0.0")) < 0,
         "rc.1 < sem pre");
  // numerico < alfabetico
  EXPECT(compareSemVer(parseSemVer("1.0.0-1"), parseSemVer("1.0.0-alpha")) < 0,
         "1 < alpha");
  EXPECT(compareSemVer(parseSemVer("1.0.0-1"), parseSemVer("1.0.0-2")) < 0,
         "1 < 2 (numerico)");
}

void testResolve() {
  std::vector<std::string> vs = {"1.0.0", "1.2.0", "1.2.3", "2.0.0", "2.1.0"};
  // * = mais recente
  EXPECT(resolveVersion("*", vs) == "2.1.0", "* = mais recente");
  EXPECT(resolveVersion("1.2.3", vs) == "1.2.3", "exato");
  // ^1.2.3: >=1.2.3 <2.0.0
  EXPECT(resolveVersion("^1.2.3", vs) == "1.2.3", "^1.2.3 (max em 1.x)");
  EXPECT(resolveVersion("^1.0.0", vs) == "1.2.3", "^1.0.0 (max em 1.x)");
  // ~1.2.3: >=1.2.3 <1.3.0
  EXPECT(resolveVersion("~1.2.0", vs) == "1.2.3", "~1.2.0 (max em 1.2.x)");
  EXPECT(resolveVersion("~1.0.0", vs) == "1.0.0", "~1.0.0 (sem 1.0.x maior)");
  // >=1.2.3
  EXPECT(resolveVersion(">=1.2.3", vs) == "2.1.0", ">=1.2.3");
  EXPECT(resolveVersion(">1.0.0", vs) == "2.1.0", ">1.0.0");
  EXPECT(resolveVersion("<2.0.0", vs) == "1.2.3", "<2.0.0");
  EXPECT(resolveVersion("<=1.2.0", vs) == "1.2.0", "<=1.2.0");
  // sem match
  EXPECT(resolveVersion("^3.0.0", vs) == "", "^3.0.0 sem match");
  // invalido
  EXPECT(resolveVersion("invalid", vs) == "", "spec invalida");
}

}  // namespace
}  // namespace hphl

int main() {
  hphl::testParse();
  hphl::testCompare();
  hphl::testResolve();
  std::printf("\nhphlc_semver_test: %d passed, %d failed\n", hphl::passed, hphl::failed);
  return hphl::failed > 0 ? 1 : 0;
}
