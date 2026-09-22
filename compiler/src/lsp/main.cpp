#include "lsp.h"
#include "json/json.h"
#include "frontend/lexer.h"
#include "loader/loader.h"
#include "frontend/parser.h"
#include "semantic/semantic.h"
#include "version.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace hphl {
namespace {


#include "uri.inc"
#include "utf16.inc"
#include "transport.inc"
#include "diagnostics.inc"
#include "types.inc"
#include "server.inc"
#include "transport_impl.inc"
#include "analysis.inc"
#include "features.inc"
} // namespace

int lspMain() {
  LspServer server;
  return server.run();
}

} // namespace hphl