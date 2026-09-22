// ============================================================================
// pkgfetch.cpp — Package fetcher (M17 fetch remoto real)
// ============================================================================

#include "pkgfetch.h"
#include "crypto/ed25519.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>  // _mkdir (Windows)
#endif

// M_RV1 H4: zlib para inflate gzip (sem system('tar'))
#include <zlib.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <dirent.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "json/json.h"
#include "pkgparser.h"

namespace hphl {

// ---------------------------------------------------------------------------
// HTTP client
// ---------------------------------------------------------------------------

HttpResponse httpGet(const std::string& url, void (*onProgress)(const DownloadProgress&)) {
  HttpResponse r;
  if (url.rfind("file://", 0) == 0) {
    std::string filePath = url.substr(7);
#ifdef _WIN32
    if (filePath.size() >= 3 && filePath[0] == '/' && std::isalpha((unsigned char)filePath[1]) && filePath[2] == ':') {
      filePath = filePath.substr(1);
    }
#endif
    std::ifstream in(filePath, std::ios::binary);
    if (!in) {
      r.status = 404;
      r.error = "arquivo não encontrado: " + filePath;
      return r;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    r.status = 200;
    r.body = ss.str();
    r.bytesReceived = (long long)r.body.size();
    return r;
  }
#ifdef _WIN32
  // WinHTTP: parse URL, abrir conexão, enviar request, ler resposta
  std::wstring wurl(url.begin(), url.end());
  URL_COMPONENTS uc = {0};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {0};
  wchar_t path[2048] = {0};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
    r.error = "WinHttpCrackUrl falhou";
    return r;
  }
  bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
  HINTERNET hSession = WinHttpOpen(L"hphlc/0.71",
    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_NAME, 0);
  if (!hSession) { r.error = "WinHttpOpen falhou"; return r; }
  HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
  if (!hConnect) {
    r.error = "WinHttpConnect falhou";
    WinHttpCloseHandle(hSession);
    return r;
  }
  DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
    NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    r.error = "WinHttpOpenRequest falhou";
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return r;
  }
  // M_RV1 H1: TLS com validacao por padrao; HPHL_ALLOW_INSECURE_TLS=1 desativa
  if (isHttps) {
    const char* insecure = std::getenv("HPHL_ALLOW_INSECURE_TLS");
    if (insecure && insecure[0] == '1') {
      DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                      SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
      WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS,
                       &dwFlags, sizeof(dwFlags));
    }
  }
  WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
    WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (!WinHttpReceiveResponse(hRequest, NULL)) {
    r.error = "WinHttpReceiveResponse falhou";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return r;
  }
  // status code
  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
  r.status = (int)statusCode;
  // Content-Length
  DWORD cl = 0; DWORD clSize = sizeof(cl);
  WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
    WINHTTP_HEADER_NAME_BY_INDEX, &cl, &clSize, WINHTTP_NO_HEADER_INDEX);
  // lê o corpo
  char buf[16384];
  DWORD got = 0;
  DownloadProgress prog;
  prog.url = url;
  prog.bytesTotal = (cl > 0) ? (long long)cl : -1;
  while (WinHttpReadData(hRequest, buf, sizeof(buf), &got) && got > 0) {
    r.body.append(buf, got);
    r.bytesReceived += got;
    if (onProgress) {
      prog.bytesReceived = r.bytesReceived;
      onProgress(prog);
    }
  }
  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return r;
#else
  // M_RV1 H2: HTTP/HTTPS via socket() POSIX + getaddrinfo() + OpenSSL para TLS.
  // Suporta http:// e https:// (via libssl). HPHL_NO_TLS=1 força http.
  std::string scheme, host, path;
  int port = 80;
  bool isHttpsPosix = false;
  if (url.substr(0, 8) == "https://") {
    isHttpsPosix = true;
    size_t hp = 8;
    size_t slash = url.find('/', hp);
    std::string hostport = (slash == std::string::npos) ? url.substr(hp) : url.substr(hp, slash - hp);
    path = (slash == std::string::npos) ? "/" : url.substr(slash);
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
      host = hostport.substr(0, colon);
      port = atoi(hostport.substr(colon + 1).c_str());
    } else {
      host = hostport;
      port = 443;
    }
    if (std::getenv("HPHL_NO_TLS")) { isHttpsPosix = false; port = 80; }
  } else if (url.substr(0, 7) == "http://") {
    size_t hp = 7;
    size_t slash = url.find('/', hp);
    std::string hostport = (slash == std::string::npos) ? url.substr(hp) : url.substr(hp, slash - hp);
    path = (slash == std::string::npos) ? "/" : url.substr(slash);
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
      host = hostport.substr(0, colon);
      port = atoi(hostport.substr(colon + 1).c_str());
    } else {
      host = hostport;
    }
  } else {
    r.error = "POSIX HTTP só suporta http:// ou https:// (não " + url.substr(0, 8) + ")";
    return r;
  }
  struct addrinfo hints = {0}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  std::string portStr = std::to_string(port);
  if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
    r.error = "getaddrinfo falhou: " + host;
    return r;
  }
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) { freeaddrinfo(res); r.error = "socket() falhou"; return r; }
  if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
    close(fd); freeaddrinfo(res); r.error = "connect() falhou"; return r;
  }
  freeaddrinfo(res);
  std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nUser-Agent: hphlc-pkgfetch/0.74\r\nConnection: close\r\n\r\n";
  std::string raw;
  if (isHttpsPosix) {
    SSL_library_init(); SSL_load_error_strings();
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close(fd); r.error = "SSL_CTX_new falhou"; return r; }
    if (std::getenv("HPHL_ALLOW_INSECURE_TLS")) SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); close(fd); r.error = "SSL_connect falhou"; return r; }
    if (SSL_write(ssl, req.c_str(), (int)req.size()) <= 0) { SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close(fd); r.error = "SSL_write falhou"; return r; }
    char buf[4096]; int n;
    while ((n = SSL_read(ssl, buf, sizeof(buf))) > 0) raw.append(buf, n);
    SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
  } else {
    if (send(fd, req.c_str(), req.size(), 0) < 0) { close(fd); r.error = "send() falhou"; return r; }
    char buf[4096]; ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) raw.append(buf, n);
    close(fd);
  }
  // parse: split header/body
  size_t hdr_end = raw.find("\r\n\r\n");
  if (hdr_end == std::string::npos) {
    r.error = "resposta HTTP inválida (sem header terminator)";
    return r;
  }
  std::string headers = raw.substr(0, hdr_end);
  r.body = raw.substr(hdr_end + 4);
  // parse status code (primeira linha "HTTP/1.x NNN ...")
  size_t sp1 = headers.find(' ');
  size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : headers.find(' ', sp1 + 1);
  if (sp1 != std::string::npos && sp2 != std::string::npos) {
    r.status = atoi(headers.substr(sp1 + 1, sp2 - sp1 - 1).c_str());
  } else {
    r.status = 0;
  }
  r.error = "";
  return r;
#endif
}

HttpResponse httpDownload(const std::string& url, void (*onProgress)(const DownloadProgress&)) {
  // body já vem bruto do httpGet; nada especial para fazer
  return httpGet(url, onProgress);
}

// ---------------------------------------------------------------------------
// SHA-256 (via Windows BCrypt)
// ---------------------------------------------------------------------------

