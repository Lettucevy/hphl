// Runtime (pipe)
// ---------------------------------------------------------------------------
bool DapServer::spawnTarget(const std::string& exe, std::string& err) {
  static int pipeCounter = 0;
  std::string pipeName = "hphl_dbg_" + std::to_string((unsigned long)GetCurrentProcessId()) +
                         "_" + std::to_string(pipeCounter++);
  std::string fullName = "\\\\.\\pipe\\" + pipeName;

  pipe_ = CreateNamedPipeA(fullName.c_str(),
                           PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                           4096, 4096, 0, NULL);
  if (pipe_ == INVALID_HANDLE_VALUE) {
    err = "CreateNamedPipe falhou";
    return false;
  }

  // ambiente do processo filho: HPHL_DBG_PIPE (salva/restaura o pai)
  char oldVal[512];
  DWORD oldLen = GetEnvironmentVariableA("HPHL_DBG_PIPE", oldVal, sizeof oldVal);
  SetEnvironmentVariableA("HPHL_DBG_PIPE", pipeName.c_str());

  std::string cmdline = "\"" + exe + "\"";
  std::vector<char> cmdBuf(cmdline.begin(), cmdline.end());
  cmdBuf.push_back('\0');

  // pipes anônimos para capturar stdout/stderr do runtime (os prints do
  // programa viram eventos DAP "output" — category stdout/stderr)
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;
  HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
  HANDLE inR = nullptr, inW = nullptr;
  if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0) ||
      !CreatePipe(&inR, &inW, &sa, 0)) {
    if (outR) CloseHandle(outR);
    if (outW) CloseHandle(outW);
    if (errR) CloseHandle(errR);
    if (errW) CloseHandle(errW);
    if (inR) CloseHandle(inR);
    if (inW) CloseHandle(inW);
    err = "CreatePipe (stdio) falhou";
    return false;
  }
  SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
  inPipe_ = inW;

  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = inR;
  si.hStdOutput = outW;
  si.hStdError = errW;
  if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi_)) {
    SetEnvironmentVariableA("HPHL_DBG_PIPE", oldLen ? oldVal : nullptr);
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
    CloseHandle(outR);
    CloseHandle(outW);
    CloseHandle(errR);
    CloseHandle(errW);
    CloseHandle(inR);
    if (inPipe_ != INVALID_HANDLE_VALUE) {
      CloseHandle(inPipe_);
      inPipe_ = INVALID_HANDLE_VALUE;
    }
    err = "CreateProcess falhou: " + exe;
    return false;
  }
  CloseHandle(inR);
  CloseHandle(outW);
  CloseHandle(errW);
  // os read ends ficam no pai (threads de pump); sem flag de herança para
  // não vazar para processos futuros
  SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
  if (oldLen)
    SetEnvironmentVariableA("HPHL_DBG_PIPE", oldVal);
  else
    SetEnvironmentVariableA("HPHL_DBG_PIPE", nullptr);

  // o runtime tenta conectar (retry 200x10ms) no primeiro trap
  if (!ConnectNamedPipe(pipe_, nullptr) &&
      GetLastError() != ERROR_PIPE_CONNECTED) {
    err = "o executável não conectou no pipe do debugger";
    return false;
  }

  pipeClosed_ = false;
  pipeThread_ = std::thread([this] { pipeLoop(); });
  outThread_ = std::thread([this, outR] { pumpOutput(outR, "stdout"); });
  errThread_ = std::thread([this, errR] { pumpOutput(errR, "stderr"); });
  monitorThread_ = std::thread([this] {
    while (!quit_) {
      if (WaitForSingleObject(pi_.hProcess, 250) == WAIT_OBJECT_0) {
        if (!processGone_.exchange(true)) {
          JsonValue p = JsonValue::makeObject();
          p["reason"] = JsonValue::makeString("program");
          sendEvent("terminated", p);
        }
        return;
      }
    }
  });
  return true;
}

