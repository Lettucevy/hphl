#pragma once

namespace hphl {

// Servidor LSP (Milestone 6): `hphlc --lsp` — lê mensagens JSON-RPC 2.0 do
// stdin (framing Content-Length) e responde no stdout. Logs vão para stderr.
int lspMain();

} // namespace hphl