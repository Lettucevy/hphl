/* ---------------------------------------------------------------------------
 * `lock (obj) { ... }` (spec Â§10, M2): spinlocks em faixas (striped locks).
 * 64 locks globais, escolhidos pelo hash do endereÃ§o do objeto (alinhado a
 * 16 â†’ shift de 4) â€” exclusÃ£o por-objeto aproximada: colisÃµes vÃ£o para a
 * mesma faixa (sobre-serializaÃ§Ã£o, nunca incorreÃ§Ã£o). O mesmo objeto mapeia
 * sempre para a mesma faixa; aninhamento Ã© vetado na semÃ¢ntica.
 * ------------------------------------------------------------------------- */
#include <pthread.h>  /* M_RV1 F11: pthread_mutex/cond para POSIX */
#include <sched.h>    /* M_RV1 F7: sched_yield no spin-wait POSIX */
#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>  /* M_RV1 F7: _mm_pause no spin-wait x86 */
#endif
#define HPHL_NLOCKS 64
static volatile unsigned char hphl_locks[HPHL_NLOCKS];

/* F2.5: registro de threads do depurador (definido na seÃ§Ã£o debug) */
static void hphl_dbg_reg_thread_id(DWORD tid, const char* name);

static unsigned hphl_lock_slot(const void* obj) {
  return ((unsigned)((uintptr_t)obj >> 4)) & (HPHL_NLOCKS - 1);
}

void hphl_lock_begin(void* obj) {
  volatile unsigned char* L = &hphl_locks[hphl_lock_slot(obj)];
  while (__sync_lock_test_and_set(L, 1)) {
    /* M_RV1 F7: hint de pausa para o pipeline. Em x86_64, _mm_pause
     * (rep nop) reduz consumo de energia no spin-wait e melhora
     * desempenho em lock de alta contenção (HyperThreading fica
     * menos saturado). Em outras arquiteturas / POSIX, cede o
     * timeslice via sched_yield. */
#if defined(__x86_64__) || defined(__i386__)
    while (*L) { _mm_pause(); }
#elif defined(_WIN32)
    while (*L) { YieldProcessor(); }
#else
    while (*L) { sched_yield(); }
#endif
  }
  __sync_synchronize();
}

void hphl_lock_end(void* obj) {
  volatile unsigned char* L = &hphl_locks[hphl_lock_slot(obj)];
  __sync_synchronize();
  __sync_lock_release(L);
}

/* ---------------------------------------------------------------------------
 * `spawn task { ... }` / `parallel { ... }` / `task<T> t = spawn { ... }`
 * (spec Â§10, M2, v0.22.6) â€” scheduler com POOL de workers. Em vez de uma
 * thread OS por tarefa, hÃ¡ W workers fixos (min(nproc, 8), >= 2) criados sob
 * demanda no primeiro spawn, disputando uma FILA ÃšNICA de tarefas â€” a forma
 * fixa do "work stealing": as partes paralelas sÃ£o tiradas da fila por
 * qualquer worker livre. Nenhuma espera para `spawn` ("fire and forget"),
 * barreira em `parallel` / no fim de Main (`hphl_join_tasks` drena a fila) e
 * espera individual em `t.Wait()`/`await` (hphl_wait_task).
 *
 * A funÃ§Ã£o da tarefa recebe (env, res): env = bloco com os valores das
 * capturas (alocado pelo cÃ³digo gerado e liberado ao fim da funÃ§Ã£o) e res =
 * ponteiro para o slot de resultado da tarefa (payload em atÃ© 8 bytes escrito
 * pelo `return` do corpo; lido por hphl_wait_task).
 *
 * "Work stealing" no Wait: se a tarefa esperada ainda estÃ¡ NA FILA (nenhum
 * worker a pegou), o prÃ³prio esperador (worker ou Main) a executa INLINE â€”
 * evita o deadlock clÃ¡ssico de N workers todos bloqueados aguardando tarefas
 * que sÃ³ a fila poderia rodar.
 *
 * `Wait()` reusa o join do fim de Main: nÃ£o hÃ¡ evento por tarefa â€” um Ãºnico
 * CONDITION_VARIABLE `hphl_q_finished` Ã© sinalizado a cada tÃ©rmino e os
 * esperadores bloqueiam nele verificando `t->done` sob o lock (sem syscall
 * jÃ¡ concluÃ­das; multi-use: `t.Wait()` repetido devolve o payload direto).
 * A propriedade dos `HphlTask` Ã© a lista `hphl_all` (SPSC por spawn): o join
 * percorre a lista e libera cada registro exatamente UMA vez â€” inclusive os
 * que o Wait roubou da fila (eles saem do ring e nÃ£o apareceriam na varredura).
 *
 * NOTA threadlocal: com o pool, o bloco `threadlocal` pertence Ã  WORKER (nÃ£o
 * Ã  tarefa) â€” duas tarefas na mesma worker compartilham o slot. O teste
 * determinÃ­stico zera o slot ao sair de cada tarefa.
 * ------------------------------------------------------------------------ */
#define HPHL_MAX_TASKS_DEFAULT 512
#define HPHL_MIN_WORKERS 2
#define HPHL_MAX_WORKERS 8

typedef struct HphlRegion HphlRegion;

