/* ==========================================================================
 * Debugger (Milestone 7): instrumentaÃ§Ã£o --debug do hphlc (backend x64).
 *
 * O codegen com --debug emite, por statement executÃ¡vel:
 *     movq $LINHA, %rdi ; call hphl_dbg_trap
 * e no prÃ³logo/epÃ­logo de cada funÃ§Ã£o:
 *     movq $FNID, %rdi  ; call hphl_dbg_enter
 *     call hphl_dbg_leave
 *
 * ComunicaÃ§Ã£o: named pipe do Windows (o hphlc --dap cria o pipe, spawna o
 * executÃ¡vel com HPHL_DBG_PIPE=<nome> e faz o handshake). Mensagens de texto
 * (uma por linha). Enquanto uma thread estÃ¡ parada, os traps das demais sÃ£o
 * no-op (v1: thread do hit, step restrito).
 *
 * runtime -> adapter:
 *   ready
 *   meta <nFns> <nGlobals> <nClasses>
 *   fn <id> <nome> <arquivo> <nLocals> <nTraps>     (strings: \s = espaÃ§o, \\ = \)
 *   loc <id> <nome> <tipoStr> <offset> <flags>
 *     tipoStr: tipo canÃ´nico ("int", "class:Pessoa", "int[2][3]"...)
 *     flags: bit0 = byRef, bit1 = argumento, bit2 = array inline no frame
 *   trp <id> <line>
 *   gbl <nome> <tipoStr> <tls>
 *   cls <id> <nome> <nCampos>
 *   fld <clsId> <nome> <tipoStr> <offsetBytes>
 *   paused <tid> <nFrames>
 *   frame <fnId> <line> <rbp>                       (rbp em hex)
 *   rvar <hex> | rvar none
 *   rstr <conteudo>                                 (escapes: \\, \n, \r, \t)
 *   memok <hex>...                                  (slots de 8 bytes, mem)
 *   ok | fail <msg>
 *
 * adapter -> runtime (somente quando o processo estÃ¡ parado):
 *   continue | next | stepin | stepout | pause | quit
 *   bpx <fnId> <line> <0|1> [<msg>]  (msg = logpoint F2.7; {nome} = local do topo)
 *   read <fnId> <rbp> <nome>          -> rvar (byRef: deref)
 *   readarr <fnId> <rbp> <nome> <idx> -> rvar (array; stride 8)
 *   readgbl <nome>                    -> rvar (threadlocal resolvido)
 *   readgblarr <nome> <idx>           -> rvar
 *   readstr <ptr>                     -> rstr
 *   base <fnId> <rbp> <nome>          -> rvar: endereÃ§o base do valor
 *   basegbl <nome>                    -> rvar
 *   mem <addr> <count>                -> memok (count slots de 8 bytes)
 *
 * Tabelas de metadados (emitidas pelo codegen em .data):
 *   hphl_dbg_meta[] = [fnCount, fnTable, gblCount, gblTable, clsCount, clsTable]
 *
 *   fnTable = array de ptr DbgFnMeta
 *   DbgFnMeta = [namePtr, filePtr, startLine, nLocals, locsPtr, nTraps, trapsPtr]
 *   locsPtr = array de ptr DbgVarMeta
 *   DbgVarMeta = [namePtr, tipoStrPtr, offset, flags]
 *     offset = 8*(slot+1); endereço do local = rbp - offset
 *     flags: bit0 = byRef (slot guarda endereço), bit1 = argumento,
 *            bit2 = array inline (base = endereço do slot)
 *   gblTable = array de ptr DbgGblMeta
 *   DbgGblMeta = [namePtr, tipoStrPtr, tlsOffset, addr, flags]
 *     tlsOffset >= 0: endereço = hphl_tls_block() + 8*tlsOffset (addr = 0)
 *     flags: bit0 = array (base = addr direto, sem deref)
 *   clsTable = array de ptr DbgClsMeta
 *   DbgClsMeta = [namePtr, nFields, fieldsPtr]
 *   field = [namePtr, tipoStrPtr, offsetBytes]
 *
 * M20: fornecemos um stub zero-initializado no runtime.o para que o link do
 * hphlc.exe encontre o símbolo mesmo quando o programa compilado (.s) não
 * emite nada. O programa compilado (--debug) emite seu próprio hphl_dbg_meta
 * com dados reais no .s que sobrescreve este stub no link final.
 * ------------------------------------------------------------------------- */
unsigned long long hphl_dbg_meta[6] __attribute__((weak)) = {0, 0, 0, 0, 0, 0};

typedef struct DbgFrame {
  unsigned long long fnId;
  unsigned long long rbp;
  unsigned long long line;
} DbgFrame;

typedef struct DbgBp {
  unsigned long long fnId;
  unsigned long long line;
  int isLog;             /* F2.7: logpoint â€” nÃ£o pausa, imprime a msg */
  char msg[256];         /* F2.7: mensagem com {nome} dos locais do topo */
} DbgBp;

static HANDLE hphl_dbg_pipe = INVALID_HANDLE_VALUE;
static int hphl_dbg_active = 0;            /* HPHL_DBG_PIPE presente */
static volatile LONG hphl_dbg_pause_req = 0;
static volatile LONG hphl_dbg_handshook = 0;

/* F3.1: falha fatal (panic/assert/bounds/...) com --debug ativo */
static volatile LONG g_dbg_exc_cod = 0;
static char g_dbg_exc_msg[512];
static volatile LONG g_dbg_exc_pending = 0;

/* F2.5: threads no depurador â€” registro das threads vivas (para o comando
 * `threads` do DAP) e paradas cooperativas por thread:
 *  - hphl_dbg_loop_tid: a ÃšNICA thread no cmd_loop do pipe (CAS); as demais
 *    paradas ficam esperando no prÃ³prio evento e sÃ£o acordadas sÃ³ quando um
 *    comando as resume (continue global ou continue <tid>).
 *  - hphl_dbg_pause_tid: alvo do pause assÃ­ncrono (0 = qualquer thread). */
static volatile LONG hphl_dbg_loop_tid = 0;
static volatile LONG hphl_dbg_pause_tid = 0;
#define HPHL_DBG_MAX_THREADS 256
/* F15 (Sprint 5): env override. Define HPHL_DBG_MAX_THREADS=1024 para alocar
 * mais threads de debug. Padrao: 256. Maximo: 65536. */
static int hphl_dbg_max_threads = 0;
static int hphl_dbg_get_max_threads(void) {
    if (hphl_dbg_max_threads != 0) return hphl_dbg_max_threads;
    const char* env = getenv("HPHL_DBG_MAX_THREADS");
    if (env) {
        int n = atoi(env);
        if (n > 0 && n <= 65536) {
            hphl_dbg_max_threads = n;
            return n;
        }
    }
    hphl_dbg_max_threads = HPHL_DBG_MAX_THREADS;
    return HPHL_DBG_MAX_THREADS;
}
#define HPHL_DBG_MAX_STOPPED 64
typedef struct {
  DWORD tid;
  char name[32];
} HphlDbgThread;
static HphlDbgThread hphl_dbg_threads[65536];  /* F15: alocado max, usado ate hphl_dbg_get_max_threads() */
static int hphl_dbg_thread_count = 0;
static CRITICAL_SECTION hphl_dbg_thread_cs;
typedef struct {
  DWORD tid;
  HANDLE ev; /* auto-reset: acordado por continue (seu tid ou global) */
  int active;
} HphlDbgStopped;
static HphlDbgStopped hphl_dbg_stopped[HPHL_DBG_MAX_STOPPED];
static CRITICAL_SECTION hphl_dbg_stop_cs;
static volatile LONG hphl_dbg_cs_ready = 0;

