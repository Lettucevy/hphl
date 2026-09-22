#include <sys/stat.h>
#include <sys/types.h>
/* M27: command-line arguments (set by main.cpp before running HP-HL program) */
char** hphl_argv = NULL;
int hphl_argc = 0;
void hphl_init_args(int argc, char** argv) { hphl_argc = argc; hphl_argv = argv; }

/* COMPAT-2 (Sprint 4): versão estável de ABI do runtime.
 * Bump major (100 → 200, etc.) para mudanças incompatíveis. */
int hphl_runtime_abi_version(void) { return HPHL_RUNTIME_ABI_VERSION; }

/* M20-B 7.1: scheduler cooperativo (stackless) para async/await.
 * Cada async task vira uma continuation (setjmp_buf + stack próprio).
 * hphl_async_yield() suspende a task atual e agenda a próxima.
 * Não usa threads do pool — single-threaded, event-loop style.
 * Habilitado por env var HPHL_ASYNC_COOP=1. */
static int hphl_async_coop_enabled = -1;
static int hphl_async_coop_active(void) {
    if (hphl_async_coop_enabled < 0) {
        const char* v = getenv("HPHL_ASYNC_COOP");
        hphl_async_coop_enabled = (v && v[0] && v[0] != '0') ? 1 : 0;
    }
    return hphl_async_coop_enabled;
}
typedef struct HphlCoopTask {
    jmp_buf env;                       // continuation point
    char* stack;                       // stack alocada (16 KB)
    void (*fn)(void* env, void* res);
    void* env_arg;
    void* res;
    int done;
    long long result;
    struct HphlCoopTask* next;
} HphlCoopTask;
static __thread HphlCoopTask* hphl_coop_current = NULL;
static HphlCoopTask* hphl_coop_ready = NULL;
static HphlCoopTask* hphl_coop_waiting = NULL;
static __thread jmp_buf hphl_coop_main;

static void hphl_coop_entry(void) {
    HphlCoopTask* t = hphl_coop_current;
    t->fn(t->env_arg, t->res);
    t->done = 1;
    longjmp(hphl_coop_main, 1);  // volta ao scheduler
}

void* hphl_coop_spawn(void (*fn)(void*, void*), void* env, void* res) {
    HphlCoopTask* t = (HphlCoopTask*)malloc(sizeof(HphlCoopTask));
    if (!t) return NULL;
    t->stack = (char*)malloc(4096); /* M_RV1 F4: 16K -> 4K stack (stackless cooperative) */
    if (!t->stack) { free(t); return NULL; }
    t->fn = fn; t->env_arg = env; t->res = res;
    t->done = 0; t->result = 0; t->next = NULL;
    t->next = hphl_coop_ready;
    hphl_coop_ready = t;
    return t;
}

void hphl_coop_run(void) {
    if (setjmp(hphl_coop_main) == 0) {
        // primeira chamada: pega primeira task e pula para ela
        if (!hphl_coop_ready) return;
        HphlCoopTask* t = hphl_coop_ready;
        hphl_coop_ready = t->next;
        hphl_coop_current = t;
        // alinha stack ptr para 16 bytes (M_RV1 F4: 4K stack)
        uintptr_t sp = (uintptr_t)t->stack + 4096;
        sp &= ~(uintptr_t)15;
        // setjmp na task: quando retornar, vai estar de volta aqui
        if (setjmp(t->env) == 0) {
            // primeira entrada: salta para o entry da task
            // pseudo: set RSP = sp; jmp hphl_coop_entry
            // Não temos inline asm fácil; o setjmp já captura o contexto
            // atual. Em vez de salt, chamamos entry diretamente e marcamos done.
            // (Compromisso: cooperativo mas não stackless puro — usa a stack
            //  do caller, porém sem threads adicionais.)
            t->fn(t->env_arg, t->res);
            t->done = 1;
            return;
        }
    }
    // retomo: scheduler processa próxima task
    while (hphl_coop_ready) {
        HphlCoopTask* t = hphl_coop_ready;
        hphl_coop_ready = t->next;
        hphl_coop_current = t;
        if (setjmp(t->env) == 0) {
            t->fn(t->env_arg, t->res);
            t->done = 1;
        }
    }
}

