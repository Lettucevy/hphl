// ---------------------------------------------------------------------------
// Features
// ---------------------------------------------------------------------------
std::string LspServer::hoverFor(const std::string& name) {
  Semantic* sem = an_.semantic.get();
  if (!sem) return "";

  if (name == "print" || name == "println")
    return "builtin\n\n`" + name +
           "(a, b, ...)` imprime os valores (concatenação com coerção; "
           "println acrescenta uma quebra de linha)";
  if (name == "toString")
    return "builtin\n\n`toString(x)` converte int, uint, float, bool ou char "
           "em string";
  if (name == "clock_ns")
    return "builtin\n\n`clock_ns()` relógio de alta resolução em "
           "nanossegundos (QPC)";
  if (isPrimitiveTypeName(name)) return "tipo primitivo `" + name + "`";

  // funções e métodos
  for (auto& fi : sem->functions()) {
    if (fi.decl->name != name) continue;
    std::string s = "`" + signature(fi.decl) + "`";
    if (!fi.moduleName.empty() && fi.moduleName != "main")
      s += "\n\nmódulo: " + fi.moduleName;
    s += "\n" + fi.decl->filePath + ":" + std::to_string(fi.decl->line);
    return s;
  }

  // classes / structs / interfaces / actors (nome simples ou canônico)
  const ClassInfo* ci = nullptr;
  auto it = sem->classes().find(name);
  if (it != sem->classes().end()) {
    ci = &it->second;
  } else {
    std::string suffix = "." + name;
    for (auto& kv : sem->classes()) {
      if (kv.first.size() > suffix.size() &&
          kv.first.compare(kv.first.size() - suffix.size(), suffix.size(),
                           suffix) == 0) {
        ci = &kv.second;
        break;
      }
    }
  }
  if (ci) {
    ClassDecl* cd = ci->decl;
    std::string kind = cd->isStruct ? "struct" : cd->isInterface ? "interface"
                                   : cd->isActor  ? "actor"
                                                  : "class";
    std::string s = "`" + kind + " " + cd->name + "`";
    if (!cd->base.empty()) s += " : " + cd->base;
    s += "\n\n" + std::to_string(cd->fields.size()) + " campo(s), " +
         std::to_string(cd->methods.size()) + " método(s)";
    s += "\n" + cd->filePath + ":" + std::to_string(cd->line);
    return s;
  }

  // enums
  const EnumDecl* en = nullptr;
  auto eit = sem->enums().find(name);
  if (eit != sem->enums().end()) {
    en = eit->second;
  } else {
    std::string suffix = "." + name;
    for (auto& kv : sem->enums()) {
      if (kv.first.size() > suffix.size() &&
          kv.first.compare(kv.first.size() - suffix.size(), suffix.size(),
                           suffix) == 0) {
        en = kv.second;
        break;
      }
    }
  }
  if (en) {
    std::string s = "`enum " + en->name + "` — " +
                    std::to_string(en->entries.size()) + " variante(s)";
    if (en->anyPayload) s += " (com dados)";
    s += "\n" + en->filePath + ":" + std::to_string(en->line);
    return s;
  }

  // variáveis globais
  for (auto* g : sem->globals()) {
    if (g->name != name) continue;
    std::string s = std::string(g->isConst ? "const " : "variável global ") +
                    typeName(g->type) + " " + g->name;
    s += "\n" + g->filePath + ":" + std::to_string(g->line);
    return s;
  }
  return "";
}