static void hphl_dbg_ensure_cs(void) {
  if (hphl_dbg_cs_ready) return;
  if (InterlockedCompareExchange(&hphl_dbg_cs_ready, 1, 0) == 0) {
    InitializeCriticalSection(&hphl_dbg_thread_cs);
    InitializeCriticalSection(&hphl_dbg_stop_cs);
  }
}

static void hphl_dbg_reg_thread_id(DWORD tid, const char* name) {
  if (!hphl_dbg_active) return;
  hphl_dbg_ensure_cs();
  EnterCriticalSection(&hphl_dbg_thread_cs);
  for (int i = 0; i < hphl_dbg_thread_count; i++)
    if (hphl_dbg_threads[i].tid == tid) { LeaveCriticalSection(&hphl_dbg_thread_cs); return; }
  if (hphl_dbg_thread_count < hphl_dbg_get_max_threads()) {
    hphl_dbg_threads[hphl_dbg_thread_count].tid = tid;
    strncpy(hphl_dbg_threads[hphl_dbg_thread_count].name, name,
            sizeof hphl_dbg_threads[0].name - 1);
    hphl_dbg_threads[hphl_dbg_thread_count].name[sizeof hphl_dbg_threads[0].name - 1] = 0;
    hphl_dbg_thread_count++;
  }
  LeaveCriticalSection(&hphl_dbg_thread_cs);
}

static void hphl_dbg_reg_thread(const char* name) {
  hphl_dbg_reg_thread_id(GetCurrentThreadId(), name);
}

static void hphl_dbg_resume_all(void) {
  hphl_dbg_ensure_cs();
  EnterCriticalSection(&hphl_dbg_stop_cs);
  for (int i = 0; i < HPHL_DBG_MAX_STOPPED; i++)
    if (hphl_dbg_stopped[i].active) SetEvent(hphl_dbg_stopped[i].ev);
  LeaveCriticalSection(&hphl_dbg_stop_cs);
}

static int hphl_dbg_resume_one(DWORD tid) {
  hphl_dbg_ensure_cs();
  int found = 0;
  EnterCriticalSection(&hphl_dbg_stop_cs);
  for (int i = 0; i < HPHL_DBG_MAX_STOPPED; i++)
    if (hphl_dbg_stopped[i].active && hphl_dbg_stopped[i].tid == tid) {
      SetEvent(hphl_dbg_stopped[i].ev);
      found = 1;
      break;
    }
  LeaveCriticalSection(&hphl_dbg_stop_cs);
  return found;
}

static __thread DbgFrame* hphl_dbg_stack = NULL;
static __thread int hphl_dbg_depth = 0;
static __thread int hphl_dbg_cap = 0;
static __thread int hphl_dbg_step_mode = 0; /* 0 off, 1 over, 2 in, 3 out */
static __thread unsigned long long hphl_dbg_step_line = 0;
static __thread int hphl_dbg_step_depth = 0;

static DbgBp hphl_dbg_bps[512];
static int hphl_dbg_bp_count = 0;

/* F2.6: watchpoints de escrita (data breakpoints) â€” pausa quando o valor de
 * um local (rbp - offset, da fn no topo da stack) ou de um global (TLS da
 * prÃ³pria thread / endereÃ§o fixo) muda. VerificaÃ§Ã£o a cada trap; o valor Ã©
 * memorizado na 1Âª verificaÃ§Ã£o (seed, sem pausar). Slots ESTÃVEIS (nÃ£o
 * compacta): o idx do slot Ã© o que o `wp <idx>` reporta ao adapter. */
#define HPHL_DBG_MAX_WP 64
typedef struct {
  int active;
  int kind;                  /* 0 = local, 1 = global */
  unsigned long long fnId;   /* local: funÃ§Ã£o dona do local */
  unsigned long long* lmeta; /* local: meta do local (byRef flags) */
  char name[64];             /* global: nome */
  int seeded;
  unsigned long long last;
} HphlDbgWp;
static HphlDbgWp hphl_dbg_wps[HPHL_DBG_MAX_WP];
static __thread int hphl_dbg_wp_hit_tls = -1; /* idx do wp (dona do cmd_loop) */

static unsigned long long hphl_dbg_cur_line(void) {
  if (hphl_dbg_depth <= 0) return 0;
  return hphl_dbg_stack[hphl_dbg_depth - 1].line;
}

static unsigned long long hphl_dbg_cur_fn(void) {
  if (hphl_dbg_depth <= 0) return 0;
  return hphl_dbg_stack[hphl_dbg_depth - 1].fnId;
}

static void hphl_dbg_send(const char* line) {
  if (hphl_dbg_pipe == INVALID_HANDLE_VALUE) return;
  DWORD len = (DWORD)strlen(line);
  DWORD w = 0;
  BOOL ok1 = WriteFile(hphl_dbg_pipe, line, len, &w, NULL);
  (void)ok1;
  BOOL ok2 = WriteFile(hphl_dbg_pipe, "\n", 1, &w, NULL);
  (void)ok2;
}

/* escapa \ e espaÃ§o (campos de meta) e \ + controles (conteÃºdo de rstr) */
static void hphl_dbg_esc(char* out, size_t cap, const char* s, int spaces) {
  size_t j = 0;
  for (size_t i = 0; s[i] && j + 4 < cap; i++) {
    char c = s[i];
    if (c == '\\') { out[j++] = '\\'; out[j++] = '\\'; }
    else if (spaces && c == ' ') { out[j++] = '\\'; out[j++] = 's'; }
    else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
    else if (c == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
    else if (c == '\t') { out[j++] = '\\'; out[j++] = 't'; }
    else out[j++] = c;
  }
  out[j] = 0;
}

/* lÃª uma linha do pipe (bloqueante). Retorna 0 se o pipe fechou. */
static int hphl_dbg_recv_line(char* buf, int cap) {
  if (hphl_dbg_pipe == INVALID_HANDLE_VALUE) return 0;
  int n = 0;
  while (n < cap - 1) {
    char c;
    DWORD r = 0;
    if (!ReadFile(hphl_dbg_pipe, &c, 1, &r, NULL) || r == 0) {
      return 0;
    }
    if (c == '\n') break;
    if (c == '\r') continue;
    buf[n++] = c;
  }
  buf[n] = 0;
  return 1;
}

static void hphl_dbg_try_init(void) {
  char name[512];
  DWORD n = GetEnvironmentVariableA("HPHL_DBG_PIPE", name, sizeof name);
  if (n == 0 || n >= sizeof name) return;
  /* aceita o nome cru ("dbg1") ou o path completo ("\\.\pipe\dbg1") */
  if (n >= 9 && memcmp(name, "\\\\.\\pipe\\", 9) != 0) {
    memmove(name + 9, name, n + 1);
    memcpy(name, "\\\\.\\pipe\\", 9);
  } else if (n < 9) {
    return;
  }
  /* conexÃ£o lazy (o adapter cria o pipe antes de spawnar o processo) */
  for (int i = 0; i < 200; i++) {
    hphl_dbg_pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                OPEN_EXISTING, 0, NULL);
    if (hphl_dbg_pipe != INVALID_HANDLE_VALUE) { hphl_dbg_active = 1; return; }
    if (GetLastError() == ERROR_PIPE_BUSY) {
      /* o servidor existe mas estÃ¡ conectando outra instÃ¢ncia: aguarda */
      Sleep(10);
      continue;
    }
    Sleep(10);
  }
}

