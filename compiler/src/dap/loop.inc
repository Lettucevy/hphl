// Loop principal
// ---------------------------------------------------------------------------
int DapServer::run() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  stdinThread_ = std::thread([this] { stdinLoop(); });
  while (!quit_) {
    // eventos pendentes do runtime (stopped/terminated) antes da próxima request
    while (true) {
      auto ev = tryRuntimeLine();
      if (!ev) break;
      std::string l = *ev;
      if (l.rfind("paused ", 0) == 0) {
        paused_ = true;
        long long tid = 0, n = 0;
        sscanf(l.c_str() + 7, "%lld %lld", &tid, &n);
        // F2.6: 4º campo opcional "wp" (watchpoint de escrita) + linha "wp <idx>"
        // F3.1: 4º campo opcional "excp" (exceção/panic) + linha "excp <cod> <msg>"
        bool wpHit = false, excHit = false;
        {
          std::string rest = l.substr(7);
          size_t sp1 = rest.find(' ');
          size_t sp2 = sp1 == std::string::npos ? std::string::npos
                                                : rest.find(' ', sp1 + 1);
          if (sp2 != std::string::npos) {
            if (rest.compare(sp2 + 1, 2, "wp") == 0) wpHit = true;
            else if (rest.compare(sp2 + 1, 4, "excp") == 0) excHit = true;
          }
        }
        pausedTid_ = tid;
        pausedFrames_.clear();
        lastWpIdx_ = -1;
        lastExcCod_ = -1;
        lastExcMsg_.clear();
        for (long long k = 0; k < n; k++) {
          std::string fl = waitRuntimeLine();
          if (fl.rfind("frame ", 0) != 0) break;
          Frame f;
          f.rbp.assign(fl, 6, fl.size() - 6);
          long long fnId, line;
          char rbp[32];
          if (sscanf(fl.c_str() + 6, "%lld %lld %31s", &fnId, &line, rbp) == 3) {
            f.fnId = fnId;
            f.line = line;
            f.rbp = rbp;
            pausedFrames_.push_back(f);
          }
        }
        if (wpHit) {
          std::string wl = waitRuntimeLine();
          if (wl.rfind("wp ", 0) == 0) lastWpIdx_ = atoll(wl.c_str() + 3);
        }
        if (excHit) {
          std::string el = waitRuntimeLine();
          if (el.rfind("excp ", 0) == 0) {
            std::string rest = el.substr(5);
            size_t sp = rest.find(' ');
            if (sp != std::string::npos) {
              lastExcCod_ = atoll(rest.c_str());
              lastExcMsg_ = rest.substr(sp + 1);
            } else {
              lastExcCod_ = atoll(rest.c_str());
            }
          }
        }
        std::reverse(pausedFrames_.begin(), pausedFrames_.end()); // topo primeiro
        std::string reason = "breakpoint";
        std::string desc;
        if (lastExcCod_ >= 0) {
          reason = "exception";
          desc = lastExcMsg_.empty() ? "exceção" : lastExcMsg_;
        } else if (lastWpIdx_ >= 0) {
          reason = "data breakpoint";
          for (auto& w : wps_) {
            if (w.active && w.runtimeIdx == lastWpIdx_) { desc = w.desc; break; }
          }
          if (desc.empty()) desc = "watchpoint de escrita";
        } else if (pauseRequested_) {
          reason = "pause";
          desc = "execução pausada manualmente";
        } else if (stepRequested_) {
          reason = "step";
          desc = "passo concluído";
        } else if (!pausedFrames_.empty()) {
          const Frame& top = pausedFrames_[0];
          BpState* bp = nullptr;
          for (auto& b : bps_) {
            if (b.fnId == top.fnId && b.line == top.line) {
              bp = &b;
              break;
            }
          }
          if (bp && !bp->hitCondition.empty()) {
            // F2.3: hit-count — incrementa e retoma a execução até satisfazer
            bp->hits++;
            if (!hitConditionMet(bp->hitCondition, bp->hits)) {
              pauseRequested_ = stepRequested_ = false;
              if (!sendLine("continue")) break;
              continue;
            }
            desc = "breakpoint (hit-count '" + bp->hitCondition +
                   "') na linha " + std::to_string(top.line + 1);
          }
          if (bp && !bp->cond.empty()) {
            std::string errDesc;
            int v = evalCondition(bp->cond, errDesc);
            if (v < 0) {
              desc = "erro ao avaliar a condição '" + bp->cond + "': " + errDesc;
            } else if (v == 0) {
              // condição falsa: retoma a execução automaticamente
              pauseRequested_ = stepRequested_ = false;
              if (!sendLine("continue")) break;
              continue;
            } else {
              desc = "breakpoint condicional '" + bp->cond + "' verdadeira";
            }
          } else if (bp && desc.empty()) {
            desc = "breakpoint na linha " + std::to_string(top.line + 1);
          }
        }
        pauseRequested_ = stepRequested_ = false;
        sendStopped(reason, desc);
        continue;
      }
      // demais linhas (rvar/rstr/ok/fail/respostas) não têm dono aqui: a
      // resposta de um comando é consumida pelo próprio handler; linhas
      // soltas são ignoradas
    }

    // próxima request do cliente (a stdinThread_ alimenta a fila). A condição
    // também observa lines_: um evento do runtime (paused) pode chegar a
    // qualquer momento e precisa ser processado antes de uma request.
    std::string body;
    {
      std::unique_lock<std::mutex> lk(mtx_);
      cv_.wait(lk, [this] {
        return !reqs_.empty() || !lines_.empty() || stdinClosed_ || quit_;
      });
      if (!reqs_.empty()) {
        body = reqs_.front();
        reqs_.pop_front();
      } else {
        continue; // volta ao topo para processar os eventos do runtime
      }
    }
    JsonValue msg;
    try {
      msg = parseJson(body);
    } catch (...) {
      // JSON inválido: responde erro para não travar o cliente DAP
      JsonValue resp = JsonValue::makeObject();
      resp["jsonrpc"] = JsonValue::makeString("2.0");
      resp["id"] = JsonValue::makeInt(-1);
      JsonValue err = JsonValue::makeObject();
      err["code"] = JsonValue::makeInt(-32700);
      err["message"] = JsonValue::makeString("parse error: JSON inválido");
      resp["error"] = err;
      writeFrame(resp.serialize());
      continue;
    }
    std::string m = msg["method"].asString();
    JsonValue id = msg["id"];

    if (m == "initialize") onInitialize(id, msg["params"]);
    else if (m == "launch") onLaunch(id, msg["params"]);
    else if (m == "setBreakpoints") onSetBreakpoints(id, msg["params"]);
    else if (m == "configurationDone") onConfigurationDone(id, msg["params"]);
    else if (m == "continue") onContinue(id, msg["params"]);
    else if (m == "next") onStep(id, msg["params"], "next");
    else if (m == "stepIn") onStep(id, msg["params"], "stepin");
    else if (m == "stepOut") onStep(id, msg["params"], "stepout");
    else if (m == "pause") onPause(id, msg["params"]);
    else if (m == "stackTrace") onStack(id, msg["params"]);
    else if (m == "scopes") onScopes(id, msg["params"]);
    else if (m == "variables") onVariables(id, msg["params"]);
    else if (m == "setVariable") onSetVariable(id, msg["params"]);
    else if (m == "evaluate") onEvaluate(id, msg["params"]);
    else if (m == "threads") onThreads(id, msg["params"]);
    else if (m == "dataBreakpointInfo") onDataBreakpointInfo(id, msg["params"]);
    else if (m == "setDataBreakpoints") onSetDataBreakpoints(id, msg["params"]);
    else if (m == "exceptionInfo") onExceptionInfo(id, msg["params"]);
    else if (m == "setFunctionBreakpoints") onFunctionBreakpoints(id, msg["params"]);
    else if (m == "source") onSource(id, msg["params"]);
    else if (m == "stdin") onStdin(id, msg["params"]);
    else if (m == "disconnect") onDisconnect(id, msg["params"]);
    else if (m == "terminate") onDisconnect(id, msg["params"]);
    else if (m == "setExceptionBreakpoints" || m == "restartFrame" || m == "completions")
      respond(id, JsonValue::makeObject()); // v1: sem suporte
    else if (m == "initialize" || m == "launch") {}
    else if (!id.isNull() && (id.isInt() || id.isString()))
      respondError(id, -32601, "method not found: " + m);
  }
  if (stdinThread_.joinable()) stdinThread_.detach();
  return 0;
}

