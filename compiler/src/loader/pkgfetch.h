// ============================================================================
// pkgfetch.h — Package fetcher (M17 fetch remoto real)
//
// Faz GET HTTPS via WinHTTP (Windows) ou POSIX sockets (Linux/macOS),
// resolve versões semver (^/~/>=/=), valida SHA-256, gera/usa package.lock.
//
// Registry: configurável via HPHL_REGISTRY env var (default:
// https://packages.hphl.dev). Layout:
//   <registry>/<name>/index.json         → lista de versões
//   <registry>/<name>/<version>.tar.gz   → tarball do pacote
//   <registry>/<name>/<version>.sha256   → hash SHA-256 do tarball
//
// Cache: ~/.hphl/packages/<name>@<version>/
// Lockfile: package.lock (JSON) no diretório do projeto
// ============================================================================

#ifndef HPHL_PKGFETCH_H
#define HPHL_PKGFETCH_H

#include <string>
#include <vector>
#include <utility>
#include <map>

namespace hphl {

// resultado de um GET HTTP/HTTPS
struct HttpResponse {
  int status = 0;           // 200, 404, etc
  std::string body;         // corpo da resposta
  std::string error;        // mensagem de erro (vazia se OK)
  long long bytesReceived = 0;
};

// progresso de download (chamado periodicamente)
struct DownloadProgress {
  std::string url;
  long long bytesReceived = 0;
  long long bytesTotal = 0; // -1 se Content-Length ausente
};

// faz GET HTTP/HTTPS. url: full URL (http:// ou https://).
// onProgress: callback opcional chamado a cada 64KB recebidos.
HttpResponse httpGet(const std::string& url,
                    void (*onProgress)(const DownloadProgress&) = nullptr);

// download binário (igual httpGet mas retorna bytes brutos, útil para tarballs)
HttpResponse httpDownload(const std::string& url,
                         void (*onProgress)(const DownloadProgress&) = nullptr);

// SHA-256 hex de uma string
std::string sha256Hex(const std::string& data);

// SHA-256 hex de um arquivo
std::string sha256File(const std::string& path);

// resolve uma especificação de versão (^1.0.0, ~1.0.0, 1.0.0, >=1.0.0) contra
// uma lista de versões candidatas. Retorna a maior versão compatível, ou
// string vazia se nenhuma casa.
std::string resolveVersion(const std::string& spec,
                          const std::vector<std::string>& available);

// faz parse de uma string semver "MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]"
struct SemVer {
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::string preRelease;
  std::string build;
  std::string original;
  bool valid = false;
};
SemVer parseSemVer(const std::string& v);
int compareSemVer(const SemVer& a, const SemVer& b); // -1, 0, 1

// entry do package.lock
struct LockEntry {
  std::string name;
  std::string version;
  std::string url;
  std::string sha256;
  long long mtime = 0;
};

// lê package.lock do diretório (retorna vetor vazio se não existe)
std::vector<LockEntry> readLockfile(const std::string& dir);

// grava package.lock no diretório (ordena por nome para reprodutibilidade)
bool writeLockfile(const std::string& dir, const std::vector<LockEntry>& entries);

// resultado de uma operação fetch
struct FetchResult {
  bool ok = false;
  std::string error;
  std::string resolvedVersion;  // versão efetivamente baixada
  std::string sha256;           // hash do tarball
  std::string installPath;      // onde foi extraído (~/.hphl/packages/<name>@<ver>)
  std::string url;              // URL do tarball
};

// Informações do manifesto (hphl.pkg.toml ou package.hpkg)
struct PackageManifest {
  std::string name;
  std::string version;
  std::string entry;
  std::string backend;
  std::vector<std::string> sources;
  std::map<std::string, std::string> dependencies;
  bool valid = false;
  std::string error;
  std::string manifestFile;
};

// Carrega hphl.pkg.toml (prioridade) ou package.hpkg de um diretório ou caminho de arquivo
PackageManifest loadManifest(const std::string& dirOrFile);

// baixa e extrai um pacote: GET <registry>/<name>/index.json → escolhe versão
// → GET <registry>/<name>/<version>.tar.gz → valida SHA-256 → extrai em
// ~/.hphl/packages/<name>@<version>/
// spec: "util" ou "util@1.0.0" ou "util@^1.0.0" ou URL git (git+https://... ou https://...git)
FetchResult fetchPackage(const std::string& spec);

// Clona ou reutiliza pacote via Git shallow clone em ~/.hphl/packages/git/<name>@<ref_ou_hash>
FetchResult fetchGitPackage(const std::string& gitUrl, const std::string& gitRef = "", const std::string& name = "");

// resolve todas as dependências de um projeto em <pkgDir> (hphl.pkg.toml ou package.hpkg),
// baixando as que faltam. Retorna lista de LockEntry para o lockfile.
struct ResolveResult {
  bool ok = false;
  std::string error;
  std::vector<LockEntry> lockEntries; // entradas a serem gravadas
};
ResolveResult resolveDependencies(const std::string& pkgDir, bool frozenLockfile);

// retorna o caminho do diretório de cache (~/.hphl/packages/) criando-o se preciso
std::string packagesCacheDir();

// retorna a URL do registry (HPHL_REGISTRY env var ou default)
std::string registryUrl();

// M20-G 6.4: retorna lista de mirrors (HPHL_REGISTRY_MIRRORS=url1,url2,...)
std::vector<std::string> registryMirrors();

// resultado de uma operação publish
struct PublishResult {
  bool ok = false;
  std::string error;
  std::string name;
  std::string version;
  std::string tarballPath;
  std::string sha256;
  std::string indexPath;
};

// Empacota o pacote em <pkgDir> e gera o tarball <outRegistryDir>/<name>/<version>.tar.gz,
// o checksum <outRegistryDir>/<name>/<version>.sha256 e atualiza <outRegistryDir>/<name>/index.json
PublishResult publishPackage(const std::string& pkgDir, const std::string& outRegistryDir);

} // namespace hphl

#endif // HPHL_PKGFETCH_H
