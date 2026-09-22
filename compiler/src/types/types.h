#pragma once
#include <memory>
#include <string>
#include <vector>

namespace hphl {

enum class StoragePolicy {
  Auto,        // compilador infere (diferencial do projeto)
  Stack,
  Heap,
  Arena,
  Pool,
  Shared,
  ThreadLocal,
};

enum class OverflowPolicy {
  Default,
  Fixed,
  Checked,
  Wrap,
  Saturate,
  Promote,
};

inline const char* storagePolicyName(StoragePolicy p) {
  switch (p) {
    case StoragePolicy::Auto: return "auto";
    case StoragePolicy::Stack: return "stack";
    case StoragePolicy::Heap: return "heap";
    case StoragePolicy::Arena: return "arena";
    case StoragePolicy::Pool: return "pool";
    case StoragePolicy::Shared: return "shared";
    case StoragePolicy::ThreadLocal: return "threadlocal";
  }
  return "?";
}

inline const char* overflowPolicyName(OverflowPolicy p) {
  switch (p) {
    case OverflowPolicy::Default: return "default";
    case OverflowPolicy::Fixed: return "fixed";
    case OverflowPolicy::Checked: return "checked";
    case OverflowPolicy::Wrap: return "wrap";
    case OverflowPolicy::Saturate: return "saturate";
    case OverflowPolicy::Promote: return "promote";
  }
  return "?";
}

struct Type {
  enum class Kind { Void, Int, UInt, Float, Bool, Char, String, Ptr, Class, Enum, Array, List, Map, Tuple, LitValue, Module, Option, Result, Task, Channel, Mutex, Semaphore, Event, Barrier, TypeVar, Func, Unknown };
  Kind kind = Kind::Unknown;
  int bits = 64;             // Int/UInt/Float: 8, 16, 32, 64
  std::string name;          // Class/Enum: nome resolvido
  std::shared_ptr<Type> elem;  // Array/List/Option/Result: tipo do payload
  std::shared_ptr<Type> elem2; // Result: tipo do erro (E); Map: valor
  std::vector<Type> tupleElems; // Tuple: tipos dos elementos (cada um 8 bytes)
  int arraySize = 0;            // Array: número de elementos
  std::string arraySizeSym;     // M10 (v0.45): dimensão simbólica (`T[N]` em
                                // template; arraySize == -1 marca simbólico)
  std::vector<Type> genericArgs; // Class genérica: argumentos de `Nome<T1, T2>`
  OverflowPolicy policy = OverflowPolicy::Default; // comportamento do overflow

  bool isNumeric() const {
    return kind == Kind::Int || kind == Kind::UInt || kind == Kind::Float;
  }
  bool isInteger() const { return kind == Kind::Int || kind == Kind::UInt; }
  bool isPointer() const {
    // v0.95 (lambdas): valor func é um handle heap {code, env} (ponteiro)
    // FFI v2: `ptr` é um ponteiro opaco (void*) para interop com C/Vulkan.
    return kind == Kind::String || kind == Kind::Ptr || kind == Kind::Class || kind == Kind::Func;
  }
  bool isRawPtr() const { return kind == Kind::Ptr; }
  bool isOption() const { return kind == Kind::Option; }
  bool isResult() const { return kind == Kind::Result; }
  bool isUnknown() const { return kind == Kind::Unknown; }
  bool isTypeVar() const { return kind == Kind::TypeVar; }

  bool operator==(const Type& o) const {
    if (kind != o.kind || bits != o.bits || name != o.name) return false;
    if (kind == Kind::TypeVar) return name == o.name && genericArgs.size() == o.genericArgs.size();
    if (kind == Kind::Class) {
      if (genericArgs.size() != o.genericArgs.size()) return false;
      for (size_t i = 0; i < genericArgs.size(); i++)
        if (!(genericArgs[i] == o.genericArgs[i])) return false;
      return true;
    }
    if (kind == Kind::Array || kind == Kind::List) {
      if (arraySize != o.arraySize) return false;
      if (!elem && !o.elem) return true;
      if (!elem || !o.elem) return false;
      return *elem == *o.elem;
    }
    if (kind == Kind::Map) {
      if (!elem && !o.elem) return true;
      if (!elem || !o.elem) return false;
      if (!elem2 && !o.elem2) return true;
      if (!elem2 || !o.elem2) return false;
      return *elem == *o.elem && *elem2 == *o.elem2;
    }
    if (kind == Kind::Tuple) {
      if (tupleElems.size() != o.tupleElems.size()) return false;
      for (size_t i = 0; i < tupleElems.size(); i++)
        if (!(tupleElems[i] == o.tupleElems[i])) return false;
      return true;
    }
    if (kind == Kind::LitValue) return arraySize == o.arraySize;
    if (kind == Kind::Option) {
      if (!elem && !o.elem) return true;
      if (!elem || !o.elem) return false;
      return *elem == *o.elem;
    }
    if (kind == Kind::Task) {
      if (!elem && !o.elem) return true;
      if (!elem || !o.elem) return false;
      return *elem == *o.elem;
    }
    if (kind == Kind::Channel) {
      if (!elem && !o.elem) return true;
      if (!elem || !o.elem) return false;
      return *elem == *o.elem;
    }
    if (kind == Kind::Func) {
      // retorno (void = elem nulo nos dois lados) + aridade + params
      if (!elem && !o.elem) {
        // ambos void: compara params
      } else if (!elem || !o.elem) {
        return false;
      } else if (!(*elem == *o.elem)) {
        return false;
      }
      if (genericArgs.size() != o.genericArgs.size()) return false;
      for (size_t i = 0; i < genericArgs.size(); i++)
        if (!(genericArgs[i] == o.genericArgs[i])) return false;
      return true;
    }
    if (kind == Kind::Result) {
      if (!elem && !o.elem) return false;
      if (!elem || !o.elem) return false;
      if (!elem2 && !o.elem2) return true;
      if (!elem2 || !o.elem2) return false;
      return *elem == *o.elem && *elem2 == *o.elem2;
    }
    return true;
  }
  bool operator!=(const Type& o) const { return !(*this == o); }

