/* M18.2: tabelas young/old separadas. Todo objeto nasce em young, é promovido
 * para old (flag gen no header + entrada em gc_registered_old) após sobreviver
 * a N minor collections (M18.5). Em M18.2 a "promoção" continua sendo apenas
 * atualizar o flag e mover a entrada de array — M18.5 troca malloc por arena
 * old com cópia real do payload.
 */
#define GC_INIT_CAP 64
static size_t gc_cap_young = GC_INIT_CAP;
static size_t gc_count_young = 0;
static void** gc_registered_young = NULL;
static size_t gc_cap_old = GC_INIT_CAP;
static size_t gc_count_old = 0;
static void** gc_registered_old = NULL;

/* M18.5: idade de cada objeto young (nº de minors sobrevividos).
 * Quando atinge GC_PROMO_AGE, é promovido para old. */
#define GC_PROMO_AGE 2
/* Diagnóstico M31: sobrescreve via env (HPHL_GC_PROMO_AGE) p/ bissectar crash. */
static int gc_promo_age(void) {
    const char* v = getenv("HPHL_GC_PROMO_AGE");
    if (v && *v) { long n = atol(v); if (n > 0 && n < 1000000) return (int)n; }
    return GC_PROMO_AGE;
}
static unsigned char* gc_young_age = NULL;
static size_t gc_young_age_cap = GC_INIT_CAP;

/* Arena old mantida para cleanup */
static HphlArenaBlock* gc_old_arena = NULL;
#define GC_MAP_EMPTY 0
#define GC_MAP_OCCUP 1
#define GC_MAP_TOMB 2
typedef struct { void* ptr; size_t idx; uint8_t gen; uint8_t state; } GcMapEntry;
static GcMapEntry* gc_map = NULL;
static size_t gc_map_cap = 0;
static size_t gc_map_len = 0;
/* M31: contador de tombstones. Removes criam tombs; adds reutilizam. Se
 * tombs acumulam (steady churn), as probe chains degradam para O(cap) e
 * cada minor de 30k vira centenas de ms. O sweep dispara rebuild quando
 * tombs > live. */
static size_t gc_map_tombs = 0;
static inline size_t gc_ptr_hash(void* p) {
    /* M31 FIX: splitmix64 finalizer (64-bit). O hash anterior (x *= const
     * 32-bit) não misturava os bits altos de endereços heap sequenciais,
     * clusterizando tudo em poucas cadeias: sweep de 32k objetos levava
     * 750ms em probe chains O(n). Com splitmix: O(1) de novo. */
    uint64_t x = (uint64_t)(uintptr_t)p + 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return (size_t)x;
}
static void gc_map_init(void) {
    if (gc_map) return;
    gc_map_cap = 64;
    gc_map = (GcMapEntry*)calloc(gc_map_cap, sizeof(GcMapEntry));
}
static void gc_map_resize(size_t ncap) {
    GcMapEntry* old = gc_map; size_t ocap = gc_map_cap;
    gc_map = (GcMapEntry*)calloc(ncap, sizeof(GcMapEntry));
    gc_map_cap = ncap; gc_map_len = 0; gc_map_tombs = 0;
    for (size_t i=0;i<ocap;i++) if (old[i].state==GC_MAP_OCCUP) {
        size_t mask = gc_map_cap-1; size_t h = gc_ptr_hash(old[i].ptr) & mask;
        while (gc_map[h].state==GC_MAP_OCCUP) h=(h+1)&mask;
        gc_map[h]=old[i]; gc_map_len++;
    }
    free(old);
}
static inline void gc_map_ensure(void) {
    if (!gc_map) gc_map_init();
    if (gc_map_len*2 >= gc_map_cap) gc_map_resize(gc_map_cap*2);
}
static void gc_map_add(void* p, size_t idx, uint8_t gen) {
    if (!p) return;
    gc_map_ensure();
    size_t mask = gc_map_cap-1; size_t h = gc_ptr_hash(p) & mask;
    size_t first_tomb=(size_t)-1;
    for (size_t probe=0; probe<gc_map_cap; probe++) {
        uint8_t s = gc_map[h].state;
        if (s==GC_MAP_EMPTY) {
            size_t ins = first_tomb!=(size_t)-1?first_tomb:h;
            if (ins != h) gc_map_tombs--;
            gc_map[ins].ptr=p; gc_map[ins].idx=idx; gc_map[ins].gen=gen; gc_map[ins].state=GC_MAP_OCCUP; gc_map_len++; return;
        }
        if (s==GC_MAP_OCCUP && gc_map[h].ptr==p) { gc_map[h].idx=idx; gc_map[h].gen=gen; return; }
        if (s==GC_MAP_TOMB && first_tomb==(size_t)-1) first_tomb=h;
        h=(h+1)&mask;
    }
    gc_map_resize(gc_map_cap*2); gc_map_add(p,idx,gen);
}
static int gc_map_contains(void* p) {
    if (!p || !gc_map) return 0;
    size_t mask=gc_map_cap-1; size_t h=gc_ptr_hash(p)&mask;
    for (size_t probe=0;probe<gc_map_cap;probe++) {
        uint8_t s=gc_map[h].state;
        if (s==GC_MAP_EMPTY) return 0;
        if (s==GC_MAP_OCCUP && gc_map[h].ptr==p) return 1;
        h=(h+1)&mask;
    }
    return 0;
}
static int gc_map_get(void* p, size_t* out_idx, uint8_t* out_gen) {
    if (!p || !gc_map) return 0;
    size_t mask=gc_map_cap-1; size_t h=gc_ptr_hash(p)&mask;
    for (size_t probe=0;probe<gc_map_cap;probe++) {
        uint8_t s=gc_map[h].state;
        if (s==GC_MAP_EMPTY) return 0;
        if (s==GC_MAP_OCCUP && gc_map[h].ptr==p) {
            if (out_idx) *out_idx=gc_map[h].idx;
            if (out_gen) *out_gen=gc_map[h].gen; return 1;
        }
        h=(h+1)&mask;
    }
    return 0;
}
static void gc_map_remove(void* p) {
    if (!p || !gc_map) return;
    size_t mask=gc_map_cap-1; size_t h=gc_ptr_hash(p)&mask;
    for (size_t probe=0;probe<gc_map_cap;probe++) {
        uint8_t s=gc_map[h].state;
        if (s==GC_MAP_EMPTY) return;
        if (s==GC_MAP_OCCUP && gc_map[h].ptr==p) { gc_map[h].state=GC_MAP_TOMB; gc_map[h].ptr=NULL; gc_map_len--; gc_map_tombs++; return; }
        h=(h+1)&mask;
    }
}
static void gc_map_update_idx(void* p, size_t nidx) {
    if (!p || !gc_map) return;
    size_t mask=gc_map_cap-1; size_t h=gc_ptr_hash(p)&mask;
    for (size_t probe=0;probe<gc_map_cap;probe++) {
        uint8_t s=gc_map[h].state;
        if (s==GC_MAP_EMPTY) return;
        if (s==GC_MAP_OCCUP && gc_map[h].ptr==p) { gc_map[h].idx=nidx; return; }
        h=(h+1)&mask;
    }
}
static void gc_map_update_gen_idx(void* p, uint8_t gen, size_t idx) {
    if (!p || !gc_map) return;
    size_t mask=gc_map_cap-1; size_t h=gc_ptr_hash(p)&mask;
    for (size_t probe=0;probe<gc_map_cap;probe++) {
        uint8_t s=gc_map[h].state;
        if (s==GC_MAP_EMPTY) return;
        if (s==GC_MAP_OCCUP && gc_map[h].ptr==p) { gc_map[h].gen=gen; gc_map[h].idx=idx; return; }
        h=(h+1)&mask;
    }
}
/* M31 FIX: rebuild do mapa quando tombstones dominam. Sem isso as probe
 * chains degradam O(n) em churn steady e cada minor de 30k vira centenas
 * de ms. Chamado no início do minor GC. Rebuild = re-hash só as entradas
 * ocupadas num mapa novo (mesmo tamanho). */
static void gc_map_rebuild_if_needed(void) {
    if (!gc_map) return;
    if (gc_map_len + gc_map_tombs < gc_map_cap / 4) return; /* healthy */
    GcMapEntry* old = gc_map; size_t ocap = gc_map_cap;
    gc_map = (GcMapEntry*)calloc(ocap, sizeof(GcMapEntry));
    gc_map_cap = ocap; gc_map_len = 0; gc_map_tombs = 0;
    size_t mask = gc_map_cap - 1;
    for (size_t i = 0; i < ocap; i++) {
        if (old[i].state != GC_MAP_OCCUP) continue;
        size_t h = gc_ptr_hash(old[i].ptr) & mask;
        while (gc_map[h].state == GC_MAP_OCCUP) h = (h + 1) & mask;
        gc_map[h] = old[i]; gc_map_len++;
    }
    free(old);
}
#define GC_SET_TOMB ((void*)1)
static void** gc_set = NULL; static size_t gc_set_cap=0; static size_t gc_set_len=0;
static void gc_set_init(void){ if(gc_set) return; gc_set_cap=64; gc_set=(void**)calloc(gc_set_cap,sizeof(void*)); }
static void gc_set_add(void* p){ if(p) gc_map_add(p,0,0); }
static void gc_set_remove(void* p){ gc_map_remove(p); }
static int gc_set_contains(void* p){ return gc_map_contains(p); }
/* M27: class descriptor table for bitmap-based GC scanning.
 * Codegen calls hphl_set_class_desc() at program startup to register
 * each class (size + bitmap). The returned classId is stored in hdr[1]
 * upper bits by hphl_shared_alloc_typed(). GC uses the bitmap to scan
 * only pointer slots instead of all slots.
 * M32: a bitmap NÃO é mais copiada para a tabela — a entrada guarda um
 * ponteiro para os bytes emitidos pelo codegen em .rdata (vida do processo).
 * Motivo: copiar truncava em MAX_BMAP_BYTES (32B = 256 slots), então classes
 * com arrays inline grandes (ex: CG com string[120000]) perdiam todos os
 * ponteiros além do slot 256 e o sweep liberava objetos vivos (crash no
 * bootstrap do amálgama). Sem cópia não há teto. */