/* percorre a tabela de funÃ§Ãµes do meta; devolve o endereÃ§o de DbgFnMeta */
static unsigned long long* hphl_dbg_fn_meta(unsigned long long fnId) {
  unsigned long long* m = hphl_dbg_meta;
  if (!m) return NULL;
  unsigned long long fnCount = m[0];
  if (fnCount == 0 || fnId >= fnCount) return NULL;
  unsigned long long* fnTable = (unsigned long long*)m[1];
  return (unsigned long long*)fnTable[fnId];
}

static unsigned long long* hphl_dbg_global_meta(const char* name) {
  unsigned long long* m = hphl_dbg_meta;
  if (!m) return NULL;
  unsigned long long gblCount = m[2];
  unsigned long long* gblTable = (unsigned long long*)m[3];
  for (unsigned long long i = 0; i < gblCount; i++) {
    unsigned long long* g = (unsigned long long*)gblTable[i];
    const char* gn = (const char*)g[0];
    if (strcmp(gn, name) == 0) return g;
  }
  return NULL;
}

static void hphl_dbg_send_meta(void) {
  unsigned long long* m = hphl_dbg_meta;
  unsigned long long fnCount = m ? m[0] : 0;
  unsigned long long gblCount = m ? m[2] : 0;
  unsigned long long clsCount = m ? m[4] : 0;
  char buf[1024];
  sprintf(buf, "meta %llu %llu %llu", fnCount, gblCount, clsCount);
  hphl_dbg_send(buf);
  unsigned long long* fnTable = (unsigned long long*)m[1];
  for (unsigned long long i = 0; i < fnCount; i++) {
    unsigned long long* f = (unsigned long long*)fnTable[i];
    const char* fn = (const char*)f[0];
    const char* file = (const char*)f[1];
    unsigned long long nLocals = f[3];
    unsigned long long nTraps = f[5];
    char en[512], ef[512];
    hphl_dbg_esc(en, sizeof en, fn, 1);
    hphl_dbg_esc(ef, sizeof ef, file, 1);
    sprintf(buf, "fn %llu %s %s %llu %llu", i, en, ef, nLocals, nTraps);
    hphl_dbg_send(buf);
    unsigned long long* locs = (unsigned long long*)f[4];
    for (unsigned long long k = 0; k < nLocals; k++) {
      unsigned long long* l = (unsigned long long*)locs[k];
      const char* ln = (const char*)l[0];
      const char* lt = (const char*)l[1];
      char el[512], et[512];
      hphl_dbg_esc(el, sizeof el, ln, 1);
      hphl_dbg_esc(et, sizeof et, lt, 1);
      sprintf(buf, "loc %llu %s %s %llu %llu", i, el, et, l[2], l[3]);
      hphl_dbg_send(buf);
    }
    unsigned long long* traps = (unsigned long long*)f[6];
    for (unsigned long long k = 0; k < nTraps; k++) {
      sprintf(buf, "trp %llu %llu", i, traps[k]);
      hphl_dbg_send(buf);
    }
  }
  unsigned long long* gblTable = (unsigned long long*)m[3];
  for (unsigned long long i = 0; i < gblCount; i++) {
    unsigned long long* g = (unsigned long long*)gblTable[i];
    const char* gn = (const char*)g[0];
    const char* gt = (const char*)g[1];
    char eg[512], et[512];
    hphl_dbg_esc(eg, sizeof eg, gn, 1);
    hphl_dbg_esc(et, sizeof et, gt, 1);
    sprintf(buf, "gbl %s %s %llu", eg, et, g[2]);
    hphl_dbg_send(buf);
  }
  unsigned long long* clsTable = (unsigned long long*)m[5];
  for (unsigned long long i = 0; i < clsCount; i++) {
    unsigned long long* c = (unsigned long long*)clsTable[i];
    const char* cn = (const char*)c[0];
    unsigned long long nFields = c[1];
    char ec[512];
    hphl_dbg_esc(ec, sizeof ec, cn, 1);
    sprintf(buf, "cls %llu %s %llu", i, ec, nFields);
    hphl_dbg_send(buf);
    unsigned long long* flds = (unsigned long long*)c[2];
    for (unsigned long long k = 0; k < nFields; k++) {
      unsigned long long* fl = (unsigned long long*)flds[k];
      const char* fn = (const char*)fl[0];
      const char* ft = (const char*)fl[1];
      char ef[512], et[512];
      hphl_dbg_esc(ef, sizeof ef, fn, 1);
      hphl_dbg_esc(et, sizeof et, ft, 1);
      sprintf(buf, "fld %llu %s %s %llu", i, ef, et, fl[2]);
      hphl_dbg_send(buf);
    }
  }
  hphl_dbg_send("metaend");
}

/* resolve um local da funÃ§Ã£o por nome (Ãºltimo vence: escopo mais interno) */
static unsigned long long* hphl_dbg_local_meta(unsigned long long fnId,
                                               const char* name) {
  unsigned long long* f = hphl_dbg_fn_meta(fnId);
  if (!f) return NULL;
  unsigned long long nLocals = f[3];
  unsigned long long* locs = (unsigned long long*)f[4];
  unsigned long long* hit = NULL;
  for (unsigned long long k = 0; k < nLocals; k++) {
    unsigned long long* l = (unsigned long long*)locs[k];
    if (strcmp((const char*)l[0], name) == 0) hit = l;
  }
  return hit;
}

/* endereÃ§o do local (rbp - offset), com deref se byRef */
static unsigned long long hphl_dbg_local_addr(unsigned long long fnId,
                                              unsigned long long rbp,
                                              unsigned long long* l) {
  unsigned long long offset = l[2];
  unsigned long long flags = l[3];
  unsigned long long addr = rbp - offset;
  if (flags & 1) addr = *(unsigned long long*)addr;
  return addr;
}

static void hphl_dbg_cmd_read(unsigned long long fnId, unsigned long long rbp,
                              const char* name) {
  unsigned long long* l = hphl_dbg_local_meta(fnId, name);
  char buf[128];
  if (!l) { hphl_dbg_send("rvar none"); return; }
  unsigned long long addr = hphl_dbg_local_addr(fnId, rbp, l);
  sprintf(buf, "rvar %llx", *(unsigned long long*)addr);
  hphl_dbg_send(buf);
}

static void hphl_dbg_cmd_readarr(unsigned long long fnId, unsigned long long rbp,
                                 const char* name, unsigned long long idx) {
  unsigned long long* l = hphl_dbg_local_meta(fnId, name);
  char buf[128];
  if (!l) { hphl_dbg_send("rvar none"); return; }
  unsigned long long base = hphl_dbg_local_addr(fnId, rbp, l);
  sprintf(buf, "rvar %llx", *(unsigned long long*)(base + 8 * idx));
  hphl_dbg_send(buf);
}

static void hphl_dbg_cmd_readgbl(const char* name) {
  unsigned long long* g = hphl_dbg_global_meta(name);
  char buf[128];
  if (!g) { hphl_dbg_send("rvar none"); return; }
  long long tls = (long long)g[2];
  unsigned long long addr;
  if (tls >= 0) addr = (unsigned long long)hphl_tls_block() + 8 * (unsigned long long)tls;
  else addr = g[3];
  sprintf(buf, "rvar %llx", *(unsigned long long*)addr);
  hphl_dbg_send(buf);
}

