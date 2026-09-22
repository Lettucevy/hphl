#include "json.h"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace hphl {

JsonValue JsonValue::makeBool(bool b) {
  JsonValue v;
  v.type_ = Type::Bool;
  v.bool_ = b;
  return v;
}

JsonValue JsonValue::makeInt(long long v) {
  JsonValue j;
  j.type_ = Type::Int;
  j.int_ = v;
  return j;
}

JsonValue JsonValue::makeDouble(double v) {
  JsonValue j;
  j.type_ = Type::Double;
  j.dbl_ = v;
  return j;
}

JsonValue JsonValue::makeString(const std::string& s) {
  JsonValue j;
  j.type_ = Type::String;
  j.str_ = s;
  return j;
}

JsonValue JsonValue::makeArray() {
  JsonValue j;
  j.type_ = Type::Array;
  return j;
}

JsonValue JsonValue::makeObject() {
  JsonValue j;
  j.type_ = Type::Object;
  return j;
}

long long JsonValue::asInt(long long dflt) const {
  switch (type_) {
    case Type::Int: return int_;
    case Type::Double: return (long long)dbl_;
    case Type::Bool: return bool_ ? 1 : 0;
    default: return dflt;
  }
}

double JsonValue::asDouble(double dflt) const {
  switch (type_) {
    case Type::Double: return dbl_;
    case Type::Int: return (double)int_;
    default: return dflt;
  }
}

bool JsonValue::asBool(bool dflt) const {
  switch (type_) {
    case Type::Bool: return bool_;
    case Type::Int: return int_ != 0;
    default: return dflt;
  }
}

const std::string& JsonValue::asString(const std::string& dflt) const {
  return type_ == Type::String ? str_ : dflt;
}

bool JsonValue::has(const std::string& key) const {
  if (type_ != Type::Object) return false;
  for (auto& kv : members_)
    if (kv.first == key) return true;
  return false;
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
  static const JsonValue nullValue;
  if (type_ != Type::Object) return nullValue;
  for (auto& kv : members_)
    if (kv.first == key) return kv.second;
  return nullValue;
}

JsonValue& JsonValue::operator[](const std::string& key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    members_.clear();
    items_.clear();
  }
  for (auto& kv : members_)
    if (kv.first == key) return kv.second;
  members_.emplace_back(key, JsonValue());
  return members_.back().second;
}

size_t JsonValue::size() const {
  return type_ == Type::Array ? items_.size() : 0;
}

size_t JsonValue::memberCount() const {
  return type_ == Type::Object ? members_.size() : 0;
}

const std::pair<std::string, JsonValue>& JsonValue::memberAt(size_t i) const {
  static const std::pair<std::string, JsonValue> nullPair;
  if (type_ != Type::Object || i >= members_.size()) return nullPair;
  return members_[i];
}

void JsonValue::push(const JsonValue& v) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    members_.clear();
    items_.clear();
  }
  items_.push_back(v);
}

const JsonValue& JsonValue::at(size_t i) const {
  static const JsonValue nullValue;
  if (type_ != Type::Array || i >= items_.size()) return nullValue;
  return items_[i];
}

std::string jsonEscape(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;
        }
    }
  }
  return out;
}

