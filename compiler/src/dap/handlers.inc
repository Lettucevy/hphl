// Handlers DAP
// ---------------------------------------------------------------------------
void DapServer::onInitialize(const JsonValue& id, const JsonValue&) {
  JsonValue c = JsonValue::makeObject();
  c["supportsConfigurationDoneRequest"] = JsonValue::makeBool(true);
  c["supportsEvaluateForHovers"] = JsonValue::makeBool(true);
  c["supportsTerminateRequest"] = JsonValue::makeBool(false);
  c["supportsSetVariable"] = JsonValue::makeBool(true);
  c["exceptionBreakpointFilters"] = JsonValue::makeArray();
  JsonValue result = JsonValue::makeObject();
  result["capabilities"] = c;
  result["supportsMemoryReferences"] = JsonValue::makeBool(false);
  respond(id, result);
}

void DapServer::onLaunch(const JsonValue& id, const JsonValue& params) {
  program_ = params["program"].asString();
  if (program_.empty()) {
    respondError(id, 1, "launch: campo 'program' (caminho do .hphl) ausente");
    return;
  }
  std::string sep = pathSep();
  exePath_ = dirOf(program_) + sep + baseOf(program_) + ".exe";

  // 1) compila com --debug
  JsonValue out = JsonValue::makeObject();
  out["category"] = JsonValue::makeString("console");
  out["output"] = JsonValue::makeString("hphlc: compilando '" + program_ +
                                        "' com instrumentação de debug...\n");
  sendEvent("output", out);

  std::string err;
  if (!compileForDebug(program_, exePath_, err, &enums_)) {
    respondError(id, 2, err);
    return;
  }

  out["output"] = JsonValue::makeString("hphlc: executável '" + exePath_ +
                                        "' gerado\n");
  sendEvent("output", out);

  // 2) spawna + conecta o pipe
  if (!spawnTarget(exePath_, err)) {
    respondError(id, 3, "launch: " + err);
    return;
  }
  if (!handshake(err)) {
    respondError(id, 4, "launch: " + err);
    return;
  }
  launched_ = true;
  respond(id, JsonValue::makeObject());
  sendEvent("initialized", JsonValue::makeObject());
}

static bool dapPathMatches(std::string p1, std::string p2) {
  std::replace(p1.begin(), p1.end(), '/', '\\');
  std::replace(p2.begin(), p2.end(), '/', '\\');
  for (char& c : p1) c = (char)tolower((unsigned char)c);
  for (char& c : p2) c = (char)tolower((unsigned char)c);
  if (p1 == p2) return true;
  if (p1.size() > p2.size() && p1[p1.size() - p2.size() - 1] == '\\' &&
      p1.substr(p1.size() - p2.size()) == p2) return true;
  if (p2.size() > p1.size() && p2[p2.size() - p1.size() - 1] == '\\' &&
      p2.substr(p2.size() - p1.size()) == p1) return true;
  return false;
}

void DapServer::onSetBreakpoints(const JsonValue& id, const JsonValue& params) {
  if (!launched_) {
    respondError(id, 1, "setBreakpoints antes do launch");
    return;
  }
  std::string path = params["source"]["path"].asString();
  if (path.empty()) {
    respondError(id, 2, "setBreakpoints: source sem path");
    return;
  }

  // desativa os breakpoints ativos (só os do arquivo afetado)
  for (auto& b : bps_) {
    long long fnId = b.fnId;
    if (fnId >= 0 && fnId < (long long)fns_.size()) {
      if (dapPathMatches(fns_[(size_t)fnId].file, path)) sendBp(fnId, b.line, 0);
    }
  }

  // resolves: linhas do arquivo que têm trap
  std::map<long long, long long> resolvable; // line → (fnId, line) 1ª fn
  for (size_t i = 0; i < fns_.size(); i++) {
    if (!dapPathMatches(fns_[i].file, path)) continue;
    for (long long t : fns_[i].traps) {
      if (!resolvable.count(t)) resolvable[t] = (long long)i;
    }
  }

  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  const JsonValue& bps = params["breakpoints"];
  for (size_t k = 0; k < bps.size(); k++) {
    long long line0 = bps.at(k)["line"].asInt();
    long long line = line0 + 1;
    JsonValue b = JsonValue::makeObject();
    b["line"] = JsonValue::makeInt(line0);
    auto it = resolvable.find(line);
    if (it == resolvable.end()) {
      // Se a linha clicada não tiver trap direta (ex: declaração/assinatura), tenta a próxima executável próxima (até +3 linhas)
      auto it2 = resolvable.lower_bound(line);
      if (it2 != resolvable.end() && (it2->first - line) <= 3) {
        it = it2;
        line = it->first;
        b["line"] = JsonValue::makeInt(line - 1);
      }
    }
    if (it != resolvable.end()) {
      long long fnId = it->second;
      std::string logMsg = bps.at(k)["logMessage"].asString();
      if (!sendBp(fnId, line, 1, logMsg)) {
        respondError(id, 5, "pipe do runtime fechado");
        return;
      }
      std::string cond = bps.at(k)["condition"].asString();
      std::string hitCond = bps.at(k)["hitCondition"].asString();
      for (auto& bi : bps_) {
        if (bi.fnId == fnId && bi.line == line) {
          bi.cond = cond;
          bi.hitCondition = hitCond;
          bi.logMsg = logMsg;
          bi.hits = 0; // reconfiguração zera o contador
          break;
        }
      }
      b["verified"] = JsonValue::makeBool(true);
    } else {
      b["verified"] = JsonValue::makeBool(false);
    }
    arr.push(b);
  }
  result["breakpoints"] = arr;
  respond(id, result);
}