static void hphl_dbg_cmd_write(unsigned long long fnId, unsigned long long rbp,
                               const char* name, unsigned long long value) {
  unsigned long long* l = hphl_dbg_local_meta(fnId, name);
  if (!l) { hphl_dbg_send("fail write"); return; }
  unsigned long long addr = hphl_dbg_local_addr(fnId, rbp, l);
  *(unsigned long long*)addr = value;
  hphl_dbg_send("ok");
}

static void hphl_dbg_cmd_writegbl(const char* name, unsigned long long value) {
  unsigned long long* g = hphl_dbg_global_meta(name);
  if (!g) { hphl_dbg_send("fail writegbl"); return; }
  long long tls = (long long)g[2];
  unsigned long long addr;
  if (tls >= 0) addr = (unsigned long long)hphl_tls_block() + 8 * (unsigned long long)tls;
  else addr = g[3];
  *(unsigned long long*)addr = value;
  hphl_dbg_send("ok");
}

static void hphl_dbg_cmd_readgblarr(const char* name, unsigned long long idx) {
  unsigned long long* g = hphl_dbg_global_meta(name);
  char buf[128];
  if (!g) { hphl_dbg_send("rvar none"); return; }
  long long tls = (long long)g[2];
  unsigned long long base;
  if (tls >= 0) base = (unsigned long long)hphl_tls_block() + 8 * (unsigned long long)tls;
  else base = g[3];
  sprintf(buf, "rvar %llx", *(unsigned long long*)(base + 8 * idx));
  hphl_dbg_send(buf);
}

static void hphl_dbg_cmd_readstr(unsigned long long ptr) {
  char esc[512];
  const char* s = (const char*)ptr;
  if (!s) { hphl_dbg_send("rstr "); return; }
  hphl_dbg_esc(esc, sizeof esc, s, 0);
  char buf[640];
  sprintf(buf, "rstr %s", esc);
  hphl_dbg_send(buf);
}

/* base de um local: endereÃ§o para navegaÃ§Ã£o (F1.2)
 * - byRef: o slot guarda o endereÃ§o do argumento (a base)
 * - array inline (bit2): o prÃ³prio slot Ã© a base
 * - ponteiros (classe/list/string/array heap): conteÃºdo do slot */
static void hphl_dbg_cmd_base(unsigned long long fnId, unsigned long long rbp,
                              const char* name) {
  unsigned long long* l = hphl_dbg_local_meta(fnId, name);
  char buf[128];
  if (!l) { hphl_dbg_send("rvar none"); return; }
  unsigned long long addr = rbp - l[2];
  unsigned long long flags = l[3];
  unsigned long long base;
  if (flags & 4) base = addr;
  else base = *(unsigned long long*)addr;
  sprintf(buf, "rvar %llx", base);
  hphl_dbg_send(buf);
}

static void hphl_dbg_cmd_basegbl(const char* name) {
  unsigned long long* g = hphl_dbg_global_meta(name);
  char buf[128];
  if (!g) { hphl_dbg_send("rvar none"); return; }
  long long tls = (long long)g[2];
  unsigned long long addr;
  if (tls >= 0) addr = (unsigned long long)hphl_tls_block() + 8 * (unsigned long long)tls;
  else addr = g[3];
  unsigned long long base = (g[4] & 1) ? addr : *(unsigned long long*)addr;
  sprintf(buf, "rvar %llx", base);
  hphl_dbg_send(buf);
}

static void hphl_dbg_cmd_mem(unsigned long long addr, unsigned long long count) {
  if (count > 256) count = 256;
  char buf[2048];
  int off = sprintf(buf, "memok");
  for (unsigned long long i = 0; i < count; i++)
    off += sprintf(buf + off, " %llx", *(unsigned long long*)(addr + 8 * i));
  hphl_dbg_send(buf);
}

static void hphl_dbg_set_bp(unsigned long long fnId, unsigned long long line,
                            int on, const char* msg) {
  if (on) {
    for (int i = 0; i < hphl_dbg_bp_count; i++) {
      if (hphl_dbg_bps[i].fnId == fnId && hphl_dbg_bps[i].line == line) {
        hphl_dbg_bps[i].isLog = msg && msg[0];
        if (msg && msg[0]) {
          strncpy(hphl_dbg_bps[i].msg, msg, sizeof hphl_dbg_bps[i].msg - 1);
          hphl_dbg_bps[i].msg[sizeof hphl_dbg_bps[i].msg - 1] = 0;
        }
        return;
      }
    }
    if (hphl_dbg_bp_count < 512) {
      DbgBp* b = &hphl_dbg_bps[hphl_dbg_bp_count];
      b->fnId = fnId;
      b->line = line;
      b->isLog = msg && msg[0];
      if (msg && msg[0]) {
        strncpy(b->msg, msg, sizeof b->msg - 1);
        b->msg[sizeof b->msg - 1] = 0;
      }
      hphl_dbg_bp_count++;
    }
  } else {
    for (int i = 0; i < hphl_dbg_bp_count; i++) {
      if (hphl_dbg_bps[i].fnId == fnId && hphl_dbg_bps[i].line == line) {
        hphl_dbg_bps[i] = hphl_dbg_bps[hphl_dbg_bp_count - 1];
        hphl_dbg_bp_count--;
        return;
      }
    }
  }
}

/* F2.7: imprime a mensagem do logpoint substituindo {nome} pelos valores
 * dos locais da funÃ§Ã£o no topo da pilha (vai para o stdout do programa,
 * que o adapter repassa como evento output). */
static void hphl_dbg_log_bp(const DbgBp* bp) {
  char out[512];
  const char* m = bp->msg;
  size_t o = 0;
  unsigned long long fnId = hphl_dbg_cur_fn();
  unsigned long long rbp =
      hphl_dbg_depth > 0 ? hphl_dbg_stack[hphl_dbg_depth - 1].rbp : 0;
  while (*m && o < sizeof out - 1) {
    if (m[0] == '{') {
      const char* e = strchr(m, '}');
      if (e) {
        size_t len = (size_t)(e - m - 1);
        if (len > 0 && len < 128) {
          char nm[128];
          memcpy(nm, m + 1, len);
          nm[len] = 0;
          unsigned long long* l = hphl_dbg_local_meta(fnId, nm);
          if (l) {
            unsigned long long addr = hphl_dbg_local_addr(fnId, rbp, l);
            const char* t = (const char*)l[1];
            char vb[192];
            if (strstr(t, "string")) {
              const char* s = (const char*)*(unsigned long long*)addr;
              snprintf(vb, sizeof vb, "%s", s ? s : "null");
            } else if (strstr(t, "float")) {
              snprintf(vb, sizeof vb, "%g", *(double*)addr);
            } else if (strstr(t, "bool")) {
              snprintf(vb, sizeof vb, "%s",
                       *(unsigned long long*)addr ? "true" : "false");
            } else if (strstr(t, "char")) {
              snprintf(vb, sizeof vb, "%c", (int)*(char*)addr);
            } else {
              snprintf(vb, sizeof vb, "%lld",
                       (long long)*(unsigned long long*)addr);
            }
            size_t vl = strlen(vb);
            size_t room = sizeof out - 1 - o;
            if (vl > room) vl = room;
            memcpy(out + o, vb, vl);
            o += vl;
            m = e + 1;
            continue;
          }
        }
      }
    }
    out[o++] = *m++;
  }
  out[o] = 0;
  printf("%s\n", out);
  fflush(stdout);
}

static int hphl_dbg_bp_hit_idx(void) {
  unsigned long long fnId = hphl_dbg_cur_fn();
  unsigned long long line = hphl_dbg_cur_line();
  for (int i = 0; i < hphl_dbg_bp_count; i++)
    if (hphl_dbg_bps[i].fnId == fnId && hphl_dbg_bps[i].line == line) return i;
  return -1;
}