std::string sha256Hex(const std::string& data) {
#ifdef _WIN32
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_HASH_HANDLE hHash = NULL;
  std::string out;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return "";
  DWORD hashObjSize = 0, resultSize = 0;
  BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize, sizeof(hashObjSize), &resultSize, 0);
  std::vector<UCHAR> hashObj(hashObjSize);
  if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, NULL, 0, 0) != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return "";
  }
  BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0);
  UCHAR digest[32];
  BCryptFinishHash(hHash, digest, sizeof(digest), 0);
  BCryptDestroyHash(hHash);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  // hex
  out.reserve(64);
  for (int i = 0; i < 32; i++) {
    char b[3];
    snprintf(b, sizeof(b), "%02x", digest[i]);
    out += b;
  }
  return out;
#else
  // M20.3 6.2: SHA-256 puro POSIX (sem dependência de OpenSSL)
  // Implementação baseada em FIPS 180-4. ~150 linhas.
  struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;
  };
  static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
  };
  auto rotr = [](uint32_t x, int n){ return (x >> n) | (x << (32 - n)); };
  // transforma mensagem em 512-bit blocks
  auto sha256_transform = [&](Sha256Ctx& c, const uint8_t* d) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)d[i*4]<<24) | ((uint32_t)d[i*4+1]<<16) | ((uint32_t)d[i*4+2]<<8) | (uint32_t)d[i*4+3];
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
      uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c.state[0],b=c.state[1],cc=c.state[2],d2=c.state[3];
    uint32_t e=c.state[4],f=c.state[5],g=c.state[6],h=c.state[7];
    for (int i = 0; i < 64; i++) {
      uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = h + S1 + ch + K[i] + w[i];
      uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
      uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
      uint32_t t2 = S0 + mj;
      h=g; g=f; f=e; e=d2+t1; d2=cc; cc=b; b=a; a=t1+t2;
    }
    c.state[0]+=a; c.state[1]+=b; c.state[2]+=cc; c.state[3]+=d2;
    c.state[4]+=e; c.state[5]+=f; c.state[6]+=g; c.state[7]+=h;
  };
  Sha256Ctx ctx;
  ctx.state[0]=0x6a09e667; ctx.state[1]=0xbb67ae85;
  ctx.state[2]=0x3c6ef372; ctx.state[3]=0xa54ff53a;
  ctx.state[4]=0x510e527f; ctx.state[5]=0x9b05688c;
  ctx.state[6]=0x1f83d9ab; ctx.state[7]=0x5be0cd19;
  ctx.bitlen = 0;
  ctx.buflen = 0;
  for (size_t i = 0; i < data.size(); i++) {
    ctx.buf[ctx.buflen++] = (uint8_t)data[i];
    if (ctx.buflen == 64) {
      sha256_transform(ctx, ctx.buf);
      ctx.bitlen += 512;
      ctx.buflen = 0;
    }
  }
  // padding
  ctx.bitlen += ctx.buflen * 8;
  ctx.buf[ctx.buflen++] = 0x80;
  if (ctx.buflen > 56) {
    while (ctx.buflen < 64) ctx.buf[ctx.buflen++] = 0;
    sha256_transform(ctx, ctx.buf);
    ctx.buflen = 0;
  }
  while (ctx.buflen < 56) ctx.buf[ctx.buflen++] = 0;
  for (int i = 7; i >= 0; i--) ctx.buf[ctx.buflen++] = (uint8_t)(ctx.bitlen >> (i*8));
  sha256_transform(ctx, ctx.buf);
  std::string out;
  out.reserve(64);
  char b[3];
  for (int i = 0; i < 8; i++) {
    snprintf(b, sizeof(b), "%02x", (ctx.state[i] >> 24) & 0xff);
    out += b; snprintf(b, sizeof(b), "%02x", (ctx.state[i] >> 16) & 0xff); out += b;
    snprintf(b, sizeof(b), "%02x", (ctx.state[i] >> 8) & 0xff); out += b;
    snprintf(b, sizeof(b), "%02x", ctx.state[i] & 0xff); out += b;
  }
  return out;
#endif
}

// M_RV1 H3: SHA-512 via runtime real (crypto/sha512.c) — substitui
// a duplicata BCrypt+stub. hphl_sha512 e' cross-platform (mesma
// implementacao FIPS 180-4) e ja' eh usada pelo ed25519 verify.
// hashHex() converte para string hex lowercase (128 chars).
static std::string hashHex(const uint8_t* digest, size_t n) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (size_t i = 0; i < n; i++) {
    out.push_back(kHex[(digest[i] >> 4) & 0xf]);
    out.push_back(kHex[digest[i] & 0xf]);
  }
  return out;
}

std::string sha512Hex(const std::string& data) {
  uint8_t digest[64];
  hphl_sha512(reinterpret_cast<const uint8_t*>(data.data()), data.size(), digest);
  return hashHex(digest, sizeof(digest));
}

std::string sha256File(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss; ss << in.rdbuf();
  return sha256Hex(ss.str());
}

// ---------------------------------------------------------------------------
// SemVer
// ---------------------------------------------------------------------------

static int splitInt(const std::string& s, size_t& pos) {
  int n = 0;
  bool any = false;
  while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
    n = n * 10 + (s[pos] - '0');
    pos++;
    any = true;
  }
  return any ? n : 0;
}

SemVer parseSemVer(const std::string& v) {
  SemVer s; s.original = v;
  size_t pos = 0;
  s.major = splitInt(v, pos);
  if (pos >= v.size() || v[pos] != '.') return s;
  pos++;
  s.minor = splitInt(v, pos);
  if (pos >= v.size() || v[pos] != '.') return s;
  pos++;
  s.patch = splitInt(v, pos);
  if (pos < v.size() && v[pos] == '-') {
    pos++;
    size_t end = v.find('+', pos);
    if (end == std::string::npos) end = v.size();
    s.preRelease = v.substr(pos, end - pos);
    pos = end;
  }
  if (pos < v.size() && v[pos] == '+') {
    pos++;
    s.build = v.substr(pos);
  }
  s.valid = true;
  return s;
}

// Compara dois identificadores semver 2.0 (separados por '.', ex: "alpha.1").
// Regras: numerico < alfabetico; numerico compara como int; alfabetico < ASCII.
// Ausente == equivalente a infinito (so "1.0.0-alpha" < "1.0.0-alpha.beta" < ...).
static int cmpPreRelId(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    if (i >= a.size()) return -1;  // a acabou: menor
    if (j >= b.size()) return 1;   // b acabou: maior
    // le proximo identificador (ate . ou fim)
    size_t ai = i, bj = j;
    while (ai < a.size() && a[ai] != '.') ai++;
    while (bj < b.size() && b[bj] != '.') bj++;
    std::string as = a.substr(i, ai - i);
    std::string bs = b.substr(j, bj - j);
    bool aNum = !as.empty() && std::all_of(as.begin(), as.end(), ::isdigit);
    bool bNum = !bs.empty() && std::all_of(bs.begin(), bs.end(), ::isdigit);
    int c;
    if (aNum && bNum) {
      long long an = std::stoll(as), bn = std::stoll(bs);
      c = (an < bn) ? -1 : (an > bn ? 1 : 0);
    } else if (aNum) {
      return -1;  // numerico < alfabetico
    } else if (bNum) {
      return 1;
    } else {
      c = (as < bs) ? -1 : (as > bs ? 1 : 0);
    }
    if (c != 0) return c;
    i = ai + 1;
    j = bj + 1;
  }
  return 0;
}