JsonValue LspServer::hover(const JsonValue& params) {
  std::string uri = params["textDocument"]["uri"].asString();
  std::string path = uriToPath(uri);
  ensureAnalysisFor(path);
  if (!an_.semantic) return JsonValue();

  const JsonValue& pos = params["position"];
  int line0 = (int)pos["line"].asInt();
  int char0 = (int)pos["character"].asInt();
  std::string line = lineAt(docText(path), line0);
  int byteCol = utf16ToByte(line, char0);
  Word w = wordAt(line, byteCol);
  if (w.text.empty()) return JsonValue();

  std::string content = hoverFor(w.text);
  if (content.empty()) return JsonValue();

  JsonValue result = JsonValue::makeObject();
  result["contents"] = JsonValue::makeObject();
  result["contents"]["kind"] = JsonValue::makeString("markdown");
  result["contents"]["value"] = JsonValue::makeString(content);
  result["range"] = JsonValue::makeObject();
  result["range"]["start"] = JsonValue::makeObject();
  result["range"]["start"]["line"] = JsonValue::makeInt(line0);
  result["range"]["start"]["character"] =
      JsonValue::makeInt(byteToUtf16(line, w.byteStart));
  result["range"]["end"] = JsonValue::makeObject();
  result["range"]["end"]["line"] = JsonValue::makeInt(line0);
  result["range"]["end"]["character"] =
      JsonValue::makeInt(byteToUtf16(line, w.byteEnd));
  return result;
}

JsonValue LspServer::locationFor(const Decl* d, const std::string& name) {
  JsonValue loc = JsonValue::makeObject();
  loc["uri"] = JsonValue::makeString(getUriForPath(d->filePath));
  std::string line = lineAt(docText(d->filePath), d->line - 1);
  // coluna do nome na linha (busca a 1ª ocorrência como identificador)
  int col = -1;
  for (int i = 0; i + (int)name.size() <= (int)line.size(); i++) {
    if (line.compare(i, name.size(), name) != 0) continue;
    bool leftOk = i == 0 || !(std::isalnum((unsigned char)line[i - 1]) ||
                              line[i - 1] == '_');
    int e = i + (int)name.size();
    bool rightOk = e >= (int)line.size() ||
                   !(std::isalnum((unsigned char)line[e]) || line[e] == '_');
    if (leftOk && rightOk) { col = i; break; }
  }
  if (col < 0) col = 0;
  JsonValue start = JsonValue::makeObject();
  start["line"] = JsonValue::makeInt(d->line - 1);
  start["character"] = JsonValue::makeInt(byteToUtf16(line, col));
  JsonValue end = JsonValue::makeObject();
  end["line"] = JsonValue::makeInt(d->line - 1);
  end["character"] =
      JsonValue::makeInt(byteToUtf16(line, col + (int)name.size()));
  loc["range"] = JsonValue::makeObject();
  loc["range"]["start"] = start;
  loc["range"]["end"] = end;
  return loc;
}

JsonValue LspServer::definition(const JsonValue& params) {
  std::string uri = params["textDocument"]["uri"].asString();
  std::string path = uriToPath(uri);
  ensureAnalysisFor(path);
  if (!an_.semantic) return JsonValue();

  const JsonValue& pos = params["position"];
  int line0 = (int)pos["line"].asInt();
  int char0 = (int)pos["character"].asInt();
  std::string line = lineAt(docText(path), line0);
  Word w = wordAt(line, utf16ToByte(line, char0));
  if (w.text.empty()) return JsonValue();
  std::string name = w.text;
  if (isPrimitiveTypeName(name)) return JsonValue();

  Semantic* sem = an_.semantic.get();
  std::string canon = canonicalPath(path);

  // M20-C 9.1: detectar padrão `obj.method` (member access) para resolver
  // método para `ClassName.method` em vez de só `method` global
  std::string memberAccess;
  {
    size_t dotPos = line.rfind('.', (size_t)char0);
    if (dotPos != std::string::npos && dotPos > 0) {
      // pega identifier imediatamente antes do '.'
      size_t s = dotPos;
      while (s > 0 && (isalnum((unsigned char)line[s-1]) || line[s-1] == '_')) s--;
      if (s < dotPos) {
        std::string objName = line.substr(s, dotPos - s);
        // resolve tipo do objeto via tabela de globals/locals
        // (simplificado: só globals — locals precisaria de scope tracking)
        const VarDecl* objVar = nullptr;
        for (auto* g : sem->globals()) {
          if (g->name == objName && g->type.kind == Type::Kind::Class) { objVar = g; break; }
        }
        if (objVar) {
          auto cit = sem->classes().find(objVar->type.name);
          if (cit != sem->classes().end()) {
            for (auto* m : cit->second.methods) {
              if (m->name == name) return locationFor(m, name);
            }
          }
        }
      }
    }
  }

  // preferência: declaração no próprio arquivo
  for (auto& fi : sem->functions()) {
    if (fi.decl->name == name && canonicalPath(fi.decl->filePath) == canon)
      return locationFor(fi.decl, name);
  }
  for (auto& kv : sem->classes()) {
    if (kv.second.decl->name == name && canonicalPath(kv.second.decl->filePath) == canon)
      return locationFor(kv.second.decl, name);
  }
  for (auto& kv : sem->enums()) {
    if (kv.second->name == name && canonicalPath(kv.second->filePath) == canon)
      return locationFor(kv.second, name);
  }
  for (auto* g : sem->globals()) {
    if (g->name == name && canonicalPath(g->filePath) == canon)
      return locationFor(g, name);
  }
  // senão: primeira declaração encontrada
  for (auto& fi : sem->functions())
    if (fi.decl->name == name) return locationFor(fi.decl, name);
  for (auto& kv : sem->classes())
    if (kv.second.decl->name == name) return locationFor(kv.second.decl, name);
  for (auto& kv : sem->enums())
    if (kv.second->name == name) return locationFor(kv.second, name);
  for (auto* g : sem->globals())
    if (g->name == name) return locationFor(g, name);
  return JsonValue();
}

