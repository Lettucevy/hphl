// ---------------------------------------------------------------------------
// Comandos do runtime
// ---------------------------------------------------------------------------
// bpx é fire-and-forget: o runtime só lê o pipe quando parado (ou em traps
// sem bp, no modo lazy), então a confirmação "ok" chega depois e é
// consumida/ignorada pelo tryRuntimeLine do run().
bool DapServer::sendBp(long long fnId, long long line, int on,
                       const std::string& msg) {
  if (!sendLine("bpx " + std::to_string(fnId) + " " + std::to_string(line) +
                " " + std::to_string(on) +
                (msg.empty() ? "" : " " + msg)))
    return false;
  if (on) {
    for (auto& b : bps_) {
      if (b.fnId == fnId && b.line == line) {
        b.logMsg = msg;
        return true;
      }
    }
    BpState b;
    b.fnId = fnId;
    b.line = line;
    b.logMsg = msg;
    bps_.push_back(b);
  } else {
    bps_.erase(std::remove_if(bps_.begin(), bps_.end(),
                              [&](const BpState& b) {
                                return b.fnId == fnId && b.line == line;
                              }),
               bps_.end());
  }
  return true;
}

// F2.3: hit-count de breakpoints ("N", ">N", "<N", ">=N", "<=N", "==N", "%N";
// sem operador = ">="; hits = contador já incrementado)
static bool hitConditionMet(const std::string& cond, long long hits) {
  if (cond.empty()) return true;
  std::string op;
  size_t i = 0;
  while (i < cond.size() && std::string("><=%").find(cond[i]) != std::string::npos)
    op += cond[i++];
  if (op == "=") op = "==";
  if (i >= cond.size()) return true;
  long long n = std::atoll(cond.c_str() + i);
  if (op == "%") return hits % n == 0;
  if (op == "==") return hits == n;
  if (op == "<") return hits < n;
  if (op == "<=") return hits <= n;
  if (op == ">") return hits > n;
  return hits >= n; // default
}

bool DapServer::cmdRead(long long fnId, const std::string& rbp,
                        const std::string& name, std::string& rvar) {
  if (!sendLine("read " + std::to_string(fnId) + " " + rbp + " " + name))
    return false;
  std::string r = waitRuntimeLine();
  if (r.rfind("rvar ", 0) != 0) return false;
  rvar = r.substr(5);
  return true;
}

bool DapServer::cmdReadArr(long long fnId, const std::string& rbp,
                           const std::string& name, long long idx,
                           std::string& rvar) {
  if (!sendLine("readarr " + std::to_string(fnId) + " " + rbp + " " + name +
                " " + std::to_string(idx)))
    return false;
  std::string r = waitRuntimeLine();
  if (r.rfind("rvar ", 0) != 0) return false;
  rvar = r.substr(5);
  return true;
}

bool DapServer::cmdReadGbl(const std::string& name, std::string& rvar) {
  if (!sendLine("readgbl " + name)) return false;
  std::string r = waitRuntimeLine();
  if (r.rfind("rvar ", 0) != 0) return false;
  rvar = r.substr(5);
  return true;
}

bool DapServer::cmdWrite(long long fnId, const std::string& rbp,
                         const std::string& name, unsigned long long value) {
  char hex[24];
  snprintf(hex, sizeof hex, "%llx", value);
  if (!sendLine("write " + std::to_string(fnId) + " " + rbp + " " + name + " " + hex))
    return false;
  return waitRuntimeLine() == "ok";
}

bool DapServer::cmdWriteGbl(const std::string& name, unsigned long long value) {
  char hex[24];
  snprintf(hex, sizeof hex, "%llx", value);
  if (!sendLine("writegbl " + name + " " + hex)) return false;
  return waitRuntimeLine() == "ok";
}

bool DapServer::cmdReadGblArr(const std::string& name, long long idx,
                              std::string& rvar) {
  if (!sendLine("readgblarr " + name + " " + std::to_string(idx)))
    return false;
  std::string r = waitRuntimeLine();
  if (r.rfind("rvar ", 0) != 0) return false;
  rvar = r.substr(5);
  return true;
}

bool DapServer::cmdReadStr(const std::string& ptr, std::string& out) {
  if (!sendLine("readstr " + ptr)) return false;
  std::string r = waitRuntimeLine();
  if (r.rfind("rstr ", 0) != 0) return false;
  out = unescape(r.substr(5));
  return true;
}

bool DapServer::cmdBase(long long fnId, const std::string& rbp,
                        const std::string& name, std::string& rvar) {
  if (!sendLine("base " + std::to_string(fnId) + " " + rbp + " " + name))
    return false;
  std::string r = waitRuntimeLine();
  if (r.rfind("rvar ", 0) != 0) return false;
  rvar = r.substr(5);
  return true;
}

bool DapServer::cmdBaseGbl(const std::string& name, std::string& rvar) {
  if (!sendLine("basegbl " + name)) return false;
  std::string r = waitRuntimeLine();
  if (r.rfind("rvar ", 0) != 0) return false;
  rvar = r.substr(5);
  return true;
}

bool DapServer::cmdMem(unsigned long long addr, long long count,
                       std::vector<std::string>& slots) {
  if (!sendLine("mem " + hexOf(addr) + " " + std::to_string(count)))
    return false;
  std::string r = waitRuntimeLine();
  if (r.rfind("memok", 0) != 0) return false;
  slots.clear();
  size_t pos = 6;
  for (long long i = 0; i < count; i++) {
    size_t sp = r.find(' ', pos);
    slots.push_back(r.substr(pos, sp == std::string::npos ? std::string::npos
                                                          : sp - pos));
    if (sp == std::string::npos) break;
    pos = sp + 1;
  }
  return true;
}