void DapServer::onConfigurationDone(const JsonValue& id, const JsonValue&) {
  respond(id, JsonValue::makeObject());
}

void DapServer::onDataBreakpointInfo(const JsonValue& id, const JsonValue&) {
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  // locais do frame do topo (só quando parado) + globais
  if (paused_ && !pausedFrames_.empty()) {
    long long fnId = pausedFrames_[0].fnId;
    if (fnId >= 0 && fnId < (long long)fns_.size()) {
      for (const DbgLoc& loc : fns_[(size_t)fnId].locs) {
        JsonValue b = JsonValue::makeObject();
        b["dataId"] = JsonValue::makeString(
            "loc " + std::to_string(fnId) + " " + loc.name);
        b["description"] = JsonValue::makeString("watch de escrita em " + loc.name);
        JsonValue at = JsonValue::makeArray();
        at.push(JsonValue::makeString("write"));
        b["accessTypes"] = at;
        b["canPersist"] = JsonValue::makeBool(false);
        arr.push(b);
      }
    }
  }
  for (const DbgGlobal& g : globals_) {
    JsonValue b = JsonValue::makeObject();
    b["dataId"] = JsonValue::makeString("gbl " + g.name);
    b["description"] = JsonValue::makeString("watch de escrita em " + g.name);
    JsonValue at = JsonValue::makeArray();
    at.push(JsonValue::makeString("write"));
    b["accessTypes"] = at;
    b["canPersist"] = JsonValue::makeBool(false);
    arr.push(b);
  }
  result["dataBreakpoints"] = arr;
  respond(id, result);
}

void DapServer::onSetDataBreakpoints(const JsonValue& id, const JsonValue& params) {
  // o DAP: setDataBreakpoints substitui o conjunto — desativa todos os ativos
  for (auto& w : wps_) {
    if (w.active && sendLine(w.cmdOff)) waitRuntimeLine();
    w.active = false;
    w.runtimeIdx = -1;
  }
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  const JsonValue& list = params["breakpoints"];
  for (size_t i = 0; i < list.size(); i++) {
    std::string dataId = list.at(i)["dataId"].asString();
    // M20-C 8.1: hit condition (opcional, formato ">=N", "==N", "%N")
    std::string hitCond = list.at(i)["hitCondition"].asString("");
    JsonValue b = JsonValue::makeObject();
    std::string cmdOn, desc;
    bool found = false;
    if (dataId.rfind("loc ", 0) == 0) {
      std::string rest = dataId.substr(4);
      size_t sp = rest.find(' ');
      if (sp != std::string::npos) {
        long long fnId = atoll(rest.substr(0, sp).c_str());
        std::string name = rest.substr(sp + 1);
        if (fnId >= 0 && fnId < (long long)fns_.size()) {
          for (const DbgLoc& loc : fns_[(size_t)fnId].locs) {
            if (loc.name == name) {
              cmdOn = "wploc " + std::to_string(fnId) + " " +
                      std::to_string(loc.offset) + " 1";
              desc = "watch de escrita em " + name;
              found = true;
              break;
            }
          }
        }
      }
    } else if (dataId.rfind("gbl ", 0) == 0) {
      std::string name = dataId.substr(4);
      for (const DbgGlobal& g : globals_) {
        if (g.name == name) {
          cmdOn = "wpg " + name + " 1";
          desc = "watch de escrita em " + name;
          found = true;
          break;
        }
      }
    }
    if (found && sendLine(cmdOn)) {
      std::string r = waitRuntimeLine();
      if (r.rfind("ok", 0) == 0) {
        WpState w;
        w.runtimeIdx = r.size() > 3 ? atoi(r.c_str() + 3) : 0;
        w.dataId = dataId;
        w.cmdOff = cmdOn.substr(0, cmdOn.size() - 2) + " 0";
        w.desc = desc;
        w.active = true;
        wps_.push_back(w);
        b["verified"] = JsonValue::makeBool(true);
        // M20-C 8.1: hit condition
        if (!hitCond.empty()) b["hitCondition"] = JsonValue::makeString(hitCond);
      } else {
        b["verified"] = JsonValue::makeBool(false);
        b["message"] = JsonValue::makeString("runtime: " + r);
      }
    } else {
      b["verified"] = JsonValue::makeBool(false);
      b["message"] = JsonValue::makeString("variável não encontrada");
    }
    arr.push(b);
  }
  result["breakpoints"] = arr;
  respond(id, result);
}

void DapServer::onContinue(const JsonValue& id, const JsonValue& params) {
  std::string cmd = "continue";
  long long tid = 0;
  if (params.isObject() && params.has("threadId")) {
    tid = params["threadId"].asInt();
  }
  if (tid > 0 && tid == pausedTid_) cmd += " " + std::to_string(tid);
  if (sendLine(cmd)) {
    paused_ = false;
    pausedFrames_.clear();
    respond(id, JsonValue::makeObject());
  } else {
    respondError(id, 1, "pipe do runtime fechado");
  }
}