std::string JsonValue::serialize() const {
  switch (type_) {
    case Type::Null: return "null";
    case Type::Bool: return bool_ ? "true" : "false";
    case Type::Int: return std::to_string(int_);
    case Type::Double: {
      std::ostringstream ss;
      ss << dbl_;
      return ss.str();
    }
    case Type::String: return "\"" + jsonEscape(str_) + "\"";
    case Type::Array: {
      std::string s = "[";
      for (size_t i = 0; i < items_.size(); i++) {
        if (i) s += ",";
        s += items_[i].serialize();
      }
      return s + "]";
    }
    case Type::Object: {
      std::string s = "{";
      for (size_t i = 0; i < members_.size(); i++) {
        if (i) s += ",";
        s += "\"" + jsonEscape(members_[i].first) + "\":" +
             members_[i].second.serialize();
      }
      return s + "}";
    }
  }
  return "null";
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
namespace {

void utf8Encode(std::string& out, unsigned cp) {
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | (cp >> 18));
    out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

class JsonParser {
public:
  explicit JsonParser(const std::string& s) : s_(s) {}

  JsonValue parse() {
    skipWs();
    JsonValue v = parseValue();
    skipWs();
    if (i_ != s_.size()) fail("conteúdo após o fim do JSON");
    return v;
  }

private:
  const std::string& s_;
  size_t i_ = 0;

  void fail(const std::string& msg) {
    throw std::runtime_error("JSON: " + msg + " (posição " +
                             std::to_string(i_) + ")");
  }

  void skipWs() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' ||
                              s_[i_] == '\n' || s_[i_] == '\r'))
      i_++;
  }

  char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }

  char next() {
    if (i_ >= s_.size()) fail("fim inesperado");
    return s_[i_++];
  }

  void expectLiteral(const char* lit) {
    for (size_t k = 0; lit[k]; k++)
      if (next() != lit[k]) fail("literal inválido");
  }

  JsonValue parseValue() {
    char c = peek();
    switch (c) {
      case '{': return parseObject();
      case '[': return parseArray();
      case '"': return parseString();
      case 't': expectLiteral("true"); return JsonValue::makeBool(true);
      case 'f': expectLiteral("false"); return JsonValue::makeBool(false);
      case 'n': expectLiteral("null"); return JsonValue();
      default:
        if (c == '-' || std::isdigit((unsigned char)c)) return parseNumber();
        fail(std::string("valor inesperado '") + c + "'");
    }
    return JsonValue();
  }

  JsonValue parseObject() {
    next(); // '{'
    JsonValue obj = JsonValue::makeObject();
    skipWs();
    if (peek() == '}') {
      next();
      return obj;
    }
    for (;;) {
      skipWs();
      if (peek() != '"') fail("esperava chave de objeto");
      std::string key = parseString().asString();
      skipWs();
      if (next() != ':') fail("esperava ':'");
      skipWs();
      obj[key] = parseValue();
      skipWs();
      char c = next();
      if (c == '}') return obj;
      if (c != ',') fail("esperava ',' ou '}'");
    }
  }

  JsonValue parseArray() {
    next(); // '['
    JsonValue arr = JsonValue::makeArray();
    skipWs();
    if (peek() == ']') {
      next();
      return arr;
    }
    for (;;) {
      skipWs();
      arr.push(parseValue());
      skipWs();
      char c = next();
      if (c == ']') return arr;
      if (c != ',') fail("esperava ',' ou ']'");
    }
  }

  JsonValue parseString() {
    next(); // '"'
    std::string out;
    while (i_ < s_.size()) {
      char c = s_[i_++];
      if (c == '"') return JsonValue::makeString(out);
      if (c == '\\') {
        if (i_ >= s_.size()) fail("escape incompleto");
        char e = s_[i_++];
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            if (i_ + 4 > s_.size()) fail("\\u incompleto");
            unsigned cp = 0;
            for (int k = 0; k < 4; k++) {
              char h = s_[i_++];
              cp <<= 4;
              if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
              else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
              else fail("hex inválido em \\u");
            }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              // par substituto (astral)
              if (!(i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u'))
                fail("par substituto incompleto");
              i_ += 2;
              unsigned lo = 0;
              for (int k = 0; k < 4; k++) {
                char h = s_[i_++];
                lo <<= 4;
                if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                else fail("hex inválido em \\u (baixo)");
              }
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              fail("par substituto inválido (sem alto)");
            }
            utf8Encode(out, cp);
            break;
          }
          default: fail(std::string("escape inválido '\\") + e + "'");
        }
      } else {
        out += c;
      }
    }
    fail("string não terminada");
    return JsonValue();
  }

  JsonValue parseNumber() {
    size_t start = i_;
    if (peek() == '-') next();
    while (std::isdigit((unsigned char)peek())) next();
    bool isFloat = false;
    if (peek() == '.') {
      isFloat = true;
      next();
      while (std::isdigit((unsigned char)peek())) next();
    }
    if (peek() == 'e' || peek() == 'E') {
      isFloat = true;
      next();
      if (peek() == '+' || peek() == '-') next();
      while (std::isdigit((unsigned char)peek())) next();
    }
    std::string num = s_.substr(start, i_ - start);
    if (num.empty()) fail("número inválido");
    if (isFloat) return JsonValue::makeDouble(std::stod(num));
    return JsonValue::makeInt(std::stoll(num));
  }
};

} // namespace

JsonValue parseJson(const std::string& text) {
  JsonParser p(text);
  return p.parse();
}

} // namespace hphl