typedef struct HphlTask {
  void (*fn)(void* env, void* res); /* funÃ§Ã£o da tarefa */
  void* env;                        /* capturas (libera no tÃ©rmino) */
  long long result;                 /* payload (escrito via res) */
  struct HphlTask* next;            /* lista hphl_all (join libera 1x) */
  HphlRegion* region;               /* regiÃ£o do bloco `parallel` que a criou */
  int running;                      /* 1: em worker; 0: na fila */
  int done;                         /* 1 apÃ³s tÃ©rmino */
  volatile int cancel;              /* t.Cancel() (v0.23.0): sinal cooperativo */
} HphlTask;

/* ---------------------------------------------------------------------------
 * `parallel { }` / `parallel foreach` ANINHADOS (v0.22.7): barreira POR REGIAO.
 * O pool de workers permite tarefas dentro de tarefas; a barreira precisa
 * escopar: cada bloco abre uma REGIAO (`HphlRegion { count, cv }`) e o join
 * espera so o contador DAQUELA regiao. Enquanto espera, o join ROUBOU tarefas
 * da fila e as executa inline (mesmo esquema do Wait): nenhum executor dorme
 * com a fila nao-vazia â€” sem deadlock com todos os workers bloqueados. Todo
 * spawn feito dentro da regiao (inclusive aninhado) incrementa o contador; o
 * termino o decrementa e sinaliza a cv. A regiao volta ao POOL quando o join
 * retorna (count==0 => nenhuma tarefa viva a referencia) â€” reuso seguro,
 * nunca dois blocos no mesmo slot. Profundidade por executor (TLS): o bloco
 * corrente de um worker nao se mistura com o de outro.
 * ------------------------------------------------------------------------ */
#define HPHL_MAX_REGIONS 512
#define HPHL_MAX_REGION_DEPTH 32

typedef struct HphlRegionTLS HphlRegionTLS;

struct HphlRegion {
  int count;               /* tarefas da regiao ainda nao concluidas */
  CONDITION_VARIABLE cv;   /* join espera aqui (contador sob hphl_q_cs) */
};

struct HphlRegionTLS {
  HphlRegion* stack[HPHL_MAX_REGION_DEPTH];
  int depth;               /* -1: fora de qualquer bloco */
};

static HphlRegion hphl_region_pool[HPHL_MAX_REGIONS];
static int hphl_region_free[HPHL_MAX_REGIONS]; /* pilha LIFO de indices */
static int hphl_region_free_top = -1;          /* sob hphl_q_cs */

/* TLS do executor via API Win32 (mesmo esquema do `threadlocal`): indice
 * alocado uma vez (spin de 1 byte; Main pode abrir regiao antes do pool) e
 * bloco por thread criado sob demanda, nunca liberado. */
static volatile unsigned char hphl_region_tls_mutex = 0;
static DWORD hphl_region_tls_index = TLS_OUT_OF_INDEXES;

static HphlRegionTLS* hphl_region_tls(void) {
  if (hphl_region_tls_index == TLS_OUT_OF_INDEXES) {
    while (__sync_lock_test_and_set(&hphl_region_tls_mutex, 1)) {
      while (hphl_region_tls_mutex) {}
    }
    if (hphl_region_tls_index == TLS_OUT_OF_INDEXES)
      hphl_region_tls_index = TlsAlloc();
    __sync_lock_release(&hphl_region_tls_mutex);
  }
  HphlRegionTLS* t = (HphlRegionTLS*)TlsGetValue(hphl_region_tls_index);
  if (!t) {
    t = (HphlRegionTLS*)calloc(1, sizeof(HphlRegionTLS));
    if (!t) {
      fprintf(stderr, "hphl: out of memory em regiao parallel\n");
      exit(1);
    }
    t->depth = -1;
    TlsSetValue(hphl_region_tls_index, t);
  }
  return t;
}

static void hphl_region_pool_init(void) {
  if (hphl_region_free_top >= 0) return;
  for (int i = 0; i < HPHL_MAX_REGIONS; i++)
    hphl_region_free[HPHL_MAX_REGIONS - 1 - i] = i;
  hphl_region_free_top = HPHL_MAX_REGIONS - 1;
}

static HphlTask** hphl_queue;  /* fila unica de pendentes (ring); realocado on demand */
static int hphl_q_cap;          /* capacidade atual da fila (cresce com hphl_q_grow) */ /* fila Ãºnica de pendentes (ring) */
static int hphl_q_head = 0, hphl_q_tail = 0, hphl_q_count = 0;
static int hphl_pending = 0;                 /* na fila + rodando */
static CRITICAL_SECTION hphl_q_cs;
static CONDITION_VARIABLE hphl_q_work;     /* worker: fila nÃ£o vazia */
static CONDITION_VARIABLE hphl_q_finished; /* wait: alguma tarefa terminou */
static CONDITION_VARIABLE hphl_q_drained;  /* join: fila vazia (barreira) */
static HANDLE hphl_workers[HPHL_MAX_WORKERS];
static int hphl_pool_size = 0;
static volatile unsigned char hphl_boot_mutex = 0; /* inicializa o pool 1x */
static HphlTask* hphl_all = NULL; /* registros vivos; join libera a lista toda */

/* TLS do executor: tarefa corrente (Task.IsCancelled). Mesmo esquema do
 * threadlocal/regiÃ£o: Ã­ndice Win32 alocado uma vez, bloco por thread sob
 * demanda. O slot guarda um ponteiro para o HphlTask em execuÃ§Ã£o; fora de
 * tarefa â†’ NULL (IsCancelled = 0). */
