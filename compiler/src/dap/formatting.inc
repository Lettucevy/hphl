// ---------------------------------------------------------------------------
// Formatação de valores
// ---------------------------------------------------------------------------
// o runtime envia escalares em hex (%llx) — converte para o formato do DAP
static std::string hexOf(unsigned long long v) {
  char buf[24];
  snprintf(buf, sizeof buf, "%llx", v);
  return buf;
}

static unsigned long long hexToULL(const std::string& s) {
  if (s.empty() || s == "none") return 0;
  return strtoull(s.c_str(), nullptr, 16);
}

// F2.2: nome da variante de um enum simples pelo valor ("" se não encontrado)
std::string DapServer::enumVariantName(const std::string& enumName,
                                       long long value) const {
  for (const EnumDbg& e : enums_) {
    if (e.name != enumName) continue;
    for (const auto& entry : e.entries)
      if (entry.second == value) return entry.first;
    break;
  }
  return "";
}

std::string DapServer::formatRaw(long long tipo, const std::string& rvar) {
  if (rvar == "none") return "<indisponível>";
  if (rvar.rfind("0x", 0) == 0 || rvar.rfind("0X", 0) == 0)
    return rvar; // ponteiro: já hex
  long long v = strtoull(rvar.c_str(), nullptr, 16);
  switch (tipo) {
    case 1: return std::to_string((long long)v);
    case 2: return std::to_string((unsigned long long)v);
    case 3: {
      double d;
      memcpy(&d, &v, 8);
      char buf[64];
      snprintf(buf, sizeof buf, "%.6g", d);
      return buf;
    }
    case 4: return v ? "true" : "false";
    case 5: {
      if (v >= 32 && v < 127) return std::string("'") + (char)v + "'";
      return "'\\x" + rvar + "'";
    }
    default: return rvar;
  }
}

JsonValue DapServer::variableFor(const std::string& name, const TipoInfo& ti,
                                 const std::string& rvar, bool isLocal,
                                 long long fnId, const std::string& rbp) {
  JsonValue v = JsonValue::makeObject();
  v["name"] = JsonValue::makeString(name);
  if (!ti.dims.empty()) {
    // array (1-D ou multi): base via comando; nó navegável
    std::string base;
    if (isLocal) cmdBase(fnId, rbp, name, base);
    else cmdBaseGbl(name, base);
    if (base == "none") base = "0";
    VarNode nd;
    nd.kind = 1;
    nd.base = hexToULL(base);
    nd.dims = ti.dims;
    nd.elemTipo = ti.tipo;
    nd.elemCls = ti.cls;
    nd.name = name;
    int ref = (int)(NODE_REF_BASE + varNodes_.size());
    varNodes_.push_back(std::move(nd));
    std::string type = "array[" + dimsTypeStr(ti.dims) + "]";
    v["type"] = JsonValue::makeString(type);
    v["value"] = JsonValue::makeString(base.empty() ? "<indisponível>" : base);
    v["variablesReference"] = JsonValue::makeInt(ref);
    v["indexedVariables"] = JsonValue::makeInt(ti.dims[0]);
    return v;
  }
  if (ti.tipo == 6) {
    std::string content;
    if (rvar != "none" && cmdReadStr(rvar, content)) {
      v["value"] = JsonValue::makeString("\"" + content + "\"");
      v["type"] = JsonValue::makeString("string");
      return v;
    }
    v["value"] = JsonValue::makeString("<indisponível>");
    v["type"] = JsonValue::makeString("string");
    return v;
  }
  if (ti.tipo == 9) {
    // classe: nó navegável pelos campos
    long long clsId = clsIdOf(ti.cls);
    if (rvar != "none" && clsId >= 0) {
      VarNode nd;
      nd.kind = 2;
      nd.base = hexToULL(rvar);
      nd.clsId = clsId;
      nd.name = name;
      int ref = (int)(NODE_REF_BASE + varNodes_.size());
      varNodes_.push_back(std::move(nd));
      v["type"] = JsonValue::makeString(ti.cls);
      v["value"] = JsonValue::makeString(rvar);
      v["variablesReference"] = JsonValue::makeInt(ref);
      return v;
    }
    v["value"] = JsonValue::makeString("<indisponível>");
    v["type"] = JsonValue::makeString(ti.cls);
    return v;
  }
  if (ti.tipo == 10) {
    // enum simples: mostra o nome da variante (F2.2)
    long long vv = strtoull(rvar.c_str(), nullptr, 16);
    std::string vn = enumVariantName(ti.cls, vv);
    v["value"] = JsonValue::makeString(vn.empty() ? formatRaw(1, rvar) : vn);
    v["type"] = JsonValue::makeString(ti.cls);
    v["variablesReference"] = JsonValue::makeInt(0);
    return v;
  }
  std::string val = formatRaw(ti.tipo, rvar);
  v["value"] = JsonValue::makeString(val);
  switch (ti.tipo) {
    case 1: v["type"] = JsonValue::makeString("int"); break;
    case 2: v["type"] = JsonValue::makeString("uint"); break;
    case 3: v["type"] = JsonValue::makeString("float"); break;
    case 4: v["type"] = JsonValue::makeString("bool"); break;
    case 5: v["type"] = JsonValue::makeString("char"); break;
    case 8: v["type"] = JsonValue::makeString("list"); break;
    default: v["type"] = JsonValue::makeString("?"); break;
  }
  v["variablesReference"] = JsonValue::makeInt(0);
  return v;
}