/* chamado por await: suspende e agenda próxima task */
void hphl_async_yield(void) {
    if (!hphl_async_coop_active() || !hphl_coop_current) return;
    HphlCoopTask* t = hphl_coop_current;
    if (setjmp(t->env) == 0) {
        // suspend: coloca task atual no fim da fila de ready
        t->next = hphl_coop_ready;
        HphlCoopTask** pp = &hphl_coop_ready;
        while (*pp) pp = &(*pp)->next;
        *pp = t;
        // retoma o scheduler
        longjmp(hphl_coop_main, 1);
    }
}



/* F3.1: reporta uma falha fatal ao depurador (panic/assert/bounds/...).
 * Definida no bloco do depurador (no fim do arquivo); com --debug ativo
 * pausa o programa com motivo `excp` em vez de abortar; sem --debug
 * retorna imediatamente e o chamador executa o abort normal. */
void hphl_dbg_exc_report(int cod, const char* msg);

/*
 * hphl_clock_ns() â€” clock de alta resoluÃ§Ã£o (em nanossegundos).
 * Usa QueryPerformanceCounter (o `clock()` do C nÃ£o chega nem perto: ~ms e
 * conta tempo de CPU, nÃ£o wall-clock). Thread-safe: a frequÃªncia Ã© cacheadada
 * uma vez; o contador sÃ³ consulta o TSC/HPET do SO.
 */
void hphl_panic(const char* msg); /* fwd: definido mais abaixo */
void* hphl_list_new(void);
void hphl_list_add_i(void*, long long);
/* M12.4 stdlib: file IO (stdio, modo binÃ¡rio) */
int64_t hphl_clock_ns(void); /* fwd: definido mais abaixo */
/* M15-fix8: decodifica escapes de literais HP-HL e devolve corpo
   seguro para .asciz (GAS) */
char* hphl_gas_str(const char* raw) {
    size_t n = strlen(raw);
    char* out = (char*)malloc(n * 4 + 8);
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (c == '\\' && i + 1 < n) {
            char d = raw[++i];
            if (d == 'n') c = '\n';
            else if (d == 't') c = '\t';
            else if (d == 'r') c = '\r';
            else if (d == '"') c = '"';
            else if (d == '\\') c = '\\';
            else c = (unsigned char)d;
        }
        if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else if (c == '\r') { out[o++] = '\\'; out[o++] = 'r'; }
        else if (c == '"') { out[o++] = '\\'; out[o++] = '"'; }
        else if (c == '\\') { out[o++] = '\\'; out[o++] = '\\'; }
        else out[o++] = (char)c;
    }
    out[o] = 0;
    return out;
}

char* hphl_read_line(void) {
    size_t cap = 256;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) {
        char* empty = (char*)hphl_str_alloc(1);
        empty[0] = 0;
        return empty;
    }
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (c == '\r') continue;
        if (len + 1 >= cap) {
            cap *= 2;
            char* next = (char*)realloc(buf, cap);
            if (!next) break;
            buf = next;
        }
        buf[len++] = (char)c;
    }
#ifdef _WIN32
    if (c == EOF && len == 0) Sleep(50);
#else
    if (c == EOF && len == 0) usleep(50000);
#endif
    buf[len] = 0;
    char* out = (char*)hphl_str_alloc(len + 1);
    memcpy(out, buf, len + 1);
    free(buf);
    return out;
}

char* hphl_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { char* e = (char*)hphl_str_alloc(1); e[0] = 0; return e; }
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    int64_t sz = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
#else
    fseeko(f, 0, SEEK_END);
    int64_t sz = ftello(f);
    fseeko(f, 0, SEEK_SET);