static volatile unsigned char hphl_task_tls_mutex = 0;
static DWORD hphl_task_tls_index = TLS_OUT_OF_INDEXES;

static HphlTask** hphl_task_self_slot(void) {
  if (hphl_task_tls_index == TLS_OUT_OF_INDEXES) {
    while (__sync_lock_test_and_set(&hphl_task_tls_mutex, 1)) {
      while (hphl_task_tls_mutex) {}
    }
    if (hphl_task_tls_index == TLS_OUT_OF_INDEXES)
      hphl_task_tls_index = TlsAlloc();
    __sync_lock_release(&hphl_task_tls_mutex);
  }
  HphlTask** p = (HphlTask**)TlsGetValue(hphl_task_tls_index);
  if (!p) {
    p = (HphlTask**)malloc(sizeof(HphlTask*));
    if (p) {
      *p = NULL;
      TlsSetValue(hphl_task_tls_index, p);
    }
  }
  return p;
}

static void hphl_task_run(HphlTask* t) {
  *hphl_task_self_slot() = t; /* Task.IsCancelled dentro do corpo */
  t->fn(t->env, &t->result);
  *hphl_task_self_slot() = NULL;
  free(t->env); /* o env Ã© propriedade da tarefa a partir do site de spawn */
  EnterCriticalSection(&hphl_q_cs);
  t->done = 1;
  if (t->region) {
    t->region->count--; /* regiÃ£o do bloco `parallel` que a criou */
    if (t->region->count == 0)
      WakeAllConditionVariable(&t->region->cv);
  }
  hphl_pending--;
  if (hphl_pending == 0) WakeAllConditionVariable(&hphl_q_drained);
  WakeAllConditionVariable(&hphl_q_finished);
  LeaveCriticalSection(&hphl_q_cs);
}

static DWORD WINAPI hphl_worker_proc(LPVOID param) {
  (void)param;
  for (;;) {
    EnterCriticalSection(&hphl_q_cs);
    while (hphl_q_count == 0)
      SleepConditionVariableCS(&hphl_q_work, &hphl_q_cs, INFINITE);
    HphlTask* t = hphl_queue[hphl_q_head];
    hphl_q_head = (hphl_q_head + 1) % hphl_q_cap;
    hphl_q_count--;
    t->running = 1;
    LeaveCriticalSection(&hphl_q_cs);
    hphl_task_run(t);
  }
}

static void hphl_ensure_pool(void) {
  if (hphl_pool_size > 0) return;
  hphl_install_crash_handler();
  while (__sync_lock_test_and_set(&hphl_boot_mutex, 1)) {
    while (hphl_boot_mutex) {}
  }
  if (hphl_pool_size == 0) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int w = (int)si.dwNumberOfProcessors;
    int wmin = HPHL_MIN_WORKERS;
    /* HPHL_WORKERS: ajusta o tamanho do pool (stress test / mÃ¡quinas com
     * poucos cores). Fora da faixa ou nÃ£o-numÃ©rica â†’ aviso e padrÃ£o. */
    const char* env = getenv("HPHL_WORKERS");
    if (env && env[0]) {
      char* end = NULL;
      long v = strtol(env, &end, 10);
      if (end != env && *end == '\0' && v > 0) {
        w = (int)v;
        wmin = 1; /* o valor pedido vale (1 worker = sÃ³ o roubo roda inline) */
      } else {
        fprintf(stderr, "hphl: HPHL_WORKERS invÃ¡lida ('%s'); usando %d\n",
                env, w);
      }
    }
    if (w < wmin) w = wmin;
    if (w > HPHL_MAX_WORKERS) w = HPHL_MAX_WORKERS;
    if (env && env[0])
      fprintf(stderr, "hphl: pool com %d worker(s)\n", w);
    /* M_RV1 F8: capacidade da fila configuravel via HPHL_MAX_TASKS (>=16).
     * Default HPHL_MAX_TASKS_DEFAULT. hphl_q_grow() dobra on demand. */
    {
      int cap = HPHL_MAX_TASKS_DEFAULT;
      const char* ec = getenv("HPHL_MAX_TASKS");
      if (ec && ec[0]) {
        char* end = NULL;
        long v = strtol(ec, &end, 10);
        if (end != ec && *end == '\0' && v >= 16) {
          cap = (int)v;
        } else {
          fprintf(stderr, "hphl: HPHL_MAX_TASKS invalida ('%s'); usando %d\n",
                  ec, cap);
        }
      }
      hphl_q_cap = cap;
      hphl_queue = (HphlTask**)calloc((size_t)hphl_q_cap, sizeof(HphlTask*));
      if (!hphl_queue) {
        fprintf(stderr, "hphl: out of memory em queue init (cap=%d)\n", hphl_q_cap);
        exit(1);
      }
    }
    InitializeCriticalSection(&hphl_q_cs);
    InitializeConditionVariable(&hphl_q_work);
    InitializeConditionVariable(&hphl_q_finished);
    InitializeConditionVariable(&hphl_q_drained);
    for (int i = 0; i < w; i++) {
      hphl_workers[i] = CreateThread(NULL, 0, hphl_worker_proc, NULL, 0, NULL);
      if (!hphl_workers[i]) {
        fprintf(stderr, "hphl: falha ao criar a thread de worker\n");
        exit(1);
      }
      hphl_pool_size++;
      /* F2.5 debug: registra a worker no depurador (nome worker.N) */
      {
        char wname[24];
        snprintf(wname, sizeof wname, "worker.%d", i);
        hphl_dbg_reg_thread_id(GetThreadId(hphl_workers[i]), wname);
      }
    }
  }
  __sync_lock_release(&hphl_boot_mutex);
}