JsonValue LspServer::symbolEntry(const std::string& name, int kind,
                                 const Decl* d) {
  JsonValue s = JsonValue::makeObject();
  s["name"] = JsonValue::makeString(name);
  s["kind"] = JsonValue::makeInt(kind);
  std::string line = lineAt(docText(d->filePath), d->line - 1);
  int col = -1;
  for (int i = 0; i + (int)name.size() <= (int)line.size(); i++) {
    if (line.compare(i, name.size(), name) != 0) continue;
    bool leftOk = i == 0 || !(std::isalnum((unsigned char)line[i - 1]) ||
                              line[i - 1] == '_');
    int e = i + (int)name.size();
    bool rightOk = e >= (int)line.size() ||
                   !(std::isalnum((unsigned char)line[e]) || line[e] == '_');
    if (leftOk && rightOk) { col = i; break; }
  }
  if (col < 0) col = 0;
  JsonValue start = JsonValue::makeObject();
  start["line"] = JsonValue::makeInt(d->line - 1);
  start["character"] = JsonValue::makeInt(byteToUtf16(line, col));
  JsonValue end = JsonValue::makeObject();
  end["line"] = JsonValue::makeInt(d->line - 1);
  end["character"] =
      JsonValue::makeInt(byteToUtf16(line, col + (int)name.size()));
  s["range"] = JsonValue::makeObject();
  s["range"]["start"] = start;
  s["range"]["end"] = end;
  s["selectionRange"] = s["range"];
  return s;
}

