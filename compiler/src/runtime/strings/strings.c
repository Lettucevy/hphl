void* hphl_split(const char* s, const char* sep) {
    void* list = hphl_list_new();
    if (!s) return list;
    size_t ls = strlen(sep);
    if (ls == 0) {
        // M_RV1 F12: sem separador, cada caractere vira um item proprio
        // (evita o double-free antigo: antes adicionava o ponteiro de s
        // como int via hphl_list_add_i e depois ainda tentava adicionar a
        // "last" substring que apontava para o mesmo buffer).
        for (const char* p = s; *p; p++) {
            char* piece = (char*)hphl_str_alloc(2);
            if (!piece) { hphl_panic("split: sem memoria"); return list; }
            piece[0] = *p;
            piece[1] = 0;
            hphl_list_add_i(list, (int64_t)(intptr_t)piece);
        }
        return list;
    }
    const char* start = s;
    const char* p;
    while ((p = strstr(start, sep)) != 0) {
        char* piece = (char*)hphl_str_alloc(p - start + 1);
        memcpy(piece, start, p - start);
        piece[p - start] = 0;
        hphl_list_add_i(list, (int64_t)(intptr_t)piece);
        start = p + ls;
    }
    char* last = (char*)hphl_str_alloc(strlen(start) + 1);
    strcpy(last, start);
    hphl_list_add_i(list, (int64_t)(intptr_t)last);
    return list;
}

int64_t hphl_parse_int(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if (*s < '0' || *s > '9') return 0;
    int64_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

double hphl_parse_f64(const char* s) {
    if (!s) return 0.0;
    return strtod(s, 0);
}

int64_t hphl_is_numeric(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    char* end;
    strtod(s, &end);
    while (*end == ' ' || *end == '\t') end++;
    return *end == 0 && end != s ? 1 : 0;
}
/* M12.2 stdlib: string queries (strings HP-HL sao NUL-terminadas) */
int64_t hphl_str_len(const char* s) { return s ? (int64_t)strlen(s) : 0; }
char* hphl_str_sub(const char* s, int64_t start, int64_t n) {
    if (!s) s = "";
    size_t len = strlen(s);
    if (start < 0) start = 0;
    if ((size_t)start > len) start = len;
    if (n < 0 || (size_t)start + (size_t)n > len) n = (int64_t)(len - start);
    char* out = (char*)hphl_str_alloc((size_t)n + 1);
    if (!out) { hphl_panic("str: sem memoria"); return 0; }
    memcpy(out, s + start, (size_t)n);
    out[n] = 0;
    return out;
}
int64_t hphl_str_index_of(const char* s, const char* needle) {
    if (!s || !needle) return -1;
    const char* p = strstr(s, needle);
    return p ? (int64_t)(p - s) : -1;
}
/* M27: charAt(s, i) - returns single-char string at position i (or empty) */
char* hphl_str_char_at(const char* s, int64_t i) {
    if (!s) s = "";
    size_t len = strlen(s);
    if (i < 0 || (size_t)i >= len) {
        char* r = (char*)hphl_str_alloc(1);
        if (r) r[0] = 0;
        return r;
    }
    return hphl_str_sub(s, i, 1);
}
/* M: string indexing s[i] — retorna o char (i64) na posição i, ou 0 fora */
int64_t hphl_str_char_index(const char* s, int64_t i) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (i < 0 || (size_t)i >= len) return 0;
    return (int64_t)(unsigned char)s[i];
}
/* ord(s)/ord(s, i): codigo do byte (0 fora); chr(c): string de 1 char */
int64_t hphl_str_ord(const char* s) {
  return hphl_str_char_index(s, 0);
}
int64_t hphl_str_ord_at(const char* s, int64_t i) {
  return hphl_str_char_index(s, i);
}
char* hphl_str_chr(int64_t c) {
  char* r = (char*)hphl_str_alloc(2);
  if (r) {
    r[0] = (char)(c & 0xFF);
    r[1] = 0;
  }
  return r;
}
/* M27: lastIndexOf(s, sub) - last occurrence of sub in s, or -1 */
int64_t hphl_str_last_index_of(const char* s, const char* needle) {
    size_t ls = strlen(s), ln = strlen(needle);
    if (ln == 0 || ln > ls) return -1;
    int64_t last = -1;
    for (size_t i = 0; i + ln <= ls; i++) {
        if (strncmp(s + i, needle, ln) == 0) last = (int64_t)i;
    }
    return last;
}