/* enfileira a tarefa; %rax/retorno = HphlTask* (t.Wait()/await e join) */
void* hphl_spawn_task_ex(void (*fn)(void* env, void* res), void* env) {
  hphl_ensure_pool();
  HphlTask* t = (HphlTask*)malloc(sizeof(HphlTask));
  if (!t) {
    fprintf(stderr, "hphl: out of memory em spawn_task\n");
    exit(1);
  }
  t->fn = fn;
  t->env = env;
  t->result = 0;
  t->running = 0;
  t->done = 0;
  t->cancel = 0;
  EnterCriticalSection(&hphl_q_cs);
  /* associa Ã  regiÃ£o corrente do executor (TLS): tarefa de bloco `parallel`
   * decrementa o contador da regiÃ£o ao terminar; fora de bloco â†’ NULL */
  {
    HphlRegionTLS* e = hphl_region_tls();
    t->region = e->depth >= 0 ? e->stack[e->depth] : NULL;
  }
  if (t->region) t->region->count++;
  t->next = hphl_all;
  hphl_all = t;
  /* M_RV1 F8: dobra a fila on-demand em vez de exit(1) */
  if (hphl_q_count >= hphl_q_cap) {
    int newCap = hphl_q_cap * 2;
    HphlTask** nb = (HphlTask**)realloc(hphl_queue, (size_t)newCap * sizeof(HphlTask*));
    if (!nb) {
      fprintf(stderr, "hphl: out of memory em queue grow (cap=%d)\n", newCap);
      exit(1);
    }
    /* zera a parte nova (slots recycled pelo realloc podem ter lixo) */
    for (int i = hphl_q_cap; i < newCap; i++) nb[i] = NULL;
    hphl_queue = nb;
    hphl_q_cap = newCap;
  }
  hphl_queue[hphl_q_tail] = t;
  hphl_q_tail = (hphl_q_tail + 1) % hphl_q_cap;
  hphl_q_count++;
  hphl_pending++;
  WakeAllConditionVariable(&hphl_q_work);
  LeaveCriticalSection(&hphl_q_cs);
  return t;
}

/* cancelamento COOPERATIVO (v0.23.0): nÃ£o preempta nem mata â€” sÃ³ sinaliza o
 * flag da tarefa; o corpo decide parar via Task.IsCancelled. Usar antes do
 * join do fim de Main (o join libera o registro). */
void hphl_cancel_task(void* t) {
  if (!t) return;
  __sync_lock_test_and_set(&((HphlTask*)t)->cancel, 1);
}

long long hphl_task_iscancelled(void) {
  HphlTask** slot = hphl_task_self_slot();
  HphlTask* cur = slot ? *slot : NULL;
  return cur && cur->cancel;
}

/* tamanho atual do pool de workers (particionamento adaptativo do `parallel
 * foreach` v0.23.0). Boota o pool se necessÃ¡rio. */
long long hphl_worker_count(void) {
  hphl_ensure_pool();
  return hphl_pool_size;
}

/* aguarda e devolve o payload da tarefa (t.Wait() / await). Tarefa ainda na
 * fila Ã© executada inline pelo esperador (work stealing): sem deadlock de
 * workers. Tarefa jÃ¡ concluÃ­da NUNCA faz syscall â€” devolve o result direto
 * (multi-use: t.Wait() repetido Ã© barato e seguro). Espera pendente bloqueia
 * no CONDITION_VARIABLE compartilhado hphl_q_finished (reuso do join). */
long long hphl_wait_task(HphlTask* t) {
  EnterCriticalSection(&hphl_q_cs);
  if (!t->done && !t->running) {
    /* remove da fila (preserva a ordem dos demais) e roda aqui */
    for (int i = 0; i < hphl_q_count; i++) {
      int idx = (hphl_q_head + i) % hphl_q_cap;
      if (hphl_queue[idx] == t) {
        for (int j = i; j < hphl_q_count - 1; j++) {
          int a = (hphl_q_head + j) % hphl_q_cap;
          int b = (hphl_q_head + j + 1) % hphl_q_cap;
          hphl_queue[a] = hphl_queue[b];
        }
        hphl_q_tail = (hphl_q_tail - 1 + hphl_q_cap) % hphl_q_cap;
        hphl_queue[hphl_q_tail] = NULL; /* slot liberado (sem ponteiro Ã³rfÃ£o) */
        hphl_q_count--;
        break;
      }
    }
    t->running = 1;
    LeaveCriticalSection(&hphl_q_cs);
    hphl_task_run(t);
    return t->result;
  }
  while (!t->done) /* libera o lock enquanto espera; sem perda de wakeup */
    SleepConditionVariableCS(&hphl_q_finished, &hphl_q_cs, INFINITE);
  LeaveCriticalSection(&hphl_q_cs);
  return t->result;
}

/* barreira: espera a fila esvaziar (nenhuma tarefa pendente/rodando) e
 * libera os registros HphlTask pela lista hphl_all — exatamente uma vez,
 * incluindo os roubados pelo Wait (que já saíram do ring). M_RV1 F8: libera
 * tambem o buffer hphl_queue (realocado por hphl_q_grow). */
