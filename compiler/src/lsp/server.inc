// ---------------------------------------------------------------------------
// Servidor
// ---------------------------------------------------------------------------
class LspServer {
public:
  int run();

private:
  // ---- transporte/rpc ----
  void onMessage(const JsonValue& msg);
  JsonValue handleRequest(const std::string& method, const JsonValue& params);
  void respond(const JsonValue& id, const JsonValue& result);
  void respondError(const JsonValue& id, int code, const std::string& message);

  // ---- sincronização de documentos ----
  void onDidOpen(const JsonValue& params);
  void onDidChange(const JsonValue& params);
  void onDidClose(const JsonValue& params);
  void publishDiagnostics(const std::string& triggerUri);
  std::string docText(const std::string& path) const;

  // ---- análise ----
  void reanalyze(const std::string& mainPath);
  void ensureAnalysisFor(const std::string& path);

  // ---- features ----
  JsonValue hover(const JsonValue& params);
  JsonValue definition(const JsonValue& params);
  JsonValue documentSymbol(const JsonValue& params);
  JsonValue completion(const JsonValue& params);
  JsonValue inlayHints(const JsonValue& params); // M25 14.4
  JsonValue signatureHelp(const JsonValue& params);
  JsonValue references(const JsonValue& params);
  JsonValue codeAction(const JsonValue& params);
  void collectLocals(Stmt* s, std::vector<std::pair<std::string, std::pair<Type, int>>>& out);

  std::string hoverFor(const std::string& word);
  JsonValue locationFor(const Decl* d, const std::string& name);
  JsonValue symbolEntry(const std::string& name, int kind, const Decl* d);
  JsonValue completionItem(const std::string& label, int kind,
                           const std::string& detail);

  std::map<std::string, std::string> documents_; // uri → texto
  std::set<std::string> lastErrFiles_;            // canônicos que receberam diagnóstico
  bool shutdown_ = false;
  bool exitRequested_ = false;

  struct Analysis {
    std::unique_ptr<Program> prog;
    std::unique_ptr<Semantic> semantic;
    std::vector<std::string> files; // caminhos carregados (ordem)
    std::string mainPath;
    bool ok = false;
    // erro da última análise (se !ok)
    std::string errPath;
    int errLine0 = 0;
    int errCol0 = -1;
    std::string errMessage;
  };
  Analysis an_;
};

