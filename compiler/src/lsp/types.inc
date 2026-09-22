// ---------------------------------------------------------------------------
// Nomes de tipos (exibição)
// ---------------------------------------------------------------------------
std::string typeName(const Type& t) {
  switch (t.kind) {
    case Type::Kind::Void: return "void";
    case Type::Kind::Int:
      return t.bits == 64 ? "int" : "int<" + std::to_string(t.bits) + ">";
    case Type::Kind::UInt: return "u" + std::to_string(t.bits);
    case Type::Kind::Float:
      return t.bits == 64 ? "float" : "float<" + std::to_string(t.bits) + ">";
    case Type::Kind::Bool: return "bool";
    case Type::Kind::Char: return "char";
    case Type::Kind::String: return "string";
    case Type::Kind::Class: {
      std::string s = t.name;
      if (!t.genericArgs.empty()) {
        s += "<";
        for (size_t i = 0; i < t.genericArgs.size(); i++) {
          if (i) s += ", ";
          s += typeName(t.genericArgs[i]);
        }
        s += ">";
      }
      return s;
    }
    case Type::Kind::Enum: return t.name;
    case Type::Kind::Array:
      return typeName(*t.elem) + "[" + std::to_string(t.arraySize) + "]";
    case Type::Kind::List: return "list<" + typeName(*t.elem) + ">";
    case Type::Kind::Module: return t.name;
    case Type::Kind::Option:
      return "option<" + (t.elem ? typeName(*t.elem) : "?") + ">";
    case Type::Kind::Result:
      return "result<" + (t.elem ? typeName(*t.elem) : "void") + ", " +
             (t.elem2 ? typeName(*t.elem2) : "?") + ">";
    case Type::Kind::Task:
      return "task<" + (t.elem ? typeName(*t.elem) : "void") + ">";
    case Type::Kind::Channel:
      return "channel<" + (t.elem ? typeName(*t.elem) : "void") + ">";
    case Type::Kind::Func: {
      std::string s = "func<(";
      for (size_t i = 0; i < t.genericArgs.size(); i++) {
        if (i) s += ", ";
        s += typeName(t.genericArgs[i]);
      }
      s += ") -> " + (t.elem ? typeName(*t.elem) : std::string("void")) + ">";
      return s;
    }
    case Type::Kind::Mutex: return "mutex";
    case Type::Kind::Semaphore: return "semaphore";
    case Type::Kind::Event: return "event";
    case Type::Kind::Barrier: return "barrier";
    case Type::Kind::TypeVar: return t.name;
    default: return "?";
  }
}

bool isPrimitiveTypeName(const std::string& name) {
  static const char* prims[] = {"int",   "float",  "double", "bool",
                                "char",  "string", "u8",     "u16",
                                "u32",   "u64",    "void",   nullptr};
  for (int i = 0; prims[i]; i++)
    if (name == prims[i]) return true;
  return false;
}

std::string signature(const FunctionDecl* fn) {
  std::string name = fn->name;
  if (fn->isMethod && !fn->ownerClass.empty()) name = fn->ownerClass + "." + fn->name;
  std::string s = name + "(";
  for (size_t i = 0; i < fn->params.size(); i++) {
    if (i) s += ", ";
    const Param* p = fn->params[i].get();
    if (p->byOut) s += "out ";
    else if (p->byRef) s += "ref ";
    else if (p->byIn) s += "in ";
    s += typeName(p->type) + " " + p->name;
    if (p->defaultVal) s += " = ...";
  }
  s += ")";
  if (fn->hasReturnType && fn->returnType.kind != Type::Kind::Void)
    s += ": " + typeName(fn->returnType);
  return s;
}