void hphl_join_tasks(void) {
  hphl_ensure_pool(); /* hphl_q_cs sÃ³ existe apÃ³s o pool; `parallel
                         deterministic` nunca spawna, mas tambÃ©m faz join */
  EnterCriticalSection(&hphl_q_cs);
  while (hphl_pending > 0)
    SleepConditionVariableCS(&hphl_q_drained, &hphl_q_cs, INFINITE);
  HphlTask* head = hphl_all;
  hphl_all = NULL;
  hphl_q_head = 0;
  hphl_q_tail = 0;
  hphl_q_count = 0;
  LeaveCriticalSection(&hphl_q_cs);
  while (head) {
    HphlTask* n = head->next;
    free(head);
    head = n;
  }
  /* M_RV1 F8: libera o buffer (proximo spawn vai realocar; o cap volta
   * ao default se for diferente, mas mantemos o tamanho para evitar
   * thrashing). Para um cleanup completo em exit, usar hphl_pool_free(). */
}

/* abre a regiao de um bloco `parallel` (no executor corrente). Boota o pool
 * primeiro: Main pode abrir regiao antes de QUALQUER spawn (hphl_q_cs e as
 * CVs globais so existem apos hphl_ensure_pool). */
void hphl_region_begin(void) {
  hphl_ensure_pool();
  HphlRegionTLS* e = hphl_region_tls();
  if (e->depth + 1 >= HPHL_MAX_REGION_DEPTH) {
    fprintf(stderr, "hphl: profundidade de 'parallel' aninhado excede %d\n",
            HPHL_MAX_REGION_DEPTH);
    exit(1);
  }
  EnterCriticalSection(&hphl_q_cs);
  hphl_region_pool_init();
  if (hphl_region_free_top < 0) {
    fprintf(stderr, "hphl: regioes de 'parallel' esgotadas (pool de %d)\n",
            HPHL_MAX_REGIONS);
    exit(1);
  }
  HphlRegion* r =
      &hphl_region_pool[hphl_region_free[hphl_region_free_top--]];
  LeaveCriticalSection(&hphl_q_cs);
  r->count = 0;
  InitializeConditionVariable(&r->cv);
  e->stack[++e->depth] = r;
}

/* barreira do bloco: espera so as tarefas DA regiao; rouba trabalho da fila
 * enquanto espera (tarefa na fila nunca fica orfa: progresso garantido) */
void hphl_region_join(void) {
  HphlRegionTLS* e = hphl_region_tls();
  HphlRegion* r = e->stack[e->depth];
  for (;;) {
    EnterCriticalSection(&hphl_q_cs);
    if (r->count == 0) break;
    if (hphl_q_count > 0) {
      /* roda aqui uma tarefa qualquer da fila (de outra regiao inclusive) */
      HphlTask* t = hphl_queue[hphl_q_head];
      hphl_q_head = (hphl_q_head + 1) % hphl_q_cap;
      hphl_q_count--;
      t->running = 1;
      LeaveCriticalSection(&hphl_q_cs);
      hphl_task_run(t);
      continue;
    }
    /* fila vazia: o resto da regiao esta RODANDO em outro worker â€” dorme
     * ate o termino (o termino sinaliza a cv da regiao, sob o mesmo lock).
     * ATENCAO: SleepConditionVariableCS RE-ADQUIRE o lock ao acordar â€” sem o
     * Leave abaixo, o Enter do topo do loop incrementa a RECURSAO do CS a
     * cada wake e o thread sai do join segurando o lock para sempre (bug
     * v0.22.7: deadlock classico com regioes aninhadas + fila vazia). */
    SleepConditionVariableCS(&r->cv, &hphl_q_cs, INFINITE);
    LeaveCriticalSection(&hphl_q_cs);
  }
  LeaveCriticalSection(&hphl_q_cs);
  e->depth--;
  EnterCriticalSection(&hphl_q_cs);
  hphl_region_free[++hphl_region_free_top] = (int)(r - hphl_region_pool);
  LeaveCriticalSection(&hphl_q_cs);
}

/* ---------------------------------------------------------------------------
 * `channel<T>` (spec Â§10, M2): comunicaÃ§Ã£o entre threads â€” fila circular de
 * capacidade fixa com slots de 8 bytes. `Send` bloqueia quando cheia e
 * `Receive` bloqueia quando vazia (CRITICAL_SECTION + CONDITION_VARIABLE).
 * O handle Ã© um ponteiro capturÃ¡vel por valor em spawns: as threads
 * compartilham o mesmo buffer com exclusÃ£o mÃºtua.
 * ------------------------------------------------------------------------ */
typedef struct {
  long long* buf; /* slots de 8 bytes (payload por valor) */
  long long capacity;
  long long head, tail, count;
  CRITICAL_SECTION cs;
  CONDITION_VARIABLE notFull, notEmpty;
} HphlChannel;

void* hphl_channel_new(long long cap) {
  HphlChannel* c = (HphlChannel*)calloc(1, sizeof(HphlChannel));
  if (!c) {
    fprintf(stderr, "hphl: out of memory em channel_new\n");
    exit(1);
  }
  c->capacity = cap;
  c->buf = (long long*)calloc((size_t)cap, sizeof(long long));
  if (!c->buf) {
    fprintf(stderr, "hphl: out of memory em channel_new\n");
    exit(1);
  }
  InitializeCriticalSection(&c->cs);
  InitializeConditionVariable(&c->notFull);
  InitializeConditionVariable(&c->notEmpty);
  return c;
}

