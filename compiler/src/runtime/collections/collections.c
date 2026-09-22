/* -------------------------------------------------------------------------
 * list<T> dinâmico (Milestone 2)
 * ------------------------------------------------------------------------- */
#include <stdint.h>
#include <limits.h>
typedef struct {
  long long count;
  long long capacity;
  long long* items;
} HphlList;

void* hphl_list_new(void) {
  HphlList* l = (HphlList*)malloc(sizeof(HphlList));
  if (!l) {
    fprintf(stderr, "hphl: out of memory em list_new\n");
    exit(1);
  }
  l->count = 0;
  l->capacity = 0;
  l->items = NULL;
  return l;
}
// B11: cria list com capacidade inicial n (evita realocações durante Add)
void* hphl_list_with_cap(long long n) {
  HphlList* l = (HphlList*)malloc(sizeof(HphlList));
  if (!l) {
    fprintf(stderr, "hphl: out of memory em list_with_cap\n");
    exit(1);
  }
  l->count = 0;
  l->capacity = n > 0 ? n : 0;
  l->items = NULL;
  if (l->capacity > 0) {
    l->items = (long long*)calloc((size_t)l->capacity, sizeof(long long));
    if (!l->items) {
      fprintf(stderr, "hphl: out of memory em list_with_cap items\n");
      exit(1);
    }
  }
  return l;
}

void hphl_list_free(void* p) {
  HphlList* l = (HphlList*)p;
  if (l->items) free(l->items);
  free(l);
}

static void hphl_list_grow(HphlList* l) {
  if (l->capacity > SIZE_MAX / 2 / sizeof(long long)) {
    fprintf(stderr, "hphl: list capacity overflow\n");
    exit(1);
  }
  long long nc = l->capacity ? l->capacity * 2 : 8;
  long long* ni = (long long*)realloc(l->items, (size_t)nc * sizeof(long long));
  if (!ni) {
    fprintf(stderr, "hphl: out of memory em list_add\n");
    exit(1);
  }
  l->items = ni;
  l->capacity = nc;
}

void hphl_list_add_i(void* p, long long v) {
  HphlList* l = (HphlList*)p;
  if (l->count == l->capacity) hphl_list_grow(l);
  l->items[l->count++] = v;
}

void hphl_list_add_f(void* p, double v) {
  HphlList* l = (HphlList*)p;
  if (l->count == l->capacity) hphl_list_grow(l);
  memcpy(l->items + l->count, &v, sizeof(double));
  l->count++;
}

char* hphl_join(void* listHandle, const char* sep) {
    HphlList* l = (HphlList*)listHandle;
    size_t lsep = strlen(sep);
    size_t total = 0;
    for (long long i = 0; i < l->count; i++)
        total += strlen((const char*)(intptr_t)l->items[i]) + lsep;
    if (total > 0) total -= lsep;
    char* out = (char*)hphl_str_alloc(total + 1);
    char* o = out;
    for (long long i = 0; i < l->count; i++) {
        const char* piece = (const char*)(intptr_t)l->items[i];
        size_t lp = strlen(piece);
        memcpy(o, piece, lp); o += lp;
        if (i < l->count - 1) { memcpy(o, sep, lsep); o += lsep; }
    }
    *o = 0;
    return out;
}
/* M27: writeLines - writes list<string> to file, one line per element */
int64_t hphl_write_lines(const char* path, void* listHandle) {
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    HphlList* l = (HphlList*)listHandle;
    for (long long i = 0; i < l->count; i++) {
        const char* s = (const char*)(intptr_t)l->items[i];
        fprintf(f, "%s\n", s);
    }
    fclose(f);
    return 1;
}

long long hphl_list_len(void* p) { return ((HphlList*)p)->count; }

/* endereÃ§o do buffer de elementos (NULL se vazio) */
void* hphl_list_data(void* p) { return ((HphlList*)p)->items; }