static int hphl_dbg_bp_hit(void) {
  return hphl_dbg_bp_hit_idx() >= 0;
}

static int hphl_dbg_wp_free_slot(void) {
  for (int i = 0; i < HPHL_DBG_MAX_WP; i++)
    if (!hphl_dbg_wps[i].active) return i;
  return -1;
}

/* ativa/desativa um watchpoint de local (por offset, como no meta loc).
 * Devolve o slot (>= 0) ou -1. */
static int hphl_dbg_wp_set_loc(unsigned long long fnId, long long ofs, int on) {
  if (on) {
    unsigned long long* f = hphl_dbg_fn_meta(fnId);
    if (!f) return -1;
    unsigned long long nLocals = f[3];
    unsigned long long* locs = (unsigned long long*)f[4];
    unsigned long long* hit = NULL;
    for (unsigned long long k = 0; k < nLocals; k++) {
      unsigned long long* l = (unsigned long long*)locs[k];
      if ((long long)l[2] == ofs) { hit = l; break; }
    }
    if (!hit) return -1;
    int s = hphl_dbg_wp_free_slot();
    if (s < 0) return -1;
    hphl_dbg_wps[s].active = 1;
    hphl_dbg_wps[s].kind = 0;
    hphl_dbg_wps[s].fnId = fnId;
    hphl_dbg_wps[s].lmeta = hit;
    hphl_dbg_wps[s].seeded = 0;
    hphl_dbg_wps[s].last = 0;
    return s;
  }
  for (int i = 0; i < HPHL_DBG_MAX_WP; i++) {
    if (hphl_dbg_wps[i].active && hphl_dbg_wps[i].kind == 0 &&
        hphl_dbg_wps[i].fnId == fnId && (long long)hphl_dbg_wps[i].lmeta[2] == ofs) {
      hphl_dbg_wps[i].active = 0;
      return i;
    }
  }
  return -1;
}

/* ativa/desativa um watchpoint de global (por nome) */
static int hphl_dbg_wp_set_gbl(const char* name, int on) {
  if (on) {
    unsigned long long* g = hphl_dbg_global_meta(name);
    if (!g) return -1;
    int s = hphl_dbg_wp_free_slot();
    if (s < 0) return -1;
    hphl_dbg_wps[s].active = 1;
    hphl_dbg_wps[s].kind = 1;
    strncpy(hphl_dbg_wps[s].name, name, sizeof hphl_dbg_wps[s].name - 1);
    hphl_dbg_wps[s].name[sizeof hphl_dbg_wps[s].name - 1] = 0;
    hphl_dbg_wps[s].seeded = 0;
    hphl_dbg_wps[s].last = 0;
    return s;
  }
  for (int i = 0; i < HPHL_DBG_MAX_WP; i++) {
    if (hphl_dbg_wps[i].active && hphl_dbg_wps[i].kind == 1 &&
        strcmp(hphl_dbg_wps[i].name, name) == 0) {
      hphl_dbg_wps[i].active = 0;
      return i;
    }
  }
  return -1;
}

/* verifica os watchpoints no trap atual; devolve o slot do 1Âº que mudou
 * (ou -1). Local: sÃ³ quando a fn dona estÃ¡ no topo (o endereÃ§o Ã© resolvido
 * com o rbp ATUAL, entÃ£o o watchpoint segue o frame vivo da funÃ§Ã£o). */
static int hphl_dbg_wp_hit(void) {
  if (hphl_dbg_depth <= 0) return -1;
  unsigned long long curFn = hphl_dbg_cur_fn();
  unsigned long long rbp = hphl_dbg_stack[hphl_dbg_depth - 1].rbp;
  for (int i = 0; i < HPHL_DBG_MAX_WP; i++) {
    HphlDbgWp* w = &hphl_dbg_wps[i];
    if (!w->active) continue;
    unsigned long long addr;
    if (w->kind == 0) {
      if (curFn != w->fnId) continue;
      addr = hphl_dbg_local_addr(curFn, rbp, w->lmeta);
    } else {
      unsigned long long* g = hphl_dbg_global_meta(w->name);
      if (!g) continue;
      long long tls = (long long)g[2];
      if (tls >= 0) addr = (unsigned long long)hphl_tls_block() + 8 * (unsigned long long)tls;
      else addr = g[3];
    }
    unsigned long long val = *(unsigned long long*)addr;
    if (!w->seeded) { w->seeded = 1; w->last = val; continue; }
    if (val != w->last) { w->last = val; return i; }
  }
  return -1;
}

/* paused [<motivo>]: o motivo `wp` (watchpoint de escrita) acrescenta uma
 * linha `wp <idx>` depois dos frames */
static void hphl_dbg_send_paused(DWORD tid, const char* reason, int wpIdx) {
  char buf[256];
  if (reason)
    sprintf(buf, "paused %lu %d %s", (unsigned long)tid, hphl_dbg_depth, reason);
  else
    sprintf(buf, "paused %lu %d", (unsigned long)tid, hphl_dbg_depth);
  hphl_dbg_send(buf);
  for (int i = 0; i < hphl_dbg_depth; i++) {
    sprintf(buf, "frame %llu %llu %llx",
            hphl_dbg_stack[i].fnId, hphl_dbg_stack[i].line, hphl_dbg_stack[i].rbp);
    hphl_dbg_send(buf);
  }
  if (reason) {
    if (strcmp(reason, "excp") == 0) {
      sprintf(buf, "excp %d %s", (int)g_dbg_exc_cod, g_dbg_exc_msg);
      hphl_dbg_send(buf);
    } else {
      sprintf(buf, "wp %d", wpIdx);
      hphl_dbg_send(buf);
    }
  }
}

/* espera comandos do adapter (sÃ³ quando parado). Sai no continue/step.
 * F2.5: continue [<tid>] â€” sem tid resumia TODAS as paradas (inclusive eu);
 * com tid, sÃ³ a thread alvo (se for eu, apenas saio do loop; se for outra,
 * acordo a parada dela e continuo lendo o pipe). */
