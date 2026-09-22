// Servidor DAP
// ---------------------------------------------------------------------------
struct Frame {
  long long fnId = 0, line = 0;
  std::string rbp;
};

// evaluate v1: nomes + literais + aritmética simples (int/float/bool/string)
struct EvalVal {
  enum Kind { Int, UInt, Float, Bool, Str } kind = Int;
  long long i = 0;
  double f = 0;
  bool b = false;
  std::string s;
  std::string display; // F2.2: texto alternativo (nome da variante de enum)
  bool ok = false;
  std::string err;
};

class DapServer {
public:
  int run();
  EvalVal evalName(const std::string& name); // usado pelo EvalParser (friend)

private:
  // ---- io DAP ----
  JsonValue makeResponse(const JsonValue& id);
  void respond(const JsonValue& id, const JsonValue& result);
  void respondError(const JsonValue& id, long long code, const std::string& msg);
  void sendStopped(const std::string& reason, const std::string& desc = "");

  // ---- runtime (pipe) ----
  bool spawnTarget(const std::string& exe, std::string& err);
  void closeRuntime();
  void pipeLoop();
  void stdinLoop();
  void pumpOutput(HANDLE h, const char* category);
  bool sendLine(const std::string& line);
  std::string waitRuntimeLine();
  std::optional<std::string> tryRuntimeLine();
  bool handshake(std::string& err);

  // ---- comandos do runtime ----
  bool sendBp(long long fnId, long long line, int on,
              const std::string& msg = std::string());
  bool cmdRead(long long fnId, const std::string& rbp, const std::string& name,
               std::string& rvar);
  bool cmdWrite(long long fnId, const std::string& rbp, const std::string& name,
                unsigned long long value);
  bool cmdWriteGbl(const std::string& name, unsigned long long value);
  bool cmdReadArr(long long fnId, const std::string& rbp, const std::string& name,
                  long long idx, std::string& rvar);
  bool cmdReadGbl(const std::string& name, std::string& rvar);
  bool cmdReadGblArr(const std::string& name, long long idx, std::string& rvar);
  bool cmdReadStr(const std::string& ptr, std::string& out);
  bool cmdBase(long long fnId, const std::string& rbp, const std::string& name,
               std::string& rvar);
  bool cmdBaseGbl(const std::string& name, std::string& rvar);
  bool cmdMem(unsigned long long addr, long long count,
              std::vector<std::string>& slots);

  // ---- handlers DAP ----
  void onInitialize(const JsonValue& id, const JsonValue& params);
  void onLaunch(const JsonValue& id, const JsonValue& params);
  void onSetBreakpoints(const JsonValue& id, const JsonValue& params);
  void onConfigurationDone(const JsonValue& id, const JsonValue& params);
  void onContinue(const JsonValue& id, const JsonValue& params);
  void onStep(const JsonValue& id, const JsonValue& params,
              const std::string& cmd);
  void onPause(const JsonValue& id, const JsonValue& params);
  void onStack(const JsonValue& id, const JsonValue& params);
  void onScopes(const JsonValue& id, const JsonValue& params);
  void onVariables(const JsonValue& id, const JsonValue& params);
  void onSetVariable(const JsonValue& id, const JsonValue& params);
  void onEvaluate(const JsonValue& id, const JsonValue& params);
  void onThreads(const JsonValue& id, const JsonValue& params);
  void onDataBreakpointInfo(const JsonValue& id, const JsonValue& params);
  void onSetDataBreakpoints(const JsonValue& id, const JsonValue& params);
  void onSource(const JsonValue& id, const JsonValue& params);
  void onDisconnect(const JsonValue& id, const JsonValue& params);
  void onStdin(const JsonValue& id, const JsonValue& params);