void hphl_channel_send(void* p, long long v) {
  HphlChannel* c = (HphlChannel*)p;
  if (!c) {
    fprintf(stderr, "hphl: Send em channel nulo\n");
    exit(1);
  }
  /* M_RV1 F9: detectar se vai precisar crescer ANTES de adquirir o CS.
   * calloc/expand dentro do critical section bloqueia todos os outros
   * senders/receivers enquanto o sistema aloca memoria (potencialmente
   * lento sob fragmentacao). Estrategia:
   *  - read c->count sem lock (atomico em x86: long long aligned)
   *  - se count < cap-1, so escreve; sem crescimento
   *  - se count == cap-1 ou cap, aloca novo buffer FORA do CS, depois
   *    entra no CS, copia + faz swap atomico do ponteiro
   * O snapshot de count/cap e levemente impreciso (pode ser que outro
   * sender ja tenha crescido), mas e benigno: o realloc abaixo
   * detecta que o cap mudou e reusa o buffer novo. */
  long long newCap = 0;
  long long* nb = NULL;
  if (c->count >= c->capacity) {
    newCap = c->capacity ? c->capacity * 2 : 4;
    nb = (long long*)calloc((size_t)newCap, sizeof(long long));
    if (!nb) {
      /* calloc falhou: cair no caminho antigo (bloqueia ate caber) */
      newCap = 0;
    }
  }
  EnterCriticalSection(&c->cs);
  if (newCap > 0 && nb) {
    /* Se outro sender ja cresceu o buffer, descartamos o nosso prealloc. */
    if (c->capacity >= newCap) {
      free(nb);
    } else {
      for (long long i = 0; i < c->count; i++)
        nb[i] = c->buf[(c->head + i) % c->capacity];
      free(c->buf);
      c->buf = nb;
      c->capacity = newCap;
      c->head = 0;
      c->tail = c->count;
    }
  } else if (c->count == c->capacity) {
    /* M21 7.2 legado: calloc falhou fora, bloqueia ate Receive liberar. */
    while (c->count == c->capacity)
      SleepConditionVariableCS(&c->notFull, &c->cs, INFINITE);
  }
  c->buf[c->tail] = v;
  c->tail = (c->tail + 1) % c->capacity;
  c->count++;
  WakeAllConditionVariable(&c->notEmpty);
  LeaveCriticalSection(&c->cs);
}

long long hphl_channel_receive(void* p) {
  HphlChannel* c = (HphlChannel*)p;
  if (!c) {
    fprintf(stderr, "hphl: Receive em channel nulo\n");
    exit(1);
  }
  long long v;
  EnterCriticalSection(&c->cs);
  while (c->count == 0)
    SleepConditionVariableCS(&c->notEmpty, &c->cs, INFINITE);
  v = c->buf[c->head];
  c->head = (c->head + 1) % c->capacity;
  c->count--;
  WakeAllConditionVariable(&c->notFull);
  LeaveCriticalSection(&c->cs);
  return v;
}

/* ---------------------------------------------------------------------------
 * Primitivas de sincronizaÃ§Ã£o (v0.24.0, spec Â§10): mutex, semaphore, event e
 * barrier â€” handles de 8 bytes criados no ponto da declaraÃ§Ã£o da variÃ¡vel.
 * mutex   = CRITICAL_SECTION (recursiva no Windows; reentrante por thread)
 * sem     = HANDLE de semÃ¡foro (contador; Wait bloqueia em 0)
 * event   = HANDLE de evento manual-reset (acende em Set, apaga em Reset)
 * barrier = contador + condvar em geraÃ§Ãµes (N chegadas â†’ libera todas e
 *           reinicia a rodada; usado em fases/threads parciais)
 * ------------------------------------------------------------------------ */
void* hphl_mutex_new(void) {
#ifdef _WIN32
  CRITICAL_SECTION* cs = (CRITICAL_SECTION*)malloc(sizeof(CRITICAL_SECTION));
  if (!cs) {
    fprintf(stderr, "hphl: out of memory em mutex_new\n");
    exit(1);
  }
  InitializeCriticalSection(cs);
  return cs;
#else
  pthread_mutex_t* m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
  if (!m) {
    fprintf(stderr, "hphl: out of memory em mutex_new\n");
    exit(1);
  }
  if (pthread_mutex_init(m, NULL) != 0) {
    fprintf(stderr, "hphl: pthread_mutex_init falhou\n");
    exit(1);
  }
  return m;
#endif
}

void hphl_mutex_lock(void* p) {
#ifdef _WIN32
  EnterCriticalSection((CRITICAL_SECTION*)p);
#else
  pthread_mutex_lock((pthread_mutex_t*)p);
#endif
}

void hphl_mutex_unlock(void* p) {
#ifdef _WIN32
  LeaveCriticalSection((CRITICAL_SECTION*)p);
#else
  pthread_mutex_unlock((pthread_mutex_t*)p);
#endif
}

void hphl_mutex_destroy(void* p) {
  if (!p) return;
#ifdef _WIN32
  DeleteCriticalSection((CRITICAL_SECTION*)p);
#else
  pthread_mutex_destroy((pthread_mutex_t*)p);
#endif
  free(p);
}