void DapServer::closeRuntime() {
  quit_ = true;
  if (inPipe_ != INVALID_HANDLE_VALUE) {
    CloseHandle(inPipe_);
    inPipe_ = INVALID_HANDLE_VALUE;
  }
  if (pipe_ != INVALID_HANDLE_VALUE) {
    sendLine("quit");
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }
  {
    std::lock_guard<std::mutex> lk(mtx_);
    pipeClosed_ = true;
  }
  cv_.notify_all();
  if (pipeThread_.joinable()) pipeThread_.join();
  if (monitorThread_.joinable()) monitorThread_.join();
  if (pi_.hProcess) {
    if (WaitForSingleObject(pi_.hProcess, 1000) == WAIT_TIMEOUT)
      TerminateProcess(pi_.hProcess, 1);
    CloseHandle(pi_.hProcess);
    CloseHandle(pi_.hThread);
    pi_.hProcess = pi_.hThread = nullptr;
  }
  // após a morte do processo, os pipes anônimos fecham e o pump termina
  if (outThread_.joinable()) outThread_.join();
  if (errThread_.joinable()) errThread_.join();
}

// lê a saída (stdout/stderr) do runtime e emite eventos DAP "output"
void DapServer::pumpOutput(HANDLE h, const char* category) {
  char buf[4096];
  while (true) {
    DWORD n = 0;
    if (!ReadFile(h, buf, sizeof buf, &n, nullptr) || n == 0) break;
    JsonValue p = JsonValue::makeObject();
    p["category"] = JsonValue::makeString(category);
    p["output"] = JsonValue::makeString(std::string(buf, n));
    sendEvent("output", p);
  }
  CloseHandle(h);
}

void DapServer::pipeLoop() {
  char c;
  std::string line;
  OVERLAPPED ov{};
  ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  while (true) {
    DWORD r = 0;
    if (!ReadFile(pipe_, &c, 1, &r, &ov)) {
      if (GetLastError() != ERROR_IO_PENDING) break;
      WaitForSingleObject(ov.hEvent, INFINITE);
      if (!GetOverlappedResult(pipe_, &ov, &r, FALSE) || r == 0) break;
    }
    if (c == '\n') {
      {
        std::lock_guard<std::mutex> lk(mtx_);
        lines_.push_back(line);
      }
      cv_.notify_all();
      line.clear();
      continue;
    }
    if (c == '\r') continue;
    line += c;
  }
  CloseHandle(ov.hEvent);
  {
    std::lock_guard<std::mutex> lk(mtx_);
    pipeClosed_ = true;
  }
  cv_.notify_all();
}

// lê os frames DAP do stdin numa thread própria (o run() não pode bloquear
// no stdin enquanto eventos do runtime chegam — p.ex. o paused após continue)
void DapServer::stdinLoop() {
  std::string body;
  while (readFrame(body)) {
    std::lock_guard<std::mutex> lk(mtx_);
    reqs_.push_back(body);
    cv_.notify_all();
  }
  {
    std::lock_guard<std::mutex> lk(mtx_);
    stdinClosed_ = true;
  }
  cv_.notify_all();
}

bool DapServer::sendLine(const std::string& line) {
  if (pipe_ == INVALID_HANDLE_VALUE) return false;
  std::string msg = line + "\n";
  OVERLAPPED ov{};
  ov.hEvent = CreateEventA(nullptr, TRUE, TRUE, nullptr);
  DWORD w = 0;
  if (!WriteFile(pipe_, msg.data(), (DWORD)msg.size(), &w, &ov)) {
    if (GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(ov.hEvent);
      return false;
    }
    WaitForSingleObject(ov.hEvent, INFINITE);
    if (!GetOverlappedResult(pipe_, &ov, &w, FALSE)) {
      CloseHandle(ov.hEvent);
      return false;
    }
  }
  CloseHandle(ov.hEvent);
  return true;
}

// espera (bloqueante) a próxima linha do runtime
std::string DapServer::waitRuntimeLine() {
  std::unique_lock<std::mutex> lk(mtx_);
  cv_.wait(lk, [this] { return !lines_.empty() || pipeClosed_; });
  if (lines_.empty()) return "";
  std::string l = lines_.front();
  lines_.pop_front();
  return l;
}

// linha pendente sem bloquear (para processar eventos entre requests)
std::optional<std::string> DapServer::tryRuntimeLine() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (lines_.empty()) return std::nullopt;
  std::string l = lines_.front();
  lines_.pop_front();
  return l;
}

