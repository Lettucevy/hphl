// ---------------------------------------------------------------------------
// URI ↔ caminho
// ---------------------------------------------------------------------------
std::string uriToPath(const std::string& uri) {
  std::string p = uri;
  if (p.rfind("file://", 0) == 0) p = p.substr(7);
  // "/C:/..." → "C:/..." (uri de arquivo no Windows)
  if (p.size() >= 3 && p[0] == '/' && std::isalpha((unsigned char)p[1]) &&
      p[2] == ':')
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
  return "file:///" + p;
#else
  return "file://" + p;
#endif
}