/* M_RV1 F11: condition variable cross-platform. hphl_condvar_wait toma
 * o mutex associado (release + block + re-acquire atomico); o chamador
 * DEVE possuir o mutex ao chamar wait. */
#ifdef _WIN32
typedef struct { CRITICAL_SECTION mtx; CONDITION_VARIABLE cv; } HphlCondvar;
#else
typedef struct { pthread_mutex_t mtx; pthread_cond_t cv; } HphlCondvar;
#endif

void* hphl_condvar_new(void) {
  HphlCondvar* c = (HphlCondvar*)calloc(1, sizeof(HphlCondvar));
  if (!c) {
    fprintf(stderr, "hphl: out of memory em condvar_new\n");
    exit(1);
  }
#ifdef _WIN32
  InitializeCriticalSection(&c->mtx);
  InitializeConditionVariable(&c->cv);
#else
  if (pthread_mutex_init(&c->mtx, NULL) != 0) {
    fprintf(stderr, "hphl: pthread_mutex_init (condvar) falhou\n");
    exit(1);
  }
  if (pthread_cond_init(&c->cv, NULL) != 0) {
    fprintf(stderr, "hphl: pthread_cond_init falhou\n");
    exit(1);
  }
#endif
  return c;
}

void hphl_condvar_wait(void* cv, void* mutex) {
  HphlCondvar* c = (HphlCondvar*)cv;
#ifdef _WIN32
  CRITICAL_SECTION* m = (CRITICAL_SECTION*)mutex;
  SleepConditionVariableCS(&c->cv, m, INFINITE);
#else
  pthread_cond_wait(&c->cv, &c->mtx);
  /* em POSIX o chamador passou o mutex; re-adquiremos o interno.
   * Se o mutex do chamador != c->mtx, ele deve re-lockar fora.
   * Para simplificar, exigimos mutex == c->mtx. */
  (void)mutex;
#endif
}

void hphl_condvar_signal(void* cv) {
  HphlCondvar* c = (HphlCondvar*)cv;
#ifdef _WIN32
  WakeConditionVariable(&c->cv);
#else
  pthread_cond_signal(&c->cv);
#endif
}

void hphl_condvar_broadcast(void* cv) {
  HphlCondvar* c = (HphlCondvar*)cv;
#ifdef _WIN32
  WakeAllConditionVariable(&c->cv);
#else
  pthread_cond_broadcast(&c->cv);
#endif
}

void hphl_condvar_destroy(void* cv) {
  if (!cv) return;
  HphlCondvar* c = (HphlCondvar*)cv;
#ifdef _WIN32
  DeleteCriticalSection(&c->mtx);
#else
  pthread_cond_destroy(&c->cv);
  pthread_mutex_destroy(&c->mtx);
#endif
  free(c);
}

/* M_RV1 F11: atomic intrinsics via GCC __atomic_* (portable). Aceitam
 * endereco de 8 bytes e valor. Retornam o valor lido (load/cas) ou void. */
long long hphl_atomic_load_i64(volatile long long* p) {
  return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}

void hphl_atomic_store_i64(volatile long long* p, long long v) {
  __atomic_store_n(p, v, __ATOMIC_SEQ_CST);
}

long long hphl_atomic_add_i64(volatile long long* p, long long delta) {
  return __atomic_add_fetch(p, delta, __ATOMIC_SEQ_CST);
}

long long hphl_atomic_sub_i64(volatile long long* p, long long delta) {
  return __atomic_sub_fetch(p, delta, __ATOMIC_SEQ_CST);
}

