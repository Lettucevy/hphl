/* ---------------------------------------------------------------------------
 * M10.2 (v0.47): alocadores especializados das Memory Policies (spec ï¿½3)
 *
 * arena  ï¿½ bump allocator por frame de funï¿½ï¿½o: blocos encadeados; tudo ï¿½
 *          liberado de uma vez com hphl_arena_free_all(handle) no retorno.
 * pool   ï¿½ freelist por classe de tamanho (thread-local, sem locks):
 *          reuso O(1) para objetos reciclados (partï¿½culas, projï¿½teis).
 * shared ï¿½ refcount atï¿½mico: liberado quando chega a zero.
 * --------------------------------------------------------------------------- */

typedef struct HphlArenaBlock {
    struct HphlArenaBlock* next;
    size_t used;
    size_t cap;
} HphlArenaBlock;

void* hphl_arena_alloc(void** arena, size_t n) {
    HphlArenaBlock* b = (HphlArenaBlock*)*arena;
    if (!b || b->used + n > b->cap) {
        size_t cap = n > 4096 ? n : 4096;
        b = (HphlArenaBlock*)malloc(sizeof(HphlArenaBlock) + cap);
        if (!b) { hphl_panic("arena: sem memoria"); return 0; }
        b->used = 0;
        b->cap = cap;
        b->next = (HphlArenaBlock*)*arena;
        *arena = b;
    }
    void* p = (char*)(b + 1) + b->used;
    b->used += n; /* M_RV1 F1: sem padding - evita garbage entre objetos */
    memset(p, 0, n); /* objetos nascem zerados */
    return p;
}

void hphl_arena_free_all(void* arena) {
    while (arena) {
        HphlArenaBlock* b = (HphlArenaBlock*)arena;
        arena = b->next;
        free(b);
    }
}

/* M11.9: reset da frame-arena â€” devolve TODOS os blocos de uma vez sem
 * desmapear nada (o handle zera; o prximo alloc recomea do zero). Os
 * ponteiros emitidos antes do reset ficam invlidos: o compilador rejeita
 * acessos posteriores (USE AFTER FREE, semantic.cpp). */
void hphl_arena_reset(void** arena) {
    if (!arena) return; /* backend sem frame-arena (LLVM): no-op */
    hphl_arena_free_all(*arena);
    *arena = 0;
}
/* pool: bins por classe (8 << cls).
 * M31 FIX: era `static __thread` (mesmo risco do tc_bins — offset TLS errado
 * no GCC 16). Estáticos com spinlock. */
typedef struct HphlPoolNode { struct HphlPoolNode* next; } HphlPoolNode;
#define HPHL_POOL_CLASSES 13 /* até 32 KiB */
static HphlPoolNode* hphl_pool_bins[HPHL_POOL_CLASSES];
static volatile int hphl_pool_lock = 0;

static int hphl_pool_class(size_t n) {
    int cls = 0;
    size_t sz = 8;
    while (sz < n && cls < HPHL_POOL_CLASSES - 1) { sz <<= 1; cls++; }
    return cls;
}

void* hphl_pool_alloc(size_t n) {
    if (n > ((size_t)8 << (HPHL_POOL_CLASSES - 1))) return malloc(n);
    int cls = hphl_pool_class(n);
    HphlPoolNode* nd = NULL;
    while (__sync_lock_test_and_set(&hphl_pool_lock, 1)) { /* spin */ }
    nd = hphl_pool_bins[cls];
    if (nd) hphl_pool_bins[cls] = nd->next;
    __sync_lock_release(&hphl_pool_lock);
    if (nd) {
        memset((void*)nd, 0, n); /* reuso: zera o bloco reciclado */
        return (void*)nd;
    }
    void* fresh = malloc((size_t)8 << cls);
    if (fresh) memset(fresh, 0, n);
    return fresh;
}

void hphl_pool_free(void* p, size_t n) {
    if (!p) return;
    if (n > ((size_t)8 << (HPHL_POOL_CLASSES - 1))) { free(p); return; }
    int cls = hphl_pool_class(n);
    HphlPoolNode* nd = (HphlPoolNode*)p;
    while (__sync_lock_test_and_set(&hphl_pool_lock, 1)) { /* spin */ }
    nd->next = hphl_pool_bins[cls];
    hphl_pool_bins[cls] = nd;
    __sync_lock_release(&hphl_pool_lock);
}