JsonValue LspServer::documentSymbol(const JsonValue& params) {
  std::string uri = params["textDocument"]["uri"].asString();
  std::string path = uriToPath(uri);
  ensureAnalysisFor(path);
  if (!an_.semantic) return JsonValue();
  Semantic* sem = an_.semantic.get();
  std::string canon = canonicalPath(path);

  JsonValue arr = JsonValue::makeArray();
  for (auto& fi : sem->functions()) {
    if (fi.decl->isMethod || fi.decl->isConstructor) continue;
    if (canonicalPath(fi.decl->filePath) != canon) continue;
    arr.push(symbolEntry(fi.decl->name, 12, fi.decl));
  }
  for (auto& kv : sem->classes()) {
    ClassDecl* cd = kv.second.decl;
    if (canonicalPath(cd->filePath) != canon) continue;
    int kind = cd->isStruct ? 23 : cd->isInterface ? 11 : 5;
    JsonValue c = symbolEntry(cd->name, kind, cd);
    JsonValue kids = JsonValue::makeArray();
    for (auto& f : cd->fields) kids.push(symbolEntry(f->name, 8, f.get()));
    for (auto& p : cd->properties)
      kids.push(symbolEntry(p->name, 7, p.get()));
    for (auto& m : cd->methods)
      kids.push(symbolEntry(m->name, m->isConstructor ? 9 : 6, m.get()));
    for (auto& e : cd->enums) kids.push(symbolEntry(e->name, 10, e.get()));
    if (kids.size() > 0) c["children"] = kids;
    arr.push(c);
  }
  for (auto& kv : sem->enums()) {
    EnumDecl* en = kv.second;
    if (canonicalPath(en->filePath) != canon) continue;
    JsonValue e = symbolEntry(en->name, 10, en);
    JsonValue kids = JsonValue::makeArray();
    for (auto& ent : en->entries) {
      JsonValue k = JsonValue::makeObject();
      k["name"] = JsonValue::makeString(ent.name);
      k["kind"] = JsonValue::makeInt(22);
      JsonValue s = JsonValue::makeObject();
      s["line"] = JsonValue::makeInt(en->line - 1);
      s["character"] = JsonValue::makeInt(0);
      k["range"] = JsonValue::makeObject();
      k["range"]["start"] = s;
      k["range"]["end"] = s;
      k["selectionRange"] = k["range"];
      kids.push(k);
    }
    if (kids.size() > 0) e["children"] = kids;
    arr.push(e);
  }
  for (auto* g : sem->globals()) {
    if (canonicalPath(g->filePath) != canon) continue;
    arr.push(symbolEntry(g->name, g->isConst ? 14 : 13, g));
  }
  return arr;
}

JsonValue LspServer::completionItem(const std::string& label, int kind,
                                    const std::string& detail) {
  JsonValue item = JsonValue::makeObject();
  item["label"] = JsonValue::makeString(label);
  item["kind"] = JsonValue::makeInt(kind);
  if (!detail.empty())
    item["detail"] = JsonValue::makeString(detail);
  return item;
}

// M25 14.4: inlayHints retorna tipo inferido para `var x = ...`
static bool inRange(int line, int startLine, int endLine) {
  return line >= startLine && line <= endLine;
}