  static Type makeVoid() { Type t; t.kind = Kind::Void; return t; }
  static Type makeInt(int bits = 64) { Type t; t.kind = Kind::Int; t.bits = bits; return t; }
  static Type makeUInt(int bits = 64) { Type t; t.kind = Kind::UInt; t.bits = bits; return t; }
  static Type makeFloat(int bits = 64) { Type t; t.kind = Kind::Float; t.bits = bits; return t; }
  static Type makeBool() { Type t; t.kind = Kind::Bool; t.bits = 8; return t; }
  static Type makeChar() { Type t; t.kind = Kind::Char; t.bits = 8; return t; }
  static Type makeString() { Type t; t.kind = Kind::String; return t; }
  static Type makeClass(const std::string& name) { Type t; t.kind = Kind::Class; t.name = name; return t; }
  static Type makeEnum(const std::string& name) { Type t; t.kind = Kind::Enum; t.name = name; return t; }
  static Type makeArray(const Type& elem, int size) {
    Type t;
    t.kind = Kind::Array;
    t.elem = std::make_shared<Type>(elem);
    t.arraySize = size;
    return t;
  }
  static Type makeList(const Type& elem) {
    Type t;
    t.kind = Kind::List;
    t.elem = std::make_shared<Type>(elem);
    return t;
  }
  static Type makeMap(const Type& key, const Type& val) {
    Type t;
    t.kind = Kind::Map;
    t.elem = std::make_shared<Type>(key);
    t.elem2 = std::make_shared<Type>(val);
    return t;
  }
  static Type makeTuple(const std::vector<Type>& elems) {
    Type t;
    t.kind = Kind::Tuple;
    t.tupleElems = elems;
    return t;
  }
  // M10 (v0.45): argumento de valor em genérico — `Buffer<int, 64>`; o valor
  // viaja em arraySize (campo livre para este kind)
  static Type makeLitValue(long long v) {
    Type t;
    t.kind = Kind::LitValue;
    t.arraySize = (int)v;
    return t;
  }
  static Type makeOption(const Type& elem) {
    Type t;
    t.kind = Kind::Option;
    t.elem = std::make_shared<Type>(elem);
    return t;
  }
  static Type makeResult(const Type& ok, const Type& err) {
    Type t;
    t.kind = Kind::Result;
    t.elem = std::make_shared<Type>(ok);
    t.elem2 = std::make_shared<Type>(err);
    return t;
  }
  static Type makeTask(const Type& payload) {
    Type t;
    t.kind = Kind::Task;
    if (payload.kind != Kind::Void)
      t.elem = std::make_shared<Type>(payload);
    return t;
  }
  static Type makeChannel(const Type& payload) {
    Type t;
    t.kind = Kind::Channel;
    if (payload.kind != Kind::Void)
      t.elem = std::make_shared<Type>(payload);
    return t;
  }
  static Type makeModule(const std::string& name) { Type t; t.kind = Kind::Module; t.name = name; return t; }
  static Type makeMutex() { Type t; t.kind = Kind::Mutex; return t; }
  static Type makeSemaphore() { Type t; t.kind = Kind::Semaphore; return t; }
  static Type makeEvent() { Type t; t.kind = Kind::Event; return t; }
  static Type makeBarrier() { Type t; t.kind = Kind::Barrier; return t; }
  static Type makeTypeVar(const std::string& name) { Type t; t.kind = Kind::TypeVar; t.name = name; return t; }
  // v0.95 (lambdas): `func<R, P...>` — retorno em elem (nulo = void),
  // tipos dos parâmetros em genericArgs. O valor é um handle heap {code, env}.
  static Type makeFunc(const Type& ret, const std::vector<Type>& params) {
    Type t;
    t.kind = Kind::Func;
    if (ret.kind != Kind::Void)
      t.elem = std::make_shared<Type>(ret);
    t.genericArgs = params;
    return t;
  }
  // ponteiro opaco (i8*): buffers internos do lowering (list_data, etc.)
  static Type makePtr() { Type t; t.kind = Kind::Ptr; return t; }
  // FFI v2: ponteiro opaco explícito `ptr` (void*) para interop com C/Vulkan.
  static Type makeRawPtr() { Type t; t.kind = Kind::Ptr; return t; }
};

// tamanho em bytes de um tipo (arrays = elementos contíguos; primitivos =
// slots de 8 bytes no runtime, exceto para stride de array via globalSizeBytes)
inline int typeSize(const Type& t) {
  if (t.kind == Type::Kind::Array) return typeSize(*t.elem) * t.arraySize;
  if (t.kind == Type::Kind::Tuple) {
    int n = 0;
    for (auto& e : t.tupleElems) n += typeSize(e);
    return n;
  }
  if ((t.kind == Type::Kind::Int || t.kind == Type::Kind::UInt || t.kind == Type::Kind::Float) && t.bits > 64) {
    return (t.bits + 7) / 8;
  }
  return 8;
}

} // namespace hphl