bool DapServer::handshake(std::string& err) {
  fns_.clear();
  globals_.clear();
  classes_.clear();
  varNodes_.clear();
  std::string ready = waitRuntimeLine();
  if (ready != "ready") {
    err = "handshake: esperado 'ready', recebido '" + ready + "'";
    return false;
  }
  // meta <nFns> <nGlobals> <nClasses>, fn/loc/trp/gbl/cls/fld... até metaend
  bool seenMeta = false;
  DbgClass* curCls = nullptr;
  while (true) {
    std::string l = waitRuntimeLine();
    if (l.empty()) { err = "handshake: pipe fechado"; return false; }
    if (l.rfind("meta ", 0) == 0) { seenMeta = true; continue; }
    if (l == "metaend") break;
    if (l.rfind("fn ", 0) == 0) {
      DbgFn fn;
      char name[256], file[512];
      long long id, nLocals, nTraps;
      if (sscanf(l.c_str() + 3, "%lld %255s %511s %lld %lld", &id, name, file,
                 &nLocals, &nTraps) == 5) {
        fn.name = unescape(name);
        fn.file = unescape(file);
        fn.locs.resize((size_t)nLocals);
        fn.traps.resize((size_t)nTraps);
        fns_.push_back(fn);
      }
    } else if (l.rfind("loc ", 0) == 0) {
      long long id, off, fl;
      char name[256], ts[512];
      if (sscanf(l.c_str() + 4, "%lld %255s %511s %lld %lld", &id, name, ts,
                 &off, &fl) == 5) {
        if (id >= 0 && id < (long long)fns_.size()) {
          DbgLoc loc;
          loc.name = unescape(name);
          loc.ti = parseTipo(unescape(ts));
          loc.offset = off;
          loc.flags = fl;
          size_t idx = 0;
          for (size_t k = 0; k < fns_[(size_t)id].locs.size(); k++) {
            if (fns_[(size_t)id].locs[k].name.empty()) { idx = k; break; }
          }
          if (idx < fns_[(size_t)id].locs.size())
            fns_[(size_t)id].locs[idx] = loc;
        }
      }
    } else if (l.rfind("trp ", 0) == 0) {
      long long id, line;
      if (sscanf(l.c_str() + 4, "%lld %lld", &id, &line) == 2) {
        if (id >= 0 && id < (long long)fns_.size()) {
          size_t idx = 0;
          for (size_t k = 0; k < fns_[(size_t)id].traps.size(); k++) {
            if (fns_[(size_t)id].traps[k] == 0) { idx = k; break; }
          }
          if (idx < fns_[(size_t)id].traps.size())
            fns_[(size_t)id].traps[idx] = line;
        }
      }
    } else if (l.rfind("gbl ", 0) == 0) {
      char name[256], ts[512];
      long long tls;
      if (sscanf(l.c_str() + 4, "%255s %511s %lld", name, ts, &tls) == 3) {
        DbgGlobal g;
        g.name = unescape(name);
        g.ti = parseTipo(unescape(ts));
        g.tls = tls;
        g.isArray = !g.ti.dims.empty();
        globals_.push_back(g);
      }
    } else if (l.rfind("cls ", 0) == 0) {
      long long id, nFields;
      char name[256];
      if (sscanf(l.c_str() + 4, "%lld %255s %lld", &id, name, &nFields) == 3) {
        if (id >= 0 && id < 1024) {
          DbgClass c;
          c.name = unescape(name);
          c.fields.resize((size_t)nFields);
          classes_.resize((size_t)id + 1);
          classes_[(size_t)id] = c;
          curCls = &classes_[(size_t)id];
        }
      }
    } else if (l.rfind("fld ", 0) == 0) {
      long long id, off;
      char name[256], ts[512];
      if (sscanf(l.c_str() + 4, "%lld %255s %511s %lld", &id, name, ts, &off) == 4) {
        if (curCls && id >= 0 && id < (long long)classes_.size() &&
            !classes_[(size_t)id].fields.empty()) {
          DbgField f;
          f.name = unescape(name);
          f.ti = parseTipo(unescape(ts));
          f.offset = off;
          // campos chegam na ordem da meta; preenche o primeiro vazio
          for (auto& slot : classes_[(size_t)id].fields) {
            if (slot.name.empty()) { slot = f; break; }
          }
        }
      }
    }
  }
  if (!seenMeta || fns_.empty()) {
    err = "handshake: meta inválido";
    return false;
  }
  return true;
}

long long DapServer::clsIdOf(const std::string& name) const {
  for (size_t i = 0; i < classes_.size(); i++)
    if (classes_[i].name == name) return (long long)i;
  return -1;
}