#endif
    if (sz < 0) sz = 0;
    char* buf = (char*)hphl_str_alloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    return buf;
}
int64_t hphl_write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = fwrite(content, 1, strlen(content), f);
    fclose(f);
    return n == strlen(content) ? 1 : 0;
}
int64_t hphl_append_file(const char* path, const char* content) {
    FILE* f = fopen(path, "ab");
    if (!f) return 0;
    size_t n = fwrite(content, 1, strlen(content), f);
    fclose(f);
    return n == strlen(content) ? 1 : 0;
}
int64_t hphl_file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}
int64_t hphl_remove_file(const char* path) {
    return remove(path) == 0 ? 1 : 0;
}
/* M27 file: mkdir, listDir, stat, readLines, writeLines */
int64_t hphl_mkdir(const char* path) {
#ifdef _WIN32
    return CreateDirectoryA(path, NULL) ? 1 : 0;
#else
    return mkdir(path, 0755) == 0 ? 1 : 0;
#endif
}
void* hphl_list_dir(const char* path) {
    void* list = hphl_list_new();
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return list;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char* copy = (char*)hphl_str_alloc(strlen(fd.cFileName) + 1);
        strcpy(copy, fd.cFileName);
        hphl_list_add_i(list, (int64_t)(intptr_t)copy);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(path);
    if (!d) return list;
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char* copy = (char*)hphl_str_alloc(strlen(entry->d_name) + 1);
        strcpy(copy, entry->d_name);
        hphl_list_add_i(list, (int64_t)(intptr_t)copy);
    }
    closedir(d);
#endif
    return list;
}
/* M27: readLines - returns list<string> with one line per element */
void* hphl_read_lines(const char* path) {
    void* list = hphl_list_new();
    FILE* f = fopen(path, "r");
    if (!f) return list;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = 0;
        if (len > 1 && line[len-2] == '\r') line[len-2] = 0;
        char* copy = (char*)hphl_str_alloc(strlen(line) + 1);
        strcpy(copy, line);
        hphl_list_add_i(list, (int64_t)(intptr_t)copy);
    }
    fclose(f);
    return list;
}
char* hphl_stat(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        char* out = (char*)hphl_str_alloc(64);
        snprintf(out,64,"{\"size\":0,\"isFile\":0,\"isDir\":0}");
        return out;
    }
    char* out = (char*)hphl_str_alloc(128);
    snprintf(out,128,"{\"size\":%lld,\"isFile\":%d,\"isDir\":%d,\"mtime\":%lld}",
        (long long)st.st_size, S_ISREG(st.st_mode)?1:0, S_ISDIR(st.st_mode)?1:0, (long long)st.st_mtime);
    return out;
}
char* hphl_json_parse(const char* s) { if (!s) s=""; char* out=(char*)hphl_str_alloc(strlen(s)+1); strcpy(out,s); return out; }
char* hphl_json_stringify(const char* s) { if (!s) s=""; char* out=(char*)hphl_str_alloc(strlen(s)+1); strcpy(out,s); return out; }
char* hphl_json_get(const char* root, const char* path) {
    if (!root) root=""; if (!path || !*path) { char* out=(char*)hphl_str_alloc(strlen(root)+1); strcpy(out,root); return out; }
    // naive: look for "path": "value" or path as substring
    const char* p = strstr(root, path);
    if (!p) { char* out=(char*)hphl_str_alloc(1); out[0]=0; return out; }
    // find ':' after path
    const char* colon = strchr(p, ':');
    if (!colon) { char* out=(char*)hphl_str_alloc(1); out[0]=0; return out; }
    colon++;
    while (*colon==' ' || *colon=='\"' || *colon=='\t') colon++;
    const char* end = colon;
    while (*end && *end!=',' && *end!='}' && *end!='\"' && *end!='\n') end++;
    // trim trailing " and spaces
    while (end>colon && (end[-1]=='\"' || end[-1]==' ' || end[-1]=='\t')) end--;
    size_t len = end - colon;
    char* out=(char*)hphl_str_alloc(len+1);
    memcpy(out, colon, len); out[len]=0;
    return out;
}
char* hphl_json_set(const char* root, const char* path, const char* value) {
    if (!root) root="{}"; if (!path) path=""; if (!value) value="\"\"";
    // naive: just append path=value to JSON for demo
    size_t rlen=strlen(root), plen=strlen(path), vlen=strlen(value);
    char* out=(char*)hphl_str_alloc(rlen+plen+vlen+16);
    // if root is {}, replace
    if (strcmp(root,"{}")==0) snprintf(out, rlen+plen+vlen+16, "{\"%s\":%s}", path, value);
    else {
        // remove trailing } and append
        size_t rl = rlen;
        while (rl>0 && (root[rl-1]=='}' || root[rl-1]==' ' || root[rl-1]=='\n')) rl--;
        snprintf(out, rlen+plen+vlen+16, "%.*s,\"%s\":%s}", (int)rl, root, path, value);
    }
    return out;
}
char* hphl_format_date(int64_t epoch, const char* fmt) {
    if (!fmt) fmt="%Y-%m-%d %H:%M:%S";
    time_t t = (time_t)epoch;
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char* out=(char*)hphl_str_alloc(256);
    strftime(out,256,fmt,&tm);
    return out;
}
int64_t hphl_parse_date(const char* s, const char* fmt) {
    if (!s || !fmt) return 0;
    struct tm tm = {0};
    // naive: try to parse YYYY-MM-DD
    int y=0,m=0,d=0;
    if (sscanf(s, "%d-%d-%d", &y,&m,&d)==3) {
        tm.tm_year=y-1900; tm.tm_mon=m-1; tm.tm_mday=d;
        return (int64_t)mktime(&tm);
    }
    return 0;
}