/* fatia `[from, from+count)` do list como list novo (cÃ³pia de `..resto` no
   pattern). elemSize = tamanho em bytes de cada elemento (stride). */
void* hphl_list_slice(void* p, long long from, long long count, long long elemSize) {
  HphlList* src = (HphlList*)p;
  HphlList* out = (HphlList*)malloc(sizeof(HphlList));
  if (!out) {
    fprintf(stderr, "hphl: out of memory em list_slice\n");
    exit(1);
  }
  out->count = count;
  out->capacity = count;
  if (count == 0) {
    out->items = NULL;
  } else {
    out->items = (long long*)malloc((size_t)count * 8);
    if (!out->items) {
      fprintf(stderr, "hphl: out of memory em list_slice\n");
      exit(1);
    }
    if (src->items)
      memcpy(out->items, (const char*)src->items + from * elemSize,
             (size_t)count * elemSize);
  }
  return out;
}

/* bounds check de indexaÃ§Ã£o de list (tamanho dinÃ¢mico) */
void hphl_list_check(long long index, void* p) {
  long long len = ((HphlList*)p)->count;
  if (index < 0 || index >= len) {
    char m[256];
    snprintf(m, sizeof m, "Ã­ndice %lld fora dos limites do list (tamanho %lld)",
             index, len);
    fprintf(stderr, "hphl: %s\n", m);
    hphl_dbg_exc_report(2, m);
    exit(1);
  }
}

/* map<K,V> dinÃ¢mico (M10.1) â€” tabela hash aberta simples com linear probing */
typedef struct {
  long long count;
  long long capacity;
  long long *keys;
  long long *vals;
  char *occupied; // 0 vazio, 1 ocupado, 2 tombstone
  int keyKind; // 0 int, 1 string, 2 bool, 3 char
} HphlMap;