JsonValue LspServer::inlayHints(const JsonValue& params) {
  JsonValue hints = JsonValue::makeArray();
  std::string uri = params["textDocument"]["uri"].asString();
  std::string path = uriToPath(uri);
  ensureAnalysisFor(path);
  if (!an_.semantic || !an_.prog) return hints;
  int startLine = 0, endLine = INT_MAX;
  if (params.has("range")) {
    startLine = params["range"]["start"]["line"].asInt();
    endLine = params["range"]["end"]["line"].asInt();
  }
  std::string text = docText(path);
  auto addVarHint = [&](const std::string& name, const Type& t, int line) {
    if (!inRange(line, startLine, endLine)) return;
    if (t.kind == Type::Kind::Void || t.kind == Type::Kind::Unknown) return;
    JsonValue hint = JsonValue::makeObject();
    hint["label"] = JsonValue::makeString(": " + typeName(t));
    hint["position"] = JsonValue::makeObject();
    hint["position"]["line"] = JsonValue::makeInt(line);
    std::string lineText = lineAt(text, line);
    int col = (int)lineText.find(name);
    if (col < 0) col = 0;
    hint["position"]["character"] = JsonValue::makeInt(col + (int)name.size());
    hint["kind"] = JsonValue::makeInt(1); // Type hint
    hints.push(hint);
  };
  for (auto& d : an_.prog->decls) {
    if (auto vd = dynamic_cast<VarDecl*>(d.get())) {
      if (vd->isVar && vd->init) {
        addVarHint(vd->name, vd->type, vd->line - 1);
      }
    }
    if (auto fd = dynamic_cast<FunctionDecl*>(d.get())) {
      if (fd->body) {
        std::vector<std::pair<std::string, std::pair<Type, int>>> locals;
        collectLocals(fd->body.get(), locals);
        for (auto& lv : locals) {
          addVarHint(lv.first, lv.second.first, lv.second.second - 1);
        }
      }
    }
  }
  return hints;
}
void LspServer::collectLocals(Stmt* s, std::vector<std::pair<std::string, std::pair<Type, int>>>& out) {
  if (!s) return;
  if (auto bs = dynamic_cast<BlockStmt*>(s)) {
    for (auto& s2 : bs->stmts) collectLocals(s2.get(), out);
  } else if (auto svd = dynamic_cast<StmtVarDecl*>(s)) {
    for (auto& decl : svd->decls) {
      if (decl && decl->isVar && decl->init) {
        out.push_back({decl->name, {decl->type, decl->line}});
      }
    }
  } else if (auto vd = dynamic_cast<VarDecl*>(s)) {
    if (vd->isVar && vd->init) {
      out.push_back({vd->name, {vd->type, vd->line}});
    }
  } else if (auto ifs = dynamic_cast<IfStmt*>(s)) {
    if (ifs->thenBranch) collectLocals(ifs->thenBranch.get(), out);
    if (ifs->elseBranch) collectLocals(ifs->elseBranch.get(), out);
  } else if (auto fors = dynamic_cast<ForStmt*>(s)) {
    if (fors->body) collectLocals(fors->body.get(), out);
  } else if (auto foreachs = dynamic_cast<ForeachStmt*>(s)) {
    if (foreachs->body) collectLocals(foreachs->body.get(), out);
  } else if (auto whiles = dynamic_cast<WhileStmt*>(s)) {
    if (whiles->body) collectLocals(whiles->body.get(), out);
  } else if (auto trys = dynamic_cast<TryStmt*>(s)) {
    if (trys->body) collectLocals(trys->body.get(), out);
    if (trys->catchBody) collectLocals(trys->catchBody.get(), out);
  }
}
JsonValue LspServer::completion(const JsonValue& params) {
  std::string uri = params["textDocument"]["uri"].asString();
  std::string path = uriToPath(uri);
  ensureAnalysisFor(path);

  JsonValue result = JsonValue::makeObject();
  JsonValue items = JsonValue::makeArray();

  // keywords (inclui os tipos primitivos)
  for (auto& kv : keywordMap())
    items.push(completionItem(kv.first, 14, kv.first));

  // Tipos parametrizados por bits
  static const char* const s_bitTypes[] = {
    "int<1>", "int<2>", "int<4>", "int<8>", "int<16>", "int<32>", "int<64>", "int<128>", "int<256>", "int<512>", "int<1024>",
    "uint<1>", "uint<2>", "uint<4>", "uint<8>", "uint<16>", "uint<32>", "uint<64>", "uint<128>", "uint<256>", "uint<512>", "uint<1024>",
    "float<16>", "float<32>", "float<64>", "float<128>", "float<256>", "float<512>", "float<1024>",
    "bool<1>", "bool<8>",
    "char<8>", "char<16>", "char<32>"
  };
  for (const char* bt : s_bitTypes)
    items.push(completionItem(bt, 7, "tipo primitivo HPHL"));

  // builtins
  items.push(completionItem("print", 3, "print(a, b, ...)"));
  items.push(completionItem("println", 3, "println(a, b, ...)"));
  items.push(completionItem("toString", 3, "toString(x)"));
  items.push(completionItem("clock_ns", 3, "clock_ns()"));

  if (an_.semantic) {
    Semantic* sem = an_.semantic.get();
    for (auto& kv : sem->classes()) {
      ClassDecl* cd = kv.second.decl;
      int kind = cd->isStruct ? 22 : cd->isInterface ? 8 : 7;
      std::string detail = std::string(cd->isStruct ? "struct"
                                       : cd->isInterface ? "interface"
                                       : cd->isActor  ? "actor"
                                                      : "class") +
                           " " + cd->name;
      if (!cd->base.empty()) detail += " : " + cd->base;
      items.push(completionItem(cd->name, kind, detail));

      for (auto& f : cd->fields) {
        items.push(completionItem(f->name, 5, typeName(f->type) + " " + cd->name + "." + f->name));
      }
      for (auto& p : cd->properties) {
        items.push(completionItem(p->name, 10, typeName(p->type) + " " + cd->name + "." + p->name));
      }
      for (auto& m : cd->methods) {
        items.push(completionItem(m->name, 2, signature(m.get())));
      }
    }
    for (auto& kv : sem->enums()) {
      const EnumDecl* en = kv.second;
      std::string detail = "enum " + en->name;
      if (en->anyPayload) detail += " (com dados)";
      items.push(completionItem(en->name, 13, detail));
    }
    for (auto& fi : sem->functions()) {
      if (fi.decl->isMethod || fi.decl->isConstructor) continue;
      items.push(completionItem(fi.decl->name, 3, signature(fi.decl)));
    }
    for (auto& fi : sem->functions()) {
      if (!fi.decl->isMethod || fi.decl->isConstructor) continue;
      items.push(completionItem(fi.decl->name, 2, signature(fi.decl)));
    }
    for (auto* g : sem->globals())
      items.push(completionItem(g->name, 6, typeName(g->type) + " " + g->name));
  }

  result["isIncomplete"] = JsonValue::makeBool(false);
  result["items"] = items;
  return result;
}

