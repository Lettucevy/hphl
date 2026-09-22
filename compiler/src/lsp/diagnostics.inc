// ---------------------------------------------------------------------------
// Erros do compilador → diagnóstico
// ---------------------------------------------------------------------------
struct ParsedError {
  std::string path; // arquivo extraído do prefixo ('' se sem posição)
  int line0 = 0;    // 0-based
  int col0 = -1;    // -1 = sem coluna
  std::string message;
};

ParsedError parseError(const std::string& raw) {
  ParsedError out;
  std::string s = raw;
  if (s.rfind("hphlc: ", 0) == 0) {
    out.message = s.substr(7);
    return out;
  }
  // "path:line:col: erro ..." ou "path:line: erro semântico: ..."
  size_t n = s.size();
  size_t pathEnd = std::string::npos;
  for (size_t p = 0; p < n; p++) {
    if (s[p] != ':') continue;
    size_t q = p + 1;
    if (q >= n || !std::isdigit((unsigned char)s[q])) continue;
    while (q < n && std::isdigit((unsigned char)s[q])) q++;
    if (q < n && s[q] == ':') { pathEnd = p; break; }
  }
  if (pathEnd == std::string::npos) {
    out.message = s;
    return out;
  }
  out.path = s.substr(0, pathEnd);
  size_t p = pathEnd + 1, q = p;
  while (q < n && std::isdigit((unsigned char)s[q])) q++;
  long long line = std::atoll(s.substr(p, q - p).c_str());
  out.line0 = (int)(line - 1);
  if (out.line0 < 0) out.line0 = 0;
  if (q < n && s[q] == ':' && q + 1 < n && std::isdigit((unsigned char)s[q + 1])) {
    p = q + 1;
    q = p;
    while (q < n && std::isdigit((unsigned char)s[q])) q++;
    long long col = std::atoll(s.substr(p, q - p).c_str());
    out.col0 = (int)(col - 1);
    if (out.col0 < 0) out.col0 = 0;
  }
  if (q < n && s[q] == ':') q++;
  if (q < n && s[q] == ' ') q++;
  out.message = s.substr(q);
  return out;
}