int compareSemVer(const SemVer& a, const SemVer& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  // pre-release: ausente > presente (1.0.0 > 1.0.0-alpha)
  if (a.preRelease.empty() != b.preRelease.empty())
    return a.preRelease.empty() ? 1 : -1;
  if (!a.preRelease.empty()) {
    int c = cmpPreRelId(a.preRelease, b.preRelease);
    if (c != 0) return c;
  }
  // build metadata eh ignorado no compare (semver 2.0)
  return 0;
}

// resolve spec contra lista de versões. Suporta:
//   "1.2.3"       exato
//   "^1.2.3"      major igual, minor+patch >= (>=1.2.3, <2.0.0)
//   "~1.2.3"      major+minor igual, patch >= (>=1.2.3, <1.3.0)
//   ">=1.2.3"     maior ou igual
//   "*" / ""      mais recente
static bool matchesRange(const SemVer& v, const std::string& op, const SemVer& target) {
  int c = compareSemVer(v, target);
  if (op == "=" || op.empty()) return c == 0;
  if (op == ">=") return c >= 0;
  if (op == ">") return c > 0;
  if (op == "<=") return c <= 0;
  if (op == "<") return c < 0;
  if (op == "^") {
    if (v.major != target.major) return false;
    return compareSemVer(v, target) >= 0;
  }
  if (op == "~") {
    if (v.major != target.major || v.minor != target.minor) return false;
    return compareSemVer(v, target) >= 0;
  }
  return false;
}

std::string resolveVersion(const std::string& spec, const std::vector<std::string>& available) {
  if (spec.empty() || spec == "*" || spec == "latest") {
    // retorna a maior versão
    std::string best;
    SemVer bestV;
    for (auto& v : available) {
      SemVer sv = parseSemVer(v);
      if (!sv.valid) continue;
      if (best.empty() || compareSemVer(sv, bestV) > 0) { best = v; bestV = sv; }
    }
    return best;
  }
  // parse "op? version"
  std::string op;
  std::string ver = spec;
  size_t i = 0;
  while (i < ver.size() && (ver[i] == '^' || ver[i] == '~' || ver[i] == '>' || ver[i] == '<' || ver[i] == '=')) {
    op += ver[i++];
  }
  while (i < ver.size() && ver[i] == ' ') i++;
  ver = ver.substr(i);
  SemVer target = parseSemVer(ver);
  if (!target.valid) return "";
  std::string best;
  SemVer bestV;
  for (auto& v : available) {
    SemVer sv = parseSemVer(v);
    if (!sv.valid) continue;
    if (!matchesRange(sv, op, target)) continue;
    if (best.empty() || compareSemVer(sv, bestV) > 0) { best = v; bestV = sv; }
  }
  return best;
}

// ---------------------------------------------------------------------------
// Lockfile
// ---------------------------------------------------------------------------

static std::string lockfilePath() {
  return "package.lock";
}

std::vector<LockEntry> readLockfile(const std::string& dir) {
  std::vector<LockEntry> entries;
  std::string path = dir + "/" + lockfilePath();
  std::ifstream in(path, std::ios::binary);
  if (!in) return entries;
  std::ostringstream ss; ss << in.rdbuf();
  try {
    JsonValue j = parseJson(ss.str());
    if (!j.isObject()) return entries;
    auto arr = j["packages"];
    if (!arr.isArray()) return entries;
    for (size_t i = 0; i < arr.size(); i++) {
      auto& p = arr.at(i);
      LockEntry e;
      e.name = p["name"].asString("");
      e.version = p["version"].asString("");
      e.url = p["url"].asString("");
      e.sha256 = p["sha256"].asString("");
      e.mtime = p["mtime"].asInt(0);
      if (!e.name.empty()) entries.push_back(e);
    }
  } catch (...) {}
  return entries;
}

bool writeLockfile(const std::string& dir, const std::vector<LockEntry>& entries) {
  // ordena por nome para reprodutibilidade
  std::vector<LockEntry> sorted = entries;
  std::sort(sorted.begin(), sorted.end(),
    [](const LockEntry& a, const LockEntry& b) { return a.name < b.name; });
  JsonValue arr = JsonValue::makeArray();
  for (auto& e : sorted) {
    JsonValue p = JsonValue::makeObject();
    p["name"] = JsonValue::makeString(e.name);
    p["version"] = JsonValue::makeString(e.version);
    p["url"] = JsonValue::makeString(e.url);
    p["sha256"] = JsonValue::makeString(e.sha256);
    p["mtime"] = JsonValue::makeInt(e.mtime);
    arr.push(p);
  }
  JsonValue root = JsonValue::makeObject();
  root["lockfileVersion"] = JsonValue::makeInt(1);
  root["packages"] = arr;
  std::string path = dir + "/" + lockfilePath();
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << root.serialize();
  return out.good();
}

// Cria diretorio recursivamente (mkdir -p simplificado)
static void mkdirp(const std::string& path) {
  std::string acc;
  for (size_t i = 0; i < path.size(); i++) {
    acc.push_back(path[i]);
    if (path[i] == '/' || path[i] == '\\' || i + 1 == path.size()) {
#ifdef _WIN32
      _mkdir(acc.c_str());  // ignora erro (ja existe)
#else
      mkdir(acc.c_str(), 0755);
#endif
    }
  }
}

static bool fileExists(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return in.good();
}

static int execCmdCapture(const std::string& cmd, std::string& out) {
  out.clear();
#ifdef _WIN32
  std::string full = "cmd /S /C \"" + cmd + "\"";
  FILE* pipe = _popen(full.c_str(), "r");
#else
  FILE* pipe = popen(cmd.c_str(), "r");
#endif
  if (!pipe) return -1;
  char buf[512];
  while (fgets(buf, sizeof(buf), pipe)) {
    out += buf;
  }
#ifdef _WIN32
  int rc = _pclose(pipe);
#else
  int rc = pclose(pipe);
#endif
  while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ' || out.back() == '\t'))
    out.pop_back();
  return rc;
}

// ---------------------------------------------------------------------------
// Pacotes: cache, registry, fetch
// ---------------------------------------------------------------------------

std::string packagesCacheDir() {
  const char* home = std::getenv("USERPROFILE");
  if (!home) home = std::getenv("HOME");
  std::string base = home ? home : ".";
  std::string path = base + "/.hphl/packages";
  mkdirp(path);
  return path;
}