JsonValue LspServer::references(const JsonValue& params) {
  std::string uri = params["textDocument"]["uri"].asString();
  std::string path = uriToPath(uri);
  ensureAnalysisFor(path);

  const JsonValue& pos = params["position"];
  int line0 = (int)pos["line"].asInt();
  int char0 = (int)pos["character"].asInt();
  std::string line = lineAt(docText(path), line0);
  int byteCol = utf16ToByte(line, char0);
  Word w = wordAt(line, byteCol);
  if (w.text.empty()) return JsonValue::makeArray();

  std::string symbol = w.text;
  JsonValue locs = JsonValue::makeArray();

  std::vector<std::string> searchFiles = an_.files;
  if (searchFiles.empty()) searchFiles.push_back(path);
  for (const auto& kv : documents_) {
    const std::string& p = kv.first;
    if (std::find(searchFiles.begin(), searchFiles.end(), p) == searchFiles.end()) {
      searchFiles.push_back(p);
    }
  }

  for (const auto& f : searchFiles) {
    std::string text = docText(f);
    if (text.empty()) continue;
    std::string fUri = getUriForPath(f);

    std::istringstream stream(text);
    std::string curLine;
    int curLineNum = 0;
    while (std::getline(stream, curLine)) {
      int idx = 0;
      while ((idx = (int)curLine.find(symbol, idx)) != -1) {
        bool leftOk = (idx == 0) || !(std::isalnum((unsigned char)curLine[idx - 1]) || curLine[idx - 1] == '_');
        int endIdx = idx + (int)symbol.size();
        bool rightOk = (endIdx >= (int)curLine.size()) || !(std::isalnum((unsigned char)curLine[endIdx]) || curLine[endIdx] == '_');
        if (leftOk && rightOk) {
          JsonValue loc = JsonValue::makeObject();
          loc["uri"] = JsonValue::makeString(fUri);
          JsonValue range = JsonValue::makeObject();
          JsonValue start = JsonValue::makeObject();
          start["line"] = JsonValue::makeInt(curLineNum);
          start["character"] = JsonValue::makeInt(byteToUtf16(curLine, idx));
          JsonValue end = JsonValue::makeObject();
          end["line"] = JsonValue::makeInt(curLineNum);
          end["character"] = JsonValue::makeInt(byteToUtf16(curLine, endIdx));
          range["start"] = start;
          range["end"] = end;
          loc["range"] = range;
          locs.push(loc);
        }
        idx += (int)symbol.size();
      }
      curLineNum++;
    }
  }

  return locs;
}