#define MAX_CLASS_DESCS 256
typedef struct {
    int size;                         /* object payload size in bytes */
    int nbytes;                       /* bitmap byte count = (slots+7)/8 */
    const unsigned char* bitmap;      /* rodata do programa; NULL = sem bits */
} ClassDescEntry;
static ClassDescEntry class_desc_table[MAX_CLASS_DESCS];
static int class_desc_count = 0;
/* Returns classId (1-based) or -1 on full table. Called by codegen startup. */
int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm) {
    if (class_desc_count >= MAX_CLASS_DESCS - 1) return -1;
    int id = ++class_desc_count;  /* 1-based; 0 = untyped */
    ClassDescEntry* e = &class_desc_table[id];
    e->size = size;
    e->nbytes = (nbytes > 0) ? nbytes : 0;
    e->bitmap = (nbytes > 0) ? bm : NULL;  /* sem cópia: aponta p/ .rdata */
    return id;
}
static ClassDescEntry* get_class_desc(int id) {
    if (id <= 0 || id > class_desc_count) return NULL;
    return &class_desc_table[id];
}
/* Forward declaration for hphl_hdr_class_id (defined later, used in GC scan) */
static int hphl_hdr_class_id(unsigned long long* hdr);
static void* gc_old_arena_alloc(size_t n) {
    n = (n + 7) & ~(size_t)7;
    HphlArenaBlock* b = gc_old_arena;
    if (!b || b->used + n > b->cap) {
        size_t cap = n > 16384 ? n : 16384;
        b = (HphlArenaBlock*)malloc(sizeof(HphlArenaBlock)+cap);
        if (!b) { hphl_panic("old_arena: sem memoria"); return 0; }
        b->used = 0; b->cap = cap; b->next = gc_old_arena; gc_old_arena = b;
    }
    void* p = (char*)(b+1) + b->used;
    b->used += n;
    memset(p, 0, n);
    return p;
}
/* Bit MAGIC do hdr[1] (bit 63): marca "este bloco é um objeto GC válido".
 * Usado por hasGCHdr() para validar candidatos em scanning conservador.
 * Como esse bit é setado exclusivamente por hphl_shared_alloc*, qualquer
 * valor arbitrário (inteiro, lixo) em slots escalares nunca coincidirá.
 *
 * Definido cedo (perto de isPlausibleGC) para que hasGCHdr() e outras
 * funções GC declaradas antes da seção de layout de header possam usá-lo. */
#define HPHL_HDR_MAGIC_BIT   (1ULL << 63)
#define HPHL_HDR_SIZE_MASK    0xFFFFFFFFULL
#define HPHL_HDR_GEN_BIT      (1ULL << 32)
#define HPHL_HDR_MARK_BIT     (1ULL << 33)
#define HPHL_HDR_FORWARD_BIT  (1ULL << 34)
#define HPHL_HDR_PINNED_BIT   (1ULL << 35)
#define HPHL_HDR_CARD_BIT     (1ULL << 36)
#define HPHL_HDR_CLSID_MASK  (0x3FFULL << 37)
#define HPHL_HDR_CLSID_SHIFT 37
#define HPHL_HDR_CLSID_MAX   1023

static inline int isPlausibleGC(void* p) {
    uintptr_t v = (uintptr_t)p;
    return v != 0 && (v & 7) == 0 && v >= 0x10000;
}
/* Validação segura de ponteiro GC: além de plausibilidade (alinhamento +
 * limite mínimo), exige que o bit MAGIC em hdr[1] esteja setado. Isso só
 * acontece para blocos emitidos por hphl_shared_alloc*, então inteiros
 * arbitrários ou ponteiros para memória não-GC nunca passam. */
static inline int hasGCHdr(void* p) {
    if (!isPlausibleGC(p)) return 0;
    unsigned long long* hdr = (unsigned long long*)p - 2;
    unsigned long long w = hdr[1];
    if ((w & HPHL_HDR_MAGIC_BIT) == 0) return 0;
    if ((w & HPHL_HDR_SIZE_MASK) == 0) return 0;
    return 1;
}
static void fwd_patch_all(void);

/* Forward decls de helpers de header (definidos mais abaixo) — necessários
 * aqui porque gc_scan_obj e hasGCHdr já os usam. */
static inline unsigned long long hphl_hdr_size(unsigned long long* hdr);
static inline int                hphl_hdr_mark(unsigned long long* hdr);
static inline void               hphl_hdr_set_mark(unsigned long long* hdr, int v);
static inline int                hphl_hdr_class_id(unsigned long long* hdr);
static inline int                hphl_hdr_gen(unsigned long long* hdr);
static inline int gc_obj_alive_after_mark(unsigned long long* hdr, int has_roots);

static int gc_is_registered(void* p) {
    return gc_set_contains(p);
}

/* Percorre os slots de ponteiro de um objeto. Se o objeto tem classId,
 * usa a bitmap precisa. Caso contrário, cai no modo conservador SEGURO:
 * valida cada slot com hasGCHdr() E gc_is_registered() antes de tratar
 * como ponteiro. Nenhum escalar será interpretado como referência.
 * Para cada filho válido, chama o callback `visit`. Retorna 1 se a varredura
 * foi precisa (bitmap), 0 se foi conservadora. */
typedef int (*gc_visit_fn)(void* child, void* ud);
static int gc_scan_obj(void* p, gc_visit_fn visit, void* ud) {
    if (!p) return 1;
    unsigned long long* hdr = (unsigned long long*)p - 2;
    size_t sz = (size_t)hphl_hdr_size(hdr);
    if (sz == 0) return 1;
    unsigned long long* fields = (unsigned long long*)p;
    size_t slots = sz / 8;
    int cid = hphl_hdr_class_id(hdr);
    ClassDescEntry* cd = (cid > 0) ? get_class_desc(cid) : NULL;
    if (cd && cd->bitmap) {
        int nbytes = cd->nbytes;
        for (size_t s = 0; s < slots; s++) {
            int byte = (int)(s / 8);
            if (byte < nbytes) {
                if (!(cd->bitmap[byte] & (1u << (s % 8)))) continue;
                void* cand = (void*)(uintptr_t)fields[s];
                visit(cand, ud);
            } else {
                /* M32: além da bitmap emitida (layout cresceu depois do
                 * registro, ou campo sem bit): fallback conservador validado.
                 * Sem isso, qualquer sub-marcação vira free prematuro. */
                void* cand = (void*)(uintptr_t)fields[s];
                if (!gc_is_registered(cand)) continue;
                if (!hasGCHdr(cand)) continue;
                visit(cand, ud);
            }
        }
        return 1;
    } else {
        for (size_t s = 0; s < slots && s < 64; s++) {
            void* cand = (void*)(uintptr_t)fields[s];
            if (!gc_is_registered(cand)) continue;
            if (!hasGCHdr(cand)) continue;
            visit(cand, ud);
        }
        return 0;
    }
}

/* M19 1.6 incremental tri-color */
#define GREY_INIT 64
static size_t grey_cap = GREY_INIT;
static size_t grey_count = 0;
static void** grey_list = NULL;
static void grey_init(void) {
    if (!grey_list) { grey_list = (void**)malloc(GREY_INIT*sizeof(void*)); grey_cap = GREY_INIT; }
}
static void grey_push(void* p) {
    if (!p) return;
    unsigned long long* hdr = (unsigned long long*)p - 2;
    if (hphl_hdr_mark(hdr)) return;
    hphl_hdr_set_mark(hdr, 1);
    grey_init();
    if (grey_count >= grey_cap) {
        size_t nc = grey_cap*2;
        void** nb = (void**)realloc(grey_list, nc*sizeof(void*));
        if (!nb) { hphl_panic("grey_push: sem memoria"); return; }
        grey_list = nb;
        grey_cap = nc;
    }
    grey_list[grey_count++] = p;
}
static void* grey_pop(void) {
    if (grey_count==0) return NULL;
    return grey_list[--grey_count];
}
static void grey_clear(void) { grey_count = 0; }
static int visit_grey(void* cand, void* ud) {
    (void)ud;
    if (!cand) return 0;
    if (!gc_is_registered(cand)) return 0;
    unsigned long long* hdr = (unsigned long long*)cand - 2;
    if (hphl_hdr_mark(hdr)) return 0;
    grey_push(cand);
    return 0;
}
static void grey_scan_step(int budget) {
    while (grey_count>0 && budget-- >0) {
        void* p = grey_pop();
        gc_scan_obj(p, visit_grey, NULL);
    }
}

