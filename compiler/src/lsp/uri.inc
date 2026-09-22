// ---------------------------------------------------------------------------
// URI ↔ caminho
// ---------------------------------------------------------------------------
static std::string urlDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int h1 = hexVal(in[i + 1]);
      int h2 = hexVal(in[i + 2]);
      if (h1 >= 0 && h2 >= 0) {
        out += static_cast<char>((h1 << 4) | h2);
        i += 2;
        continue;
      }
    }
    out += in[i];
  }
  return out;
}

std::string uriToPath(const std::string& uri) {
  std::string p = uri;
  if (p.rfind("file://", 0) == 0) p = p.substr(7);
  p = urlDecode(p);
  // "/C:/..." or "/c:/..." → "C:/..." (uri de arquivo no Windows)
  if (p.size() >= 3 && p[0] == '/' && std::isalpha((unsigned char)p[1]) && p[2] == ':')
    p = p.substr(1);
  for (auto& c : p)
    if (c == '/') c = '\\';
  return p;
}

std::string pathToUri(const std::string& path) {
  std::string p = path;
  for (auto& c : p)
    if (c == '\\') c = '/';
#ifdef _WIN32
  if (p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':') {
    return "file:///" + p;
  }
  return "file:///" + p;
#else
  return "file://" + p;
#endif
}