void DapServer::onStep(const JsonValue& id, const JsonValue& params,
                       const std::string& cmd) {
  std::string line = cmd;
  long long tid = params["threadId"].asInt();
  if (tid > 0) line += " " + std::to_string(tid);
  if (sendLine(line)) {
    paused_ = false;
    pausedFrames_.clear();
    stepRequested_ = true;
    pauseRequested_ = false;
    respond(id, JsonValue::makeObject());
  } else {
    respondError(id, 1, "pipe do runtime fechado");
  }
}

void DapServer::onPause(const JsonValue& id, const JsonValue& params) {
  std::string cmd = "pause";
  long long tid = params["threadId"].asInt();
  if (tid > 0) cmd += " " + std::to_string(tid);
  if (sendLine(cmd)) {
    pauseRequested_ = true;
    stepRequested_ = false;
    respond(id, JsonValue::makeObject());
  } else {
    respondError(id, 1, "pipe do runtime fechado");
  }
}

void DapServer::onStack(const JsonValue& id, const JsonValue& params) {
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  long long total = 0;
  for (size_t i = 0; i < pausedFrames_.size(); i++) {
    const Frame& f = pausedFrames_[i];
    JsonValue fr = JsonValue::makeObject();
    fr["id"] = JsonValue::makeInt((long long)i);
    std::string name = f.fnId >= 0 && f.fnId < (long long)fns_.size()
                           ? fns_[(size_t)f.fnId].name
                           : "?";
    fr["name"] = JsonValue::makeString(name);
    JsonValue src = JsonValue::makeObject();
    src["path"] = JsonValue::makeString(
        f.fnId >= 0 && f.fnId < (long long)fns_.size() ? fns_[(size_t)f.fnId].file
                                                       : program_);
    fr["source"] = src;
    JsonValue ln = JsonValue::makeObject();
    ln["line"] = JsonValue::makeInt(f.line - 1);
    ln["column"] = JsonValue::makeInt(0);
    JsonValue end = JsonValue::makeObject();
    end["line"] = JsonValue::makeInt(f.line - 1);
    end["column"] = JsonValue::makeInt(0);
    fr["line"] = JsonValue::makeInt(f.line - 1);
    fr["column"] = JsonValue::makeInt(0);
    fr["endLine"] = JsonValue::makeInt(f.line - 1);
    fr["endColumn"] = JsonValue::makeInt(0);
    fr["canRestart"] = JsonValue::makeBool(false);
    arr.push(fr);
    total++;
  }
  result["frames"] = arr;
  result["totalFrames"] = JsonValue::makeInt(total);
  (void)params;
  respond(id, result);
}

void DapServer::onScopes(const JsonValue& id, const JsonValue& params) {
  long long frameId = params["frameId"].asInt();
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  JsonValue loc = JsonValue::makeObject();
  loc["name"] = JsonValue::makeString("Locais");
  loc["variablesReference"] = JsonValue::makeInt(1 + 1000000 * (frameId + 1));
  loc["expensive"] = JsonValue::makeBool(false);
  arr.push(loc);
  JsonValue gbl = JsonValue::makeObject();
  gbl["name"] = JsonValue::makeString("Globais");
  gbl["variablesReference"] = JsonValue::makeInt(2 + 1000000 * (frameId + 1));
  gbl["expensive"] = JsonValue::makeBool(false);
  arr.push(gbl);
  result["scopes"] = arr;
  respond(id, result);
}

void DapServer::onVariables(const JsonValue& id, const JsonValue& params) {
  long long ref = params["variablesReference"].asInt();
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();

  if (ref >= NODE_REF_BASE) {
    // nó de navegação (F1.2/F2.1): array, classe ou list
    size_t idx = (size_t)(ref - NODE_REF_BASE);
    if (idx < varNodes_.size()) {
      if (varNodes_[idx].kind == 1) expandArrayNode(id, idx);
      else if (varNodes_[idx].kind == 2) expandClassNode(id, idx);
      else expandListNode(id, idx);
      return;
    }
    respond(id, result);
    return;
  }

  long long frameId = ref / 1000000 - 1;
  long long kind = ref % 1000000;
  if (frameId < 0) {
    respond(id, result); // scopes inválidos
    return;
  }
  if (frameId >= (long long)pausedFrames_.size()) {
    respond(id, result);
    return;
  }
  const Frame& frame = pausedFrames_[(size_t)frameId];
  long long fnId = frame.fnId;

  if (kind == 1) {
    // locais da função
    if (fnId >= 0 && fnId < (long long)fns_.size()) {
      for (const DbgLoc& loc : fns_[(size_t)fnId].locs) {
        std::string rvar;
        cmdRead(fnId, frame.rbp, loc.name, rvar);
        arr.push(variableFor(loc.name, loc.ti, rvar, true, fnId, frame.rbp));
      }
    }
  } else if (kind == 2) {
    // globais
    for (const DbgGlobal& g : globals_) {
      std::string rvar;
      cmdReadGbl(g.name, rvar);
      arr.push(variableFor(g.name, g.ti, rvar, false, -1, ""));
    }
  }
  result["variables"] = arr;
  respond(id, result);
}