/* M21 1.6: fatiamento real budget 64 (M20 usava 256). */
int hphl_gc_step(int budget) {
    if (budget <= 0) budget = 64;
    int processed = 0;
    while (grey_count > 0 && processed < budget) {
        void* p = grey_pop();
        gc_scan_obj(p, visit_grey, NULL);
        processed++;
    }
    return (grey_count == 0) ? 1 : 0;
}

/* ==== BDWGC-inspired conservative stack scan safety net ===================
 * Adaptação do algoritmo do BDWGC (Boehm-Demers-Weiser): quando não há roots
 * precisos suficientes (ex: chamada FFI, ou M28 ainda não emitiu roots para
 * alguma função), percorre a pilha como um conjunto de words e tenta
 * interpretar cada word como um ponteiro para objeto GC. Inspirado em
 * GC_push_all_stack() de bdwgc/misc.c. Semelhante também ao fallback
 * conservador do MPS para stacks de linguagens C externas.
 *
 * SAFETY: usa hasGCHdr() + gc_is_registered() como filtro estrito — só
 * tratamos como ponteiro valores que satisfazem ambos. Não há risco de
 * tratar inteiros arbitrários como ponteiros.
 *
 * Chamado automaticamente em hphl_gc_pressure() se roots == 0 (debug env
 * opt-in: HPHL_GC_CONSERVATIVE=1) ou como rede de segurança quando
 * gc_root_count == 0 e gc_pressure_env == 1.
 * =========================================================================*/
#if defined(_WIN32)
#include <windows.h>
static void* hphl_get_stack_base(void) {
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    return tib ? tib->StackBase : NULL;
}
#else
static void* hphl_get_stack_base(void) {
    return NULL;
}
#endif

static void gc_push_stack_words(void** lo, void** hi, gc_visit_fn visit, void* ud) {
    void* base = hphl_get_stack_base();
    if (base && (void*)hi > base) hi = (void**)base;
    void** p = lo;
    while (p < hi) {
        void* cand = *p;
        if (cand && gc_is_registered(cand) && hasGCHdr(cand)) {
            visit(cand, ud);
        }
        p++;
    }
}
static int gc_conservative_stack_scan_enabled = -1;
static int gc_conservative_stack_check(void) {
    if (gc_conservative_stack_scan_enabled < 0) {
        const char* v = getenv("HPHL_GC_CONSERVATIVE");
        if (!v) gc_conservative_stack_scan_enabled = 1; /* M_RV1 E12: default on */
        else gc_conservative_stack_scan_enabled = (v[0] != '0') ? 1 : 0;
    }
    return gc_conservative_stack_scan_enabled;
}
void hphl_gc_push_stack(void* lo_addr, void* hi_addr) {
    gc_push_stack_words((void**)lo_addr, (void**)hi_addr, visit_grey, NULL);
}

/* ==== rpmalloc-inspired thread cache for shared_alloc =====================
 * Cache LIFO de blocos recentemente liberados do shared heap, indexado por
 * classe de tamanho (potências de 2, 16..2048 bytes). Hits evitam malloc/free
 * do sistema (gargalo em workloads many-short-lived, ex. binary-trees).
 *
 * M31 FIX: era `static __thread` — o GCC 16 gerou acesso TLS com offset
 * errado (lê base+0x10+b*8 em vez de &tc_len[b]; provado: &tc_len-&tc_bins
 * = -64 mas o asm lê +0x10). Isso lia/escrevia lixo (uninit read acusado
 * pelo Dr. Memory 1000× em concat, heap corruption 0xC0000374). Agora são
 * estáticos com spinlock (correto em single e multi-thread).
 * Apenas para a faixa hot (16..2048) — fora disso cai no malloc.
 * Cap por classe = 32 entries (256 KiB worst case) para limitar footprint.
 * =========================================================================*/
#define HPHL_TC_BIN_MAX     8      /* bins: 16,32,64,128,256,512,1024,2048 */
#define HPHL_TC_BIN_MAX_SZ  2048
/* M31: cap 32 -> 64 por bin (churn uniforme string/objeto atinge ~100% de
 * reuse LIFO; worst case 8*64*2KB = 1MB, típico <100KB). */
#define HPHL_TC_CAP_PER_BIN 64
static void* tc_bins[HPHL_TC_BIN_MAX][HPHL_TC_CAP_PER_BIN];
static size_t tc_len[HPHL_TC_BIN_MAX];
static volatile int tc_lock = 0;
static inline void tc_lock_acq(void) {
    while (__sync_lock_test_and_set(&tc_lock, 1)) { /* spin */ }
}
static inline void tc_lock_rel(void) { __sync_lock_release(&tc_lock); }
static int tc_bin_for(size_t nbytes) {
    /* nbytes = payload + header (total block size). */
    size_t s = 16;
    for (int i = 0; i < HPHL_TC_BIN_MAX; i++) {
        if (nbytes <= s) return i;
        s <<= 1;
    }
    return -1;
}
void* hphl_tc_alloc(size_t n) {
    int b = tc_bin_for(n);
    if (b < 0) return NULL;
    tc_lock_acq();
    void* hit = (tc_len[b] > 0) ? tc_bins[b][--tc_len[b]] : NULL;
    tc_lock_rel();
    return hit;
}
/* M31 FIX: getenv por objeto no sweep era o gargalo real (getenv é lento
 * no Windows). Cache em static int lidos uma vez. */
static int gc_env_nocache = -1;
static int gc_env_nofree = -1;
static inline int gc_nocache_env(void) {
    if (gc_env_nocache < 0) gc_env_nocache = getenv("HPHL_GC_NOCACHE") ? 1 : 0;
    return gc_env_nocache;
}
static inline int gc_nofree_env(void) {
    if (gc_env_nofree < 0) gc_env_nofree = getenv("HPHL_GC_NOFREE") ? 1 : 0;
    return gc_env_nofree;
}
void hphl_tc_free(void* payload, size_t n) {
    /* payload é o ponteiro de payload. n é o payload size. */
    if (!payload) return;
    if (gc_nocache_env()) { free((char*)payload - 16); return; }
    int b = tc_bin_for(n);
    if (b < 0) { free((char*)payload - 16); return; }
    tc_lock_acq();
    if (tc_len[b] >= HPHL_TC_CAP_PER_BIN) {
        tc_lock_rel();
        free((char*)payload - 16);
        return;
    }
    tc_bins[b][tc_len[b]++] = payload;
    tc_lock_rel();
}

/* ==== MPS-inspired pool class dispatch =====================================
 * Inspirado no Memory Pool System (Ravenbrook): cada pool class define
 * uma estratégia de alocação + reclamation. Em hphl o runtime já tinha
 * 3 "memory policies" (arena, pool, shared); aqui adicionamos uma
 * tabela de despacho unificada para que gc.c decida a política certa
 * baseada no tipo (não no storage policy legado).
 *
 * Classes:
 *  - MANUAL: arena/pool legado — não participa do GC. (HPHL_POOL_MANUAL)
 *  - AMC:    mostly-copying automático, com GC tracing. (HPHL_POOL_AMC)
 *  - EPVM:   "exact-life pool manual": coletado quando o owner libera.
 *
 * Hoje o hphl_shared_alloc* já cobre AMC; as outras são stubs de expansão.
 * =========================================================================*/
typedef enum {
    HPHL_POOL_MANUAL = 0,  /* arena/pool, sem GC tracing */
    HPHL_POOL_AMC    = 1,  /* tracing GC, moving/compacting */
    HPHL_POOL_EPVM   = 2   /* exact-life manual pool (não-collected) */
} HphlPoolClass;
static HphlPoolClass class_for_policy(int policy) {
    /* Mapeia o legacy storage policy do codegen para a classe MPS-like. */
    /* 0=Stack, 1=Heap, 2=Arena, 3=Pool, 4=Shared — valores do StoragePolicy
     * em codegen_main.cpp. Como não temos header de policy aqui, tratamos:
     * - Arena/Pool => MANUAL (não GC)
     * - Shared/Heap => AMC (GC) */
    if (policy == 2 || policy == 3) return HPHL_POOL_MANUAL;
    return HPHL_POOL_AMC;
}

/* Mantemos um alias gc_registered/gc_count/gc_cap para o M17 (sweep atual
 * varre tudo). O array "unificado" é construído on-the-fly varrendo ambos
 * os arrays; mas como hphl_gc_sweep faz linear search (O(n²) já), o custo
 * de varrer 2x é aceitável. Em M18.6 o sweep vira dois loops diretos. */
static inline size_t gc_count_all(void) { return gc_count_young + gc_count_old; }

/* M18.6: GC stats/Debug (BACKLOG 1.5) */
static long long gc_collections = 0;
static long long gc_total_freed = 0;
static long long gc_max_pause_ns = 0;
static long long gc_last_freed = 0;

static inline long long hphl_gc_now_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (c.QuadPart * 1000000000LL) / f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
}
long long hphl_gc_stats_collections(void) { return gc_collections; }
long long hphl_gc_stats_freed(void) { return gc_total_freed; }
long long hphl_gc_stats_max_pause(void) { return gc_max_pause_ns; }
long long hphl_gc_stats_last_freed(void) { return gc_last_freed; }