int64_t hphl_str_contains(const char* s, const char* needle) {
    return strstr(s, needle) ? 1 : 0;
}
int64_t hphl_str_starts(const char* s, const char* pre) {
    if (!s || !pre) return 0;
    return strncmp(s, pre, strlen(pre)) == 0 ? 1 : 0;
}
int64_t hphl_str_ends(const char* s, const char* suf) {
    if (!s || !suf) return 0;
    size_t ls = strlen(s), lf = strlen(suf);
    if (lf > ls) return 0;
    return strcmp(s + ls - lf, suf) == 0 ? 1 : 0;
}
char* hphl_str_up(const char* s) {
    size_t n = strlen(s);
    char* out = (char*)hphl_str_alloc(n + 1);
    if (!out) { hphl_panic("str: sem memoria"); return 0; }
    for (size_t i = 0; i < n; i++) out[i] = (char)toupper((unsigned char)s[i]);
    out[n] = 0;
    return out;
}
char* hphl_str_down(const char* s) {
    size_t n = strlen(s);
    char* out = (char*)hphl_str_alloc(n + 1);
    if (!out) { hphl_panic("str: sem memoria"); return 0; }
    for (size_t i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)s[i]);
    out[n] = 0;
    return out;
}
char* hphl_str_trim(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='\r'||s[len-1]=='\n')) len--;
    char* out = (char*)hphl_str_alloc(len + 1);
    if (!out) { hphl_panic("str: sem memoria"); return 0; }
    memcpy(out, s, len); out[len] = 0;
    return out;
}
int64_t hphl_min_i64(int64_t a, int64_t b) { return a < b ? a : b; }
int64_t hphl_max_i64(int64_t a, int64_t b) { return a > b ? a : b; }
double hphl_pi(void) { return 3.14159265358979323846; }
double hphl_e(void) { return 2.71828182845904523536; }
double hphl_floor(double v) { return floor(v); }
double hphl_ceil(double v) { return ceil(v); }
double hphl_round(double v) { return round(v); }
double hphl_fmod(double a, double b) { return fmod(a, b); }
/* M27 math: sqrt, sin, cos, tan, log, exp, trunc */
double hphl_sqrt(double v) { return sqrt(v); }
/* conversÃ£o escalar â†’ string (Milestone 5, coerÃ§Ã£o no '+'/toString):
 * resultado em memÃ³ria alocada (o compilador faz free via hphl_str_free
 * quando a string Ã© temporÃ¡ria â€” print de concat/toString) */
char* hphl_str_from_int(long long v) {
  char* s = (char*)hphl_str_alloc(24);
  if (!s) return NULL;
  snprintf(s, 24, "%lld", v);
  return s;
}

char* hphl_str_from_uint(unsigned long long v) {
  char* s = (char*)hphl_str_alloc(24);
  if (!s) return NULL;
  snprintf(s, 24, "%llu", v);
  return s;
}

char* hphl_str_from_float(double v) {
  char* s = (char*)hphl_str_alloc(32);
  if (!s) return NULL;
  snprintf(s, 32, "%g", v);
  return s;
}

char* hphl_str_from_bool(int b) {
  const char* t = b ? "true" : "false";
  char* s = (char*)hphl_str_alloc(6);
  if (!s) return NULL;
  strcpy(s, t);
  return s;
}