  // ---- formatação / expansão de variáveis (F1.2) ----
  std::string formatRaw(long long tipo, const std::string& rvar);
  std::string enumVariantName(const std::string& enumName, long long value) const;
  JsonValue variableFor(const std::string& name, const TipoInfo& ti,
                        const std::string& rvar, bool isLocal, long long fnId,
                        const std::string& rbp);
  JsonValue valueForNode(const std::string& name, const TipoInfo& ti,
                         unsigned long long addr, long long stride);
  void expandArrayNode(const JsonValue& id, size_t idx);
  void expandClassNode(const JsonValue& id, size_t idx);
  void expandListNode(const JsonValue& id, size_t idx);
  bool evaluateExpr(const std::string& expr, JsonValue& out);
  int evalCondition(const std::string& cond, std::string& errDesc);

  // ---- estado ----
  std::string program_, exePath_;
  bool launched_ = false;
  bool quit_ = false;
  std::vector<EnumDbg> enums_; // F2.2: enums simples do programa (watch/variáveis)

  // pipe
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  HANDLE inPipe_ = INVALID_HANDLE_VALUE; // stdin pipe para o debuggee
  PROCESS_INFORMATION pi_{};
  std::thread pipeThread_;
  std::thread monitorThread_;
  std::thread stdinThread_;
  std::thread outThread_;  // stdout do runtime -> eventos output
  std::thread errThread_;  // stderr do runtime -> eventos output
  std::mutex mtx_;
  std::condition_variable cv_;
  std::deque<std::string> lines_;
  std::deque<std::string> reqs_; // frames DAP lidos do stdin
  bool pipeClosed_ = false;
  bool stdinClosed_ = false;
  std::atomic<bool> processGone_{false};

  // meta + sessão
  std::vector<DbgFn> fns_;
  std::vector<DbgGlobal> globals_;
  std::vector<DbgClass> classes_;
  std::vector<Frame> pausedFrames_; // [0] = topo
  long long pausedTid_ = 0;
  bool paused_ = false;
  struct BpState {
    long long fnId = 0, line = 0;
    std::string cond; // "" = incondicional
    std::string hitCondition; // "" = sem hit-count (F2.3)
    std::string logMsg;       // F2.7: logpoint — imprime em vez de pausar
    long long hits = 0;       // contador de paradas neste breakpoint
  };
  std::vector<BpState> bps_; // breakpoints ativos
  bool pauseRequested_ = false; // "pause" enviado: próximo paused é manual
  bool stepRequested_ = false;  // passo enviado: próximo paused é de step
  // F2.6: watchpoints de escrita (data breakpoints) — slots estáveis do
  // runtime; o setDataBreakpoints substitui o conjunto (DAP)
  struct WpState {
    int runtimeIdx = -1;
    std::string dataId;
    std::string cmdOff; // comando para desativar no runtime
    std::string desc;
    bool active = false;
  };
  std::vector<WpState> wps_;
  long long lastWpIdx_ = -1; // slot do runtime do último stopped (wp)
  // F3.1: exceções no depurador
  long long lastExcCod_ = -1;
  std::string lastExcMsg_;
  void onExceptionInfo(const JsonValue& id, const JsonValue& params);
  // F3.5: breakpoints de função
  struct FnBpState { std::string name; long long fnId = -1; long long line = 0; bool active = false; };
  std::vector<FnBpState> fnBps_;
  void onFunctionBreakpoints(const JsonValue& id, const JsonValue& params);
  // nós de navegação (F1.2): refs DAP = NODE_REF_BASE + índice
  static const long long NODE_REF_BASE = 1000000000;
  struct VarNode {
    int kind = 0;             // 1 = array, 2 = classe, 3 = list
    unsigned long long base = 0;
    std::vector<long long> dims; // array: dims restantes (nível atual à frente)
    long long elemTipo = 0;   // array: tipo do elemento final (1..11)
    std::string elemCls;      // array: classe do elemento final
    long long clsId = 0;      // classe: índice em classes_
    std::shared_ptr<TipoInfo> elem; // list: tipo do elemento (recursivo)
    std::string name;         // rótulo exibido no nó pai
  };
  std::vector<VarNode> varNodes_;
  long long clsIdOf(const std::string& name) const;
};