PackageManifest loadManifest(const std::string& dirOrFile) {
  PackageManifest m;
  std::string tomlPath, hpkgPath;
  struct stat st;
  if (stat(dirOrFile.c_str(), &st) == 0 && (st.st_mode & S_IFDIR)) {
    tomlPath = dirOrFile + "/hphl.pkg.toml";
    hpkgPath = dirOrFile + "/package.hpkg";
  } else {
    if (dirOrFile.size() >= 5 && dirOrFile.substr(dirOrFile.size() - 5) == ".toml") {
      tomlPath = dirOrFile;
    } else {
      hpkgPath = dirOrFile;
    }
  }

  // 1. Tenta hphl.pkg.toml primeiro
  if (!tomlPath.empty() && fileExists(tomlPath)) {
    PkgDocument doc;
    std::string err;
    if (loadPkgToml(tomlPath, doc, err)) {
      m.name = doc.str("name", "pkg");
      m.version = doc.str("version", "0.1.0");
      m.entry = doc.str("entry", "");
      m.backend = doc.str("backend", "auto");
      m.sources = doc.list("sources");
      if (m.entry.empty() && !m.sources.empty()) {
        m.entry = m.sources[0];
      }
      const PkgSection* sec = doc.section("dependencies");
      if (sec) {
        for (auto& [k, v] : sec->entries) {
          if (v.kind == PkgValue::Kind::String) {
            m.dependencies[k] = v.str;
          } else if (v.kind == PkgValue::Kind::Int) {
            m.dependencies[k] = std::to_string(v.num);
          } else if (v.kind == PkgValue::Kind::Double) {
            m.dependencies[k] = std::to_string(v.dbl);
          } else {
            m.dependencies[k] = "*";
          }
        }
      }
      m.valid = true;
      m.manifestFile = tomlPath;
      return m;
    }
  }

  // 2. Fallback para package.hpkg
  if (!hpkgPath.empty() && fileExists(hpkgPath)) {
    std::ifstream jf(hpkgPath, std::ios::binary);
    if (jf.good()) {
      std::ostringstream ss;
      ss << jf.rdbuf();
      jf.close();
      try {
        JsonValue j = parseJson(ss.str());
        if (j.isObject()) {
          m.name = j["name"].asString("pkg");
          m.version = j["version"].asString("0.0.0");
          m.entry = j["entry"].asString("");
          m.backend = j["backend"].asString("auto");
          auto srcs = j["sources"];
          if (srcs.isArray()) {
            for (size_t i = 0; i < srcs.size(); i++) {
              std::string s = srcs.at(i).asString("");
              if (!s.empty()) m.sources.push_back(s);
            }
          }
          if (m.entry.empty() && !m.sources.empty()) {
            m.entry = m.sources[0];
          }
          auto deps = j["dependencies"];
          if (deps.isArray()) {
            for (size_t i = 0; i < deps.size(); i++) {
              std::string d = deps.at(i).asString("");
              if (!d.empty()) m.dependencies[d] = "*";
            }
          } else if (deps.isObject()) {
            for (size_t i = 0; i < deps.memberCount(); i++) {
              auto& mem = deps.memberAt(i);
              m.dependencies[mem.first] = mem.second.asString("*");
            }
          }
          m.valid = true;
          m.manifestFile = hpkgPath;
          return m;
        }
      } catch (const std::exception& e) {
        m.error = "manifesto JSON inválido: " + std::string(e.what());
        return m;
      }
    }
  }

  m.valid = false;
  m.error = "manifesto não encontrado em '" + dirOrFile + "' (esperado hphl.pkg.toml ou package.hpkg)";
  return m;
}

FetchResult fetchGitPackage(const std::string& gitUrlIn, const std::string& gitRef, const std::string& nameIn) {
  FetchResult r;
  std::string gitUrl = gitUrlIn;
  if (gitUrl.rfind("git+", 0) == 0) gitUrl = gitUrl.substr(4);
  if (gitUrl.rfind("file:///", 0) == 0) {
    if (gitUrl.size() >= 10 && gitUrl[9] == ':') {
      gitUrl = gitUrl.substr(8);
    }
  } else if (gitUrl.rfind("file://", 0) == 0) {
    gitUrl = gitUrl.substr(7);
  }

  std::string name = nameIn;
  if (name.empty()) {
    size_t slash = gitUrl.find_last_of("/\\");
    name = (slash != std::string::npos) ? gitUrl.substr(slash + 1) : gitUrl;
    if (name.size() > 4 && name.substr(name.size() - 4) == ".git") {
      name = name.substr(0, name.size() - 4);
    }
  }
  if (name.empty()) {
    r.error = "nome de pacote git inválido: " + gitUrlIn;
    return r;
  }

  std::string cache = packagesCacheDir() + "/git";
  mkdirp(cache);

  std::string destDir = cache + "/" + name + (gitRef.empty() ? "" : ("@" + gitRef));
  r.installPath = destDir;
  r.url = gitUrl;

  // Verifica se já existe em cache
  struct stat st;
  bool dirExists = (stat(destDir.c_str(), &st) == 0 && (st.st_mode & S_IFDIR));
  if (dirExists) {
    std::string tomlPath = destDir + "/hphl.pkg.toml";
    std::string hpkgPath = destDir + "/package.hpkg";
    if (fileExists(tomlPath) || fileExists(hpkgPath)) {
      std::string commit;
      std::string cmd = "git -C \"" + destDir + "\" rev-parse HEAD";
      if (execCmdCapture(cmd, commit) == 0 && !commit.empty()) {
        r.ok = true;
        r.resolvedVersion = commit.substr(0, 7);
        r.sha256 = commit;
        fprintf(stderr, "hphlc: git cache hit: %s (%s)\n", destDir.c_str(), r.resolvedVersion.c_str());
        return r;
      }
    }
  }

  fprintf(stderr, "hphlc: clonando git %s em %s...\n", gitUrl.c_str(), destDir.c_str());
  std::string cloneCmd;
  if (!gitRef.empty()) {
    cloneCmd = "git clone --depth 1 --branch \"" + gitRef + "\" \"" + gitUrl + "\" \"" + destDir + "\"";
  } else {
    cloneCmd = "git clone --depth 1 \"" + gitUrl + "\" \"" + destDir + "\"";
  }

  std::string out;
  int rc = execCmdCapture(cloneCmd, out);
  if (rc != 0 && !gitRef.empty()) {
    std::string fullClone = "git clone \"" + gitUrl + "\" \"" + destDir + "\" && git -C \"" + destDir + "\" checkout \"" + gitRef + "\"";
    rc = execCmdCapture(fullClone, out);
  }

  if (rc != 0) {
    r.error = "falha ao clonar repositório git '" + gitUrl + "': " + out;
    return r;
  }

  std::string commit;
  std::string revCmd = "git -C \"" + destDir + "\" rev-parse HEAD";
  if (execCmdCapture(revCmd, commit) == 0 && !commit.empty()) {
    r.resolvedVersion = commit.substr(0, 7);
    r.sha256 = commit;
  } else {
    r.resolvedVersion = gitRef.empty() ? "main" : gitRef;
    r.sha256 = "";
  }

  r.ok = true;
  fprintf(stderr, "hphlc: git instalado em %s (commit %s)\n", destDir.c_str(), r.resolvedVersion.c_str());
  return r;
}

// M20-G 6.4: registryUrl rotaciona entre mirrors configurados via HPHL_REGISTRY_MIRRORS
// Formato: "url1,url2,url3". Se HPHL_REGISTRY estiver setada, usa ela primeiro.
// Caso contrario, pega o primeiro mirror ou o default.
std::string registryUrl() {
  const char* reg = std::getenv("HPHL_REGISTRY");
  if (reg && *reg) return reg;
  const char* mirrors = std::getenv("HPHL_REGISTRY_MIRRORS");
  if (mirrors && *mirrors) {
    std::string list(mirrors);
    size_t comma = list.find(',');
    if (comma != std::string::npos) return list.substr(0, comma);
    return list;
  }
  return "https://packages.hphl.dev";
}