/* M17 Fase 1: root set para tracing GC (pilha/globais) */
#define GC_ROOT_INIT 64
static void** gc_roots = NULL; // array de ponteiros para slots (void**)
static size_t gc_root_cap = 0;
static size_t gc_root_count = 0;

void hphl_gc_add_root(void* slot) {
  if (!slot) return;
  if (!gc_roots) {
    gc_roots = (void**)malloc(GC_ROOT_INIT * sizeof(void*));
    gc_root_cap = GC_ROOT_INIT;
  }
  if (gc_root_count >= gc_root_cap) {
    size_t nc = gc_root_cap * 2;
    void** nb = (void**)realloc(gc_roots, nc * sizeof(void*));
    if (!nb) { hphl_panic("gc_add_root: sem memoria"); return; }
    gc_roots = nb;
    gc_root_cap = nc;
  }
  gc_roots[gc_root_count++] = slot;
}
/* M29 FIX: batch API — registra N slots de uma vez. Evita o custo de
 * subq $32 + call + addq $32 por root (64 bytes de stack por root, que
 * estourava a pilha em binary-trees). O codegen agora gera:
 *   leaq slots(%rip), %rcx
 *   movq N, %rdx
 *   call hphl_gc_add_roots_batch
 * em vez de N chamadas individuais.
 * Internamente aloca/reserva o array roots de uma vez.
 * M31: esta API serve EXCLUSIVAMENTE aos roots permanentes de globals
 * string (emitGlobalStringRoots). Eles vivem na lista `gc_roots_perm`,
 * separada dos roots transientes de frame: (a) o guard `roots>16` de
 * gc_maybe_collect_on_alloc conta só transientes (2260 roots permanentes
 * desabilitavam TODAS as coletas automáticas!); (b) pops de frame nunca
 * os tocam. O mark os visita junto com os transientes. */
static void** gc_roots_perm = NULL;
static size_t gc_perm_cap = 0;
static size_t gc_perm_count = 0;
void hphl_gc_add_roots_batch(void* slots[], int n) {
    if (!slots || n <= 0) return;
    if (!gc_roots_perm) {
        gc_roots_perm = (void**)malloc(GC_ROOT_INIT * sizeof(void*));
        gc_perm_cap = GC_ROOT_INIT;
    }
    if (gc_perm_count + n > gc_perm_cap) {
        size_t nc = gc_perm_cap;
        while (nc < gc_perm_count + n) nc *= 2;
        void** nb = (void**)realloc(gc_roots_perm, nc * sizeof(void*));
        if (!nb) { hphl_panic("gc_add_roots_batch: sem memoria"); return; }
        gc_roots_perm = nb;
        gc_perm_cap = nc;
    }
    for (int i = 0; i < n; i++) {
        if (slots[i]) gc_roots_perm[gc_perm_count++] = slots[i];
    }
}
void hphl_gc_remove_root(void* slot) {
  if (!slot || !gc_roots) return;
  for (size_t i=0;i<gc_root_count;i++) if (gc_roots[i]==slot) { gc_roots[i]=gc_roots[--gc_root_count]; break; }
}
/* M29 FIX: batch remove (lista permanente — par de add_roots_batch). */
void hphl_gc_remove_roots_batch(void* slots[], int n) {
    if (!slots || n <= 0 || !gc_roots_perm) return;
    for (int i = 0; i < n; i++) {
        void* s = slots[i];
        if (!s) continue;
        for (size_t j = 0; j < gc_perm_count; j++) {
            if (gc_roots_perm[j] == s) {
                gc_roots_perm[j] = gc_roots_perm[--gc_perm_count];
                break;
            }
        }
    }
}
/* Semeia um candidato root (slot address ou ponteiro direto). */
static inline void gc_seed_root(void* rv) {
    if (!rv) return;
    void* cand = rv;
    if (!gc_is_registered(cand)) {
        cand = *(void**)rv;
    }
    if (cand && gc_is_registered(cand)) {
        grey_push(cand);
    }
}
/* M29 FIX: registra N slots a partir de uma tabela de offsets em bytes
 * relativa a `base` (frame pointer). O codegen emite:
  *   leaq .LgcOffs_NN(%rip), %rcx   // tabela de offsets (i64)
  *   movq %rbp, %rdx               // base
  *   movq $K, %r8                  // count
  *   call hphl_gc_register_slots
  *
  * Evita N chamadas individuais (cada uma custa 64 bytes de stack por
  * shadow+align Win64). Causa real do stack overflow em binary-trees. */
void hphl_gc_register_slots(const int64_t* offs, void* base, int n) {
    if (!offs || !base || n <= 0) return;
    if (!gc_roots) {
        gc_roots = (void**)malloc(GC_ROOT_INIT * sizeof(void*));
        gc_root_cap = GC_ROOT_INIT;
    }
    if (gc_root_count + n > gc_root_cap) {
        size_t nc = gc_root_cap;
        while (nc < gc_root_count + n) nc *= 2;
        void** nb = (void**)realloc(gc_roots, nc * sizeof(void*));
        if (!nb) { hphl_panic("gc_register_slots: sem memoria"); return; }
        gc_roots = nb;
        gc_root_cap = nc;
    }
    char* b = (char*)base;
    for (int i = 0; i < n; i++) {
        void** slot = (void**)(b + offs[i]);
        gc_roots[gc_root_count++] = slot;
    }
}
/* M29 FIX: versão batch remove para slots por offset. */
void hphl_gc_unregister_slots(const int64_t* offs, void* base, int n) {
    if (!offs || !base || n <= 0 || !gc_roots) return;
    char* b = (char*)base;
    for (int i = 0; i < n; i++) {
        void** slot = (void**)(b + offs[i]);
        for (size_t j = 0; j < gc_root_count; j++) {
            if (gc_roots[j] == slot) {
                gc_roots[j] = gc_roots[--gc_root_count];
                break;
            }
        }
    }
}

void hphl_gc_pop_roots(int n) {
    if (n <= 0 || !gc_roots) return;
    if ((size_t)n <= gc_root_count) gc_root_count -= (size_t)n;
    else gc_root_count = 0;
}

/* forward decl para atexit */
void hphl_gc_cleanup(void);
void hphl_gc_cleanup_rem(void);

void hphl_gc_init(void) {
  static int gc_atexit_registered = 0;
  if (!gc_atexit_registered) {
    atexit(hphl_gc_cleanup);
    atexit(hphl_gc_cleanup_rem);
    gc_atexit_registered = 1;
  }
  if (!gc_registered_young) {
    gc_registered_young = (void**)malloc(GC_INIT_CAP * sizeof(void*));
    if (!gc_registered_young) hphl_panic("gc: sem memória para tabela");
    memset(gc_registered_young, 0, GC_INIT_CAP * sizeof(void*));
  }
  if (!gc_young_age) {
    gc_young_age = (unsigned char*)malloc(GC_INIT_CAP * sizeof(unsigned char));
    if (!gc_young_age) hphl_panic("gc: sem memória para idade");
    memset(gc_young_age, 0, GC_INIT_CAP);
    gc_young_age_cap = GC_INIT_CAP;
  }
  if (!gc_registered_old) {
    gc_registered_old = (void**)malloc(GC_INIT_CAP * sizeof(void*));
    if (!gc_registered_old) hphl_panic("gc: sem memória para tabela");
    memset(gc_registered_old, 0, GC_INIT_CAP * sizeof(void*));
  }
  if (!gc_map) gc_map_init();
  if (!gc_set) gc_set_init();
}

void hphl_gc_cleanup(void) {
  if (gc_registered_young) free(gc_registered_young);
  if (gc_young_age) free(gc_young_age);
  if (gc_registered_old) free(gc_registered_old);
  if (grey_list) free(grey_list);
  if (gc_map) free(gc_map);
  if (gc_set) free(gc_set);
  hphl_arena_free_all(gc_old_arena);
  gc_registered_young = NULL;
  gc_young_age = NULL;
  gc_young_age_cap = GC_INIT_CAP;
  gc_registered_old = NULL;
  gc_map = NULL; gc_map_cap=0; gc_map_len=0;
  gc_set = NULL; gc_set_cap=0; gc_set_len=0;
  grey_list = NULL; grey_cap = GREY_INIT; grey_count = 0;
  gc_old_arena = NULL;
  gc_cap_young = GC_INIT_CAP;
  gc_count_young = 0;
  gc_cap_old = GC_INIT_CAP;
  gc_count_old = 0;
  /* remembered/roots são liberados em hphl_gc_cleanup_full() abaixo
   * (definido após gc_remembered), chamado via segundo atexit. */
}

/* Contador de minors para decidir quando ir ao major. */
static int gc_minor_count_since_major = 0;
#define GC_MINOR_THRESHOLD 10

long long hphl_gc_minor(void);
long long hphl_gc_major(void);

/* M30: dispara minor GC durante alocação quando young cresce demais.
 * Evita acumular milhões de objetos mortos até o epílogo de Main.
 * Override: HPHL_GC_ALLOC_THRESHOLD=<N> (default 32768 objetos young). */
#define GC_ALLOC_YOUNG_DEFAULT 32768  /* M_RV1 E11: default 32K young objects (was 0 = disabled) */
static size_t gc_alloc_young_threshold = 0;
static int gc_in_collection = 0;