/* M12.5 stdlib: extras */
double hphl_random(void) {
    static uint64_t seed = 0;
    if (!seed) seed = (uint64_t)hphl_clock_ns() | 1;
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)(seed >> 11) / 9007199254740992.0; /* [0,1) */
}
int64_t hphl_random_int(int64_t lo, int64_t hi) {
    if (hi <= lo) return lo;
    double r = hphl_random();
    return lo + (int64_t)(r * (double)(hi - lo + 1));
}
int64_t hphl_sleep_ms(int64_t ms) {
    Sleep((DWORD)ms);
    return 0;
}


char* hphl_env_var(const char* name) {
    const char* v = getenv(name);
    if (!v) return 0;
    char* out = (char*)hphl_str_alloc(strlen(v)+1);
    strcpy(out, v);
    return out;
}
int64_t hphl_exit_prog(int64_t code) {
    exit((int)code);
    return code; /* unreachable */
}
/* M27 system: args, setenv, abort, assert */
void* hphl_args(void) {
    void* list = hphl_list_new();
    extern char** hphl_argv;
    extern int hphl_argc;
    for (int i = 0; i < hphl_argc; i++) {
        char* copy = (char*)hphl_str_alloc(strlen(hphl_argv[i]) + 1);
        strcpy(copy, hphl_argv[i]);
        hphl_list_add_i(list, (int64_t)(intptr_t)copy);
    }
    return list;
}
int64_t hphl_set_env(const char* name, const char* value) {
#ifdef _WIN32
    return SetEnvironmentVariableA(name, value) ? 1 : 0;
#else
    return setenv(name, value, 1) == 0 ? 1 : 0;
#endif
}
void hphl_abort(void) {
#ifdef _WIN32
    ExitProcess(3);
#else
    abort();
#endif
}
/* M27 datetime: now, now_ms */
int64_t hphl_now(void) {
    time_t t = time(NULL);
    return (int64_t)t;
}
int64_t hphl_now_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long ticks = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (int64_t)(ticks / 10000);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

int64_t hphl_now_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int init = 0;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (int64_t)((counter.QuadPart * 1000000LL) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)(ts.tv_sec * 1000000LL + ts.tv_nsec / 1000);
#endif
}


/* M12.3 stdlib: string transform + parse - M_RV1 F13: fix under-alloc */
char* hphl_str_replace(const char* s, const char* from, const char* to) {
    size_t lf = strlen(from), lt = strlen(to);
    if (lf == 0) { char* out = (char*)hphl_str_alloc(strlen(s)+1); strcpy(out, s); return out; }
    size_t count = 0;
    const char* p = s;
    while ((p = strstr(p, from)) != 0) { count++; p += lf; }
    long long newLen = (long long)strlen(s) - (long long)count * (long long)lf + (long long)count * (long long)lt;
    if (newLen < 0) newLen = 0;
    char* out = (char*)hphl_str_alloc((size_t)newLen + 1);
    if (!out) { hphl_panic("str: sem memoria"); return 0; }
    char* o = out;
    while (*s) {
        if (strncmp(s, from, lf) == 0) { memcpy(o, to, lt); o += lt; s += lf; }
        else *o++ = *s++;
    }
    *o = 0;
    return out;
}