// converte o texto do cliente (ex.: "42", "1.5", "true", "'x'") nos 8 bytes
// do slot da variável; só escalares (int/uint/float/bool/char)
static unsigned long long parseScalarValue(long long tipo, const std::string& text,
                                           bool& ok) {
  ok = true;
  switch (tipo) {
    case 1: return (unsigned long long)strtoll(text.c_str(), nullptr, 10);
    case 2: return strtoull(text.c_str(), nullptr, 10);
    case 3: {
      double d = strtod(text.c_str(), nullptr);
      unsigned long long bits;
      memcpy(&bits, &d, 8);
      return bits;
    }
    case 4: {
      std::string t = text;
      std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
      });
      if (t == "true" || t == "1") return 1;
      if (t == "false" || t == "0") return 0;
      ok = false;
      return 0;
    }
    case 5: {
      if (text.size() == 3 && text[0] == '\'' && text[2] == '\'')
        return (unsigned char)text[1];
      if (text.size() == 4 && text[0] == '\'' && text[3] == '\'') {
        if (text[1] == '\\' && text[2] == 'n') return '\n';
        if (text[1] == '\\' && text[2] == 't') return '\t';
        if (text[1] == '\\' && text[2] == '\\') return '\\';
        if (text[1] == '\\' && text[2] == '\'') return '\'';
        if (text[1] == '\\' && text[2] == '0') return 0;
      }
      return (unsigned long long)strtoll(text.c_str(), nullptr, 10);
    }
    default:
      ok = false;
      return 0;
  }
}

void DapServer::onSetVariable(const JsonValue& id, const JsonValue& params) {
  long long ref = params["variablesReference"].asInt();
  std::string name = params["name"].asString();
  std::string text = params["value"].asString();
  if (name.empty()) {
    respondError(id, 5, "setVariable: nome vazio");
    return;
  }
  long long frameId = ref / 1000000 - 1;
  long long kind = ref % 1000000;
  bool found = false;

  if (kind == 1 && frameId >= 0 &&
      frameId < (long long)pausedFrames_.size()) {
    const Frame& frame = pausedFrames_[(size_t)frameId];
    long long fnId = frame.fnId;
    if (fnId >= 0 && fnId < (long long)fns_.size()) {
      for (const DbgLoc& loc : fns_[(size_t)fnId].locs) {
        if (loc.name != name) continue;
        found = true;
        if (loc.ti.tipo < 1 || loc.ti.tipo > 5) break;
        bool ok = false;
        unsigned long long bits = parseScalarValue(loc.ti.tipo, text, ok);
        if (!ok) {
          respondError(id, 6, "valor inválido para '" + name + "'");
          return;
        }
        if (!cmdWrite(fnId, frame.rbp, name, bits)) {
          respondError(id, 7, "não foi possível gravar '" + name + "'");
          return;
        }
        std::string rvar;
        cmdRead(fnId, frame.rbp, name, rvar);
        JsonValue result = JsonValue::makeObject();
        result["value"] =
            JsonValue::makeString(formatRaw(loc.ti.tipo, rvar));
        result["variablesReference"] = JsonValue::makeInt(0);
        respond(id, result);
        return;
      }
    }
  } else if (kind == 2) {
    for (const DbgGlobal& g : globals_) {
      if (g.name != name) continue;
      found = true;
      if (g.ti.tipo < 1 || g.ti.tipo > 5) break;
      bool ok = false;
      unsigned long long bits = parseScalarValue(g.ti.tipo, text, ok);
      if (!ok) {
        respondError(id, 6, "valor inválido para '" + name + "'");
        return;
      }
      if (!cmdWriteGbl(name, bits)) {
        respondError(id, 7, "não foi possível gravar '" + name + "'");
        return;
      }
      std::string rvar;
      cmdReadGbl(name, rvar);
      JsonValue result = JsonValue::makeObject();
      result["value"] =
          JsonValue::makeString(formatRaw(g.ti.tipo, rvar));
      result["variablesReference"] = JsonValue::makeInt(0);
      respond(id, result);
      return;
    }
  }
  if (!found)
    respondError(id, 4, "variável '" + name + "' não encontrada");
  else
    respondError(id, 8,
                 "setVariable suporta apenas escalares (int/uint/float/bool/char)");
}

// evaluate v1: nomes + literais + aritmética simples (int/float/bool/string)
class EvalParser {
public:
  EvalParser(DapServer* srv, const std::string& src) : srv_(srv), src_(src) {}
  EvalVal parse() {
    EvalVal v = andOr();
    if (!v.ok) return v;
    skipWs();
    if (pos_ < src_.size()) {
      v.ok = false;
      v.err = "expressão não suportada (v1: nomes, literais, + - * / %, comparações e &&/||)";
    }
    return v;
  }

private:
  DapServer* srv_;
  const std::string& src_;
  size_t pos_ = 0;

  void skipWs() {
    while (pos_ < src_.size() &&
           (src_[pos_] == ' ' || src_[pos_] == '\t'))
      pos_++;
  }

  EvalVal err(const std::string& m) {
    EvalVal v;
    v.ok = false;
    v.err = m;
    return v;
  }

