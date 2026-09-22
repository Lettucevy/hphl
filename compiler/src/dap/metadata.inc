// ---------------------------------------------------------------------------
// Meta do programa (enviado pelo runtime no handshake)
// ---------------------------------------------------------------------------
// tipo canônico parseado: base (1..11) + dims (F1.2); "class:Nome"/"enum:Nome"
// têm o nome no campo cls
struct TipoInfo {
  long long tipo = 0; // 1 int, 2 uint, 3 float, 4 bool, 5 char, 6 string,
                      // 8 list, 9 class, 10 enum, 11 ptr
  std::string cls;    // nome da classe/enum (tipo 9/10)
  std::vector<long long> dims; // array: dimensões (nível atual à frente)
  std::shared_ptr<TipoInfo> elem; // list: tipo do elemento (recursivo)
};

static TipoInfo parseTipo(const std::string& s) {
  TipoInfo ti;
  size_t b = s.find('[');
  std::string base = b == std::string::npos ? s : s.substr(0, b);
  if (base == "int") ti.tipo = 1;
  else if (base == "uint") ti.tipo = 2;
  else if (base == "float") ti.tipo = 3;
  else if (base == "bool") ti.tipo = 4;
  else if (base == "char") ti.tipo = 5;
  else if (base == "string") ti.tipo = 6;
  else if (base.rfind("list:", 0) == 0) {
    ti.tipo = 8;
    ti.elem = std::make_shared<TipoInfo>(parseTipo(s.substr(5)));
  }
  else if (base == "list") ti.tipo = 8;
  else if (base.rfind("class:", 0) == 0) { ti.tipo = 9; ti.cls = base.substr(6); }
  else if (base.rfind("enum:", 0) == 0) { ti.tipo = 10; ti.cls = base.substr(5); }
  else ti.tipo = 11;
  if (ti.tipo == 8) return ti; // dims de "list:T[..]" pertencem ao elemento
  while (b != std::string::npos) {
    size_t c = s.find(']', b);
    if (c == std::string::npos) break;
    ti.dims.push_back(atoll(s.substr(b + 1, c - b - 1).c_str()));
    b = s.find('[', c);
  }
  return ti;
}

static std::string dimsTypeStr(const std::vector<long long>& dims) {
  std::string t;
  for (size_t k = 0; k < dims.size(); k++) {
    if (k) t += "][";
    t += std::to_string(dims[k]);
  }
  return t;
}

struct DbgLoc {
  std::string name;
  long long offset = 0, flags = 0;
  TipoInfo ti;
};
struct DbgFn {
  std::string name, file;
  std::vector<DbgLoc> locs;
  std::vector<long long> traps;
};
struct DbgGlobal {
  std::string name;
  long long tls = 0;
  bool isArray = false;
  TipoInfo ti;
};
struct DbgField {
  std::string name;
  long long offset = 0; // bytes (múltiplo de 8)
  TipoInfo ti;
};
struct DbgClass {
  std::string name;
  std::vector<DbgField> fields;
};

static std::string hexOf(unsigned long long v);

static unsigned long long hexToULL(const std::string& s);

std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      if (c == 's') out += ' ';
      else if (c == 'n') out += '\n';
      else if (c == 'r') out += '\r';
      else if (c == 't') out += '\t';
      else out += c;
    } else {
      out += s[i];
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