static size_t gc_alloc_threshold(void) {
    if (gc_alloc_young_threshold) return gc_alloc_young_threshold;
    const char* env = getenv("HPHL_GC_ALLOC_THRESHOLD");
    if (env && *env) {
        long v = atol(env);
        gc_alloc_young_threshold = (v > 0) ? (size_t)v : 0;
    } else {
        gc_alloc_young_threshold = GC_ALLOC_YOUNG_DEFAULT;
    }
    return gc_alloc_young_threshold;
}

static void gc_maybe_collect_on_alloc(void) {
    size_t thr = gc_alloc_threshold();
    if (thr == 0) return;
    if (gc_in_collection) return;
    if (gc_count_young < thr) return;
    /* M30: adia GC em recursão profunda — muitos frames com roots ativos
     * aumentam risco de sweep prematuro; coleta quando pilha de roots rasa. */
    if (gc_root_count > 16) return;
    gc_in_collection = 1;
    hphl_gc_minor();
    gc_in_collection = 0;
}

void hphl_gc_register(void* p) {
  if (!p) return;
  if (!gc_registered_young) hphl_gc_init();
  /* M30: coleta ANTES de registrar o novo objeto — evita sweep do bloco
   * que acabou de ser malloc'd mas ainda não tem ctor/roots. */
  gc_maybe_collect_on_alloc();
  if (gc_count_young >= gc_cap_young) {
    size_t new_cap = gc_cap_young * 2;
    void** nb = (void**)realloc(gc_registered_young, new_cap * sizeof(void*));
    if (!nb) { hphl_panic("gc_register: sem memoria (young tabela)"); return; }
    gc_registered_young = nb;
    gc_cap_young = new_cap;
    unsigned char* ab = (unsigned char*)realloc(gc_young_age, new_cap * sizeof(unsigned char));
    if (!ab) { hphl_panic("gc_register: sem memoria (young age)"); return; }
    gc_young_age = ab;
    gc_young_age_cap = new_cap;
  }
  size_t idx = gc_count_young;
  gc_young_age[idx] = 0;
  gc_registered_young[idx] = p;
  gc_count_young++;
  gc_map_add(p, idx, 0);
}

/* M18.2: usado em M18.5 para mover obj jovem promovido para a tabela old.
 * Já no M18.2 a promoção continua sendo in-place (atualiza flag gen) — esta
 * função só é chamada a partir de M18.5. */
void hphl_gc_register_old(void* p) {
  if (!p) return;
  if (!gc_registered_old) hphl_gc_init();
  if (gc_count_old >= gc_cap_old) {
    size_t new_cap = gc_cap_old * 2;
    void** nb = (void**)realloc(gc_registered_old, new_cap * sizeof(void*));
    if (!nb) { hphl_panic("gc_register_old: sem memoria"); return; }
    gc_registered_old = nb;
    gc_cap_old = new_cap;
  }
  size_t idx = gc_count_old;
  gc_registered_old[idx] = p;
  gc_count_old++;
  gc_map_add(p, idx, 1);
}

void hphl_gc_unregister(void* p) {
  if (!p) return;
  size_t idx; uint8_t gen;
  if (gc_map_get(p, &idx, &gen)) {
    gc_map_remove(p);
    if (gen==0) {
      if (idx >= gc_count_young) return;
      size_t last = --gc_count_young;
      if (idx < gc_count_young) {
        void* moved = gc_registered_young[last];
        gc_registered_young[idx] = moved;
        gc_young_age[idx] = gc_young_age[last];
        gc_map_update_idx(moved, idx);
      }
    } else {
      if (idx >= gc_count_old) return;
      size_t last = --gc_count_old;
      if (idx < gc_count_old) {
        void* moved = gc_registered_old[last];
        gc_registered_old[idx] = moved;
        gc_map_update_idx(moved, idx);
      }
    }
    return;
  }
  /* fallback linear (desync) */
  gc_map_remove(p);
  for (size_t i = 0; i < gc_count_young; i++) {
    if (gc_registered_young[i] == p) {
      gc_registered_young[i] = gc_registered_young[--gc_count_young];
      gc_young_age[i] = gc_young_age[gc_count_young];
      return;
    }
  }
  for (size_t i = 0; i < gc_count_old; i++) {
    if (gc_registered_old[i] == p) {
      gc_registered_old[i] = gc_registered_old[--gc_count_old];
      return;
    }
  }
}

/* M18.1: header layout de objetos shared (16 bytes antes do payload)
 *
 *   hdr[0] = refcount (u64)
 *   hdr[1] = size[31:0] | gen[32] | mark[33] | forward[34] | pinned[35] | card_dirty[36]
 *
 *   - size       : tamanho do payload em bytes (sem o header)
 *   - gen        : 0 = young, 1 = old
 *   - mark       : usado em mark-sweep (hphl_gc_sweep, transient; cleared a cada coleta)
 *   - forward    : M18.5 - quando setado, hdr[1] ainda carrega size, mas o ponteiro
 *                  "real" do objeto (após promoção) é o valor armazenado nos bits
 *                  [31:0] da MESMA word apenas como stub. Implementação completa em M18.5
 *                  usa um slot extra (ver hphl_hp_forward).
 *   - pinned     : M18.5 - objeto não deve ser movido (ex: referenciado por C-FFI)
 *   - card_dirty : M18.3 - bit coarse: o old_obj referenciou algum young no último write
 */
/* Magic no bit 63 do hdr[1] e demais flags de layout — definidos
 * anteriormente (perto de isPlausibleGC) para que hasGCHdr() e
 * gc_scan_obj() possam usá-los. */

static inline unsigned long long hphl_hdr_size(unsigned long long* hdr) { return hdr[1] & HPHL_HDR_SIZE_MASK; }
static inline int                hphl_hdr_gen(unsigned long long* hdr)   { return (int)((hdr[1] & HPHL_HDR_GEN_BIT) != 0); }
static inline int                hphl_hdr_mark(unsigned long long* hdr)  { return (int)((hdr[1] & HPHL_HDR_MARK_BIT) != 0); }
static inline void               hphl_hdr_set_mark(unsigned long long* hdr, int v) {
    if (v) hdr[1] |= HPHL_HDR_MARK_BIT; else hdr[1] &= ~HPHL_HDR_MARK_BIT;
}
static inline void               hphl_hdr_set_gen(unsigned long long* hdr, int old) {
    if (old) hdr[1] |= HPHL_HDR_GEN_BIT; else hdr[1] &= ~HPHL_HDR_GEN_BIT;
}
static inline int               hphl_hdr_class_id(unsigned long long* hdr) {
    return (int)((hdr[1] & HPHL_HDR_CLSID_MASK) >> HPHL_HDR_CLSID_SHIFT);
}
static inline void              hphl_hdr_set_class_id(unsigned long long* hdr, int cid) {
    hdr[1] = (hdr[1] & ~HPHL_HDR_CLSID_MASK) | (((unsigned long long)cid << HPHL_HDR_CLSID_SHIFT) & HPHL_HDR_CLSID_MASK);
}
static inline void               hphl_hdr_clear_mark_all(void) {
    /* usado em hphl_gc_sweep início */
}

/* M18.3: card table para write barrier
 *
 * Write barrier clássica: chamada pelo codegen em StoreVar/genNew quando
 * o owner é old. API exposta via hphl_write_barrier(old_obj, field_addr).
 *
 * Barreira conservadora: além da API explícita, o mark phase do
 * hphl_gc_sweep também executa a barreira ao varrer campos de old_objs:
 * se encontra uma ref young → marca o card (cobre o caso onde o
 * codegen esqueceu de emitir a barreira ou quando o obj foi promovido
 * entre dois GCs). É uma "snapshot-at-the-beginning" simplificada —
 * correta mas pode ter falsos positivos (cards sujos sem necessidade).
 *
 * Tabela: indexada por (uintptr_t(old_obj) / GC_CARD_SIZE) & (GC_CARDS-1).
 *   - 16k cards × 512 B = 8 MB de espaço de endereçamento coberto.
 *   - Colisões = dois old_objs no mesmo card = um falso positivo
 *     (o remembered set fica com um obj a mais; coletor varre-o e ele
 *     não tem ref young → ignora; sem correção).
 */
#define GC_CARD_SIZE  512
#define GC_CARDS      (16 * 1024)        /* 16k cards = 8 MB hash range */
#define GC_CARDS_MASK (GC_CARDS - 1)
static unsigned char gc_cards[GC_CARDS];  /* 0 = clean, 1 = dirty */

/* Marca o card que contém o OLD_OBJ que contém o field em `field_addr`.
 *
 * Adaptação da write barrier clássica do generational GC (BDWGC, HotSpot,
 * MMTk): sempre que o codegen stores em um field de um objeto old, o card
 * do objeto deve ser dirty para que o minor GC possa reenumerar o old.
 *
 * IMPORTANTE: o codegen passa `(field_addr, field_addr)` historicamente
 * (bug M29), portanto esta função aceita o ENDEREÇO DO FIELD como primeiro
 * arg (vamos chamar de `obj_or_field`). Quando o caller sabe o old_obj,
 * passa como segundo arg (que era o contrato original); quando não sabe,
 * passa o field_addr nos dois — esta função detecta o caso pelo alignment.
 *
 * Como o payload do objeto tem 16 bytes de header antes dele, o field_addr
 * está em `obj_payload + offset` onde offset ∈ {8, 16, 24, ...}. O objeto
 * (header) está 16 bytes antes do payload. Para descobrir o obj a partir do
 * field_addr, alinhar para baixo até o início de payload. Como objetos têm
 * tamanho fixo por classe, e o codegen emite o offset do field relativo ao
 * início do payload, podemos fazer: header = alinhar_field_addr_para_baixo
 * por 8 (já que payload começa em offset 16 do header).
 *
 * Para simplicidade e segurança, esta versão aceita `field_addr` no
 * primeiro arg e usa-o DIRETAMENTE para indexar o card. Isso é conservador:
 * pode causar falso positivo (card sujo desnecessário), mas nunca causa
 * corrupção. */