// valor de um elemento/nó no endereço dado (stride = bytes por elemento)
JsonValue DapServer::valueForNode(const std::string& name, const TipoInfo& ti,
                                  unsigned long long addr, long long stride) {
  JsonValue e = JsonValue::makeObject();
  e["name"] = JsonValue::makeString(name);
  if (stride <= 0) stride = 8;
  if (!ti.dims.empty()) {
    // sub-array: novo nó navegável com as dims restantes
    VarNode nd;
    nd.kind = 1;
    nd.base = addr;
    nd.dims = ti.dims;
    nd.elemTipo = ti.tipo;
    nd.elemCls = ti.cls;
    nd.name = name;
    int ref = (int)(NODE_REF_BASE + varNodes_.size());
    varNodes_.push_back(std::move(nd));
    e["type"] = JsonValue::makeString("array[" + dimsTypeStr(ti.dims) + "]");
    e["value"] = JsonValue::makeString(hexOf(addr));
    e["variablesReference"] = JsonValue::makeInt(ref);
    e["indexedVariables"] = JsonValue::makeInt(ti.dims[0]);
    return e;
  }
  std::vector<std::string> slots;
  if (!cmdMem(addr, stride > 8 ? stride / 8 : 1, slots)) {
    e["value"] = JsonValue::makeString("<indisponível>");
    e["type"] = JsonValue::makeString("?");
    e["variablesReference"] = JsonValue::makeInt(0);
    return e;
  }
  std::string rvar = slots.empty() ? "none" : slots[0];
  if (ti.tipo == 6) {
    std::string content;
    if (rvar != "none" && cmdReadStr(rvar, content)) {
      e["value"] = JsonValue::makeString("\"" + content + "\"");
      e["type"] = JsonValue::makeString("string");
      e["variablesReference"] = JsonValue::makeInt(0);
      return e;
    }
    e["value"] = JsonValue::makeString("<indisponível>");
    e["type"] = JsonValue::makeString("string");
    e["variablesReference"] = JsonValue::makeInt(0);
    return e;
  }
  if (ti.tipo == 9) {
    long long clsId = clsIdOf(ti.cls);
    if (rvar != "none" && clsId >= 0) {
      VarNode nd;
      nd.kind = 2;
      nd.base = hexToULL(rvar);
      nd.clsId = clsId;
      nd.name = name;
      int ref = (int)(NODE_REF_BASE + varNodes_.size());
      varNodes_.push_back(std::move(nd));
      e["type"] = JsonValue::makeString(ti.cls);
      e["value"] = JsonValue::makeString(rvar);
      e["variablesReference"] = JsonValue::makeInt(ref);
      return e;
    }
    e["value"] = JsonValue::makeString("<indisponível>");
    e["type"] = JsonValue::makeString(ti.cls);
    e["variablesReference"] = JsonValue::makeInt(0);
    return e;
  }
  if (ti.tipo == 8) {
    // list<T>: nó navegável; o header (count/capacity/items) é lido na expansão
    if (ti.elem && ti.elem->tipo != 0 && rvar != "none") {
      VarNode nd;
      nd.kind = 3;
      nd.base = hexToULL(rvar);
      nd.elem = ti.elem;
      nd.name = name;
      int ref = (int)(NODE_REF_BASE + varNodes_.size());
      varNodes_.push_back(std::move(nd));
      e["type"] = JsonValue::makeString("list");
      e["value"] = JsonValue::makeString(rvar);
      e["variablesReference"] = JsonValue::makeInt(ref);
      e["indexedVariables"] = JsonValue::makeInt(0);
      return e;
    }
    e["value"] = JsonValue::makeString(rvar);
    e["type"] = JsonValue::makeString("list");
    e["variablesReference"] = JsonValue::makeInt(0);
    return e;
  }
  if (ti.tipo == 10) {
    // enum simples: mostra o nome da variante (F2.2)
    long long vv = strtoull(rvar.c_str(), nullptr, 16);
    std::string vn = enumVariantName(ti.cls, vv);
    e["value"] = JsonValue::makeString(vn.empty() ? formatRaw(1, rvar) : vn);
    e["type"] = JsonValue::makeString(ti.cls);
    e["variablesReference"] = JsonValue::makeInt(0);
    return e;
  }
  e["value"] = JsonValue::makeString(formatRaw(ti.tipo, rvar));
  e["variablesReference"] = JsonValue::makeInt(0);
  switch (ti.tipo) {
    case 1: e["type"] = JsonValue::makeString("int"); break;
    case 2: e["type"] = JsonValue::makeString("uint"); break;
    case 3: e["type"] = JsonValue::makeString("float"); break;
    case 4: e["type"] = JsonValue::makeString("bool"); break;
    case 5: e["type"] = JsonValue::makeString("char"); break;
    default: e["type"] = JsonValue::makeString("?"); break;
  }
  return e;
}