static void hphl_dbg_cmd_loop(void) {
  char cmd[1024];
  DWORD me = GetCurrentThreadId();
  while (hphl_dbg_recv_line(cmd, sizeof cmd)) {
    if (strcmp(cmd, "continue") == 0) {
      hphl_dbg_step_mode = 0;
      hphl_dbg_resume_all();
      return;
    }
    if (strncmp(cmd, "continue ", 9) == 0) {
      unsigned long long t;
      if (sscanf(cmd + 9, "%llu", &t) == 1) {
        if ((DWORD)t == me || (DWORD)t == 0 || (DWORD)t == 1) {
          hphl_dbg_step_mode = 0;
          hphl_dbg_resume_all();
          return;
        }
        if (hphl_dbg_resume_one((DWORD)t)) continue;
        hphl_dbg_step_mode = 0;
        hphl_dbg_resume_all();
        return;
      }
      hphl_dbg_step_mode = 0;
      hphl_dbg_resume_all();
      return;
    }
    if (strcmp(cmd, "next") == 0) {
      hphl_dbg_step_mode = 1;
      hphl_dbg_step_line = hphl_dbg_cur_line();
      hphl_dbg_step_depth = hphl_dbg_depth;
      return;
    }
    if (strcmp(cmd, "stepin") == 0) { hphl_dbg_step_mode = 2; return; }
    if (strcmp(cmd, "stepout") == 0) {
      hphl_dbg_step_mode = 3;
      hphl_dbg_step_depth = hphl_dbg_depth;
      return;
    }
    /* step com threadId: só a dona do cmd_loop pode stepar */
    if (strncmp(cmd, "next ", 5) == 0 || strncmp(cmd, "stepin ", 7) == 0 ||
        strncmp(cmd, "stepout ", 8) == 0) {
      unsigned long long t;
      const char* rest = strchr(cmd, ' ') + 1;
      if (sscanf(rest, "%llu", &t) == 1) {
        if ((DWORD)t == me || (DWORD)t == 0 || (DWORD)t == 1) {
          if (cmd[0] == 'n') {
            hphl_dbg_step_mode = 1;
            hphl_dbg_step_line = hphl_dbg_cur_line();
            hphl_dbg_step_depth = hphl_dbg_depth;
          } else if (cmd[0] == 's' && cmd[3] == 'i') {
            hphl_dbg_step_mode = 2;
          } else {
            hphl_dbg_step_mode = 3;
            hphl_dbg_step_depth = hphl_dbg_depth;
          }
          return;
        }
        hphl_dbg_send("fail thread");
        continue;
      }
      hphl_dbg_send("fail step");
      continue;
    }
    if (strcmp(cmd, "pause") == 0) continue; /* jÃ¡ estÃ¡ parado */
    if (strncmp(cmd, "pause ", 6) == 0) {
      unsigned long long t;
      if (sscanf(cmd + 6, "%llu", &t) == 1) {
        if ((DWORD)t == me) continue; /* jÃ¡ estou parado */
        /* alvo Ã© outra thread: ela para quando bater num trap */
        InterlockedExchange(&hphl_dbg_pause_tid, (LONG)t);
        InterlockedExchange(&hphl_dbg_pause_req, 1);
        continue;
      }
      hphl_dbg_send("fail pause");
      continue;
    }
    if (strcmp(cmd, "threads") == 0) {
      char buf[512];
      hphl_dbg_ensure_cs();
      EnterCriticalSection(&hphl_dbg_thread_cs);
      sprintf(buf, "threads %d", hphl_dbg_thread_count);
      hphl_dbg_send(buf);
      for (int i = 0; i < hphl_dbg_thread_count; i++) {
        char name[64];
        hphl_dbg_esc(name, sizeof name, hphl_dbg_threads[i].name, 1);
        sprintf(buf, "thread %lu %s",
                (unsigned long)hphl_dbg_threads[i].tid, name);
        hphl_dbg_send(buf);
      }
      LeaveCriticalSection(&hphl_dbg_thread_cs);
      hphl_dbg_send("threadsend");
      continue;
    }
    if (strcmp(cmd, "quit") == 0) ExitProcess(0);
    if (strncmp(cmd, "bpx ", 4) == 0) {
      char* msg = NULL;
      char* sp1 = strchr(cmd + 4, ' ');
      char* sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
      char* sp3 = sp2 ? strchr(sp2 + 1, ' ') : NULL;
      if (sp3) {
        *sp3 = 0;
        msg = sp3 + 1;
      }
      unsigned long long fnId, line; int on;
      if (sscanf(cmd + 4, "%llu %llu %d", &fnId, &line, &on) == 3) {
        hphl_dbg_set_bp(fnId, line, on, msg);
        hphl_dbg_send("ok");
      } else hphl_dbg_send("fail bpx");
      continue;
    }
    if (strncmp(cmd, "wploc ", 6) == 0) {
      unsigned long long fnId; long long ofs; int on;
      if (sscanf(cmd + 6, "%llu %lld %d", &fnId, &ofs, &on) == 3) {
        int s = hphl_dbg_wp_set_loc(fnId, ofs, on);
        if (s >= 0) {
          char buf[32];
          sprintf(buf, "ok %d", s);
          hphl_dbg_send(buf);
        } else hphl_dbg_send("fail wploc");
      } else hphl_dbg_send("fail wploc");
      continue;
    }
    if (strncmp(cmd, "wpg ", 4) == 0) {
      char name[256]; int on;
      if (sscanf(cmd + 4, "%255s %d", name, &on) == 2) {
        int s = hphl_dbg_wp_set_gbl(name, on);
        if (s >= 0) {
          char buf[32];
          sprintf(buf, "ok %d", s);
          hphl_dbg_send(buf);
        } else hphl_dbg_send("fail wpg");
      } else hphl_dbg_send("fail wpg");
      continue;
    }
    if (strncmp(cmd, "read ", 5) == 0) {
      unsigned long long fnId, rbp;
      char name[256];
      if (sscanf(cmd + 5, "%llu %llx %255s", &fnId, &rbp, name) == 3)
        hphl_dbg_cmd_read(fnId, rbp, name);
      else hphl_dbg_send("fail read");
      continue;
    }
    if (strncmp(cmd, "readarr ", 8) == 0) {
      unsigned long long fnId, rbp, idx;
      char name[256];
      if (sscanf(cmd + 8, "%llu %llx %255s %llu", &fnId, &rbp, name, &idx) == 4)
        hphl_dbg_cmd_readarr(fnId, rbp, name, idx);
      else hphl_dbg_send("fail readarr");
      continue;
    }
    if (strncmp(cmd, "readgblarr ", 11) == 0) {
      unsigned long long idx;
      char name[256];
      if (sscanf(cmd + 11, "%255s %llu", name, &idx) == 2)
        hphl_dbg_cmd_readgblarr(name, idx);
      else hphl_dbg_send("fail readgblarr");
      continue;
    }
    if (strncmp(cmd, "readgbl ", 8) == 0) {
      hphl_dbg_cmd_readgbl(cmd + 8);
      continue;
    }
    if (strncmp(cmd, "write ", 6) == 0) {
      unsigned long long fnId, rbp, value;
      char name[256];
      if (sscanf(cmd + 6, "%llu %llx %255s %llx", &fnId, &rbp, name, &value) == 4)
        hphl_dbg_cmd_write(fnId, rbp, name, value);
      else hphl_dbg_send("fail write");
      continue;
    }
    if (strncmp(cmd, "writegbl ", 9) == 0) {
      unsigned long long value;
      char name[256];
      if (sscanf(cmd + 9, "%255s %llx", name, &value) == 2)
        hphl_dbg_cmd_writegbl(name, value);
      else hphl_dbg_send("fail writegbl");
      continue;
    }
    if (strncmp(cmd, "readstr ", 8) == 0) {
      unsigned long long ptr;
      if (sscanf(cmd + 8, "%llx", &ptr) == 1) hphl_dbg_cmd_readstr(ptr);
      else hphl_dbg_send("fail readstr");
      continue;
    }
    if (strncmp(cmd, "base ", 5) == 0) {
      unsigned long long fnId, rbp;
      char name[256];
      if (sscanf(cmd + 5, "%llu %llx %255s", &fnId, &rbp, name) == 3)
        hphl_dbg_cmd_base(fnId, rbp, name);
      else hphl_dbg_send("fail base");
      continue;
    }
    if (strncmp(cmd, "basegbl ", 8) == 0) {
      hphl_dbg_cmd_basegbl(cmd + 8);
      continue;
    }
    if (strncmp(cmd, "mem ", 4) == 0) {
      unsigned long long addr, count;
      if (sscanf(cmd + 4, "%llx %llu", &addr, &count) == 2)
        hphl_dbg_cmd_mem(addr, count);
      else hphl_dbg_send("fail mem");
      continue;
    }
    hphl_dbg_send("fail unknown");
  }
  /* pipe fechado pelo adapter: segue sem debug */
  hphl_dbg_active = 0;
}