/* Limpa todos os cards. Chamado em hphl_gc_sweep início (M18.4)
 * e ao final de cada minor collection. */
void hphl_gc_cards_clear(void) {
    memset(gc_cards, 0, sizeof gc_cards);
}

/* M18.4: remembered set — old_objs com cards dirty.
 * Varrido a cada minor GC; objetos aqui são raízes adicionais
 * para o mark do minor (junto com gc_roots). */
#define GC_REM_INIT 64
static size_t gc_rem_cap = GC_REM_INIT;
static size_t gc_rem_count = 0;
static void** gc_remembered = NULL;

static void hphl_gc_rem_add(void* p) {
    if (!p) return;
    unsigned long long* hdr = (unsigned long long*)p - 2;
    if (hdr[1] & HPHL_HDR_CARD_BIT) return;
    hdr[1] |= HPHL_HDR_CARD_BIT;
    if (!gc_remembered) {
        gc_remembered = (void**)malloc(GC_REM_INIT * sizeof(void*));
        gc_rem_cap = GC_REM_INIT;
    }
    if (gc_rem_count >= gc_rem_cap) {
        size_t nc = gc_rem_cap * 2;
        void** nb = (void**)realloc(gc_remembered, nc * sizeof(void*));
        if (!nb) { hphl_panic("gc_rem_add: sem memoria"); return; }
        gc_remembered = nb;
        gc_rem_cap = nc;
    }
    gc_remembered[gc_rem_count++] = p;
}

/* Varre gc_cards[] em O(old) e converte cards sujos em remembered set.
 * Limpa os cards após varrer. */
void hphl_gc_cards_scan(void) {
    int has_dirty = 0;
    for (size_t c=0;c<GC_CARDS;c++) if (gc_cards[c]) { has_dirty=1; break; }
    if (!has_dirty) return;
    for (size_t i=0;i<gc_count_old;i++) {
        void* p = gc_registered_old[i];
        if (!p) continue;
        uintptr_t addr = (uintptr_t)p;
        size_t idx = (addr / GC_CARD_SIZE) & GC_CARDS_MASK;
        if (!gc_cards[idx]) continue;
        hphl_gc_rem_add(p);
    }
    memset(gc_cards, 0, sizeof gc_cards);
}
void hphl_write_barrier(void* old_obj, void* field_addr) {
    void* obj = old_obj ? old_obj : field_addr;
    if (!obj) return;
    void* base = (old_obj && old_obj != field_addr) ? old_obj : obj;
    if (!gc_set_contains(base)) {
        void* target = field_addr ? field_addr : old_obj;
        if (!target) return;
        uintptr_t addr = (uintptr_t)target;
        size_t idx = (addr / GC_CARD_SIZE) & GC_CARDS_MASK;
        gc_cards[idx] = 1;
        return;
    }
    unsigned long long* hdr = (unsigned long long*)base - 2;
    if ((hdr[1] & HPHL_HDR_GEN_BIT)==0) return;
    if (hdr[1] & HPHL_HDR_CARD_BIT) return;
    hdr[1] |= HPHL_HDR_CARD_BIT;
    if (!gc_remembered) { gc_remembered = (void**)malloc(GC_REM_INIT*sizeof(void*)); gc_rem_cap=GC_REM_INIT; }
    if (gc_rem_count >= gc_rem_cap) {
        size_t nc = gc_rem_cap ? gc_rem_cap*2 : 64;
        void** nb = (void**)realloc(gc_remembered, nc*sizeof(void*));
        if (!nb) { hphl_panic("remembered set: sem memoria"); return; }
        gc_remembered=nb; gc_rem_cap=nc;
    }
    gc_remembered[gc_rem_count++]=base;
}
void hphl_write_barrier_slow(void* old_obj, void* field_addr) {
    (void)field_addr;
    if (!old_obj) return;
    unsigned long long* hdr = (unsigned long long*)old_obj - 2;
    if (hdr[1] & HPHL_HDR_CARD_BIT) return;
    hdr[1] |= HPHL_HDR_CARD_BIT;
    if (!gc_remembered) { gc_remembered = (void**)malloc(GC_REM_INIT*sizeof(void*)); gc_rem_cap=GC_REM_INIT; }
    if (gc_rem_count >= gc_rem_cap) {
        size_t nc = gc_rem_cap ? gc_rem_cap*2 : 64;
        void** nb = (void**)realloc(gc_remembered, nc*sizeof(void*));
        if (!nb) { hphl_panic("remembered set: sem memoria"); return; }
        gc_remembered=nb; gc_rem_cap=nc;
    }
    gc_remembered[gc_rem_count++]=old_obj;
}

/* M18.6: cleanup de remembered/roots no exit (BACKLOG 1.3).
 * Registrado via segundo atexit em hphl_gc_init após definição. */
void hphl_gc_cleanup_rem(void) {
    if (gc_remembered) { free(gc_remembered); gc_remembered = NULL; }
    if (gc_roots) { free(gc_roots); gc_roots = NULL; }
    if (gc_roots_perm) { free(gc_roots_perm); gc_roots_perm = NULL; }
    gc_rem_cap = GC_REM_INIT;
    gc_rem_count = 0;
    gc_root_cap = 0;
    gc_root_count = 0;
}
/* M18.4: minor GC — varre só young + remembered set.
 * Objetos young mortos são liberados. Young vivos são marcados
 * mas NÃO promovidos aqui (promoção real em M18.5).
 * O remembered set é consumido e limpo. */
long long hphl_gc_minor(void) {
    long long t0 = hphl_gc_now_ns();
    int dotrace = getenv("HPHL_GC_TRACE") ? 1 : 0;
    if (dotrace) fprintf(stderr, "[gc] minor start young=%zu old=%zu roots=%zu(+%zu perm) rem=%zu map=%zu\n", gc_count_young, gc_count_old, gc_root_count, gc_perm_count, gc_rem_count, gc_map_len);
    gc_map_rebuild_if_needed();
    hphl_gc_cards_scan();
    if (dotrace) fprintf(stderr, "[gc] minor cards ok\n");
    hphl_gc_cards_scan();
    grey_clear();
    // limpar marks nos young
    for (size_t i = 0; i < gc_count_young; i++) {
        void* p = gc_registered_young[i];
        if (!p) continue;
        hphl_hdr_set_mark((unsigned long long*)p - 2, 0);
    }
    // tri-color: grey roots e remembered young (transientes + permanentes)
    if (gc_root_count > 0 && gc_roots) {
        for (size_t r = 0; r < gc_root_count; r++) {
            gc_seed_root(gc_roots[r]);
        }
    }
    if (gc_perm_count > 0 && gc_roots_perm) {
        for (size_t r = 0; r < gc_perm_count; r++) {
            gc_seed_root(gc_roots_perm[r]);
        }
    }
    for (size_t i = 0; i < gc_rem_count; i++) {
        void* p = gc_remembered[i];
        if (!p) continue;
        gc_scan_obj(p, visit_grey, NULL);
        unsigned long long* hdr = (unsigned long long*)p - 2;
        hdr[1] &= ~HPHL_HDR_CARD_BIT;
    }
    gc_rem_count = 0;
    if (dotrace) fprintf(stderr, "[gc] minor rem ok\n");
    /* M30: mark completo — grey list é iterativa (sem recursão na pilha). */
    while (grey_count > 0) {
        if (hphl_gc_step(4096)) break;
    }
    if (dotrace) fprintf(stderr, "[gc] minor mark ok t=%lld\n", (long long)(hphl_gc_now_ns() - t0) / 1000);
    if (dotrace) {
        size_t bad = 0;
        for (size_t vi = 0; vi < gc_count_young; vi++) {
            void* vp = gc_registered_young[vi];
            if (!vp) continue;
            unsigned long long* vh = (unsigned long long*)vp - 2;
            if ((vh[1] & HPHL_HDR_MAGIC_BIT) == 0) {
                if (bad < 5) fprintf(stderr, "[gc] BAD young[%zu]=%p hdr=%llx\n", vi, vp, vh[1]);
                bad++;
            }
        }
        if (bad) fprintf(stderr, "[gc] BAD total=%zu\n", bad);
    }
    // sweep young + promoção por idade (M18.5)
    long long freed = 0;
    for (size_t i = 0; i < gc_count_young; ) {
        void* p = gc_registered_young[i];
        if (!p) {
            size_t last_y0 = --gc_count_young;
            if (i < gc_count_young) {
                void* moved0 = gc_registered_young[last_y0];
                gc_registered_young[i] = moved0;
                gc_young_age[i] = gc_young_age[last_y0];
                gc_map_update_idx(moved0, i);
            }
            continue;
        }
    unsigned long long* hdr = (unsigned long long*)p - 2;
    int isRootReachable = gc_obj_alive_after_mark(hdr, (gc_root_count + gc_perm_count) > 0);
    if (!isRootReachable) {
      unsigned long long* deadHdr = (unsigned long long*)p - 2;
      size_t deadSz = (size_t)(deadHdr[1] & HPHL_HDR_SIZE_MASK);
      gc_map_remove(p);
      if (!gc_nofree_env()) hphl_tc_free(p, deadSz);
      freed++;
            size_t last_y = --gc_count_young;
            if (i < gc_count_young) {
                void* moved = gc_registered_young[last_y];
                gc_registered_young[i] = moved;
                gc_young_age[i] = gc_young_age[last_y];
                gc_map_update_idx(moved, i);
            }
        } else {
            /* sobreviveu a este minor → incrementa idade */
            gc_young_age[i]++;
            if (gc_young_age[i] >= gc_promo_age()) {
                hphl_hdr_set_gen(hdr, 1);
                hphl_gc_register_old(p);
                /* Ao promover, o objeto vira old e passa a NÃO ser varrido nos
                 * minors seguintes. Se ele aponta para young, esse old→young
                 * precisa entrar no remembered set (a write barrier não foi
                 * emitida na época do store, pois o owner ainda era young). */
                hphl_gc_rem_add(p);
                size_t last_y2 = --gc_count_young;
                if (i < gc_count_young) {
                    void* moved2 = gc_registered_young[last_y2];
                    gc_registered_young[i] = moved2;
                    gc_young_age[i] = gc_young_age[last_y2];
                    gc_map_update_idx(moved2, i);
                }
                continue;
            } else {
                i++;
            }
        }
    }
    gc_collections++;
    gc_total_freed += freed;
    gc_last_freed = freed;
    long long dt = hphl_gc_now_ns() - t0;
    if (dt > gc_max_pause_ns) gc_max_pause_ns = dt;
    if (dotrace) fprintf(stderr, "[gc] minor end freed=%lld young=%zu old=%zu dt_us=%lld\n", freed, gc_count_young, gc_count_old, dt / 1000);
    return freed;
}