char* hphl_str_from_char(int c) {
  char* s = (char*)hphl_str_alloc(2);
  if (!s) return NULL;
  s[0] = (char)c;
  s[1] = '\0';
  return s;
}

void hphl_str_free(char* s) {
  if (!s) return;
  if (hphl_gc_is_managed(s)) hphl_gc_free_managed(s);
  else free(s);
}
/* concatenaÃ§Ã£o de strings (resultado em memÃ³ria alocada, liberada via free) */
char* hphl_str_concat(const char* a, const char* b) {
  if (!a) a = "";
  if (!b) b = "";
  size_t na = strlen(a), nb = strlen(b);
  char* out = (char*)hphl_str_alloc(na + nb + 1);
  if (!out) {
    fprintf(stderr, "hphl: out of memory em concatenaÃ§Ã£o de strings\n");
    exit(1);
  }
  memcpy(out, a, na);
  memcpy(out + na, b, nb + 1);
  return out;
}
/* M_RV1 G10: padLeft/padRight - preenche com fill ate n (UTF-8 conta bytes por enquanto) */
char* hphl_str_pad_left(const char* s, int64_t n, const char* fill) {
  if (!s) s = "";
  if (!fill || !*fill) fill = " ";
  size_t slen = strlen(s);
  size_t flen = strlen(fill);
  if ((int64_t)slen >= n) { char* out = (char*)hphl_str_alloc(slen+1); strcpy(out,s); return out; }
  size_t pad = (size_t)(n - (int64_t)slen);
  // fill pode ser multi-char, mas para simplicidade repetimos primeiro char
  char f = fill[0];
  char* out = (char*)hphl_str_alloc((size_t)n + 1);
  if (!out) { hphl_panic("str: sem memoria"); return 0; }
  memset(out, f, pad);
  memcpy(out+pad, s, slen+1);
  return out;
}
char* hphl_str_pad_right(const char* s, int64_t n, const char* fill) {
  if (!s) s = "";
  if (!fill || !*fill) fill = " ";
  size_t slen = strlen(s);
  size_t flen = strlen(fill);
  if ((int64_t)slen >= n) { char* out = (char*)hphl_str_alloc(slen+1); strcpy(out,s); return out; }
  size_t pad = (size_t)(n - (int64_t)slen);
  char f = fill[0];
  char* out = (char*)hphl_str_alloc((size_t)n + 1);
  if (!out) { hphl_panic("str: sem memoria"); return 0; }
  memcpy(out, s, slen);
  memset(out+slen, f, pad);
  out[n] = 0;
  return out;
}
/* M_RV1 G1: format - simples: substitui %s, %d, %f por arg */
char* hphl_str_format(const char* fmt, const char* arg) {
  if (!fmt) fmt = "";
  if (!arg) arg = "";
  // conta %s (cada ocorrencia consome `arg` — o buffer precisa de todas)
  size_t flen = strlen(fmt);
  size_t alen = strlen(arg);
  size_t nsub = 0;
  for (const char* q = fmt; *q; q++) {
    if (q[0] == '%' && q[1] == 's') { nsub++; q++; }
  }
  char* out = (char*)hphl_str_alloc(flen + alen * nsub + 64);
  if (!out) { hphl_panic("str: sem memoria"); return 0; }
  char* o = out;
  for (const char* p = fmt; *p; p++) {
    if (p[0]=='%' && p[1]=='s') { strcpy(o, arg); o+=alen; p++; }
    else *o++ = *p;
  }
  *o = 0;
  return out;
}
char* hphl_str_format_int(const char* fmt, int64_t v) {
  char tmp[32]; snprintf(tmp,sizeof(tmp),"%lld",(long long)v);
  return hphl_str_format(fmt, tmp);
}
char* hphl_str_format_float(const char* fmt, double v) {
  char tmp[64]; snprintf(tmp,sizeof(tmp),"%g",v);
  return hphl_str_format(fmt, tmp);
}

