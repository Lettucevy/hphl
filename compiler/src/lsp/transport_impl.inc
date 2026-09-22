// ---------------------------------------------------------------------------
// Transporte
// ---------------------------------------------------------------------------
int LspServer::run() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  SetConsoleOutputCP(CP_UTF8);
#endif
  std::string body;
  while (readFrame(body)) {
    JsonValue msg;
    try {
      msg = parseJson(body);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "hphl-lsp: JSON inválido: %s\n", e.what());
      continue;
    }
    try {
      onMessage(msg);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "hphl-lsp: erro interno: %s\n", e.what());
    }
    if (exitRequested_) break;
  }
  return 0;
}

void LspServer::respond(const JsonValue& id, const JsonValue& result) {
  JsonValue msg = JsonValue::makeObject();
  msg["jsonrpc"] = JsonValue::makeString("2.0");
  msg["id"] = id;
  msg["result"] = result;
  writeFrame(msg.serialize());
}

void LspServer::respondError(const JsonValue& id, int code,
                             const std::string& message) {
  JsonValue msg = JsonValue::makeObject();
  msg["jsonrpc"] = JsonValue::makeString("2.0");
  msg["id"] = id;
  msg["error"] = JsonValue::makeObject();
  msg["error"]["code"] = JsonValue::makeInt(code);
  msg["error"]["message"] = JsonValue::makeString(message);
  writeFrame(msg.serialize());
}

void LspServer::onMessage(const JsonValue& msg) {
  if (!msg.isObject()) return;
  const JsonValue& method = msg["method"];
  const JsonValue& id = msg["id"];
  if (!method.isString()) return; // resposta a um pedido nosso (não enviamos)
  std::string m = method.asString();
  const JsonValue& params = msg["params"];

  if (m == "initialize") {
    JsonValue result = JsonValue::makeObject();
    result["capabilities"] = JsonValue::makeObject();
    result["capabilities"]["textDocumentSync"] = JsonValue::makeInt(1); // full
    result["capabilities"]["hoverProvider"] = JsonValue::makeBool(true);
    result["capabilities"]["definitionProvider"] = JsonValue::makeBool(true);
    result["capabilities"]["documentSymbolProvider"] = JsonValue::makeBool(true);
    result["capabilities"]["referencesProvider"] = JsonValue::makeBool(true);
    result["capabilities"]["codeActionProvider"] = JsonValue::makeBool(true);
    result["capabilities"]["inlayHintProvider"] = JsonValue::makeBool(true);
    result["capabilities"]["signatureHelpProvider"] = JsonValue::makeObject();
    result["capabilities"]["signatureHelpProvider"]["triggerCharacters"] = JsonValue::makeArray();
    result["capabilities"]["signatureHelpProvider"]["triggerCharacters"].push(JsonValue::makeString("("));
    result["capabilities"]["signatureHelpProvider"]["triggerCharacters"].push(JsonValue::makeString(","));
    result["capabilities"]["completionProvider"] = JsonValue::makeObject();
    result["capabilities"]["completionProvider"]["triggerCharacters"] =
        JsonValue::makeArray();
    result["capabilities"]["completionProvider"]["triggerCharacters"].push(
        JsonValue::makeString("."));
    result["serverInfo"] = JsonValue::makeObject();
    result["serverInfo"]["name"] = JsonValue::makeString("hphl");
    result["serverInfo"]["version"] = JsonValue::makeString(HPHL_VERSION);
    respond(id, result);
    return;
  }
  if (m == "initialized") return;
  if (m == "shutdown") {
    shutdown_ = true;
    respond(id, JsonValue());
    return;
  }
  if (m == "exit") {
    exitRequested_ = true;
    return;
  }
  if (m == "$/cancelRequest") return;

  if (m == "textDocument/didOpen") { onDidOpen(params); return; }
  if (m == "textDocument/didChange") { onDidChange(params); return; }
  if (m == "textDocument/didClose") { onDidClose(params); return; }

  if (m == "textDocument/hover") { respond(id, hover(params)); return; }
  if (m == "textDocument/definition") { respond(id, definition(params)); return; }
  if (m == "textDocument/documentSymbol") { respond(id, documentSymbol(params)); return; }
  if (m == "textDocument/completion") { respond(id, completion(params)); return; }
  if (m == "textDocument/inlayHint") { respond(id, inlayHints(params)); return; }
  if (m == "textDocument/signatureHelp") { respond(id, signatureHelp(params)); return; }
  if (m == "textDocument/references") { respond(id, references(params)); return; }
  if (m == "textDocument/codeAction") { respond(id, codeAction(params)); return; }

  if (id.isInt() || id.isString())
    respondError(id, -32601, "method not found: " + m);
}

