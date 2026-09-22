// ---------------------------------------------------------------------------
// Documentos e análise
// ---------------------------------------------------------------------------
void LspServer::onDidOpen(const JsonValue& params) {
  const JsonValue& td = params["textDocument"];
  std::string uri = td["uri"].asString();
  documents_[uri] = td["text"].asString();
  reanalyze(uriToPath(uri));
  publishDiagnostics(uri);
}

void LspServer::onDidChange(const JsonValue& params) {
  std::string uri = params["textDocument"]["uri"].asString();
  const JsonValue& changes = params["contentChanges"];
  if (changes.isArray() && changes.size() > 0) {
    const JsonValue& last = changes.at(changes.size() - 1);
    documents_[uri] = last["text"].asString();
  }
  reanalyze(uriToPath(uri));
  publishDiagnostics(uri);
}

void LspServer::onDidClose(const JsonValue& params) {
  std::string uri = params["textDocument"]["uri"].asString();
  documents_.erase(uri);
  JsonValue msg = JsonValue::makeObject();
  msg["jsonrpc"] = JsonValue::makeString("2.0");
  msg["method"] = JsonValue::makeString("textDocument/publishDiagnostics");
  msg["params"] = JsonValue::makeObject();
  msg["params"]["uri"] = JsonValue::makeString(uri);
  msg["params"]["diagnostics"] = JsonValue::makeArray();
  writeFrame(msg.serialize());
}

std::string LspServer::docText(const std::string& path) const {
  auto it = documents_.find(pathToUri(path));
  if (it != documents_.end()) return it->second;
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void LspServer::reanalyze(const std::string& mainPath) {
  an_.errMessage.clear();
  an_.errPath.clear();
  an_.errLine0 = 0;
  an_.errCol0 = -1;

  auto overlay = [this](const std::string& path) -> std::optional<std::string> {
    auto it = documents_.find(pathToUri(path));
    if (it != documents_.end()) return it->second;
    return std::nullopt;
  };

  ImportLoader loader(mainPath, overlay);
  auto prog = std::make_unique<Program>();
  try {
    loader.load(mainPath, *prog);
    auto semantic = std::make_unique<Semantic>(prog.get(), mainPath);
    semantic->analyze();
    an_.prog = std::move(prog);
    an_.semantic = std::move(semantic);
    an_.ok = true;
  } catch (const CompileError& e) {
    ParsedError pe = parseError(e.message);
    an_.ok = false;
    an_.errPath = pe.path;
    an_.errLine0 = pe.line0;
    an_.errCol0 = pe.col0;
    an_.errMessage = pe.message;
    // mantém prog/semantic da última análise boa (hover continua útil)
  }
  an_.files = loader.loadedPaths();
  an_.mainPath = mainPath;
}

void LspServer::ensureAnalysisFor(const std::string& path) {
  if (an_.semantic &&
      canonicalPath(path) == canonicalPath(an_.mainPath))
    return;
  reanalyze(path);
}

void LspServer::publishDiagnostics(const std::string& triggerUri) {
  std::map<std::string, std::vector<JsonValue>> perFile;

  if (!an_.errMessage.empty()) {
    std::string path = an_.errPath.empty() ? an_.mainPath : an_.errPath;
    std::string text = docText(path);
    JsonValue d = JsonValue::makeObject();
    d["range"] = JsonValue::makeObject();
    JsonValue start = JsonValue::makeObject();
    std::string line = lineAt(text, an_.errLine0);
    int startChar = an_.errCol0 >= 0 ? byteToUtf16(line, an_.errCol0) : 0;
    start["line"] = JsonValue::makeInt(an_.errLine0);
    start["character"] = JsonValue::makeInt(startChar);
    JsonValue end = JsonValue::makeObject();
    int endChar = startChar;
    if (an_.errCol0 >= 0) {
      int e = an_.errCol0;
      int n = (int)line.size();
      while (e < n && (std::isalnum((unsigned char)line[e]) || line[e] == '_'))
        e++;
      endChar = byteToUtf16(line, e);
      if (endChar == startChar) endChar = startChar + 1;
    } else {
      endChar = byteToUtf16(line, (int)line.size());
    }
    end["line"] = JsonValue::makeInt(an_.errLine0);
    end["character"] = JsonValue::makeInt(endChar);
    d["range"]["start"] = start;
    d["range"]["end"] = end;
    d["severity"] = JsonValue::makeInt(1);
    d["source"] = JsonValue::makeString("hphl");
    d["message"] = JsonValue::makeString(an_.errMessage);
    perFile[canonicalPath(path)].push_back(d);
  }

  auto publish = [&](const std::string& uri) {
    JsonValue arr = JsonValue::makeArray();
    auto it = perFile.find(canonicalPath(uriToPath(uri)));
    if (it != perFile.end())
      for (auto& d : it->second) arr.push(d);
    JsonValue msg = JsonValue::makeObject();
    msg["jsonrpc"] = JsonValue::makeString("2.0");
    msg["method"] = JsonValue::makeString("textDocument/publishDiagnostics");
    msg["params"] = JsonValue::makeObject();
    msg["params"]["uri"] = JsonValue::makeString(uri);
    msg["params"]["diagnostics"] = arr;
    writeFrame(msg.serialize());
  };

  // publica só arquivos afetados + limpa erros antigos (nada de re-publicar tudo)
  bool publishedAny = false;
  std::set<std::string> newErr;
  for (auto& kv : perFile) {
    if (!triggerUri.empty() && canonicalPath(uriToPath(triggerUri)) == kv.first)
      publish(triggerUri);
    else
      publish(pathToUri(kv.first));
    newErr.insert(kv.first);
    publishedAny = true;
  }
  for (auto& f : lastErrFiles_)
    if (!perFile.count(f)) {
      if (!triggerUri.empty() && canonicalPath(uriToPath(triggerUri)) == f)
        publish(triggerUri);
      else
        publish(pathToUri(f));
      publishedAny = true;
    }
  lastErrFiles_ = newErr;
  // sempre sinaliza o estado do arquivo analisado (limpo => vazio)
  if (!publishedAny) publish(triggerUri);
}

