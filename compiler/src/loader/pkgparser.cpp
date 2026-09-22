// ============================================================================
// pkgparser.cpp — M_RV1 H6: parser para hphl.pkg.toml (TOML subset)
// Suporta apenas o subset estritamente necessario para o build system:
//   [section]                     -> tabela
//   key = "string"                -> string
//   key = 123                     -> int
//   key = 1.5                    -> double
//   key = true / false            -> bool
//   key = ["a", "b", "c"]         -> lista de strings
//   # comentario                  -> ignorado ate fim da linha
// Espacos em branco ignorados; aspas duplas para strings (escape basico).
// ============================================================================

#include "pkgparser.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace hphl {

namespace {

std::string readFileRaw(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void skipWsAndComments(const std::string& s, size_t& i) {
  while (i < s.size()) {
    char c = s[i];
    if (c == '#') {
      while (i < s.size() && s[i] != '\n') i++;
    } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      i++;
    } else {
      break;
    }
  }
}

std::string parseKey(const std::string& s, size_t& i) {
  // ate = ou fim de secao
  std::string k;
  while (i < s.size() && s[i] != '=' && s[i] != '\n') {
    k += s[i++];
  }
  // trim
  while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
  return k;
}

std::string parseString(const std::string& s, size_t& i) {
  // aspas duplas com escape basico (\" e \\)
  if (i >= s.size() || s[i] != '"') return "";
  i++;
  std::string out;
  while (i < s.size() && s[i] != '"') {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char nx = s[i + 1];
      if (nx == '"' || nx == '\\') out += nx, i += 2;
      else out += s[i++];
    } else {
      out += s[i++];
    }
  }
  if (i < s.size()) i++;  // consome "
  return out;
}

long long parseInt(const std::string& s, size_t& i) {
  long long v = 0;
  bool neg = false;
  if (i < s.size() && s[i] == '-') { neg = true; i++; }
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    v = v * 10 + (s[i] - '0');
    i++;
  }
  return neg ? -v : v;
}

double parseDouble(const std::string& s, size_t& i) {
  std::string t;
  if (i < s.size() && s[i] == '-') { t += '-'; i++; }
  while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
    t += s[i++];
  }
  return t.empty() ? 0.0 : std::stod(t);
}

bool parseBool(const std::string& s, size_t& i) {
  if (i + 4 <= s.size() && s.compare(i, 4, "true") == 0) { i += 4; return true; }
  if (i + 5 <= s.size() && s.compare(i, 5, "false") == 0) { i += 5; return false; }
  return false;
}

// Parse "value" — retorna o tipo detectado e preenche o PkgValue.
PkgValue parseValue(const std::string& s, size_t& i) {
  skipWsAndComments(s, i);
  if (i >= s.size()) return PkgValue{};
  if (s[i] == '"') {
    return PkgValue(parseString(s, i));
  }
  if (i + 4 <= s.size() && s.compare(i, 4, "true") == 0) { i += 4; return PkgValue(true); }
  if (i + 5 <= s.size() && s.compare(i, 5, "false") == 0) { i += 5; return PkgValue(false); }
  if (s[i] == '[') {
    // lista de strings
    i++;
    PkgValue::ListType lst;
    while (i < s.size()) {
      skipWsAndComments(s, i);
      if (i >= s.size()) break;
      if (s[i] == ']') { i++; break; }
      if (s[i] == ',') { i++; continue; }
      if (s[i] == '"') {
        lst.push_back(parseString(s, i));
      } else {
        // token não quotado ou malformado: consome até vírgula, ] ou newline para evitar loop infinito
        std::string rawItem;
        while (i < s.size() && s[i] != ',' && s[i] != ']' && s[i] != '\n') {
          rawItem += s[i++];
        }
        while (!rawItem.empty() && (rawItem.back() == ' ' || rawItem.back() == '\t' || rawItem.back() == '\r')) rawItem.pop_back();
        while (!rawItem.empty() && (rawItem.front() == ' ' || rawItem.front() == '\t')) rawItem.erase(0, 1);
        if (!rawItem.empty()) lst.push_back(rawItem);
      }
    }
    PkgValue v;
    v.kind = PkgValue::Kind::List;
    v.list = std::move(lst);
    return v;
  }
  if (s[i] == '-' || (s[i] >= '0' && s[i] <= '9')) {
    // tenta double (inclui '.'/'e'), fallback int
    size_t save = i;
    if (s[i] == '-') i++;
    bool hasDotOrE = false;
    size_t j = i;
    while (j < s.size() && (std::isdigit((unsigned char)s[j]) || s[j] == '.' || s[j] == 'e' || s[j] == 'E' || s[j] == '+' || s[j] == '-')) {
      if (s[j] == '.' || s[j] == 'e' || s[j] == 'E') hasDotOrE = true;
      j++;
    }
    i = save;
    if (hasDotOrE) return PkgValue(parseDouble(s, i));
    return PkgValue(parseInt(s, i));
  }
  // raw: pega ate , ou \n
  std::string raw;
  while (i < s.size() && s[i] != ',' && s[i] != '\n' && s[i] != '#') raw += s[i++];
  while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.pop_back();
  PkgValue v;
  v.kind = PkgValue::Kind::String;
  v.str = raw;
  return v;
}