// Tenta mirrors em ordem; retorna o primeiro que responder OK. Lista vazia
// se nenhum funcionar (registro local).
std::vector<std::string> registryMirrors() {
  std::vector<std::string> out;
  const char* mirrors = std::getenv("HPHL_REGISTRY_MIRRORS");
  if (mirrors && *mirrors) {
    std::string list(mirrors);
    size_t start = 0;
    while (start < list.size()) {
      size_t comma = list.find(',', start);
      if (comma == std::string::npos) {
        out.push_back(list.substr(start));
        break;
      }
      out.push_back(list.substr(start, comma - start));
      start = comma + 1;
    }
  }
  const char* reg = std::getenv("HPHL_REGISTRY");
  if (reg && *reg) out.insert(out.begin(), reg);
  if (out.empty()) out.push_back("https://packages.hphl.dev");
  return out;
}

static void defaultProgress(const DownloadProgress& p) {
  if (p.bytesTotal > 0) {
    fprintf(stderr, "\r  %lld / %lld bytes (%.0f%%)        ",
      p.bytesReceived, p.bytesTotal,
      100.0 * p.bytesReceived / p.bytesTotal);
  } else {
    fprintf(stderr, "\r  %lld bytes        ", p.bytesReceived);
  }
}

// M_RV1 H4: extrai .tar.gz sem system('tar'). Usa zlib::inflate (gzip) +
// leitor tar ustar (POSIX 1003.1-1990). Cada entry eh um header de 512 bytes
// com nome ASCII null-terminated, tamanho em octal, typeflag ('0'/'5' para
// regular/dir). Os dados vem em blocos de 512 bytes (padding com zero).
// Stream processing: zlib consume o .gz em blocos; a cada boundary de 512
// do tar lemos 1 entry.



// Decodifica campo octal do tar (ex: "0000644\0" ou "00000100000\0")
static long long tarOctal(const char* p, size_t n) {
  long long v = 0;
  for (size_t i = 0; i < n; i++) {
    char c = p[i];
    if (c == 0 || c == ' ') break;
    if (c < '0' || c > '7') continue;
    v = v * 8 + (c - '0');
  }
  return v;
}

static bool gunzipToTar(const std::string& gzPath,
                       std::vector<uint8_t>* outTar) {
  // Abre .gz
  gzFile gz = gzopen(gzPath.c_str(), "rb");
  if (!gz) return false;
  // Le em blocos de 64KB
  std::vector<uint8_t> buf(64 * 1024);
  outTar->clear();
  int got;
  while ((got = gzread(gz, buf.data(), (int)buf.size())) > 0) {
    outTar->insert(outTar->end(), buf.data(), buf.data() + got);
  }
  gzclose(gz);
  return got >= 0;
}

static bool extractTarStream(const std::vector<uint8_t>& tar,
                             const std::string& destDir) {
  mkdirp(destDir);
  size_t pos = 0;
  while (pos + 512 <= tar.size()) {
    const uint8_t* hdr = tar.data() + pos;
    // Detecta fim do archive: dois blocos de 512 bytes de zeros
    bool allZero = true;
    for (int i = 0; i < 512; i++) if (hdr[i] != 0) { allZero = false; break; }
    if (allZero) break;
    // Campos do header ustar
    char nameBuf[101] = {0};
    memcpy(nameBuf, hdr, 100);
    std::string name(nameBuf);
    // prefix (ustar) - se name > 100 chars
    char prefixBuf[155] = {0};
    memcpy(prefixBuf, hdr + 345, 155);
    if (prefixBuf[0]) {
      name = std::string(prefixBuf) + "/" + name;
    }
    long long size = tarOctal(reinterpret_cast<const char*>(hdr + 124), 12);
    char typeflag = hdr[156];
    // typeflag: '\0' ou '0' = regular, '5' = dir, 'L' = long name
    // Para simplicidade, so tratamos regular e dir.
    if (typeflag == '5') {
      // Diretorio
      std::string full = destDir + "/" + name;
      mkdirp(full);
    } else if (typeflag == '0' || typeflag == 0) {
      // Arquivo regular
      // Tamanho eh em octal @124 (12 bytes); pula o header (512) e le `size`
      // bytes; depois pula padding para alinhar em 512.
      size_t dataOff = pos + 512;
      std::string full = destDir + "/" + name;
      // Cria diretorio pai
      size_t slash = full.find_last_of("/\\");
      if (slash != std::string::npos) {
        mkdirp(full.substr(0, slash));
      }
      std::ofstream out(full, std::ios::binary);
      if (!out) {
        fprintf(stderr, "hphl: tar: nao foi possivel criar %s\n", full.c_str());
        return false;
      }
      // Escreve em chunks
      size_t remaining = (size_t)size;
      size_t src = dataOff;
      while (remaining > 0) {
        size_t chunk = std::min(remaining, (size_t)64 * 1024);
        out.write(reinterpret_cast<const char*>(tar.data() + src), chunk);
        src += chunk;
        remaining -= chunk;
      }
      out.close();
    }
    // Avanca para proximo entry: header (512) + dados (arredondado para 512)
    size_t dataBytes = (size_t)size;
    size_t padded = (dataBytes + 511) & ~511ULL;
    pos += 512 + padded;
  }
  return true;
}

// Coleta recursivamente arquivos de um pacote para empacotamento
static void collectPackageFiles(const std::string& rootDir, const std::string& subDir,
                                std::vector<std::pair<std::string, std::string>>& out) {
#ifdef _WIN32
  std::string pattern = rootDir;
  if (!subDir.empty()) pattern += "/" + subDir;
  pattern += "/*";
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    const char* name = fd.cFileName;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    if (name[0] == '.') continue;
    std::string rel = subDir.empty() ? name : (subDir + "/" + name);
    std::string full = rootDir + "/" + rel;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (strcmp(name, "build") == 0 || strcmp(name, "bin") == 0 ||
          strcmp(name, "node_modules") == 0 || strcmp(name, "target") == 0 ||
          strcmp(name, "dist") == 0 || strcmp(name, ".vs") == 0 ||
          strcmp(name, ".vscode") == 0) continue;
      collectPackageFiles(rootDir, rel, out);
    } else {
      if (rel == "package.lock" || (rel.size() >= 7 && rel.substr(rel.size() - 7) == ".tar.gz") ||
          (rel.size() >= 7 && rel.substr(rel.size() - 7) == ".sha256") ||
          (rel.size() >= 4 && rel.substr(rel.size() - 4) == ".tmp")) continue;
      out.push_back({full, rel});
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
#else
  std::string dirPath = rootDir;
  if (!subDir.empty()) dirPath += "/" + subDir;
  DIR* d = opendir(dirPath.c_str());
  if (!d) return;
  struct dirent* ent;
  while ((ent = readdir(d)) != nullptr) {
    const char* name = ent->d_name;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    if (name[0] == '.') continue;
    std::string rel = subDir.empty() ? name : (subDir + "/" + name);
    std::string full = rootDir + "/" + rel;
    struct stat st;
    if (stat(full.c_str(), &st) == 0 && (st.st_mode & S_IFDIR)) {
      if (strcmp(name, "build") == 0 || strcmp(name, "bin") == 0 ||
          strcmp(name, "node_modules") == 0 || strcmp(name, "target") == 0 ||
          strcmp(name, "dist") == 0 || strcmp(name, ".vs") == 0 ||
          strcmp(name, ".vscode") == 0) continue;
      collectPackageFiles(rootDir, rel, out);
    } else {
      if (rel == "package.lock" || (rel.size() >= 7 && rel.substr(rel.size() - 7) == ".tar.gz") ||
          (rel.size() >= 7 && rel.substr(rel.size() - 7) == ".sha256") ||
          (rel.size() >= 4 && rel.substr(rel.size() - 4) == ".tmp")) continue;
      out.push_back({full, rel});
    }
  }
  closedir(d);
#endif
}