/* Forward: sweep é definido mais abaixo (precisa ver gc_count_young/old). */
long long hphl_gc_sweep(void);

/* Callback para hphl_gc_major: encontra pointers young dentro de objetos
 * old e adiciona os jovens como roots adicionais. Isso implementa uma
 * "snapshot-at-the-beginning" conservadora — cobre o caso onde o codegen
 * esqueceu de emitir hphl_write_barrier(). É O(n_old * slots), aceitável
 * no major que já varre tudo. */
static int visit_maybe_young(void* cand, void* ud) {
    (void)ud;
    if (!cand) return 0;
    if (!hasGCHdr(cand)) return 0;
    /* verifica se é young (gen bit = 0) */
    unsigned long long* hdr = (unsigned long long*)cand - 2;
    if (hphl_hdr_gen(hdr)) return 0;
    if (hphl_hdr_mark(hdr)) return 0;
    grey_push(cand);
    return 0;
}
static void gc_major_readaquire_barrier(void) {
    for (size_t i = 0; i < gc_count_old; i++) {
        void* p = gc_registered_old[i];
        if (!p) continue;
        unsigned long long* hdr = (unsigned long long*)p - 2;
        /* só paga custo para old_objs com card_dirty OU sem cobertura
         * pelo remembered set — heurística simples: varre todos os old. */
        (void)hdr;
        gc_scan_obj(p, visit_maybe_young, NULL);
    }
}

/* M18.4: major GC — full mark-sweep (young + old).
 * Substitui o hphl_gc_sweep antigo quando minor_count >= threshold. */
long long hphl_gc_major(void) {
    gc_rem_count = 0;  /* consome remembered set */
    hphl_gc_cards_clear();
    long long freed = hphl_gc_sweep();  /* reusa o sweep atual que varre young+old */
#ifdef HPHL_GC_DEBUG
    gc_major_readaquire_barrier();
#endif
    return freed;
}

/* M20 1.4: __builtin_gc_pressure() — força GC imediato (major) independente
 * do threshold. Usado em programas com muita alocação entre dois `Main` calls.
 * Também disparado se HPHL_GC_PRESSURE estiver setado. */
static int gc_pressure_env = -1; /* lazy */
static int gc_pressure_check(void) {
    if (gc_pressure_env < 0) gc_pressure_env = getenv("HPHL_GC_PRESSURE") ? 1 : 0;
    return gc_pressure_env;
}

/* FIX gc_pressure: antes do major, sincroniza os cards (barrier snapshot) e,
 * opcionalmente, faz conservative stack scan se o env estiver setado. O
 * sync de cards garante que o mark phase do major veja TODAS as dirty refs
 * old→young — sem isso, promotions silenciosas pós-minor poderiam perder
 * roots. Antes o major fazia `gc_rem_count = 0` e `hphl_gc_cards_clear()`,
 * o que APAGAVA dirty cards antes do mark. Reordenado. */
void hphl_gc_pressure(void) {
    if (getenv("HPHL_GC_DEBUG")) {
        fprintf(stderr, "[gc_pressure] roots=%zu young=%zu old=%zu\n",
                gc_root_count, gc_count_young, gc_count_old);
    }
    /* 1. Sincroniza dirty cards AGORA (consolida remembered set antes do mark).
     * Se houvesse write barriers pendentes (codegen esqueceu de chamar), elas
     * estão nos cards; precisamos varrê-los para remembered. */
    hphl_gc_cards_scan();
    /* 2. Conservative scan opcional (BDWGC-style safety net) — só com env
     * var setada, para não pagar custo em produção. */
    if (gc_conservative_stack_check()) {
        /* Adaptação do GC_push_all_stack do BDWGC: usamos um "stack probe"
         * declarando um alloca + endereço local — sabemos o range aproximado
         * entre stack_low e stack_high. Sem threads-info no runtime,
         * capturamos via __builtin_frame_address(0) como aproximação. */
        void* frame = __builtin_frame_address(0);
        if (frame) {
            /* range arbitrário: 1 MiB acima do frame atual (cobre a pilha
             * de chamadas em qualquer profundidade razoável), limitado pelo topo da stack. */
            void* hi = (char*)frame + (1 << 20);
            void* base = hphl_get_stack_base();
            if (base && hi > base) hi = base;
            if (hi > frame) {
                gc_push_stack_words((void**)frame, (void**)hi, visit_grey, NULL);
            }
        }
    }
    /* 3. Major mark-sweep */
    long long freed = hphl_gc_major();
    if (getenv("HPHL_GC_DEBUG")) {
        fprintf(stderr, "[gc_pressure] major freed=%lld young=%zu old=%zu\n",
                freed, gc_count_young, gc_count_old);
    }
    gc_minor_count_since_major = 0;
}

/* M18.4: entry point builtin. Substitui o hphl_gc antigo (que só chamava
 * hphl_gc_sweep). A cada GC_MINOR_THRESHOLD minors, executa um major. */
void hphl_gc(void) {
    if (gc_pressure_check()) {
        hphl_gc_pressure();
        return;
    }
    gc_minor_count_since_major++;
    if (gc_minor_count_since_major >= GC_MINOR_THRESHOLD) {
        hphl_gc_major();
        gc_minor_count_since_major = 0;
    } else {
        hphl_gc_minor();
    }
}

/* Fase 1 M14 - GC cooperativo: alocacao shared com registro */
void* hphl_shared_alloc(size_t n) {
    /* M29 rpmalloc-inspired: tenta reusar bloco do thread cache (somente
     * sizes <= 2048). Bloco recuperado já vem zerado (tc_alloc faz memset
     * no rpmalloc original; aqui o memset já é feito em hphl_shared_alloc
     * ANTES de retornar, então pulamos). Aqui mantemos o memset porque
     * o cache pode conter bloco de uso anterior com lixo. */
    void* reused = NULL;
    if (n <= HPHL_TC_BIN_MAX_SZ) {
        reused = hphl_tc_alloc(n);
        if (reused) {
            unsigned long long* hdr = (unsigned long long*)reused - 2;
            /* Bloco de cache: hdr[1] já tem size + magic (cache não altera
             * header). Verificamos o magic antes de aceitar. */
            if ((hdr[1] & HPHL_HDR_MAGIC_BIT) && (hdr[1] & HPHL_HDR_SIZE_MASK) >= n) {
                hdr[0] = 1;
                memset(reused, 0, n);
                hphl_gc_register(reused);
                return reused;
            }
            /* magic ausente (provavelmente alocado via malloc bruto) —
             * descartamos e alocamos fresco abaixo. */
            free((char*)reused - 16);
        }
    }
    unsigned long long* hdr = (unsigned long long*)malloc(n + 16);
    if (!hdr) { hphl_panic("shared: sem memoria"); return 0; }
    memset((void*)(hdr + 2), 0, n); /* objetos nascem zerados */
    hdr[0] = 1; /* refcount inicial */
    /* M18.1: size | gen=0 (young) | mark=0 | forward=0 | pinned=0 | card=0 | magic */
    hdr[1] = (n & HPHL_HDR_SIZE_MASK) | HPHL_HDR_MAGIC_BIT;
    hphl_gc_register((void*)(hdr + 2));
    return (void*)(hdr + 2);
}
/* M27: typed shared allocation — stores classId in hdr[1] upper bits.
 * The classId indexes the class_desc_table, whose bitmap tells the GC
 * which slots are pointers (vs scalar fields). A bitmap is NOT embedded
 * in the object layout: it lives only inside the static class_desc_table.
 * Layout: [hdr0|refcount][hdr1|size+flags+classId][payload n bytes]. */