/* registra a parada desta thread e espera o resume (continue prÃ³prio ou
 * global). A primeira parada vira a dona do cmd_loop e reporta o `paused`;
 * as demais ficam esperando no prÃ³prio evento. */
static void hphl_dbg_do_pause(DWORD tid) {
  hphl_dbg_ensure_cs();
  int slot = -1;
  EnterCriticalSection(&hphl_dbg_stop_cs);
  for (int i = 0; i < HPHL_DBG_MAX_STOPPED; i++) {
    if (hphl_dbg_stopped[i].active && hphl_dbg_stopped[i].tid == tid) {
      LeaveCriticalSection(&hphl_dbg_stop_cs);
      return; /* jÃ¡ parada */
    }
    if (slot < 0 && !hphl_dbg_stopped[i].active) slot = i;
  }
  if (slot < 0) { LeaveCriticalSection(&hphl_dbg_stop_cs); return; } /* lotado */
  hphl_dbg_stopped[slot].tid = tid;
  if (!hphl_dbg_stopped[slot].ev)
    hphl_dbg_stopped[slot].ev = CreateEventA(NULL, FALSE, FALSE, NULL);
  hphl_dbg_stopped[slot].active = 1;
  LeaveCriticalSection(&hphl_dbg_stop_cs);

  if (InterlockedCompareExchange(&hphl_dbg_loop_tid, tid, 0) == 0) {
    /* virei dona: reporta a parada e lÃª o pipe atÃ© me resumirem */
    InterlockedExchange(&hphl_dbg_pause_req, 0);
    if (hphl_dbg_wp_hit_tls >= 0) {
      hphl_dbg_send_paused(tid, "wp", hphl_dbg_wp_hit_tls);
      hphl_dbg_wp_hit_tls = -1;
    } else if (g_dbg_exc_pending) {
      InterlockedExchange(&g_dbg_exc_pending, 0);
      hphl_dbg_send_paused(tid, "excp", -1);
    } else {
      hphl_dbg_send_paused(tid, NULL, -1);
    }
    hphl_dbg_cmd_loop();
    InterlockedExchange(&hphl_dbg_loop_tid, 0);
  } else {
    /* outra thread Ã© dona: espero o continue (meu tid ou global) */
    HANDLE ev = NULL;
    EnterCriticalSection(&hphl_dbg_stop_cs);
    if (hphl_dbg_stopped[slot].active) ev = hphl_dbg_stopped[slot].ev;
    LeaveCriticalSection(&hphl_dbg_stop_cs);
    if (ev) WaitForSingleObject(ev, INFINITE);
  }
  /* resumida: libera o slot */
  EnterCriticalSection(&hphl_dbg_stop_cs);
  hphl_dbg_stopped[slot].active = 0;
  LeaveCriticalSection(&hphl_dbg_stop_cs);
}

/* primeiro trap: conecta, envia ready+meta e espera o primeiro comando */
static void hphl_dbg_handshake(void) {
  hphl_dbg_send("ready");
  hphl_dbg_send_meta();
  hphl_dbg_reg_thread("main");
  InterlockedExchange(&hphl_dbg_handshook, 1);
  hphl_dbg_cmd_loop();
}

void hphl_dbg_trap_impl(long long line, unsigned long long rbp) {
  if (!hphl_dbg_active) {
    hphl_dbg_try_init();
    if (!hphl_dbg_active) return;
  }
  DWORD me = GetCurrentThreadId();
  if (hphl_dbg_depth > 0) hphl_dbg_stack[hphl_dbg_depth - 1].line = (unsigned long long)line;

  if (!hphl_dbg_handshook) {
    /* sÃ³ a primeira thread faz o handshake (CAS); as demais esperam */
    if (InterlockedCompareExchange(&hphl_dbg_loop_tid, me, 0) == 0) {
      hphl_dbg_handshake();
      InterlockedExchange(&hphl_dbg_loop_tid, 0);
    } else {
      while (!hphl_dbg_handshook) Sleep(1);
    }
    if (!hphl_dbg_active) return;
  }
  if (hphl_dbg_pause_req) {
    LONG pt = InterlockedCompareExchange(&hphl_dbg_pause_tid, 0, 0);
    if (pt == 0 || (DWORD)pt == me) { hphl_dbg_do_pause(me); return; }
  }
  if (hphl_dbg_step_mode) {
    unsigned long long curLine = hphl_dbg_cur_line();
    int stop = 0;
    switch (hphl_dbg_step_mode) {
      case 2: stop = 1; break; /* in: qualquer linha */
      case 1: /* over: mesmo nÃ­vel, linha diferente */
        stop = hphl_dbg_depth <= hphl_dbg_step_depth && curLine != hphl_dbg_step_line;
        break;
      case 3: /* out: nÃ­vel acima */
        stop = hphl_dbg_depth < hphl_dbg_step_depth;
        break;
    }
    if (stop) { hphl_dbg_do_pause(me); }
    return;
  }
  int w = hphl_dbg_wp_hit();
  if (w >= 0) {
    hphl_dbg_wp_hit_tls = w;
    hphl_dbg_do_pause(me);
    return;
  }
  int bi = hphl_dbg_bp_hit_idx();
  if (bi >= 0) {
    if (hphl_dbg_bps[bi].isLog) hphl_dbg_log_bp(&hphl_dbg_bps[bi]);
    else hphl_dbg_do_pause(me);
  }
}

void hphl_dbg_enter_impl(unsigned long long fnId, unsigned long long rbp) {
  if (!hphl_dbg_active) {
    hphl_dbg_try_init();
    if (!hphl_dbg_active) return;
  }
  if (hphl_dbg_depth >= hphl_dbg_cap) {
    int ncap = hphl_dbg_cap ? hphl_dbg_cap * 2 : 64;
    DbgFrame* ns = (DbgFrame*)realloc(hphl_dbg_stack, (size_t)ncap * sizeof(DbgFrame));
    if (!ns) return;
    hphl_dbg_stack = ns;
    hphl_dbg_cap = ncap;
  }
  hphl_dbg_stack[hphl_dbg_depth].fnId = fnId;
  hphl_dbg_stack[hphl_dbg_depth].rbp = rbp;
  hphl_dbg_stack[hphl_dbg_depth].line = 0;
  hphl_dbg_depth++;
}

void hphl_dbg_leave(void) {
  if (!hphl_dbg_active) return;
  if (hphl_dbg_depth > 0) hphl_dbg_depth--;
}

/* F2.4: enter com frame explÃ­cito (backends IR/LLVM) â€” o Irgen passa o
 * endereÃ§o do bloco de espelho dos locais ([N x i64]); as leituras usam o
 * mesmo cÃ¡lculo do x64 (rbp - offset, com offsets negativos no meta). */
void hphl_dbg_enter_frame(unsigned long long fnId, unsigned long long framePtr) {
  hphl_dbg_enter_impl(fnId, framePtr);
}