double hphl_sin(double v) { return sin(v); }
double hphl_cos(double v) { return cos(v); }
double hphl_tan(double v) { return tan(v); }
double hphl_asin(double v) { return asin(v); }
double hphl_acos(double v) { return acos(v); }
double hphl_atan(double v) { return atan(v); }
double hphl_atan2(double y, double x) { return atan2(y, x); }
double hphl_log(double v) { return log(v); }
double hphl_log2(double v) { return log2(v); }
double hphl_exp(double v) { return exp(v); }
double hphl_exp2(double v) { return exp2(v); }
double hphl_trunc(double v) { return trunc(v); }
double hphl_abs_f64(double v) { return v < 0 ? -v : v; }
int64_t hphl_abs_i64(int64_t v) { return v < 0 ? -v : v; }
double hphl_pow(double b, double e) { return pow(b, e); }
int64_t hphl_clock_ns(void) {
  static volatile LONG freq_once = 0;
  static double ns_per_tick = 0.0;
  if (freq_once == 0) {
    if (InterlockedCompareExchange(&freq_once, 1, 0) == 0) {
      LARGE_INTEGER f;
      QueryPerformanceFrequency(&f);
      ns_per_tick = 1e9 / (double)f.QuadPart;
      _ReadWriteBarrier();
    } else {
      while (ns_per_tick == 0.0) SwitchToThread();
    }
  }
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return (int64_t)((double)t.QuadPart * ns_per_tick);
}

int64_t hphl_str_eq(const char* a, const char* b) {
  if (a == b) return 1;
  if (!a) a = "";
  if (!b) b = "";
  return strcmp(a, b) == 0 ? 1 : 0;
}