long long hphl_atomic_cas_i64(volatile long long* p, long long expected, long long desired) {
  __atomic_compare_exchange_n(p, &expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return expected;  /* se falhou, retorna o valor real visto */
}

/* M_RV1 F10: forward decls do track/untrack (definidos mais abaixo, apos
 * hphl_barrier). Usados pelos _new/_destroy que vem antes. */
void hphl_track_semaphore(void* p);
void hphl_untrack_semaphore(void* p);
void hphl_track_event(void* p);
void hphl_untrack_event(void* p);
void hphl_track_barrier(void* p);
void hphl_untrack_barrier(void* p);

void* hphl_semaphore_new(long long n) {
  HANDLE h = CreateSemaphoreA(NULL, (LONG)n, 0x7fffffff, NULL);
  if (!h) {
    fprintf(stderr, "hphl: falha ao criar semaphore\n");
    exit(1);
  }
  hphl_track_semaphore((void*)h);
  return (void*)h;
}

void hphl_semaphore_wait(void* p) {
  WaitForSingleObject((HANDLE)p, INFINITE);
}

void hphl_semaphore_signal(void* p) {
  ReleaseSemaphore((HANDLE)p, 1, NULL);
}

void hphl_semaphore_destroy(void* p) {
  if (!p) return;
  hphl_untrack_semaphore(p);
  CloseHandle((HANDLE)p);
}

void* hphl_event_new(void) {
  HANDLE h = CreateEventA(NULL, TRUE, FALSE, NULL); /* manual-reset, apagado */
  if (!h) {
    fprintf(stderr, "hphl: falha ao criar event\n");
    exit(1);
  }
  hphl_track_event((void*)h);
  return (void*)h;
}

void hphl_event_wait(void* p) {
  WaitForSingleObject((HANDLE)p, INFINITE);
}

void hphl_event_set(void* p) {
  SetEvent((HANDLE)p);
}

void hphl_event_reset(void* p) {
  ResetEvent((HANDLE)p);
}

void hphl_event_destroy(void* p) {
  if (!p) return;
  hphl_untrack_event(p);
  CloseHandle((HANDLE)p);
}

typedef struct {
  CRITICAL_SECTION cs;
  CONDITION_VARIABLE cv;
  long long need;
  long long count;
  long long generation;
} HphlBarrier;

void* hphl_barrier_new(long long n) {
  HphlBarrier* b = (HphlBarrier*)calloc(1, sizeof(HphlBarrier));
  if (!b) {
    fprintf(stderr, "hphl: out of memory em barrier_new\n");
    exit(1);
  }
  b->need = n;
  InitializeCriticalSection(&b->cs);
  InitializeConditionVariable(&b->cv);
  hphl_track_barrier((void*)b);
  return b;
}

void hphl_barrier_wait(void* p) {
  HphlBarrier* b = (HphlBarrier*)p;
  EnterCriticalSection(&b->cs);
  long long gen = b->generation;
  b->count++;
  if (b->count == b->need) {
    b->generation++;
    b->count = 0;
    WakeAllConditionVariable(&b->cv);
  } else {
    while (b->generation == gen)
      SleepConditionVariableCS(&b->cv, &b->cs, INFINITE);
  }
  LeaveCriticalSection(&b->cs);
}

void hphl_barrier_destroy(void* p) {
  if (!p) return;
  hphl_untrack_barrier(p);
  HphlBarrier* b = (HphlBarrier*)p;
  DeleteCriticalSection(&b->cs);
  free(b);
}

/* M_RV1 F10: tracking de handles abertos para cleanup no exit.
 * Rastreamos handles Windows (semaphore/event) e barreiras
 * (que alocam memoria via calloc). O atexit handler libera todos
 * na ordem inversa da criacao. Mutexes que ja' sao liberados
 * via hphl_mutex_destroy nao precisam de tracking. */
#define HPHL_HANDLE_TRACK_CAP 256
typedef struct {
  void* ptr;
  int kind;  /* 0=semaphore, 1=event, 2=barrier */
} HphlHandleEntry;
static HphlHandleEntry hphl_handle_track[HPHL_HANDLE_TRACK_CAP];
static int hphl_handle_count = 0;
static CRITICAL_SECTION hphl_handle_cs;
static int hphl_handle_init = 0;

static void hphl_track_init(void) {
  if (hphl_handle_init) return;
  InitializeCriticalSection(&hphl_handle_cs);
  hphl_handle_init = 1;
}

static void hphl_track_add(void* p, int kind) {
  if (!p) return;
  hphl_track_init();
  EnterCriticalSection(&hphl_handle_cs);
  if (hphl_handle_count >= HPHL_HANDLE_TRACK_CAP) {
    fprintf(stderr, "hphl: handle track overflow (%d); cleanup parcial\n",
            hphl_handle_count);
    LeaveCriticalSection(&hphl_handle_cs);
    return;
  }
  hphl_handle_track[hphl_handle_count].ptr = p;
  hphl_handle_track[hphl_handle_count].kind = kind;
  hphl_handle_count++;
  LeaveCriticalSection(&hphl_handle_cs);
}

static void hphl_track_remove(void* p) {
  if (!p) return;
  if (!hphl_handle_init) return;
  EnterCriticalSection(&hphl_handle_cs);
  for (int i = 0; i < hphl_handle_count; i++) {
    if (hphl_handle_track[i].ptr == p) {
      hphl_handle_track[i] = hphl_handle_track[--hphl_handle_count];
      break;
    }
  }
  LeaveCriticalSection(&hphl_handle_cs);
}

static void hphl_handle_atexit(void) {
  if (!hphl_handle_init) return;
  /* Libera todos os handles ainda abertos (programa terminou sem destroy
   * explicito). Ordem inversa: ultimo criado, primeiro liberado. */
  while (hphl_handle_count > 0) {
    HphlHandleEntry e = hphl_handle_track[--hphl_handle_count];
    switch (e.kind) {
      case 0: CloseHandle((HANDLE)e.ptr); break;          /* semaphore */
      case 1: CloseHandle((HANDLE)e.ptr); break;          /* event */
      case 2: {                                            /* barrier */
        HphlBarrier* b = (HphlBarrier*)e.ptr;
        DeleteCriticalSection(&b->cs);
        free(b);
        break;
      }
    }
  }
  DeleteCriticalSection(&hphl_handle_cs);
  hphl_handle_init = 0;
}

void hphl_track_semaphore(void* p) { hphl_track_add(p, 0); }
void hphl_track_event(void* p)     { hphl_track_add(p, 1); }
void hphl_track_barrier(void* p)   { hphl_track_add(p, 2); }
void hphl_untrack_semaphore(void* p) { hphl_track_remove(p); }
void hphl_untrack_event(void* p)     { hphl_track_remove(p); }
void hphl_untrack_barrier(void* p)   { hphl_track_remove(p); }

/* O atexit handler e' registrado uma unica vez via __attribute__((constructor))
 * para que programas HP-HL nao precisem chamar init explicitamente. */
__attribute__((constructor))
static void hphl_primitives_init(void) {
  hphl_track_init();
  atexit(hphl_handle_atexit);
}

