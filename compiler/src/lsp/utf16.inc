// ---------------------------------------------------------------------------
// UTF-8 ↔ UTF-16 (posições do LSP são contadas em code units UTF-16)
// ---------------------------------------------------------------------------
// bytes da linha até o code unit UTF-16 `utf16Col`
int utf16ToByte(const std::string& line, int utf16Col) {
  int units = 0, i = 0;
  while (i < (int)line.size() && units < utf16Col) {
    unsigned char c = (unsigned char)line[i];
    if (c < 0x80) { i += 1; units += 1; }
    else if ((c & 0xE0) == 0xC0) { i += 2; units += 1; }
    else if ((c & 0xF0) == 0xE0) { i += 3; units += 1; }
    else if ((c & 0xF8) == 0xF0) { i += 4; units += 2; }
    else { i += 1; units += 1; }
  }
  return i;
}

int byteToUtf16(const std::string& line, int byteCol) {
  int units = 0, i = 0;
  while (i < byteCol && i < (int)line.size()) {
    unsigned char c = (unsigned char)line[i];
    if (c < 0x80) { i += 1; units += 1; }
    else if ((c & 0xE0) == 0xC0) { i += 2; units += 1; }
    else if ((c & 0xF0) == 0xE0) { i += 3; units += 1; }
    else if ((c & 0xF8) == 0xF0) { i += 4; units += 2; }
    else { i += 1; units += 1; }
  }
  return units;
}

std::string lineAt(const std::string& text, int n) {
  size_t start = 0;
  for (int i = 0; i < n; i++) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) return "";
    start = nl + 1;
  }
  size_t end = text.find('\n', start);
  std::string line = end == std::string::npos ? text.substr(start)
                                               : text.substr(start, end - start);
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return line;
}

struct Word {
  std::string text;
  int byteStart = 0; // offset em bytes na linha
  int byteEnd = 0;
};

Word wordAt(const std::string& line, int byteCol) {
  Word w;
  int n = (int)line.size();
  int s = byteCol, e = byteCol;
  while (s > 0 && (std::isalnum((unsigned char)line[s - 1]) || line[s - 1] == '_'))
    s--;
  while (e < n && (std::isalnum((unsigned char)line[e]) || line[e] == '_'))
    e++;
  if (e > s) {
    w.text = line.substr(s, e - s);
    w.byteStart = s;
    w.byteEnd = e;
  }
  return w;
}

// ---------------------------------------------------------------------------