  // &&/|| (resultado booleano)
  EvalVal andOr() {
    EvalVal a = cmp();
    if (!a.ok) return a;
    while (true) {
      skipWs();
      std::string op;
      if (pos_ + 1 < src_.size() && src_[pos_] == '&' && src_[pos_ + 1] == '&') {
        op = "&&";
        pos_ += 2;
      } else if (pos_ + 1 < src_.size() && src_[pos_] == '|' &&
                 src_[pos_ + 1] == '|') {
        op = "||";
        pos_ += 2;
      } else {
        return a;
      }
      EvalVal b = cmp();
      if (!b.ok) return b;
      auto asBool = [](const EvalVal& v, bool& out) {
        switch (v.kind) {
          case EvalVal::Bool: out = v.b; return true;
          case EvalVal::Int: out = v.i != 0; return true;
          case EvalVal::UInt: out = (unsigned long long)v.i != 0; return true;
          case EvalVal::Float: out = v.f != 0; return true;
          default: return false;
        }
      };
      bool x, y;
      if (!asBool(a, x) || !asBool(b, y))
        return err("'" + op + "' exige operandos numéricos/booleanos");
      a.kind = EvalVal::Bool;
      a.b = op == "&&" ? (x && y) : (x || y);
      a.display.clear();
    }
  }

  // == != < > <= >= (resultado booleano)
  EvalVal cmp() {
    EvalVal a = expr();
    if (!a.ok) return a;
    while (true) {
      skipWs();
      std::string op;
      if (pos_ + 1 < src_.size()) {
        if (src_[pos_] == '=' && src_[pos_ + 1] == '=') { op = "=="; pos_ += 2; }
        else if (src_[pos_] == '!' && src_[pos_ + 1] == '=') { op = "!="; pos_ += 2; }
        else if (src_[pos_] == '<' && src_[pos_ + 1] == '=') { op = "<="; pos_ += 2; }
        else if (src_[pos_] == '>' && src_[pos_ + 1] == '=') { op = ">="; pos_ += 2; }
      }
      if (op.empty() && pos_ < src_.size()) {
        if (src_[pos_] == '<') { op = "<"; pos_++; }
        else if (src_[pos_] == '>') { op = ">"; pos_++; }
      }
      if (op.empty()) return a;
      EvalVal b = expr();
      if (!b.ok) return b;
      auto asNum = [](const EvalVal& v, long double& n) {
        switch (v.kind) {
          case EvalVal::Int: n = (long double)v.i; return true;
          case EvalVal::UInt: n = (long double)(unsigned long long)v.i; return true;
          case EvalVal::Float: n = v.f; return true;
          case EvalVal::Bool: n = v.b ? 1 : 0; return true;
          default: return false;
        }
      };
      long double x, y;
      if (!asNum(a, x)) return err("comparação com string não suportada");
      if (!asNum(b, y)) return err("comparação com string não suportada");
      a.kind = EvalVal::Bool;
      a.b = op == "==" ? (x == y) : op == "!=" ? (x != y) : op == "<" ? (x < y)
            : op == ">" ? (x > y) : op == "<=" ? (x <= y) : (x >= y);
      a.display.clear();
    }
  }

