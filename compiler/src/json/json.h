#pragma once
#include <string>
#include <utility>
#include <vector>

namespace hphl {

// JSON mínimo para o LSP (JSON-RPC 2.0): parse + serialização compacta,
// sem dependências externas. Strings são tratadas como bytes UTF-8.
class JsonValue {
public:
  enum class Type { Null, Bool, Int, Double, String, Array, Object };

  JsonValue() = default; // null

  static JsonValue makeBool(bool b);
  static JsonValue makeInt(long long v);
  static JsonValue makeDouble(double v);
  static JsonValue makeString(const std::string& s);
  static JsonValue makeArray();
  static JsonValue makeObject();

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isInt() const { return type_ == Type::Int; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  long long asInt(long long dflt = 0) const;
  double asDouble(double dflt = 0) const;
  bool asBool(bool dflt = false) const;
  const std::string& asString(const std::string& dflt = "") const;

  // objeto
  bool has(const std::string& key) const;
  const JsonValue& operator[](const std::string& key) const; // null se ausente
  JsonValue& operator[](const std::string& key);             // cria se ausente
  // iteração sobre chaves de objeto (somente leitura) — M26 14.5
  size_t memberCount() const;
  const std::pair<std::string, JsonValue>& memberAt(size_t i) const;

  // array
  size_t size() const;
  void push(const JsonValue& v);
  const JsonValue& at(size_t i) const; // null se fora do intervalo

  std::string serialize() const; // compacto

private:
  Type type_ = Type::Null;
  bool bool_ = false;
  long long int_ = 0;
  double dbl_ = 0;
  std::string str_;
  std::vector<std::pair<std::string, JsonValue>> members_;
  std::vector<JsonValue> items_;
};

// Lança std::runtime_error em erro de sintaxe (com posição).
JsonValue parseJson(const std::string& text);

std::string jsonEscape(const std::string& s);

} // namespace hphl