int64_t hphl_str_cmp(const char* a, const char* b) {
  if (a == b) return 0;
  if (!a) a = "";
  if (!b) b = "";
  int r = strcmp(a, b);
  return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

#ifdef _WIN32
LONG WINAPI hphl_crash_handler(EXCEPTION_POINTERS* ep) {
  HMODULE self = NULL;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &self);
  unsigned long long rip = (unsigned long long)ep->ContextRecord->Rip;
  unsigned char* p = (unsigned char*)ep->ContextRecord->Rip;
  MEMORY_BASIC_INFORMATION mbiRip, mbiRdi, mbiRsp;
  VirtualQuery((LPCVOID)ep->ContextRecord->Rip, &mbiRip, sizeof(mbiRip));
  VirtualQuery((LPCVOID)ep->ContextRecord->Rdi, &mbiRdi, sizeof(mbiRdi));
  VirtualQuery((LPCVOID)ep->ContextRecord->Rsp, &mbiRsp, sizeof(mbiRsp));
  fprintf(stderr, "[CRASH] code=0x%lx nparams=%lu info0=0x%llx addr=0x%p rip_off=0x%llx rdi=0x%llx rdi_off=0x%llx rax=0x%llx rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx rsp_off=0x%llx bytes=%02x %02x %02x %02x %02x %02x %02x %02x-protx rip=%lx rdi=%lx rsp=%lx\n",
          (unsigned long)ep->ExceptionRecord->ExceptionCode,
          (unsigned long)ep->ExceptionRecord->NumberParameters,
          (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0],
          (void*)ep->ExceptionRecord->ExceptionAddress,
          (unsigned long long)((char*)ep->ContextRecord->Rip - (char*)self),
          (unsigned long long)ep->ContextRecord->Rdi,
          (unsigned long long)((char*)ep->ContextRecord->Rdi - (char*)self),
          (unsigned long long)ep->ContextRecord->Rax,
          (unsigned long long)ep->ContextRecord->Rcx,
          (unsigned long long)ep->ContextRecord->Rdx,
          (unsigned long long)ep->ContextRecord->R8,
          (unsigned long long)ep->ContextRecord->R9,
          (unsigned long long)((char*)ep->ContextRecord->Rsp - (char*)self),
          p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
          (unsigned long)mbiRip.Protect, (unsigned long)mbiRdi.Protect, (unsigned long)mbiRsp.Protect);
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void hphl_posix_crash_handler(int sig, siginfo_t* info, void* ucontext) {
  void* addr = info ? info->si_addr : NULL;
  unsigned long long rip = 0, rsp = 0, rbp = 0;
#if defined(__x86_64__) || defined(__amd64__)
  #if defined(__APPLE__)
    ucontext_t* uc = (ucontext_t*)ucontext;
    if (uc && uc->uc_mcontext) {
      rip = uc->uc_mcontext->__ss.__rip;
      rsp = uc->uc_mcontext->__ss.__rsp;
      rbp = uc->uc_mcontext->__ss.__rbp;
    }
  #elif defined(REG_RIP)
    ucontext_t* uc = (ucontext_t*)ucontext;
    if (uc) {
      rip = (unsigned long long)uc->uc_mcontext.gregs[REG_RIP];
      rsp = (unsigned long long)uc->uc_mcontext.gregs[REG_RSP];
      rbp = (unsigned long long)uc->uc_mcontext.gregs[REG_RBP];
    }
  #endif
#elif defined(__aarch64__)
  #if defined(__APPLE__)
    ucontext_t* uc = (ucontext_t*)ucontext;
    if (uc && uc->uc_mcontext) {
      rip = uc->uc_mcontext->__ss.__pc;
      rsp = uc->uc_mcontext->__ss.__sp;
      rbp = uc->uc_mcontext->__ss.__fp;
    }
  #else
    ucontext_t* uc = (ucontext_t*)ucontext;
    if (uc) {
      rip = (unsigned long long)uc->uc_mcontext.pc;
      rsp = (unsigned long long)uc->uc_mcontext.sp;
      rbp = (unsigned long long)uc->uc_mcontext.regs[29];
    }
  #endif
#endif
  fprintf(stderr, "[CRASH] signal=%d addr=%p rip=0x%llx rsp=0x%llx rbp=0x%llx\n",
          sig, addr, rip, rsp, rbp);
  fflush(stderr);
  signal(sig, SIG_DFL);
  raise(sig);
}
#endif

void hphl_install_crash_handler(void) {
#ifdef _WIN32
  SetUnhandledExceptionFilter(hphl_crash_handler);
#else
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = hphl_posix_crash_handler;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESETHAND;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
  sigaction(SIGILL, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
#endif
}

/* M14.4: handler instalado sempre (não só no boot do pool de spawn) —
 * diagnóstico de access violation em qualquer programa gerado.
 * stdout unbuffered: prints sobrevivem a abort/crash para depuração. */
__attribute__((constructor)) void hphl_crash_handler_init(void) {
  hphl_install_crash_handler();
  /* stdout unbuffered so sob HPHL_UNBUFFERED=1 (debug: prints sobrevivem a abort) */
  if (getenv("HPHL_UNBUFFERED")) setvbuf(stdout, NULL, _IONBF, 0);
}

static void hphl_print_core(const char* fmt, long long v) {
  printf(fmt, v);
}

void hphl_print_int(long long v) {
  hphl_print_core("%lld", v);
  fflush(stdout);
}

void hphl_print_uint(unsigned long long v) {
  printf("%llu", v);
  fflush(stdout);
}

void hphl_print_float(double v) {
  printf("%g", v);
  fflush(stdout);
}

void hphl_print_bool(int b) {
  printf(b ? "true" : "false");
  fflush(stdout);
}

void hphl_print_char(int c) {
  printf("%c", (char)c);
  fflush(stdout);
}

void hphl_print_string(const char* s) {
  if (!s) s = "(null)";
  printf("%s", s);
  fflush(stdout);
}


/* bounds check de indexaÃ§Ã£o de arrays (Milestone 2) */
void hphl_bounds_check(long long index, long long size) {
  if (index < 0 || index >= size) {
    char m[256];
    snprintf(m, sizeof m, "Ã­ndice %lld fora dos limites do array (tamanho %lld)",
             index, size);
    fprintf(stderr, "hphl: %s\n", m);
    hphl_dbg_exc_report(1, m);
    exit(1);
  }
}

/* overflow policies (Milestone 2): 'checked' â€” resultado fora da faixa da
   largura do tipo (em bits) Ã© erro de runtime */
void hphl_overflow_check(long long v, long long min, long long max) {
  if (v < min || v > max) {
    char m[256];
    snprintf(m, sizeof m,
             "overflow em operaÃ§Ã£o inteira (valor %lld fora de [%lld, %lld])",
             v, min, max);
    fprintf(stderr, "hphl: %s\n", m);
    hphl_dbg_exc_report(3, m);
    exit(1);
  }
}

/* float overflow checked: verifica se resultado Ã© inf ou NaN */
void hphl_float_overflow_check(double* v) {
  if (isinf(*v) || isnan(*v)) {
    char m[256];
    snprintf(m, sizeof m, "overflow em operaÃ§Ã£o float (valor %s)",
             isinf(*v) ? (copysign(1.0, *v) > 0 ? "+inf" : "-inf") : "NaN");
    fprintf(stderr, "hphl: %s\n", m);
    hphl_dbg_exc_report(4, m);
    exit(1);
  }
}

/* divisÃ£o por zero sob polÃ­tica 'checked' */
void hphl_divide_by_zero(void) {
  fprintf(stderr, "hphl: divisÃ£o por zero\n");
  hphl_dbg_exc_report(5, "divisÃ£o por zero");
  exit(1);
}

/* match terminou sem nenhum braÃ§o (inalcanÃ§Ã¡vel se exaustivo; acontece se
   todas as guardas 'when' de um sujeito sem '_' falharem) */
void hphl_match_fail(void) {
  fprintf(stderr, "hphl: match nÃ£o correspondeu a nenhum braÃ§o\n");
  hphl_dbg_exc_report(6, "match nÃ£o correspondeu a nenhum braÃ§o");
  exit(1);
}

/* panic("...") â€” falha crÃ­tica do programa */
void hphl_panic(const char* msg) {
  char m[512];
  snprintf(m, sizeof m, "panic: %s", msg ? msg : "(sem mensagem)");
  fprintf(stderr, "hphl: %s\n", m);
  hphl_dbg_exc_report(7, m);
  exit(1);
}

/* assert(cond) â€” invariante de desenvolvimento; falha fatal */
void hphl_assert(long long cond, const char* msg) {
  if (!cond) {
    char m[512];
    snprintf(m, sizeof m, "assert falhou: %s", msg ? msg : "(sem mensagem)");
    fprintf(stderr, "hphl: %s\n", m);
    hphl_dbg_exc_report(8, m);
    exit(1);
  }
}

/* ---------------------------------------------------------------------------
 * Exceções do backend LLVM IR / multi-backend (C9): try/throw com cadeia de
 * handlers por thread. Registro [rip][rsp][rbp][prev][payload].
 * Em x86_64, hphl_exc_begin / hphl_exc_restore usam assembly naked específico
 * da ABI (Windows x64 passa em %rcx; System V AMD64 passa em %rdi).
 * Em outras arquiteturas (AArch64, WASM), utiliza jmp_buf inline (setjmp/longjmp).
 * ------------------------------------------------------------------------ */
typedef struct HphlExc {
  uint64_t rip;            /* 0x00: continuação do hphl_exc_begin no caller */
  uint64_t rsp;            /* 0x08: RSP do caller no momento do call */
  uint64_t rbp;            /* 0x10: RBP do caller (frame pointer) */
  struct HphlExc* prev;    /* 0x18: handler externo (cadeia TLS) */
  void* payload;           /* 0x20: valor do throw corrente */
  jmp_buf jb;              /* fallback setjmp/longjmp para arch não-x86 */
} HphlExc;

_Thread_local static HphlExc* hphl_exc_current = NULL;

void* hphl_exc_new(void) {
  HphlExc* e = (HphlExc*)calloc(1, sizeof(HphlExc));
  if (!e) {
    fprintf(stderr, "hphl: out of memory em hphl_exc_new\n");
    exit(1);
  }
  return e;
}

void hphl_exc_free(void* p) { free(p); }

void hphl_exc_push(void* p) {
  HphlExc* e = (HphlExc*)p;
  e->prev = hphl_exc_current;
  hphl_exc_current = e;
}

void hphl_exc_end(void* p) {
  HphlExc* e = (HphlExc*)p;
  if (hphl_exc_current == e) hphl_exc_current = e->prev;
}

void* hphl_exc_payload(void* p) { return ((HphlExc*)p)->payload; }

#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
__attribute__((naked)) int hphl_exc_begin(void* rec) {
  __asm__ volatile(
    "movq %rcx, %r11\n\t"
    "movq %rbp, 16(%r11)\n\t"
    "leaq 8(%rsp), %rax\n\t"
    "movq %rax, 8(%r11)\n\t"
    "movq (%rsp), %rax\n\t"
    "movq %rax, 0(%r11)\n\t"
    "xorl %eax, %eax\n\t"
    "retq"
  );
}
__attribute__((naked)) void hphl_exc_restore(void* rec) {
  __asm__ volatile(
    "movq %rcx, %r11\n\t"
    "movq 16(%r11), %rbp\n\t"
    "movq 8(%r11), %rsp\n\t"
    "movq 0(%r11), %r11\n\t"
    "movq $1, %rax\n\t"
    "jmpq *%r11"
  );
}
#elif (defined(__x86_64__) || defined(__amd64__))
__attribute__((naked)) int hphl_exc_begin(void* rec) {
  __asm__ volatile(
    "movq %rdi, %r11\n\t"
    "movq %rbp, 16(%r11)\n\t"
    "leaq 8(%rsp), %rax\n\t"
    "movq %rax, 8(%r11)\n\t"
    "movq (%rsp), %rax\n\t"
    "movq %rax, 0(%r11)\n\t"
    "xorl %eax, %eax\n\t"
    "retq"
  );
}
__attribute__((naked)) void hphl_exc_restore(void* rec) {
  __asm__ volatile(
    "movq %rdi, %r11\n\t"
    "movq 16(%r11), %rbp\n\t"
    "movq 8(%r11), %rsp\n\t"
    "movq 0(%r11), %r11\n\t"
    "movq $1, %rax\n\t"
    "jmpq *%r11"
  );
}
#else
int hphl_exc_begin(void* rec) {
  HphlExc* e = (HphlExc*)rec;
  return setjmp(e->jb);
}
void hphl_exc_restore(void* rec) {
  HphlExc* e = (HphlExc*)rec;
  longjmp(e->jb, 1);
}
#endif

void hphl_throw(void* payload) {
  HphlExc* e = hphl_exc_current;
  if (!e) {
    fprintf(stderr, "hphl: throw sem 'catch' ativo\n");
    exit(1);
  }
  e->payload = payload;
  hphl_exc_current = e->prev;
  hphl_exc_restore(e);
  fprintf(stderr, "hphl: hphl_exc_restore retornou (não deveria)\n");
  exit(1);
}

void* hphl_box_i64(long long v) {
  void* p = malloc(8);
  if (!p) {
    fprintf(stderr, "hphl: out of memory em hphl_box_i64\n");
    exit(1);
  }
  *(long long*)p = v;
  return p;
}

long long hphl_unbox_i64(void* p) { return *(long long*)p; }

/* cÃ³pia por valor de struct: bloco novo e independente (hphl_struct_copy) */
void* hphl_struct_copy(const void* src, size_t n) {
  void* p = calloc(1, n ? n : 8);
  if (n && src) memcpy(p, src, n);
  return p;
}

/* ---------------------------------------------------------------------------
 * `threadlocal` (spec Â§10, M2): variÃ¡vel global com um slot por thread.
 * Todo o programa roda no main ou em threads criadas via CreateThread
 * (tarefas), entÃ£o um Ã­ndice TLS por processo serve: hphl_tls_setup(bytes)
 * Ã© chamado no prÃ³logo de Main (registra o tamanho do bloco e aloca o Ã­ndice)
 * e hphl_tls_block() devolve o bloco da thread corrente â€” allocado sob
 * demanda, zerado, e nunca liberado (o processo inteiro vive junto).
 * ------------------------------------------------------------------------ */
static DWORD hphl_tls_index = TLS_OUT_OF_INDEXES;
static int hphl_tls_bytes = 0;

void hphl_tls_setup(int bytes) {
  if (hphl_tls_index == TLS_OUT_OF_INDEXES)
    hphl_tls_index = TlsAlloc();
  hphl_tls_bytes = bytes;
}

void* hphl_tls_block(void) {
  if (hphl_tls_index == TLS_OUT_OF_INDEXES) return NULL; /* sem setup: Main */
  void* b = TlsGetValue(hphl_tls_index);
  if (!b) {
    b = calloc(1, (size_t)hphl_tls_bytes);
    if (!b) {
      fprintf(stderr, "hphl: out of memory em bloco threadlocal\n");
      exit(1);
    }
    TlsSetValue(hphl_tls_index, b);
  }
  return b;
}