  EvalVal expr() {
    EvalVal a = term();
    if (!a.ok) return a;
    while (true) {
      skipWs();
      if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) {
        char op = src_[pos_++];
        EvalVal b = term();
        if (!b.ok) return b;
        if (a.kind == EvalVal::Str || b.kind == EvalVal::Str) {
          if (op == '+' && a.kind == EvalVal::Str && b.kind == EvalVal::Str) {
            a.s = a.s + b.s;
            a.kind = EvalVal::Str;
            a.display.clear();
            continue;
          }
          return err("'+' entre strings é o único caso com string");
        }
        if (a.kind == EvalVal::Float || b.kind == EvalVal::Float) {
          double x = a.kind == EvalVal::Float ? a.f : (double)a.i;
          double y = b.kind == EvalVal::Float ? b.f : (double)b.i;
          if (b.kind == EvalVal::Bool) y = b.b ? 1 : 0;
          if (a.kind == EvalVal::Bool) x = a.b ? 1 : 0;
          a.kind = EvalVal::Float;
          a.f = op == '+' ? x + y : x - y;
          a.display.clear();
          continue;
        }
        if (a.kind == EvalVal::Bool || b.kind == EvalVal::Bool) {
          long long x = a.kind == EvalVal::Bool ? (a.b ? 1 : 0) : a.i;
          long long y = b.kind == EvalVal::Bool ? (b.b ? 1 : 0) : b.i;
          a.kind = EvalVal::Int;
          a.i = op == '+' ? x + y : x - y;
          a.display.clear();
          continue;
        }
        if (a.kind == EvalVal::UInt || b.kind == EvalVal::UInt) {
          unsigned long long x = a.kind == EvalVal::UInt ? (unsigned long long)a.i : (unsigned long long)(long long)a.i;
          unsigned long long y = b.kind == EvalVal::UInt ? (unsigned long long)b.i : (unsigned long long)(long long)b.i;
          a.kind = EvalVal::UInt;
          a.i = (long long)(op == '+' ? x + y : x - y);
          a.display.clear();
          continue;
        }
        a.i = op == '+' ? a.i + b.i : a.i - b.i;
        a.display.clear();
        continue;
      }
      break;
    }
    return a;
  }

  EvalVal term() {
    EvalVal a = factor();
    if (!a.ok) return a;
    while (true) {
      skipWs();
      if (pos_ < src_.size() &&
          (src_[pos_] == '*' || src_[pos_] == '/' || src_[pos_] == '%')) {
        char op = src_[pos_++];
        EvalVal b = factor();
        if (!b.ok) return b;
        if (a.kind == EvalVal::Float || b.kind == EvalVal::Float) {
          double x = a.kind == EvalVal::Float ? a.f : (double)a.i;
          double y = b.kind == EvalVal::Float ? b.f : (double)b.i;
          if (a.kind == EvalVal::Bool) x = a.b ? 1 : 0;
          if (b.kind == EvalVal::Bool) y = b.b ? 1 : 0;
          if (op == '/' && y == 0) return err("divisão por zero");
          a.kind = EvalVal::Float;
          a.f = op == '*' ? x * y : x / y;
          if (op == '%') a.f = std::fmod(x, y);
          a.display.clear();
          continue;
        }
        long long x = a.kind == EvalVal::Bool ? (a.b ? 1 : 0) : a.i;
        long long y = b.kind == EvalVal::Bool ? (b.b ? 1 : 0) : b.i;
        if (op == '/' && y == 0) return err("divisão por zero");
        if (op == '%' && y == 0) return err("divisão por zero");
        a.kind = EvalVal::Int;
        a.i = op == '*' ? x * y : op == '/' ? x / y : x % y;
        a.display.clear();
        continue;
      }
      break;
    }
    return a;
  }

  EvalVal factor() {
    skipWs();
    if (pos_ >= src_.size()) return err("expressão vazia");
    char c = src_[pos_];
    if (c == '(') {
      pos_++;
      EvalVal v = expr();
      skipWs();
      if (pos_ < src_.size() && src_[pos_] == ')') pos_++;
      else return err("')' esperado");
      return v;
    }
    if (c == '-') {
      pos_++;
      EvalVal v = factor();
      if (!v.ok) return v;
      if (v.kind == EvalVal::Float) v.f = -v.f;
      else v.i = -v.i;
      v.display.clear();
      return v;
    }
    if (c == '"' || c == '\'') {
      char q = c;
      pos_++;
      std::string s;
      while (pos_ < src_.size() && src_[pos_] != q) {
        if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
          char e = src_[pos_ + 1];
          pos_ += 2;
          if (e == 'n') s += '\n';
          else if (e == 't') s += '\t';
          else if (e == 'r') s += '\r';
          else if (e == '\\') s += '\\';
          else if (e == '"') s += '"';
          else if (e == '\'') s += '\'';
          else s += e;
        } else {
          s += src_[pos_++];
        }
      }
      if (pos_ >= src_.size()) return err("string sem fechamento");
      pos_++;
      EvalVal v;
      v.kind = EvalVal::Str;
      v.s = s;
      v.ok = true;
      return v;
    }
    if (std::isdigit((unsigned char)c) ||
        (c == '0' && pos_ + 1 < src_.size() && src_[pos_ + 1] == 'x')) {
      return number();
    }
    if (std::isalpha((unsigned char)c) || c == '_') {
      size_t s = pos_;
      while (pos_ < src_.size() &&
             (std::isalnum((unsigned char)src_[pos_]) || src_[pos_] == '_'))
        pos_++;
      std::string name = src_.substr(s, pos_ - s);
      if (name == "true" || name == "false") {
        EvalVal v;
        v.kind = EvalVal::Bool;
        v.b = name == "true";
        v.ok = true;
        return v;
      }
      return srv_->evalName(name);
    }
    return err("símbolo inesperado");
  }

  EvalVal number() {
    size_t s = pos_;
    bool isFloat = false;
    if (pos_ + 1 < src_.size() && src_[pos_] == '0' && src_[pos_ + 1] == 'x') {
      pos_ += 2;
      while (pos_ < src_.size() &&
             std::isxdigit((unsigned char)src_[pos_]))
        pos_++;
      std::string hx = src_.substr(s + 2, pos_ - s - 2);
      if (hx.empty()) return err("número hexadecimal inválido");
      EvalVal v;
      v.kind = EvalVal::UInt;
      v.i = (long long)strtoull(hx.c_str(), nullptr, 16);
      v.ok = true;
      return v;
    }
    while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_]))
      pos_++;
    if (pos_ < src_.size() && src_[pos_] == '.') {
      isFloat = true;
      pos_++;
      while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_]))
        pos_++;
    }
    if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
      isFloat = true;
      pos_++;
      if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-'))
        pos_++;
      while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_]))
        pos_++;
    }
    std::string num = src_.substr(s, pos_ - s);
    EvalVal v;
    v.ok = true;
    if (isFloat) {
      v.kind = EvalVal::Float;
      v.f = std::atof(num.c_str());
    } else {
      v.kind = EvalVal::Int;
      v.i = std::atoll(num.c_str());
    }
    return v;
  }
};