void* hphl_shared_alloc_typed(size_t n, int classId) {
    (void)get_class_desc(classId);
    /* M29 FIX: thread cache hit evita o malloc do runtime C — economiza
     * tempo em workloads alocação-intensivos (binary-trees). */
    if (n <= HPHL_TC_BIN_MAX_SZ) {
        void* reused = hphl_tc_alloc(n);
        if (reused) {
            unsigned long long* hdr = (unsigned long long*)reused - 2;
            if ((hdr[1] & HPHL_HDR_MAGIC_BIT) && (hdr[1] & HPHL_HDR_SIZE_MASK) >= n) {
                hdr[0] = 1;
                memset(reused, 0, n);
                hphl_hdr_set_class_id(hdr, classId);
                hphl_gc_register(reused);
                return reused;
            }
            free((char*)reused - 16);
        }
    }
    unsigned long long* hdr = (unsigned long long*)malloc(n + 16);
    if (!hdr) { hphl_panic("shared: sem memoria"); return 0; }
    memset((void*)(hdr + 2), 0, n);
    hdr[0] = 1;
    hdr[1] = (n & HPHL_HDR_SIZE_MASK) | HPHL_HDR_MAGIC_BIT;
    hphl_hdr_set_class_id(hdr, classId);
    hphl_gc_register((void*)(hdr + 2));
    return (void*)(hdr + 2);
}

void* hphl_shared_retain(void* p) {
    if (p) __sync_add_and_fetch((unsigned long long*)p - 2, 1);
    return p;
}

long long hphl_shared_release(void* p) {
    if (!p) return 0;
    unsigned long long* rc = (unsigned long long*)p - 2;
    if (__sync_sub_and_fetch(rc, 1) == 0) {
        hphl_gc_unregister(p);
        /* M29 rpmalloc-inspired: envia o bloco para o thread cache se a
         * classe couber. Senão, free() direto. Passa o PAYLOAD (p), não o
         * header (rc), consistente com hphl_tc_alloc. */
        size_t sz = (size_t)(rc[1] & HPHL_HDR_SIZE_MASK);
        hphl_tc_free(p, sz);
        return 1;
    }
    return 0;
}

/* M16-fase3: builtin gc() dispara coleta de lixo */


/* M16-fase3: mark-sweep coleta de lixo */
/* Decide se um objeto sobreviveu ao mark phase.
 *  - Com roots registrados: a única fonte de reachability é a marca
 *    tracing (hphl_hdr_mark). Refcount (hdr[0]) NÃO é fallback de
 *    reachability — bugs anteriores tratavam-no como tal e mantinham
 *    objetos vivos para sempre (hdr[0] nasce = 1).
 *  - Sem roots registrados: mark-sweep não tem como descobrir objetos
 *    vivos. A decisão passa para refcount: se hdr[0] > 0, está sendo
 *    retido por hphl_shared_retain(); se == 0, é candidato a coleta.
 *    Esse caminho é o equivalente a um pure refcount quando tracing está
 *    desabilitado. */
static inline int gc_obj_alive_after_mark(unsigned long long* hdr, int has_roots) {
    int marked = hphl_hdr_mark(hdr);
    if (has_roots) return marked;
    return hdr[0] != 0;
}

long long hphl_gc_sweep(void) {
  long long t0 = hphl_gc_now_ns();
  size_t total = gc_count_all();
  if (getenv("HPHL_GC_TRACE")) fprintf(stderr, "[gc] sweep(major) start young=%zu old=%zu roots=%zu\n", gc_count_young, gc_count_old, gc_root_count);
  if (total == 0) return 0;
  /* M18.2: percorre os dois arrays (young + old). Em M18.6 isso vira
   * hphl_gc_minor (só young + remembered set) e hphl_gc_major (full). */
  /* M17: mark phase conservativa a partir dos roots */
  // limpar marks (ambos arrays)
  for (size_t i=0;i<gc_count_young;i++) {
    void* p = gc_registered_young[i];
    if (!p) continue;
    unsigned long long* hdr = (unsigned long long*)p - 2;
    hphl_hdr_set_mark(hdr, 0);
  }
  for (size_t i=0;i<gc_count_old;i++) {
    void* p = gc_registered_old[i];
    if (!p) continue;
    unsigned long long* hdr = (unsigned long long*)p - 2;
    hphl_hdr_set_mark(hdr, 0);
  }
  grey_clear();
  if (gc_root_count > 0 && gc_roots) {
    for (size_t r=0;r<gc_root_count;r++) {
      gc_seed_root(gc_roots[r]);
    }
  }
  if (gc_perm_count > 0 && gc_roots_perm) {
    for (size_t r=0;r<gc_perm_count;r++) {
      gc_seed_root(gc_roots_perm[r]);
    }
  }
  /* M30: mark completo (fora do if roots — grey pode ter seeds do remembered set). */
  while (grey_count > 0) {
      if (hphl_gc_step(4096)) break;
  }
  long long freed = 0;
  // sweep young — M18.5: promove sobreviventes para old
  for (size_t i = 0; i < gc_count_young; ) {
    void* p = gc_registered_young[i];
    if (!p) {
      size_t last_y0s = --gc_count_young;
      if (i < gc_count_young) {
        void* moved0s = gc_registered_young[last_y0s];
        gc_registered_young[i] = moved0s;
        gc_young_age[i] = gc_young_age[last_y0s];
        gc_map_update_idx(moved0s, i);
      }
      continue;
    }
    unsigned long long* hdr = (unsigned long long*)p - 2;
    int isRootReachable = gc_obj_alive_after_mark(hdr, (gc_root_count+gc_perm_count)>0);
    if (!isRootReachable) {
      unsigned long long* deadHdr = (unsigned long long*)p - 2;
      size_t deadSz = (size_t)(deadHdr[1] & HPHL_HDR_SIZE_MASK);
      gc_map_remove(p);
      hphl_tc_free(p, deadSz);
      freed++;
      size_t last_y3 = --gc_count_young;
      if (i < gc_count_young) {
        void* moved3 = gc_registered_young[last_y3];
        gc_registered_young[i] = moved3;
        gc_young_age[i] = gc_young_age[last_y3];
        gc_map_update_idx(moved3, i);
      }
    } else {
      /* M28 28.7: SEM promoção no major — mantém em young para o próximo
       * ciclo. Causa: a promoção física com memcpy + free + realloc estava
       * estourando a pilha com 1M+ objetos. Trade-off: young cresce até
       * caber em working set; próximo major repete. */
      i++;
    }
  }
  // sweep old (mesma lógica — M18.6 separa minor/major)
  for (size_t i = 0; i < gc_count_old; ) {
    void* p = gc_registered_old[i];
    if (!p) {
      size_t last_o = --gc_count_old;
      if (i < gc_count_old) {
        void* moved_o = gc_registered_old[last_o];
        gc_registered_old[i] = moved_o;
        gc_map_update_idx(moved_o, i);
      }
      continue;
    }
    unsigned long long* hdr = (unsigned long long*)p - 2;
    int isRootReachable = gc_obj_alive_after_mark(hdr, (gc_root_count+gc_perm_count)>0);
    if (!isRootReachable) {
      unsigned long long* deadHdr = (unsigned long long*)p - 2;
      size_t deadSz = (size_t)(deadHdr[1] & HPHL_HDR_SIZE_MASK);
      gc_map_remove(p);
      unsigned long long* deadHdr2 = (unsigned long long*)p - 2;
      deadHdr2[1] &= ~HPHL_HDR_CARD_BIT;
      hphl_tc_free(p, deadSz);
      freed++;
      size_t last_o2 = --gc_count_old;
      if (i < gc_count_old) {
        void* moved_o2 = gc_registered_old[last_o2];
        gc_registered_old[i] = moved_o2;
        gc_map_update_idx(moved_o2, i);
      }
    } else {
      i++;
    }
  }
  gc_collections++;
  gc_total_freed += freed;
  gc_last_freed = freed;
  long long dt = hphl_gc_now_ns() - t0;
  if (dt > gc_max_pause_ns) gc_max_pause_ns = dt;
  return freed;
}

/* ==== Strings gerenciadas pelo GC (M31) ====
 * Strings HPHL nascem via hphl_str_alloc (header + registro) e morrem no
 * sweep quando inalcançáveis. Literais estáticos e ponteiros externos
 * (getenv, etc.) nunca são registrados: is_managed os exclui com hasGCHdr
 * + pertinência ao mapa. O GC nunca move (mark-sweep in-place), então
 * `char*` para o payload permanece válido. */
void* hphl_str_alloc(size_t n) {
  void* p = hphl_shared_alloc(n);
  if (!p) { hphl_panic("str: sem memoria"); return 0; }
  return p;
}

int hphl_gc_is_managed(void* p) {
  if (!p) return 0;
  if (!hasGCHdr(p)) return 0;
  return gc_is_registered(p);
}

void hphl_gc_free_managed(void* p) {
  if (!p) return;
  unsigned long long* hdr = (unsigned long long*)p - 2;
  size_t sz = (size_t)(hdr[1] & HPHL_HDR_SIZE_MASK);
  hphl_gc_unregister(p);
  hphl_tc_free(p, sz);
}