JsonValue LspServer::codeAction(const JsonValue& params) {
  std::string uri = params["textDocument"]["uri"].asString();
  JsonValue actions = JsonValue::makeArray();

  const JsonValue& context = params["context"];
  if (context.isObject() && context["diagnostics"].isArray()) {
    const JsonValue& diags = context["diagnostics"];
    for (size_t i = 0; i < diags.size(); i++) {
      const JsonValue& diag = diags.at(i);
      std::string msg = diag["message"].asString();
      const JsonValue& range = diag["range"];

      // 1. Missing type / module import
      size_t q1 = msg.find('\'');
      size_t q2 = (q1 != std::string::npos) ? msg.find('\'', q1 + 1) : std::string::npos;
      if (q1 != std::string::npos && q2 != std::string::npos) {
        std::string sym = msg.substr(q1 + 1, q2 - q1 - 1);
        if (!sym.empty() && std::isupper((unsigned char)sym[0])) {
          JsonValue act = JsonValue::makeObject();
          act["title"] = JsonValue::makeString("Importar módulo '" + sym + "'");
          act["kind"] = JsonValue::makeString("quickfix");
          JsonValue edit = JsonValue::makeObject();
          JsonValue changes = JsonValue::makeObject();
          JsonValue textEdits = JsonValue::makeArray();
          JsonValue textEdit = JsonValue::makeObject();
          JsonValue startPos = JsonValue::makeObject();
          startPos["line"] = JsonValue::makeInt(0);
          startPos["character"] = JsonValue::makeInt(0);
          JsonValue r = JsonValue::makeObject();
          r["start"] = startPos;
          r["end"] = startPos;
          textEdit["range"] = r;
          textEdit["newText"] = JsonValue::makeString("import " + sym + ";\n");
          textEdits.push(textEdit);
          changes[uri] = textEdits;
          edit["changes"] = changes;
          act["edit"] = edit;
          actions.push(act);
        }
      }

      // 2. Type mismatch / bit width cast
      if (msg.find("incompatível") != std::string::npos || msg.find("cast") != std::string::npos ||
          msg.find("int<") != std::string::npos || msg.find("float<") != std::string::npos) {
        std::string targetCast = "(int<8>)";
        if (msg.find("int<8>") != std::string::npos) targetCast = "(int<8>)";
        else if (msg.find("int<16>") != std::string::npos) targetCast = "(int<16>)";
        else if (msg.find("int<32>") != std::string::npos) targetCast = "(int<32>)";
        else if (msg.find("int<64>") != std::string::npos) targetCast = "(int<64>)";
        else if (msg.find("float<32>") != std::string::npos) targetCast = "(float<32>)";
        else if (msg.find("float<64>") != std::string::npos) targetCast = "(float<64>)";

        JsonValue act = JsonValue::makeObject();
        act["title"] = JsonValue::makeString("Adicionar cast explícito " + targetCast);
        act["kind"] = JsonValue::makeString("quickfix");
        JsonValue edit = JsonValue::makeObject();
        JsonValue changes = JsonValue::makeObject();
        JsonValue textEdits = JsonValue::makeArray();
        JsonValue textEdit = JsonValue::makeObject();
        textEdit["range"] = range;
        textEdit["newText"] = JsonValue::makeString(targetCast + " ");
        textEdits.push(textEdit);
        changes[uri] = textEdits;
        edit["changes"] = changes;
        act["edit"] = edit;
        actions.push(act);
      }
    }
  }

  return actions;
}