void DapServer::expandArrayNode(const JsonValue& id, size_t idx) {
  const VarNode& nd = varNodes_[idx];
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  // stride do nível = produto das dims restantes * 8 (row-major contíguo)
  long long stride = 8;
  for (size_t k = 1; k < nd.dims.size(); k++) stride *= nd.dims[k];
  long long n = nd.dims.empty() ? 0 : nd.dims[0];
  if (n > 256) n = 256;
  for (long long i = 0; i < n; i++) {
    unsigned long long addr = nd.base + (unsigned long long)i * (unsigned long long)stride;
    TipoInfo sub;
    sub.tipo = nd.elemTipo;
    sub.cls = nd.elemCls;
    if (nd.dims.size() > 1) {
      for (size_t k = 1; k < nd.dims.size(); k++) sub.dims.push_back(nd.dims[k]);
    }
    arr.push(valueForNode(nd.name + "[" + std::to_string(i) + "]", sub, addr,
                          stride));
  }
  result["variables"] = arr;
  respond(id, result);
}

void DapServer::expandClassNode(const JsonValue& id, size_t idx) {
  const VarNode& nd = varNodes_[idx];
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  if (nd.clsId >= 0 && nd.clsId < (long long)classes_.size()) {
    const DbgClass& cls = classes_[(size_t)nd.clsId];
    for (const DbgField& f : cls.fields) {
      arr.push(valueForNode(f.name, f.ti, nd.base + (unsigned long long)f.offset,
                            8));
    }
  }
  result["variables"] = arr;
  respond(id, result);
}

void DapServer::expandListNode(const JsonValue& id, size_t idx) {
  const VarNode& nd = varNodes_[idx];
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  // header HphlList: [count, capacity, items]
  std::vector<std::string> hdr;
  if (cmdMem(nd.base, 3, hdr) && hdr.size() >= 3) {
    long long count = (long long)hexToULL(hdr[0]);
    unsigned long long items = hexToULL(hdr[2]);
    long long n = count > 256 ? 256 : count;
    for (long long i = 0; i < n; i++) {
      unsigned long long addr = items + (unsigned long long)i * 8;
      arr.push(valueForNode(std::to_string(i), *nd.elem, addr, 8));
    }
  }
  result["variables"] = arr;
  respond(id, result);
}

// ---------------------------------------------------------------------------