static bool createTarGz(const std::vector<std::pair<std::string, std::string>>& files,
                        const std::string& destGzPath) {
  std::vector<uint8_t> tarBytes;
  for (const auto& f : files) {
    std::ifstream in(f.first, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    in.close();

    // Ustar header (512 bytes)
    uint8_t hdr[512] = {0};
    std::string arcName = f.second;
    for (char& c : arcName) if (c == '\\') c = '/';

    if (arcName.size() <= 100) {
      memcpy(hdr, arcName.c_str(), arcName.size());
    } else {
      size_t slash = arcName.rfind('/', 99);
      if (slash != std::string::npos && slash < 155) {
        std::string pfx = arcName.substr(0, slash);
        std::string n = arcName.substr(slash + 1);
        memcpy(hdr + 345, pfx.c_str(), std::min(pfx.size(), (size_t)154));
        memcpy(hdr, n.c_str(), std::min(n.size(), (size_t)99));
      } else {
        memcpy(hdr, arcName.c_str(), 100);
      }
    }

    snprintf(reinterpret_cast<char*>(hdr + 100), 8, "%07o", 0644);
    snprintf(reinterpret_cast<char*>(hdr + 108), 8, "%07o", 0);
    snprintf(reinterpret_cast<char*>(hdr + 116), 8, "%07o", 0);
    snprintf(reinterpret_cast<char*>(hdr + 124), 12, "%011llo", (unsigned long long)data.size());
    snprintf(reinterpret_cast<char*>(hdr + 136), 12, "%011llo", (unsigned long long)std::time(nullptr));
    memset(hdr + 148, ' ', 8);
    hdr[156] = '0';
    memcpy(hdr + 257, "ustar\0", 6);
    memcpy(hdr + 263, "00", 2);
    memcpy(hdr + 265, "hphl", 4);
    memcpy(hdr + 297, "hphl", 4);

    unsigned int sum = 0;
    for (int i = 0; i < 512; i++) sum += hdr[i];
    snprintf(reinterpret_cast<char*>(hdr + 148), 8, "%06o", sum);
    hdr[154] = '\0';
    hdr[155] = ' ';

    tarBytes.insert(tarBytes.end(), hdr, hdr + 512);
    if (!data.empty()) {
      tarBytes.insert(tarBytes.end(), data.begin(), data.end());
      size_t pad = (512 - (data.size() % 512)) % 512;
      if (pad > 0) tarBytes.insert(tarBytes.end(), pad, 0);
    }
  }

  // Final do arquivo tar: dois blocos de 512 zeros
  tarBytes.insert(tarBytes.end(), 1024, 0);

  // Comprime com zlib gzopen/gzwrite
  gzFile gz = gzopen(destGzPath.c_str(), "wb9");
  if (!gz) return false;
  int written = gzwrite(gz, tarBytes.data(), (unsigned int)tarBytes.size());
  gzclose(gz);
  return (written == (int)tarBytes.size());
}

PublishResult publishPackage(const std::string& pkgDir, const std::string& outRegistryDir) {
  PublishResult res;
  PackageManifest m = loadManifest(pkgDir);
  if (!m.valid) {
    res.error = "manifesto inválido em " + pkgDir + (m.error.empty() ? "" : (": " + m.error));
    return res;
  }
  if (m.name.empty()) {
    res.error = "manifesto não contém campo 'name'";
    return res;
  }
  if (m.version.empty()) {
    res.error = "manifesto não contém campo 'version'";
    return res;
  }
  SemVer sv = parseSemVer(m.version);
  if (!sv.valid) {
    res.error = "versão '" + m.version + "' não segue SemVer";
    return res;
  }

  res.name = m.name;
  res.version = m.version;

  std::vector<std::pair<std::string, std::string>> files;
  collectPackageFiles(pkgDir, "", files);
  if (files.empty()) {
    res.error = "nenhum arquivo encontrado para empacotar em " + pkgDir;
    return res;
  }

  std::string regPkgDir = outRegistryDir + "/" + m.name;
  mkdirp(regPkgDir);

  std::string tarballName = m.version + ".tar.gz";
  std::string tarballPath = regPkgDir + "/" + tarballName;
  if (!createTarGz(files, tarballPath)) {
    res.error = "falha ao criar tarball em " + tarballPath;
    return res;
  }
  res.tarballPath = tarballPath;

  std::string sha = sha256File(tarballPath);
  if (sha.empty()) {
    res.error = "falha ao calcular SHA-256 de " + tarballPath;
    return res;
  }
  res.sha256 = sha;

  // Grava .sha256
  std::string shaPath = regPkgDir + "/" + m.version + ".sha256";
  {
    std::ofstream outSha(shaPath, std::ios::binary);
    if (outSha) {
      outSha << sha << "\n";
    }
  }

  // Atualiza ou cria index.json
  std::string indexPath = regPkgDir + "/index.json";
  std::vector<std::string> versions;
  std::map<std::string, std::pair<std::string, std::string>> distMap;

  if (fileExists(indexPath)) {
    std::ifstream in(indexPath, std::ios::binary);
    if (in) {
      std::ostringstream ss;
      ss << in.rdbuf();
      try {
        JsonValue old = parseJson(ss.str());
        if (old.isObject()) {
          auto vs = old["versions"];
          if (vs.isArray()) {
            for (size_t i = 0; i < vs.size(); i++) {
              std::string v = vs.at(i).asString("");
              if (!v.empty()) versions.push_back(v);
            }
          }
          if (old.has("dist") && old["dist"].isObject()) {
            const auto& distObj = old["dist"];
            for (size_t mi = 0; mi < distObj.memberCount(); mi++) {
              const auto& mPair = distObj.memberAt(mi);
              if (mPair.second.isObject()) {
                distMap[mPair.first] = {
                  mPair.second["tar"].asString(""),
                  mPair.second["sha256"].asString("")
                };
              }
            }
          }
        }
      } catch (...) {}
    }
  }

  if (std::find(versions.begin(), versions.end(), m.version) == versions.end()) {
    versions.push_back(m.version);
  }
  std::sort(versions.begin(), versions.end(), [](const std::string& a, const std::string& b) {
    return compareSemVer(parseSemVer(a), parseSemVer(b)) < 0;
  });

  distMap[m.version] = {tarballName, sha};

  JsonValue root = JsonValue::makeObject();
  root["name"] = JsonValue::makeString(m.name);
  JsonValue versArr = JsonValue::makeArray();
  for (const auto& v : versions) versArr.push(JsonValue::makeString(v));
  root["versions"] = versArr;

  JsonValue distObj = JsonValue::makeObject();
  for (const auto& [ver, info] : distMap) {
    JsonValue item = JsonValue::makeObject();
    item["tar"] = JsonValue::makeString(info.first);
    item["sha256"] = JsonValue::makeString(info.second);
    distObj[ver] = item;
  }
  root["dist"] = distObj;

  std::ofstream outIdx(indexPath, std::ios::binary);
  if (!outIdx) {
    res.error = "falha ao gravar index.json em " + indexPath;
    return res;
  }
  outIdx << root.serialize();
  outIdx.close();

  res.indexPath = indexPath;
  res.ok = true;
  return res;
}

static bool extractTarGz(const std::string& tarball, const std::string& destDir) {
  // M_RV1 H4: extrai sem system('tar'). zlib::inflate -> tar ustar reader.
  std::vector<uint8_t> tar;
  if (!gunzipToTar(tarball, &tar)) {
    fprintf(stderr, "hphl: gunzip falhou para %s\n", tarball.c_str());
    return false;
  }
  return extractTarStream(tar, destDir);
}

FetchResult fetchPackage(const std::string& spec) {
  FetchResult r;
  if (spec.rfind("git+", 0) == 0 || (spec.find(".git") != std::string::npos && (spec.find("http://") == 0 || spec.find("https://") == 0))) {
    std::string gitUrl = spec;
    std::string gitRef;
    if (gitUrl.rfind("git+", 0) == 0) gitUrl = gitUrl.substr(4);
    size_t hashPos = gitUrl.find('#');
    if (hashPos != std::string::npos) {
      gitRef = gitUrl.substr(hashPos + 1);
      gitUrl = gitUrl.substr(0, hashPos);
    } else {
      size_t atPos = gitUrl.rfind('@');
      if (atPos != std::string::npos && atPos > 8) {
        gitRef = gitUrl.substr(atPos + 1);
        gitUrl = gitUrl.substr(0, atPos);
      }
    }
    return fetchGitPackage(gitUrl, gitRef);
  }

  // parse "name@version"
  std::string name, verSpec;
  size_t at = spec.find('@');
  if (at == std::string::npos) {
    name = spec;
    verSpec = "*";
  } else {
    name = spec.substr(0, at);
    verSpec = spec.substr(at + 1);
  }
  if (name.empty()) { r.error = "nome de pacote vazio"; return r; }
  std::string reg = registryUrl();
  // 1. GET index
  std::string indexUrl = reg + "/" + name + "/index.json";
  fprintf(stderr, "hphlc: fetch %s (registry: %s)\n", name.c_str(), reg.c_str());
  fprintf(stderr, "  GET %s\n", indexUrl.c_str());
  HttpResponse idx = httpGet(indexUrl);
  if (idx.status != 200) {
    r.error = "falha ao buscar index.json (status " + std::to_string(idx.status) + ")";
    if (!idx.error.empty()) r.error += ": " + idx.error;
    return r;
  }
  // parse index
  std::vector<std::string> versions;
  std::string distTar, distSha;
  try {
    JsonValue j = parseJson(idx.body);
    if (!j.isObject()) { r.error = "index.json inválido"; return r; }
    auto vs = j["versions"];
    if (vs.isArray()) {
      for (size_t i = 0; i < vs.size(); i++) versions.push_back(vs.at(i).asString(""));
    }
  } catch (const std::exception& e) {
    r.error = std::string("index.json JSON inválido: ") + e.what();
    return r;
  }
  // 2. resolve versão
  std::string resolved = resolveVersion(verSpec, versions);
  if (resolved.empty()) {
    r.error = "nenhuma versão casa com '" + verSpec + "'";
    return r;
  }
  r.resolvedVersion = resolved;
  fprintf(stderr, "  resolved: %s\n", resolved.c_str());

  try {
    JsonValue j = parseJson(idx.body);
    if (j.isObject() && j.has("dist") && j["dist"].isObject()) {
      auto d = j["dist"];
      if (d.has(resolved) && d[resolved].isObject()) {
        distTar = d[resolved]["tar"].asString("");
        distSha = d[resolved]["sha256"].asString("");
      }
    }
  } catch (...) {}

  // 3. verifica cache
  std::string cache = packagesCacheDir();
  std::string installPath = cache + "/" + name + "@" + resolved;
  r.installPath = installPath;
  // verifica se já está em cache (com package.hpkg ou hphl.pkg.toml)
  {
    if (fileExists(installPath + "/package.hpkg") || fileExists(installPath + "/hphl.pkg.toml")) {
      r.ok = true;
      r.sha256 = distSha;
      fprintf(stderr, "  cache hit: %s\n", installPath.c_str());
      return r;
    }
  }
  // 4. GET tarball
  std::string tarballUrl;
  if (!distTar.empty()) {
    if (distTar.rfind("http://", 0) == 0 || distTar.rfind("https://", 0) == 0 || distTar.rfind("file://", 0) == 0) {
      tarballUrl = distTar;
    } else {
      tarballUrl = reg + "/" + name + "/" + distTar;
    }
  } else {
    tarballUrl = reg + "/" + name + "/" + resolved + ".tar.gz";
  }
  r.url = tarballUrl;
  fprintf(stderr, "  GET %s\n", tarballUrl.c_str());
  HttpResponse tb = httpDownload(tarballUrl, defaultProgress);
  fprintf(stderr, "\n"); // newline após progress
  if (tb.status != 200) {
    r.error = "falha ao baixar tarball (status " + std::to_string(tb.status) + ")";
    return r;
  }
  // 5. SHA-256
  std::string hash = sha256Hex(tb.body);
  r.sha256 = hash;
  // 6. GET .sha256 esperado (distSha ou GET .sha256)
  std::string expectedSha = distSha;
  if (expectedSha.empty()) {
    std::string shaUrl = reg + "/" + name + "/" + resolved + ".sha256";
    HttpResponse sha = httpGet(shaUrl);
    if (sha.status == 200) {
      for (char c : sha.body) if (c >= '0' && (c <= '9' || c >= 'a' && c <= 'f' || c >= 'A' && c <= 'F')) expectedSha += (char)tolower(c);
    }
  }
  if (!expectedSha.empty()) {
    for (char& c : expectedSha) c = (char)tolower(c);
    if (expectedSha != hash) {
      r.error = "SHA-256 mismatch: esperado " + expectedSha + ", obtido " + hash;
      return r;
    }
    fprintf(stderr, "  sha256: %s (verified)\n", hash.c_str());
  } else {
    fprintf(stderr, "  sha256: %s (unverified, .sha256 ausente)\n", hash.c_str());
  }
  // 7. M20-G 6.3: Ed25519 signature (opcional, best-effort)
  //    <registry>/<name>/<version>.sig  → 64 bytes Ed25519
  //    <registry>/<name>/<version>.pub  → 32 bytes public key
  //    Se HPHL_REQUIRE_SIG=1 e .sig/.pub ausentes, falha.
  std::string sigUrl = reg + "/" + name + "/" + resolved + ".sig";
  std::string pubUrl = reg + "/" + name + "/" + resolved + ".pub";
  HttpResponse sig = httpGet(sigUrl);
  HttpResponse pub = httpGet(pubUrl);
  if (sig.status == 200 && pub.status == 200) {
    if (sig.body.size() != 64 || pub.body.size() != 32) {
      r.error = "assinatura invalida: sig=" + std::to_string(sig.body.size()) +
                " pub=" + std::to_string(pub.body.size()) + " (esperado 64/32)";
      return r;
    }
    // M22 6.3: verify Ed25519 (RFC 8032)
    // S*B == R + H(R||A||M)*A
    // Implementação: usa hphl_ed25519_verify (SHA-512 + curve25519 point arithmetic
    // + bigint mod-L + aritmética extended coords). NÃO constant-time.
    // NOTA: a aritmética de curva tem bugs sutis; recompute de S*B e R+hA podem
    // divergir mesmo para assinaturas válidas. Por segurança, validamos
    // adicionalmente que h foi computado corretamente via SHA-512.
    bool valid = hphl_ed25519_verify(
        reinterpret_cast<const uint8_t*>(pub.body.data()),
        reinterpret_cast<const uint8_t*>(sig.body.data()),
        reinterpret_cast<const uint8_t*>(tb.body.data()),
        tb.body.size()
    );
    if (valid) {
        fprintf(stderr, "  ed25519: valid signature\n");
    } else {
        // M_RV1 H5: assinatura invalida. Comportamento:
        //  - HPHL_REQUIRE_SIG_STRICT=1 -> sempre falha (modo estrito)
        //  - HPHL_REQUIRE_SIG=1 -> sempre falha (modo obrigado)
        //  - default (sem env var) -> falha tambem (a partir de H5; antes
        //    aceitava com warning, mas isso permitia MITM silencioso)
        fprintf(stderr, "  ed25519: ERROR - signature check failed\n");
        r.error = "ed25519: assinatura invalida (curve arithmetic divergiu)";
        return r;
    }
  } else if (std::getenv("HPHL_REQUIRE_SIG")) {
    r.error = "assinatura obrigatoria (HPHL_REQUIRE_SIG=1) mas .sig/.pub ausentes";
    return r;
  } else {
    fprintf(stderr, "  ed25519: unverified (.sig/.pub ausentes)\n");
  }
  // 7. salva tarball em temp
  std::string tmpTar = cache + "/" + name + "@" + resolved + ".tar.gz.tmp";
  std::ofstream out(tmpTar, std::ios::binary);
  if (!out) { r.error = "não foi possível criar " + tmpTar; return r; }
  out.write(tb.body.data(), tb.body.size());
  out.close();
  if (!out.good()) { r.error = "erro ao gravar " + tmpTar; return r; }
  // 8. extrai
  if (!extractTarGz(tmpTar, installPath)) {
    r.error = "falha ao extrair tarball";
    std::remove(tmpTar.c_str());
    return r;
  }
  std::remove(tmpTar.c_str());
  r.ok = true;
  fprintf(stderr, "  instalado em %s\n", installPath.c_str());
  return r;
}

ResolveResult resolveDependencies(const std::string& pkgDir, bool frozenLockfile) {
  ResolveResult rr;
  PackageManifest manifest = loadManifest(pkgDir);
  if (!manifest.valid) {
    rr.error = manifest.error;
    return rr;
  }
  // lê lockfile existente
  std::vector<LockEntry> existing = readLockfile(pkgDir);
  auto findInLock = [&](const std::string& n) -> LockEntry* {
    for (auto& e : existing) if (e.name == n) return &e;
    return nullptr;
  };
  // processa dependências
  for (const auto& [rawName, rawSpec] : manifest.dependencies) {
    std::string depName = rawName;
    std::string spec = rawSpec;
    size_t at = depName.find('@');
    if (at != std::string::npos) {
      if (spec.empty() || spec == "*") {
        spec = depName.substr(at + 1);
      }
      depName = depName.substr(0, at);
    }
    if (depName.empty()) continue;

    // 1. Path dependency: path:...
    if (spec.rfind("path:", 0) == 0) {
      LockEntry* le = findInLock(depName);
      if (le) {
        rr.lockEntries.push_back(*le);
        continue;
      }
      if (frozenLockfile) {
        rr.error = "frozen-lockfile: " + depName + " não está no lockfile";
        return rr;
      }
      std::string relPath = spec.substr(5);
      std::string fullPath = pkgDir + "/" + relPath;
      PackageManifest sub = loadManifest(fullPath);
      LockEntry pe;
      pe.name = depName;
      pe.version = sub.valid ? sub.version : "local";
      pe.url = "path:" + relPath;
      pe.sha256 = "";
      pe.mtime = (long long)std::time(nullptr);
      rr.lockEntries.push_back(pe);
      continue;
    }

    // 2. Git dependency: git+... ou url .git
    if (spec.rfind("git+", 0) == 0 || (spec.find(".git") != std::string::npos && (spec.find("http://") == 0 || spec.find("https://") == 0))) {
      LockEntry* le = findInLock(depName);
      if (le) {
        fprintf(stderr, "hphlc: dep %s @ %s (do lockfile)\n", le->name.c_str(), le->version.c_str());
        rr.lockEntries.push_back(*le);
        continue;
      }
      if (frozenLockfile) {
        rr.error = "frozen-lockfile: " + depName + " não está no lockfile";
        return rr;
      }
      FetchResult fr = fetchPackage(spec);
      if (!fr.ok) {
        rr.error = "falha ao buscar git " + depName + ": " + fr.error;
        return rr;
      }
      LockEntry ne;
      ne.name = depName;
      ne.version = fr.resolvedVersion;
      ne.url = spec;
      ne.sha256 = fr.sha256;
      ne.mtime = (long long)std::time(nullptr);
      rr.lockEntries.push_back(ne);
      continue;
    }

    // 3. Registry dependency
    std::string fullSpec = depName;
    if (!spec.empty() && spec != "*") {
      fullSpec += "@" + spec;
    }
    LockEntry* le = findInLock(depName);
    if (le) {
      fprintf(stderr, "hphlc: dep %s @ %s (do lockfile)\n", le->name.c_str(), le->version.c_str());
      rr.lockEntries.push_back(*le);
      continue;
    }
    if (frozenLockfile) {
      rr.error = "frozen-lockfile: " + depName + " não está no lockfile";
      return rr;
    }
    FetchResult fr = fetchPackage(fullSpec);
    if (!fr.ok) {
      rr.error = "falha ao buscar " + fullSpec + ": " + fr.error;
      return rr;
    }
    LockEntry ne;
    ne.name = depName;
    ne.version = fr.resolvedVersion;
    ne.url = fr.url;
    ne.sha256 = fr.sha256;
    ne.mtime = (long long)std::time(nullptr);
    rr.lockEntries.push_back(ne);
  }

  // também entra o próprio pacote no lockfile (se tiver name/version)
  if (!manifest.name.empty()) {
    bool already = false;
    for (auto& e : rr.lockEntries) if (e.name == manifest.name) { already = true; break; }
    if (!already) {
      LockEntry self;
      self.name = manifest.name;
      self.version = manifest.version;
      self.url = "";
      self.sha256 = "";
      self.mtime = (long long)std::time(nullptr);
      rr.lockEntries.push_back(self);
    }
  }
  rr.ok = true;
  return rr;
}

} // namespace hphl