// Skip ate fim da linha (comentario inline)
void skipToEol(const std::string& s, size_t& i) {
  while (i < s.size() && s[i] != '\n') i++;
}

}  // namespace

PkgValue::PkgValue() : kind(Kind::Null) {}
PkgValue::PkgValue(const std::string& s) : kind(Kind::String), str(s) {}
PkgValue::PkgValue(long long n) : kind(Kind::Int), num(n) {}
PkgValue::PkgValue(double d) : kind(Kind::Double), dbl(d) {}
PkgValue::PkgValue(bool b) : kind(Kind::Bool), boolean(b) {}

const PkgValue* PkgSection::get(const std::string& key) const {
  auto it = entries.find(key);
  return it != entries.end() ? &it->second : nullptr;
}

bool loadPkgToml(const std::string& path, PkgDocument& out, std::string& err) {
  std::string src = readFileRaw(path);
  if (src.empty()) {
    err = "hphl: não foi possível ler '" + path + "'";
    return false;
  }

  out.path = path;
  out.sections.clear();
  out.flat.clear();

  PkgSection* current = nullptr;  // null = secao raiz
  std::string currentName;
  size_t i = 0;
  while (i < src.size()) {
    skipWsAndComments(src, i);
    if (i >= src.size()) break;

    if (src[i] == '[') {
      // [secao] ou [secao.subsecao]
      i++;
      skipWsAndComments(src, i);
      size_t nameStart = i;
      while (i < src.size() && src[i] != ']') i++;
      std::string name = src.substr(nameStart, i - nameStart);
      // trim
      while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '\r' || name.back() == '\n')) name.pop_back();
      while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(0, 1);
      if (i < src.size() && src[i] == ']') i++;
      if (i < src.size() && src[i] == '\n') i++;
      // cria ou pega secao
      auto& sec = out.sections[name];
      sec.name = name;
      current = &sec;
      currentName = name;
      continue;
    }

    // key = value
    size_t keyStart = i;
    while (i < src.size() && src[i] != '=' && src[i] != '\n') i++;
    std::string key = src.substr(keyStart, i - keyStart);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r')) key.pop_back();
    while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(0, 1);
    if (i >= src.size() || src[i] != '=') {
      // ignora chave sem valor
      skipToEol(src, i);
      if (i < src.size() && src[i] == '\n') i++;
      continue;
    }
    i++;  // consome =
    PkgValue v = parseValue(src, i);
    // consome qualquer coisa ate o fim da linha (comentario inline, virgula
    // trailing, etc.) e o proprio \n
    skipToEol(src, i);
    if (i < src.size() && src[i] == '\n') i++;
    if (current) {
      current->entries[key] = v;
    } else {
      out.flat[key] = v;
    }
  }
  return true;
}

}  // namespace hphl
