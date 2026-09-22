// dap.cpp — Debug Adapter Protocol (Milestone 7): `hphlc --dap`
//
// Recebe requests DAP (JSON-RPC 2.0, framing Content-Length igual ao LSP) e
// depura um programa HP-HL compilado com --debug. O executável se conecta ao
// adapter via named pipe (variável de ambiente HPHL_DBG_PIPE) e fala o
// protocolo de texto do runtime.c (ready/meta/fn/loc/trp/gbl/paused/frame/
// rvar/rstr/ok/fail ← continue/next/stepin/stepout/pause/quit/bpx/read/
// readarr/readgbl/readgblarr/readstr).
//
// Escopo v1 (M7): breakpoints por linha, continue/pause/step, pilha de
// chamadas, variáveis (escalares + strings + arrays) e evaluate de nomes +
// literais + aritmética simples.

#include "dap.h"
#include "codegen/codegen.h"
#include "json/json.h"
#include "loader/loader.h"
#include "semantic/semantic.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#error "hphlc --dap: o debugger v1 depende de named pipes do Windows"
#endif

namespace hphl {
namespace {


#include "build.inc"
#include "transport.inc"
#include "metadata.inc"
#include "server.inc"
#include "io.inc"
#include "runtime.inc"
#include "commands.inc"
#include "formatting.inc"
#include "handlers.inc"
#include "loop.inc"
} // namespace

int dapMain() {
  DapServer server;
  return server.run();
}

} // namespace hphl
