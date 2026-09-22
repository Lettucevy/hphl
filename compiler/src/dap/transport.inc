// ---------------------------------------------------------------------------
// Framing Content-Length (stdin/stdout) — igual ao LSP
// ---------------------------------------------------------------------------
bool readFrame(std::string& body) {
  std::string header;
  char c;
  while (std::fread(&c, 1, 1, stdin) == 1) {
    header += c;
    if (header.size() >= 4 &&
        header.compare(header.size() - 4, 4, "\r\n\r\n") == 0)
      break;
  }
  if (header.empty()) return false;
  long long len = -1;
  size_t pos = 0;
  while (pos < header.size()) {
    size_t eol = header.find("\r\n", pos);
    if (eol == std::string::npos) break;
    std::string line = header.substr(pos, eol - pos);
    pos = eol + 2;
    std::string lower = line;
    for (auto& ch : lower) ch = (char)std::tolower((unsigned char)ch);
    if (lower.rfind("content-length:", 0) == 0) {
      len = std::atoll(line.substr(15).c_str());
      break;
    }
  }
  if (len < 0) return false;
  body.assign((size_t)len, '\0');
  size_t got = std::fread(&body[0], 1, (size_t)len, stdin);
  return got == (size_t)len;
}

std::mutex g_stdoutMtx; // stdout compartilhado (main + thread do processo)

void writeFrame(const std::string& body) {
  std::lock_guard<std::mutex> lock(g_stdoutMtx);
  std::string out = "Content-Length: " + std::to_string(body.size()) +
                    "\r\n\r\n" + body;
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void sendEvent(const std::string& method, const JsonValue& params) {
  JsonValue msg = JsonValue::makeObject();
  msg["jsonrpc"] = JsonValue::makeString("2.0");
  msg["method"] = JsonValue::makeString(method);
  msg["params"] = params;
  writeFrame(msg.serialize());
}