static unsigned long long hphl_map_hash(long long key, int kind) {
  if (kind == 1) { // string: djb2 da conteÃºdo
    const char *s = (const char*)key;
    if (!s) return 0;
    unsigned long long h = 5381;
    for (const char *p=s; *p; p++) h = ((h<<5)+h) + (unsigned char)*p;
    return h;
  }
  // int/bool/char: valor direto
  return (unsigned long long)(key ^ (key>>33));
}
static int hphl_map_eq(long long a, long long b, int kind) {
  if (kind == 1) {
    const char *sa=(const char*)a, *sb=(const char*)b;
    if (sa==sb) return 1;
    if (!sa || !sb) return 0;
    return strcmp(sa,sb)==0;
  }
  return a==b;
}
void* hphl_map_new(long long keyKind) {
  HphlMap *m=(HphlMap*)malloc(sizeof(HphlMap));
  if (!m) { fprintf(stderr,"hphl: out of memory em map_new\n"); exit(1); }
  m->count=0; m->capacity=8;
  m->keys=(long long*)calloc(8,sizeof(long long));
  m->vals=(long long*)calloc(8,sizeof(long long));
  m->occupied=(char*)calloc(8,1);
  m->keyKind=(int)keyKind;
  if (!m->keys||!m->vals||!m->occupied){fprintf(stderr,"hphl: oom map\n");exit(1);}
  return m;
}
void hphl_map_free(void *p) {
  HphlMap *m=(HphlMap*)p;
  if (!m) return;
  free(m->keys); free(m->vals); free(m->occupied); free(m);
}
static void hphl_map_grow(HphlMap *m) {
  long long nc=m->capacity*2;
  long long *nk=(long long*)calloc(nc,sizeof(long long));
  long long *nv=(long long*)calloc(nc,sizeof(long long));
  char *no=(char*)calloc(nc,1);
  if (!nk||!nv||!no){fprintf(stderr,"hphl: oom map grow\n");exit(1);}
  for (long long i=0;i<m->capacity;i++) if (m->occupied[i]==1) {
    unsigned long long h=hphl_map_hash(m->keys[i],m->keyKind);
    for (long long j=0;j<nc;j++){ long long idx=(h+j)%nc; if(!no[idx]){ nk[idx]=m->keys[i]; nv[idx]=m->vals[i]; no[idx]=1; break; }}
  }
  free(m->keys); free(m->vals); free(m->occupied);
  m->keys=nk; m->vals=nv; m->occupied=no; m->capacity=nc;
}
void hphl_map_put(void *p, long long k, long long v) {
  HphlMap *m=(HphlMap*)p;
  if (m->count*4 >= m->capacity*3) hphl_map_grow(m); /* M_RV1 F6: 0.75 load factor */
  unsigned long long h=hphl_map_hash(k,m->keyKind);
  long long firstTomb=-1;
  for (long long j=0;j<m->capacity;j++){ long long idx=(h+j)%m->capacity;
    if (m->occupied[idx]==0){ long long use= firstTomb>=0?firstTomb:idx; m->keys[use]=k; m->vals[use]=v; m->occupied[use]=1; m->count++; return; }
    if (m->occupied[idx]==2 && firstTomb<0) firstTomb=idx;
    if (m->occupied[idx]==1 && hphl_map_eq(m->keys[idx],k,m->keyKind)){ m->vals[idx]=v; return; }
  }
  if (firstTomb>=0){ m->keys[firstTomb]=k; m->vals[firstTomb]=v; m->occupied[firstTomb]=1; m->count++; return; }
  hphl_map_grow(m); hphl_map_put(p,k,v);
}
long long hphl_map_get(void *p, long long k) {
  HphlMap *m=(HphlMap*)p;
  unsigned long long h=hphl_map_hash(k,m->keyKind);
  for (long long j=0;j<m->capacity;j++){ long long idx=(h+j)%m->capacity;
    if (m->occupied[idx]==0) break;
    if (m->occupied[idx]==1 && hphl_map_eq(m->keys[idx],k,m->keyKind)) return m->vals[idx];
  }
  return 0;
}
long long hphl_map_contains(void *p, long long k) {
  HphlMap *m=(HphlMap*)p;
  unsigned long long h=hphl_map_hash(k,m->keyKind);
  for (long long j=0;j<m->capacity;j++){ long long idx=(h+j)%m->capacity;
    if (m->occupied[idx]==0) break;
    if (m->occupied[idx]==1 && hphl_map_eq(m->keys[idx],k,m->keyKind)) return 1;
  }
  return 0;
}
long long hphl_map_remove(void *p, long long k) {
  HphlMap *m=(HphlMap*)p;
  unsigned long long h=hphl_map_hash(k,m->keyKind);
  for (long long j=0;j<m->capacity;j++){ long long idx=(h+j)%m->capacity;
    if (m->occupied[idx]==0) break;
    if (m->occupied[idx]==1 && hphl_map_eq(m->keys[idx],k,m->keyKind)){ m->occupied[idx]=2; m->count--; return 1; }
  }
  return 0;
}
void hphl_map_clear(void *p){ HphlMap *m=(HphlMap*)p; memset(m->occupied,0,m->capacity); m->count=0; }
long long hphl_map_len(void *p){ return ((HphlMap*)p)->count; }

/* tupla (M10.1b): bloco heap de NÃ—8 bytes; valor = handle (como classe).
 * AtribuiÃ§Ã£o entre variÃ¡veis clona (semÃ¢ntica de valor); elementos sÃ£o
 * lidos por destructuring. */
void *hphl_tuple_new(long long n) {
  void *p = calloc((size_t)(n > 0 ? n : 1), 8);
  if (!p) { fprintf(stderr, "hphl: out of memory em tuple_new\n"); exit(1); }
  return p;
}
void *hphl_tuple_clone(void *src, long long n) {
  if (!src) return NULL;
  void *p = malloc((size_t)((n > 0 ? n : 1) * 8));
  if (!p) { fprintf(stderr, "hphl: out of memory em tuple_clone\n"); exit(1); }
  memcpy(p, src, (size_t)(n * 8));
  return p;
}