JsonValue LspServer::signatureHelp(const JsonValue& params) {
  JsonValue res = JsonValue::makeObject();
  res["signatures"] = JsonValue::makeArray();
  res["activeSignature"] = JsonValue::makeInt(0);
  res["activeParameter"] = JsonValue::makeInt(0);

  std::string uri = params["textDocument"]["uri"].asString();
  std::string path = uriToPath(uri);
  ensureAnalysisFor(path);

  int line0 = params["position"]["line"].asInt();
  int col0 = params["position"]["character"].asInt();

  std::string text = docText(path);
  std::string line = lineAt(text, line0);
  if (col0 > (int)line.size()) col0 = (int)line.size();

  int parenDepth = 0;
  int commaCount = 0;
  int openParenIdx = -1;

  for (int i = col0 - 1; i >= 0; --i) {
    char c = line[i];
    if (c == ')') {
      parenDepth++;
    } else if (c == '(') {
      if (parenDepth > 0) {
        parenDepth--;
      } else {
        openParenIdx = i;
        break;
      }
    } else if (c == ',' && parenDepth == 0) {
      commaCount++;
    }
  }

  if (openParenIdx < 0) return res;

  int endName = openParenIdx;
  while (endName > 0 && std::isspace((unsigned char)line[endName - 1])) {
    endName--;
  }
  int startName = endName;
  while (startName > 0 && (std::isalnum((unsigned char)line[startName - 1]) || line[startName - 1] == '_')) {
    startName--;
  }

  std::string funcName = line.substr(startName, endName - startName);
  if (funcName.empty()) return res;

  res["activeParameter"] = JsonValue::makeInt(commaCount);

  if (an_.prog) {
    for (auto& d : an_.prog->decls) {
      if (auto fd = dynamic_cast<FunctionDecl*>(d.get())) {
        if (fd->name == funcName) {
          JsonValue sig = JsonValue::makeObject();
          std::string label = typeName(fd->returnType) + " " + fd->name + "(";
          JsonValue paramsArr = JsonValue::makeArray();
          for (size_t p = 0; p < fd->params.size(); ++p) {
            if (p > 0) label += ", ";
            auto* pNode = fd->params[p].get();
            std::string pStr = (pNode->byRef ? "ref " : (pNode->byOut ? "out " : (pNode->byIn ? "in " : ""))) +
                               typeName(pNode->type) + " " + pNode->name;
            label += pStr;
            JsonValue paramObj = JsonValue::makeObject();
            paramObj["label"] = JsonValue::makeString(pStr);
            paramsArr.push(paramObj);
          }
          label += ")";
          sig["label"] = JsonValue::makeString(label);
          sig["parameters"] = paramsArr;
          res["signatures"].push(sig);
        }
      } else if (auto cd = dynamic_cast<ClassDecl*>(d.get())) {
        for (auto& m : cd->methods) {
          if (m->name == funcName) {
            JsonValue sig = JsonValue::makeObject();
            std::string label = typeName(m->returnType) + " " + cd->name + "::" + m->name + "(";
            JsonValue paramsArr = JsonValue::makeArray();
            for (size_t p = 0; p < m->params.size(); ++p) {
              if (p > 0) label += ", ";
              auto* pNode = m->params[p].get();
              std::string pStr = (pNode->byRef ? "ref " : (pNode->byOut ? "out " : (pNode->byIn ? "in " : ""))) +
                                 typeName(pNode->type) + " " + pNode->name;
              label += pStr;
              JsonValue paramObj = JsonValue::makeObject();
              paramObj["label"] = JsonValue::makeString(pStr);
              paramsArr.push(paramObj);
            }
            label += ")";
            sig["label"] = JsonValue::makeString(label);
            sig["parameters"] = paramsArr;
            res["signatures"].push(sig);
          }
        }
      }
    }
  }

  if (res["signatures"].size() == 0) {
    if (funcName == "print" || funcName == "println") {
      JsonValue sig = JsonValue::makeObject();
      sig["label"] = JsonValue::makeString("void " + funcName + "(any value)");
      JsonValue paramsArr = JsonValue::makeArray();
      JsonValue p = JsonValue::makeObject();
      p["label"] = JsonValue::makeString("any value");
      paramsArr.push(p);
      sig["parameters"] = paramsArr;
      sig["documentation"] = JsonValue::makeString("Imprime o valor especificado na saída padrão.");
      res["signatures"].push(sig);
    } else if (funcName == "read_line" || funcName == "readline") {
      JsonValue sig = JsonValue::makeObject();
      sig["label"] = JsonValue::makeString("string " + funcName + "()");
      sig["parameters"] = JsonValue::makeArray();
      sig["documentation"] = JsonValue::makeString("Lê uma linha de texto da entrada padrão.");
      res["signatures"].push(sig);
    }
  }

  return res;
}


