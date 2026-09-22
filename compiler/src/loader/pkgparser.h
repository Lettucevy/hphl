// pkgparser.h — M_RV1 H6: parser TOML minimalista para hphl.pkg.toml
#pragma once

#include <map>
#include <string>
#include <vector>

namespace hphl {

// Valor TOML: apenas os tipos que nos interessam (string, int, double, bool, lista de strings).
struct PkgValue {
  enum class Kind { Null, String, Int, Double, Bool, List };
  Kind kind = Kind::Null;
  using ListType = std::vector<std::string>;
  std::string str;
  long long num = 0;
  double dbl = 0.0;
  bool boolean = false;
  ListType list;

  PkgValue();
  explicit PkgValue(const std::string& s);
  explicit PkgValue(long long n);
  explicit PkgValue(double d);
  explicit PkgValue(bool b);
};

// Secao TOML ([name]). Mantem mapa de chave -> PkgValue.
struct PkgSection {
  std::string name;
  std::map<std::string, PkgValue> entries;
  const PkgValue* get(const std::string& key) const;
};

// Documento inteiro: secoes + valores no nivel raiz.
struct PkgDocument {
  std::string path;
  std::map<std::string, PkgSection> sections;
  std::map<std::string, PkgValue> flat;  // chaves antes de qualquer [section]

  // helpers
  const PkgValue* get(const std::string& key) const {  // flat lookup
    auto it = flat.find(key);
    return it != flat.end() ? &it->second : nullptr;
  }
  const PkgSection* section(const std::string& name) const {
    auto it = sections.find(name);
    return it != sections.end() ? &it->second : nullptr;
  }
  std::string str(const std::string& key, const std::string& def = "") const {
    const PkgValue* v = get(key);
    return v && v->kind == PkgValue::Kind::String ? v->str : def;
  }
  std::string strSec(const std::string& sec, const std::string& key, const std::string& def = "") const {
    const PkgSection* s = section(sec);
    const PkgValue* v = s ? s->get(key) : nullptr;
    return v && v->kind == PkgValue::Kind::String ? v->str : def;
  }
  PkgValue::ListType list(const std::string& key) const {
    const PkgValue* v = get(key);
    return (v && v->kind == PkgValue::Kind::List) ? v->list : PkgValue::ListType{};
  }
  PkgValue::ListType listSec(const std::string& sec, const std::string& key) const {
    const PkgSection* s = section(sec);
    const PkgValue* v = s ? s->get(key) : nullptr;
    return (v && v->kind == PkgValue::Kind::List) ? v->list : PkgValue::ListType{};
  }
};

// Parse TOML. Retorna true em sucesso (err fica vazio), false senao (err preenchido).
// Auto-detecta UTF-8 BOM. Suporta secoes [name], chaves simples, valores
// string/int/double/bool/lista. Comentarios comecam com # ate fim da linha.
bool loadPkgToml(const std::string& path, PkgDocument& out, std::string& err);

}  // namespace hphl