// resolve um nome no frame de topo (local → global)
EvalVal DapServer::evalName(const std::string& name) {
  EvalVal v;
  v.ok = true;
  if (!paused_ || pausedFrames_.empty()) {
    v.ok = false;
    v.err = "não há execução parada";
    return v;
  }
  const Frame& frame = pausedFrames_[0];
  long long fnId = frame.fnId;
  if (fnId >= 0 && fnId < (long long)fns_.size()) {
    for (const DbgLoc& loc : fns_[(size_t)fnId].locs) {
      if (loc.name != name) continue;
      std::string rvar;
      if (!cmdRead(fnId, frame.rbp, name, rvar)) {
        v.ok = false;
        v.err = "não foi possível ler '" + name + "'";
        return v;
      }
      if (loc.ti.tipo == 6) {
        std::string content;
        if (rvar != "none" && cmdReadStr(rvar, content)) {
          v.kind = EvalVal::Str;
          v.s = content;
          return v;
        }
      } else if (loc.ti.tipo == 4) {
        v.kind = EvalVal::Bool;
        v.b = rvar != "none" && strtoull(rvar.c_str(), nullptr, 16) != 0;
        return v;
      } else if (loc.ti.tipo == 3) {
        long long bits = strtoull(rvar.c_str(), nullptr, 16);
        memcpy(&v.f, &bits, 8);
        v.kind = EvalVal::Float;
        return v;
      } else if (loc.ti.tipo == 2) {
        v.kind = EvalVal::UInt;
        v.i = (long long)strtoull(rvar.c_str(), nullptr, 16);
        return v;
      } else {
        v.kind = EvalVal::Int;
        v.i = (long long)strtoull(rvar.c_str(), nullptr, 16);
        if (loc.ti.tipo == 10) v.display = enumVariantName(loc.ti.cls, v.i);
        return v;
      }
    }
  }
  for (const DbgGlobal& g : globals_) {
    if (g.name != name) continue;
    std::string rvar;
    if (!cmdReadGbl(name, rvar)) {
      v.ok = false;
      v.err = "não foi possível ler '" + name + "'";
      return v;
    }
    if (g.ti.tipo == 6) {
      std::string content;
      if (rvar != "none" && cmdReadStr(rvar, content)) {
        v.kind = EvalVal::Str;
        v.s = content;
        return v;
      }
    } else if (g.ti.tipo == 4) {
      v.kind = EvalVal::Bool;
      v.b = rvar != "none" && strtoull(rvar.c_str(), nullptr, 16) != 0;
      return v;
    } else if (g.ti.tipo == 3) {
      long long bits = strtoull(rvar.c_str(), nullptr, 16);
      memcpy(&v.f, &bits, 8);
      v.kind = EvalVal::Float;
      return v;
    } else if (g.ti.tipo == 2) {
      v.kind = EvalVal::UInt;
      v.i = (long long)strtoull(rvar.c_str(), nullptr, 16);
      return v;
    } else {
      v.kind = EvalVal::Int;
      v.i = (long long)strtoull(rvar.c_str(), nullptr, 16);
      if (g.ti.tipo == 10) v.display = enumVariantName(g.ti.cls, v.i);
      return v;
    }
  }
  v.ok = false;
  v.err = "'" + name + "' não é uma variável visível";
  return v;
}

bool DapServer::evaluateExpr(const std::string& expr, JsonValue& out) {
  EvalParser p(this, expr);
  EvalVal v = p.parse();
  if (!v.ok) {
    out["error"] = JsonValue::makeString(v.err);
    return false;
  }
  std::string text;
  if (!v.display.empty()) {
    text = v.display; // F2.2: enum simples → nome da variante
  } else switch (v.kind) {
    case EvalVal::Int: text = std::to_string(v.i); break;
    case EvalVal::UInt: text = std::to_string((unsigned long long)v.i); break;
    case EvalVal::Float: {
      char buf[64];
      snprintf(buf, sizeof buf, "%.6g", v.f);
      text = buf;
      break;
    }
    case EvalVal::Bool: text = v.b ? "true" : "false"; break;
    case EvalVal::Str: text = "\"" + v.s + "\""; break;
  }
  out["result"] = JsonValue::makeString(text);
  out["variablesReference"] = JsonValue::makeInt(0);
  return true;
}

// 1 = condição verdadeira, 0 = falsa, -1 = erro (errDesc preenchido)
int DapServer::evalCondition(const std::string& cond, std::string& errDesc) {
  JsonValue out = JsonValue::makeObject();
  if (!evaluateExpr(cond, out)) {
    errDesc = out["error"].asString();
    if (errDesc.empty()) errDesc = "expressão inválida";
    return -1;
  }
  std::string r = out["result"].asString();
  if (r == "true" || r == "1") return 1;
  if (r == "false" || r == "0") return 0;
  errDesc = "resultado não é booleano ('" + r + "')";
  return -1;
}

void DapServer::onEvaluate(const JsonValue& id, const JsonValue& params) {
  JsonValue result = JsonValue::makeObject();
  if (evaluateExpr(params["expression"].asString(), result))
    respond(id, result);
  else
    respondError(id, 5, result["error"].asString());
}

