#pragma once

namespace hphl {

// Servidor DAP (Milestone 7): `hphlc --dap` — Debug Adapter Protocol sobre
// stdio (framing Content-Length, mesmo esquema do LSP). O launch compila o
// fonte com --debug e depura o executável via named pipe (HPHL_DBG_PIPE).
int dapMain();

} // namespace hphl