/* wrappers naked: o enter Ã© chamado ANTES dos stores de args do corpo
 * (o codegen x64 materializa os parÃ¢metros no corpo, nÃ£o no prÃ³logo), entÃ£o
 * deve preservar os regs ABI (rcx/rdx/r8/r9/xmm0-3) ao redor da impl â€” senÃ£o
 * os parÃ¢metros chegariam zerados/corrompidos. Layout do bloco (136B):
 *   0..31  rcx/rdx/r8/r9  32..95  xmm0-3  96..103  ret  104..135  shadow
 * Alinhamento: o caller fez subq $32;call (rsp â‰¡ 0 no call, â‰¡ 8 na entrada);
 * subq $136 â†’ rsp â‰¡ 0 no call da impl. O trap, emitido no corpo (entre
 * instruÃ§Ãµes que jÃ¡ depositaram valores vivos em slots), Ã© um call comum â€”
 * clobber livre. Asm simples (sem template): '%' vai literal. */
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
__attribute__((naked)) void hphl_dbg_trap(long long line) {
  __asm__ volatile("movq %rbp, %rdx\n\t" "jmp hphl_dbg_trap_impl\n\t");
}
__attribute__((naked)) void hphl_dbg_enter(unsigned long long fnId) {
  __asm__ volatile(
    "subq $136, %rsp\n\t"
    "movq %rcx, 0(%rsp)\n\t"
    "movq %rdx, 8(%rsp)\n\t"
    "movq %r8, 16(%rsp)\n\t"
    "movq %r9, 24(%rsp)\n\t"
    "movups %xmm0, 32(%rsp)\n\t"
    "movups %xmm1, 48(%rsp)\n\t"
    "movups %xmm2, 64(%rsp)\n\t"
    "movups %xmm3, 80(%rsp)\n\t"
    "movq %r10, %rcx\n\t"
    "movq %rbp, %rdx\n\t"
    "call hphl_dbg_enter_impl\n\t"
    "movq 0(%rsp), %rcx\n\t"
    "movq 8(%rsp), %rdx\n\t"
    "movq 16(%rsp), %r8\n\t"
    "movq 24(%rsp), %r9\n\t"
    "movups 32(%rsp), %xmm0\n\t"
    "movups 48(%rsp), %xmm1\n\t"
    "movups 64(%rsp), %xmm2\n\t"
    "movups 80(%rsp), %xmm3\n\t"
    "addq $136, %rsp\n\t"
    "ret\n\t"
  );
}
#elif defined(__x86_64__) || defined(__amd64__)
/* System V AMD64 ABI (Linux / macOS x86_64):
 * Preserva regs de argumento (rdi, rsi, rdx, rcx, r8, r9, rax, xmm0-7).
 * Subq 184 garante alinhamento de 16 bytes na chamada de enter_impl (184 = 11*16 + 8).
 * Trap passa rbp em rsi (segundo argumento System V). */
__attribute__((naked)) void hphl_dbg_trap(long long line) {
  __asm__ volatile("movq %rbp, %rsi\n\t" "jmp hphl_dbg_trap_impl\n\t");
}
__attribute__((naked)) void hphl_dbg_enter(unsigned long long fnId) {
  __asm__ volatile(
    "subq $184, %rsp\n\t"
    "movq %rdi, 0(%rsp)\n\t"
    "movq %rsi, 8(%rsp)\n\t"
    "movq %rdx, 16(%rsp)\n\t"
    "movq %rcx, 24(%rsp)\n\t"
    "movq %r8, 32(%rsp)\n\t"
    "movq %r9, 40(%rsp)\n\t"
    "movq %rax, 48(%rsp)\n\t"
    "movups %xmm0, 56(%rsp)\n\t"
    "movups %xmm1, 72(%rsp)\n\t"
    "movups %xmm2, 88(%rsp)\n\t"
    "movups %xmm3, 104(%rsp)\n\t"
    "movups %xmm4, 120(%rsp)\n\t"
    "movups %xmm5, 136(%rsp)\n\t"
    "movups %xmm6, 152(%rsp)\n\t"
    "movups %xmm7, 168(%rsp)\n\t"
    "movq %r10, %rdi\n\t"
    "movq %rbp, %rsi\n\t"
    "call hphl_dbg_enter_impl\n\t"
    "movq 0(%rsp), %rdi\n\t"
    "movq 8(%rsp), %rsi\n\t"
    "movq 16(%rsp), %rdx\n\t"
    "movq 24(%rsp), %rcx\n\t"
    "movq 32(%rsp), %r8\n\t"
    "movq 40(%rsp), %r9\n\t"
    "movq 48(%rsp), %rax\n\t"
    "movups 56(%rsp), %xmm0\n\t"
    "movups 72(%rsp), %xmm1\n\t"
    "movups 88(%rsp), %xmm2\n\t"
    "movups 104(%rsp), %xmm3\n\t"
    "movups 120(%rsp), %xmm4\n\t"
    "movups 136(%rsp), %xmm5\n\t"
    "movups 152(%rsp), %xmm6\n\t"
    "movups 168(%rsp), %xmm7\n\t"
    "addq $184, %rsp\n\t"
    "ret\n\t"
  );
}
#elif defined(__aarch64__)
/* AAPCS64 ABI (Linux / macOS AArch64):
 * Preserva regs de argumento x0-x7, q0-q7, frame pointer x29 e link register x30.
 * Trap passa frame pointer em x1 (segundo argumento AArch64). */
__attribute__((naked)) void hphl_dbg_trap(long long line) {
  __asm__ volatile(
    "mov x1, x29\n\t"
    "b hphl_dbg_trap_impl\n\t"
  );
}
__attribute__((naked)) void hphl_dbg_enter(unsigned long long fnId) {
  __asm__ volatile(
    "sub sp, sp, #208\n\t"
    "stp x29, x30, [sp, #0]\n\t"
    "stp x0, x1, [sp, #16]\n\t"
    "stp x2, x3, [sp, #32]\n\t"
    "stp x4, x5, [sp, #48]\n\t"
    "stp x6, x7, [sp, #64]\n\t"
    "stp q0, q1, [sp, #80]\n\t"
    "stp q2, q3, [sp, #112]\n\t"
    "stp q4, q5, [sp, #144]\n\t"
    "stp q6, q7, [sp, #176]\n\t"
    "mov x0, x10\n\t"
    "mov x1, x29\n\t"
    "bl hphl_dbg_enter_impl\n\t"
    "ldp q6, q7, [sp, #176]\n\t"
    "ldp q4, q5, [sp, #144]\n\t"
    "ldp q2, q3, [sp, #112]\n\t"
    "ldp q0, q1, [sp, #80]\n\t"
    "ldp x6, x7, [sp, #64]\n\t"
    "ldp x4, x5, [sp, #48]\n\t"
    "ldp x2, x3, [sp, #32]\n\t"
    "ldp x0, x1, [sp, #16]\n\t"
    "ldp x29, x30, [sp, #0]\n\t"
    "add sp, sp, #208\n\t"
    "ret\n\t"
  );
}
#else
/* Fallback genérico para outras arquiteturas / WASM */
void hphl_dbg_trap(long long line) {
  hphl_dbg_trap_impl(line, (unsigned long long)__builtin_frame_address(0));
}
void hphl_dbg_enter(unsigned long long fnId) {
  hphl_dbg_enter_impl(fnId, (unsigned long long)__builtin_frame_address(0));
}
#endif

/* F3.1: reporta falha fatal ao depurador (com --debug ativo) */
void hphl_dbg_exc_report(int cod, const char* msg) {
  if (!hphl_dbg_active) {
    hphl_dbg_try_init();
    if (!hphl_dbg_active) return;
  }
  g_dbg_exc_cod = cod;
  snprintf(g_dbg_exc_msg, sizeof g_dbg_exc_msg, "%s", msg ? msg : "(sem mensagem)");
  InterlockedExchange(&g_dbg_exc_pending, 1);
  DWORD me = GetCurrentThreadId();
  if (hphl_dbg_depth > 0)
    hphl_dbg_stack[hphl_dbg_depth - 1].line = hphl_dbg_cur_line();
  hphl_dbg_do_pause(me);
}