void DapServer::onThreads(const JsonValue& id, const JsonValue&) {
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  if (sendLine("threads")) {
    // F2.5: "threads <n>" + n linhas "thread <tid> <nome>" + "threadsend"
    std::string first = waitRuntimeLine();
    long long n = 0;
    sscanf(first.c_str(), "threads %lld", &n);
    for (long long k = 0; k < n; k++) {
      std::string tl = waitRuntimeLine();
      long long tid = 0;
      char name[256];
      if (sscanf(tl.c_str(), "thread %lld %255s", &tid, name) == 2) {
        std::string nm = name;
        std::string unescaped;
        for (size_t i = 0; i < nm.size(); i++) {
          if (nm[i] == '\\' && i + 1 < nm.size()) {
            char nxt = nm[i + 1];
            if (nxt == 's') { unescaped += ' '; i++; continue; }
            if (nxt == 'n') { unescaped += '\n'; i++; continue; }
            if (nxt == 't') { unescaped += '\t'; i++; continue; }
            if (nxt == 'r') { unescaped += '\r'; i++; continue; }
            if (nxt == '\\') { unescaped += '\\'; i++; continue; }
          }
          unescaped += nm[i];
        }
        JsonValue t = JsonValue::makeObject();
        t["id"] = JsonValue::makeInt(tid);
        t["name"] = JsonValue::makeString(unescaped);
        arr.push(t);
      }
    }
    waitRuntimeLine(); // threadsend
  } else {
    JsonValue t = JsonValue::makeObject();
    t["id"] = JsonValue::makeInt(pausedTid_);
    t["name"] = JsonValue::makeString("main");
    arr.push(t);
  }
  result["threads"] = arr;
  respond(id, result);
}

void DapServer::onSource(const JsonValue& id, const JsonValue& params) {
  std::string path = params["source"]["path"].asString();
  if (path.empty()) path = params["source"]["name"].asString();
  JsonValue result = JsonValue::makeObject();
  result["content"] = JsonValue::makeString(readFile(path));
  respond(id, result);
}

void DapServer::onDisconnect(const JsonValue& id, const JsonValue&) {
  if (launched_) {
    launched_ = false;
    closeRuntime();
  }
  respond(id, JsonValue::makeObject());
  quit_ = true;
}

void DapServer::onExceptionInfo(const JsonValue& id, const JsonValue&) {
  JsonValue r = JsonValue::makeObject();
  if (lastExcCod_ >= 0) {
    r["exceptionId"] = JsonValue::makeString("hphl:" + std::to_string(lastExcCod_));
    r["description"] = JsonValue::makeString(lastExcMsg_);
    r["breakMode"] = JsonValue::makeString("always");
    JsonValue det = JsonValue::makeObject();
    det["formattedDescription"] = JsonValue::makeString(lastExcMsg_);
    det["label"] = JsonValue::makeString("hphl:" + std::to_string(lastExcCod_));
    r["details"] = det;
  } else {
    r["exceptionId"] = JsonValue::makeString("");
    r["description"] = JsonValue::makeString("");
  }
  respond(id, r);
}

void DapServer::onFunctionBreakpoints(const JsonValue& id, const JsonValue& params) {
  if (!launched_) { respondError(id, 1, "setFunctionBreakpoints antes do launch"); return; }
  // desativa os anteriores
  for (auto &fb : fnBps_) if (fb.active) sendBp(fb.fnId, fb.line, 0);
  fnBps_.clear();
  JsonValue result = JsonValue::makeObject();
  JsonValue arr = JsonValue::makeArray();
  const JsonValue& bps = params["breakpoints"];
  for (size_t k = 0; k < bps.size(); k++) {
    std::string name = bps.at(k)["name"].asString();
    JsonValue b = JsonValue::makeObject();
    long long fnId = -1;
    for (size_t i = 0; i < fns_.size(); i++) if (fns_[i].name == name) { fnId = (long long)i; break; }
    // tenta match parcial após último ponto (ex.: "Pessoa.Pessoa" vs "main.Pessoa.Pessoa")
    if (fnId < 0) for (size_t i = 0; i < fns_.size(); i++) {
      const std::string &fn = fns_[i].name;
      if (fn.size() >= name.size() && fn.compare(fn.size()-name.size(), name.size(), name)==0) { fnId = (long long)i; break; }
    }
    if (fnId >= 0 && !fns_[(size_t)fnId].traps.empty()) {
      long long line = fns_[(size_t)fnId].traps[0];
      if (!sendBp(fnId, line, 1)) { respondError(id, 5, "pipe do runtime fechado"); return; }
      FnBpState fb; fb.name = name; fb.fnId = fnId; fb.line = line; fb.active = true;
      fnBps_.push_back(fb);
      b["verified"] = JsonValue::makeBool(true);
    } else {
      b["verified"] = JsonValue::makeBool(false);
      b["message"] = JsonValue::makeString("função não encontrada");
    }
    arr.push(b);
  }
  result["breakpoints"] = arr;
  respond(id, result);
}

void DapServer::onStdin(const JsonValue& id, const JsonValue& params) {
  std::string text = params["text"].asString();
  if (inPipe_ != INVALID_HANDLE_VALUE && !text.empty()) {
    DWORD written = 0;
    WriteFile(inPipe_, text.data(), (DWORD)text.size(), &written, nullptr);
    FlushFileBuffers(inPipe_);
  }
  respond(id, JsonValue::makeObject());
}
