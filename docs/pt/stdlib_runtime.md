# Referência da API C do Runtime HP-HL

> **Gerado automaticamente** a partir de `compiler/src/runtime/*/*.c` pelo
> `tools/gen_runtime_doc.py`. Não edite manualmente; execute novamente o
> script após modificar o runtime.

Total: **267** funções `hphl_*`.

Consulte também: [Biblioteca Padrão](index.md) (funções integradas do HP-HL).

## Subsistema `libc` (libm C99, chamada direta)

### fmin

```c
double fmin(double x, double y);
```

Função da biblioteca matemática C99, utilizada diretamente pelo builtin `min(float, float)`.

_Origem: `libm`_

### fmax

```c
double fmax(double x, double y);
```

Função da biblioteca matemática C99, utilizada diretamente pelo builtin `max(float, float)`.

_Origem: `libm`_

## Subsistema `alloc`

### hphl_arena_alloc

```c
void* hphl_arena_alloc(void** arena, size_t n);
```

Aloca um bloco contíguo de `n` bytes na arena de ponteiro linear (bump-pointer) sem sobrecarga de GC por objeto.

_Origem: `compiler/src/runtime/alloc/alloc.c:17`_

### hphl_arena_free_all

```c
void hphl_arena_free_all(void* arena);
```

Libera toda a memória alocada na arena especificada em uma única operação de tempo constante.

_Origem: `compiler/src/runtime/alloc/alloc.c:34`_

### hphl_arena_reset

```c
void hphl_arena_reset(void** arena);
```

Reinicia o ponteiro de alocação da arena para o início do buffer, invalidando alocações anteriores sem desmapear páginas da memória.

_Origem: `compiler/src/runtime/alloc/alloc.c:46`_

### hphl_pool_alloc

```c
void* hphl_pool_alloc(size_t n);
```

Aloca um bloco de memória de tamanho fixo a partir do alocador de pool seguro para threads.

_Origem: `compiler/src/runtime/alloc/alloc.c:66`_

### hphl_pool_free

```c
void hphl_pool_free(void* p, size_t n);
```

Devolve um bloco previamente alocado ao alocador de pool de memória.

_Origem: `compiler/src/runtime/alloc/alloc.c:83`_

## Subsistema `collections`

### hphl_join

```c
char* hphl_join(void* listHandle, const char* sep);
```

Concatena todos os elementos de texto de uma lista dinâmica em uma única string separada pelo delimitador fornecido.

_Origem: `compiler/src/runtime/collections/collections.c:77`_

### hphl_list_add_f

```c
void hphl_list_add_f(void* p, double v);
```

Adiciona um valor de ponto flutuante de 64 bits ao final de uma lista dinâmica.

_Origem: `compiler/src/runtime/collections/collections.c:70`_

### hphl_list_add_i

```c
void hphl_list_add_i(void* p, long long v);
```

Adiciona um valor inteiro com sinal de 64 bits ao final de uma lista dinâmica.

_Origem: `compiler/src/runtime/collections/collections.c:64`_

### hphl_list_check

```c
void hphl_list_check(long long index, void* p);
```

Valida se o índice fornecido está dentro dos limites da lista dinâmica, disparando pânico de execução se estiver fora do intervalo.

_Origem: `compiler/src/runtime/collections/collections.c:140`_

### hphl_list_data

```c
void* hphl_list_data(void* p);
```

Retorna o ponteiro de memória bruta para o buffer de armazenamento de elementos da lista dinâmica, ou NULL se vazia.

_Origem: `compiler/src/runtime/collections/collections.c:111`_

### hphl_list_free

```c
void hphl_list_free(void* p);
```

Desaloca a memória utilizada pela lista dinâmica e libera seus buffers internos de armazenamento.

_Origem: `compiler/src/runtime/collections/collections.c:43`_

### hphl_list_len

```c
long long hphl_list_len(void* p);
```

Retorna o número de elementos atualmente armazenados na lista dinâmica.

_Origem: `compiler/src/runtime/collections/collections.c:108`_

### hphl_list_new

```c
void* hphl_list_new(void);
```

Aloca e inicializa uma nova lista dinâmica vazia com a capacidade inicial padrão.

_Origem: `compiler/src/runtime/collections/collections.c:12`_

### hphl_list_slice

```c
void* hphl_list_slice(void* p, long long from, long long count, long long elemSize);
```

Cria uma nova fatia rasa (shallow copy) da lista a partir do índice `from` contendo `count` elementos.

_Origem: `compiler/src/runtime/collections/collections.c:115`_

### hphl_list_with_cap

```c
void* hphl_list_with_cap(long long n);
```

Aloca e inicializa uma nova lista dinâmica com capacidade inicial pré-alocada `n`, evitando realocações durante inserções.

_Origem: `compiler/src/runtime/collections/collections.c:24`_

### hphl_map_clear

```c
void hphl_map_clear(void *p);
```

Remove todas as entradas de chave e valor do mapa hash.

_Origem: `compiler/src/runtime/collections/collections.c:251`_

### hphl_map_contains

```c
long long hphl_map_contains(void *p, long long k);
```

Retorna 1 se o mapa hash contiver uma entrada para a chave especificada, ou 0 caso contrário.

_Origem: `compiler/src/runtime/collections/collections.c:233`_

### hphl_map_free

```c
void hphl_map_free(void *p);
```

Desaloca o mapa hash e libera toda a memória interna dos buckets.

_Origem: `compiler/src/runtime/collections/collections.c:193`_

### hphl_map_get

```c
long long hphl_map_get(void *p, long long k);
```

Recupera o valor associado à chave especificada, ou 0 caso ela não esteja presente.

_Origem: `compiler/src/runtime/collections/collections.c:224`_

### hphl_map_len

```c
long long hphl_map_len(void *p);
```

Retorna o número de pares chave-valor atualmente armazenados no mapa hash.

_Origem: `compiler/src/runtime/collections/collections.c:252`_

### hphl_map_new

```c
void* hphl_map_new(long long keyKind);
```

Aloca e inicializa um novo mapa hash vazio configurado para o tipo de chave especificado usando sondagem quadrática.

_Origem: `compiler/src/runtime/collections/collections.c:182`_

### hphl_map_put

```c
void hphl_map_put(void *p, long long k, long long v);
```

Insere ou atualiza um mapeamento de chave e valor no mapa hash.

_Origem: `compiler/src/runtime/collections/collections.c:211`_

### hphl_map_remove

```c
long long hphl_map_remove(void *p, long long k);
```

Remove o mapeamento de chave e valor da chave especificada no mapa hash.

_Origem: `compiler/src/runtime/collections/collections.c:242`_

### hphl_write_lines

```c
int64_t hphl_write_lines(const char* path, void* listHandle);
```

Escreve cada string da lista dinâmica fornecida como uma linha separada no caminho de arquivo especificado.

_Origem: `compiler/src/runtime/collections/collections.c:96`_

## Subsistema `concurrency`

### hphl_atomic_add_i64

```c
long long hphl_atomic_add_i64(volatile long long* p, long long delta);
```

Adiciona atomicamente um inteiro de 64 bits ao endereço de memória de destino e retorna o valor anterior.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:744`_

### hphl_atomic_cas_i64

```c
long long hphl_atomic_cas_i64(volatile long long* p, long long expected, long long desired);
```

Compara atomicamente o valor no endereço de destino com o esperado, substituindo-o pelo desejado se forem iguais (compare-and-swap).

_Origem: `compiler/src/runtime/concurrency/concurrency.c:752`_

### hphl_atomic_load_i64

```c
long long hphl_atomic_load_i64(volatile long long* p);
```

Lê e retorna atomicamente um inteiro de 64 bits do endereço de memória de destino com ordenação de memória acquire.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:736`_

### hphl_atomic_store_i64

```c
void hphl_atomic_store_i64(volatile long long* p, long long v);
```

Armazena atomicamente um valor inteiro de 64 bits no endereço de memória de destino com ordenação de memória release.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:740`_

### hphl_atomic_sub_i64

```c
long long hphl_atomic_sub_i64(volatile long long* p, long long delta);
```

Subtrai atomicamente um inteiro de 64 bits do endereço de memória de destino e retorna o valor anterior.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:748`_

### hphl_barrier_destroy

```c
void hphl_barrier_destroy(void* p);
```

Destrói uma barreira de sincronização de threads e libera os recursos subjacentes do sistema operacional.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:855`_

### hphl_barrier_new

```c
void* hphl_barrier_new(long long n);
```

Inicializa uma barreira de sincronização configurada para um número fixo de threads participantes.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:826`_

### hphl_barrier_wait

```c
void hphl_barrier_wait(void* p);
```

Bloqueia a thread chamadora até que todas as threads participantes alcancem a barreira de sincronização.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:839`_

### hphl_cancel_task

```c
void hphl_cancel_task(void* t);
```

Solicita o cancelamento cooperativo de uma tarefa em segundo plano ativando seu sinalizador de cancelamento.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:359`_

### hphl_channel_new

```c
void* hphl_channel_new(long long cap);
```

Cria um novo canal de passagem de mensagens tipado e seguro para threads com buffer circular sem travas.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:512`_

### hphl_channel_receive

```c
long long hphl_channel_receive(void* p);
```

Recebe e desenfileira um elemento de mensagem do canal, bloqueando se o canal estiver vazio.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:583`_

### hphl_channel_send

```c
void hphl_channel_send(void* p, long long v);
```

Enfileira um elemento de mensagem no canal e notifica threads leitoras em espera.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:530`_

### hphl_condvar_broadcast

```c
void hphl_condvar_broadcast(void* cv);
```

Acorda todas as threads atualmente em espera na variável de condição especificada.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:713`_

### hphl_condvar_destroy

```c
void hphl_condvar_destroy(void* cv);
```

Destrói a variável de condição e libera os recursos associados do sistema operacional.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:722`_

### hphl_condvar_new

```c
void* hphl_condvar_new(void);
```

Aloca e inicializa uma nova variável de condição.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:668`_

### hphl_condvar_signal

```c
void hphl_condvar_signal(void* cv);
```

Acorda pelo menos uma thread atualmente em espera na variável de condição especificada.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:704`_

### hphl_condvar_wait

```c
void hphl_condvar_wait(void* cv, void* mutex);
```

Libera atomicamente o mutex associado e suspende a execução da thread até receber sinal.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:690`_

### hphl_event_destroy

```c
void hphl_event_destroy(void* p);
```

Destrói o manipulador de evento de sincronização e libera os recursos do kernel.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:812`_

### hphl_event_new

```c
void* hphl_event_new(void);
```

Cria um evento de sincronização manual ou de redefinição automática.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:790`_

### hphl_event_reset

```c
void hphl_event_reset(void* p);
```

Redefine o evento de sincronização para o estado não sinalizado.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:808`_

### hphl_event_set

```c
void hphl_event_set(void* p);
```

Define o evento para o estado sinalizado, desbloqueando threads em espera.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:804`_

### hphl_event_wait

```c
void hphl_event_wait(void* p);
```

Aguarda até que o evento de sincronização seja sinalizado.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:800`_

### hphl_join_tasks

```c
void hphl_join_tasks(void);
```

Aguarda a conclusão de todas as tarefas ativas do pool de threads antes de prosseguir.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:415`_

### hphl_lock_begin

```c
void hphl_lock_begin(void* obj);
```

Entra em uma seção crítica reentrante (lock).

_Origem: `compiler/src/runtime/concurrency/concurrency.c:23`_

### hphl_lock_end

```c
void hphl_lock_end(void* obj);
```

Sai de uma seção crítica reentrante (unlock).

_Origem: `compiler/src/runtime/concurrency/concurrency.c:42`_

### hphl_mutex_destroy

```c
void hphl_mutex_destroy(void* p);
```

Destrói um bloqueio de exclusão mútua (mutex) e libera os manipuladores do kernel subjacentes.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:649`_

### hphl_mutex_lock

```c
void hphl_mutex_lock(void* p);
```

Adquire bloqueio exclusivo no mutex, bloqueando se estiver atualmente travado por outra thread.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:633`_

### hphl_mutex_new

```c
void* hphl_mutex_new(void);
```

Aloca e inicializa um novo mutex recursivo.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:610`_

### hphl_mutex_unlock

```c
void hphl_mutex_unlock(void* p);
```

Libera o bloqueio exclusivo no mutex especificado.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:641`_

### hphl_region_begin

```c
void hphl_region_begin(void);
```

Abre o limite de uma região estruturada paralela.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:440`_

### hphl_region_join

```c
void hphl_region_join(void);
```

Sincroniza e aguarda a conclusão de todas as tarefas dentro do escopo da região paralela atual.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:465`_

### hphl_semaphore_destroy

```c
void hphl_semaphore_destroy(void* p);
```

Destrói um semáforo contador e libera os recursos do sistema operacional.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:784`_

### hphl_semaphore_new

```c
void* hphl_semaphore_new(long long n);
```

Cria um semáforo contador com a contagem inicial e máxima especificadas.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:766`_

### hphl_semaphore_signal

```c
void hphl_semaphore_signal(void* p);
```

Incrementa o valor do semáforo contador, acordando threads em espera.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:780`_

### hphl_semaphore_wait

```c
void hphl_semaphore_wait(void* p);
```

Decrementa o valor do semáforo contador, bloqueando se a contagem for zero.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:776`_

### hphl_spawn_task_ex

```c
void* hphl_spawn_task_ex(void (*fn)(void* env, void* res), void* env);
```

Enfileira uma nova tarefa na fila de work-stealing do pool de threads com ambiente de execução e armazenamento de retorno.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:311`_

### hphl_task_iscancelled

```c
long long hphl_task_iscancelled(void);
```

Verifica se o cancelamento foi solicitado para a tarefa em execução no momento.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:364`_

### hphl_track_barrier

```c
void hphl_track_barrier(void* p);
```

Registra uma barreira no sistema de rastreamento de vazamentos de recursos do runtime.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:936`_

### hphl_track_event

```c
void hphl_track_event(void* p);
```

Registra um evento no sistema de rastreamento de vazamentos de recursos do runtime.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:935`_

### hphl_track_semaphore

```c
void hphl_track_semaphore(void* p);
```

Registra um semáforo no sistema de rastreamento de vazamentos de recursos do runtime.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:934`_

### hphl_untrack_barrier

```c
void hphl_untrack_barrier(void* p);
```

Remove uma barreira do registro de detecção de vazamentos do runtime.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:939`_

### hphl_untrack_event

```c
void hphl_untrack_event(void* p);
```

Remove um evento do registro de detecção de vazamentos do runtime.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:938`_

### hphl_untrack_semaphore

```c
void hphl_untrack_semaphore(void* p);
```

Remove um semáforo do registro de detecção de vazamentos do runtime.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:937`_

### hphl_wait_task

```c
long long hphl_wait_task(HphlTask* t);
```

Bloqueia até que a tarefa especificada termine, auxiliando na execução da fila de tarefas durante a espera.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:382`_

### hphl_worker_count

```c
long long hphl_worker_count(void);
```

Retorna o número atual de threads trabalhadoras ativas no pool de threads do runtime.

_Origem: `compiler/src/runtime/concurrency/concurrency.c:372`_

## Subsistema `core`

### hphl_abort

```c
void hphl_abort(void);
```

Interrompe a execução do processo imediatamente com diagnóstico de erro e código de terminação anormal.

_Origem: `compiler/src/runtime/core/core.c:406`_

### hphl_abs_f64

```c
double hphl_abs_f64(double v);
```

Retorna o valor absoluto de um número de ponto flutuante de 64 bits.

_Origem: `compiler/src/runtime/core/core.c:482`_

### hphl_abs_i64

```c
int64_t hphl_abs_i64(int64_t v);
```

Retorna o valor absoluto de um inteiro com sinal de 64 bits.

_Origem: `compiler/src/runtime/core/core.c:483`_

### hphl_acos

```c
double hphl_acos(double v);
```

Calcula o arco cosseno principal de um valor de ponto flutuante em radianos.

_Origem: `compiler/src/runtime/core/core.c:474`_

### hphl_append_file

```c
int64_t hphl_append_file(const char* path, const char* content);
```

Acrescenta dados de texto ao final do arquivo no caminho especificado.

_Origem: `compiler/src/runtime/core/core.c:215`_

### hphl_args

```c
void* hphl_args(void);
```

Retorna uma lista dinâmica contendo os argumentos de linha de comando passados ao programa.

_Origem: `compiler/src/runtime/core/core.c:388`_

### hphl_asin

```c
double hphl_asin(double v);
```

Calcula o arco seno principal de um valor de ponto flutuante em radianos.

_Origem: `compiler/src/runtime/core/core.c:473`_

### hphl_assert

```c
void hphl_assert(long long cond, const char* msg);
```

Avalia uma condição booleana e dispara pânico de asserção em tempo de execução se for falsa.

_Origem: `compiler/src/runtime/core/core.c:717`_

### hphl_async_yield

```c
void hphl_async_yield(void);
```

Cede cooperativamente a execução da corrotina ou tarefa atual permitindo o escalonamento de outras tarefas.

_Origem: `compiler/src/runtime/core/core.c:95`_

### hphl_atan

```c
double hphl_atan(double v);
```

Calcula o arco tangente principal de um valor de ponto flutuante em radianos.

_Origem: `compiler/src/runtime/core/core.c:475`_

### hphl_atan2

```c
double hphl_atan2(double y, double x);
```

Calcula o arco tangente de y/x utilizando os sinais dos argumentos para determinar o quadrante correto.

_Origem: `compiler/src/runtime/core/core.c:476`_

### hphl_bounds_check

```c
void hphl_bounds_check(long long index, long long size);
```

Valida se um índice de array ou buffer está dentro dos limites válidos [0, tamanho - 1].

_Origem: `compiler/src/runtime/core/core.c:655`_

### hphl_box_i64

```c
void* hphl_box_i64(long long v);
```

Empacota (box) um inteiro primitivo de 64 bits em um objeto gerenciado alocado no heap.

_Origem: `compiler/src/runtime/core/core.c:839`_

### hphl_clock_ns

```c
int64_t hphl_clock_ns(void);
```

Retorna o tempo monotônico atual de alta resolução em nanossegundos.

_Origem: `compiler/src/runtime/core/core.c:485`_

### hphl_coop_run

```c
void hphl_coop_run(void);
```

Executa o laço do escalonador cooperativo até que todas as tarefas agendadas terminem.

_Origem: `compiler/src/runtime/core/core.c:59`_

### hphl_coop_spawn

```c
void* hphl_coop_spawn(void (*fn)(void*, void*), void* env, void* res);
```

Cria e agenda uma nova tarefa leve cooperativa com o contexto de ambiente especificado.

_Origem: `compiler/src/runtime/core/core.c:47`_

### hphl_cos

```c
double hphl_cos(double v);
```

Calcula o cosseno de um ângulo fornecido em radianos.

_Origem: `compiler/src/runtime/core/core.c:471`_

### hphl_crash_handler

```c
LONG WINAPI hphl_crash_handler(EXCEPTION_POINTERS* ep);
```

Manipulador global de falhas do sistema operacional que gera diagnósticos de pilha em falhas fatais.

_Origem: `compiler/src/runtime/core/core.c:519`_

### hphl_divide_by_zero

```c
void hphl_divide_by_zero(void);
```

Dispara diagnóstico de pânico em tempo de execução quando ocorre uma divisão por zero.

_Origem: `compiler/src/runtime/core/core.c:693`_

### hphl_env_var

```c
char* hphl_env_var(const char* name);
```

Obtém o valor de uma variável de ambiente como string, ou retorna string vazia caso não definida.

_Origem: `compiler/src/runtime/core/core.c:376`_

### hphl_exc_begin

```c
int hphl_exc_begin(void* rec);
```

Inicializa um registro de quadro de exceção estruturada antes de entrar em um bloco try protegido.

_Origem: `compiler/src/runtime/core/core.c:816`_

### hphl_exc_end

```c
void hphl_exc_end(void* p);
```

Desfaz e encerra o quadro de manipulador de exceção do topo ao sair de um bloco try.

_Origem: `compiler/src/runtime/core/core.c:762`_

### hphl_exc_free

```c
void hphl_exc_free(void* p);
```

Desaloca um objeto de encapsulamento de exceção ativo.

_Origem: `compiler/src/runtime/core/core.c:754`_

### hphl_exc_new

```c
void* hphl_exc_new(void);
```

Aloca uma nova estrutura de exceção.

_Origem: `compiler/src/runtime/core/core.c:745`_

### hphl_exc_payload

```c
void* hphl_exc_payload(void* p);
```

Extrai o ponteiro do objeto de payload associado a uma exceção em andamento.

_Origem: `compiler/src/runtime/core/core.c:767`_

### hphl_exc_push

```c
void hphl_exc_push(void* p);
```

Empilha um quadro de pouso (landing pad) de exceção na pilha local da thread.

_Origem: `compiler/src/runtime/core/core.c:756`_

### hphl_exc_restore

```c
void hphl_exc_restore(void* rec);
```

Restaura os registradores e a pilha da CPU para retomar a execução no bloco catch correspondente.

_Origem: `compiler/src/runtime/core/core.c:820`_

### hphl_exit_prog

```c
int64_t hphl_exit_prog(int64_t code);
```

Encerra o programa normalmente e retorna o código de saída especificado ao sistema operacional.

_Origem: `compiler/src/runtime/core/core.c:383`_

### hphl_exp

```c
double hphl_exp(double v);
```

Calcula a constante de Euler e elevada à potência fornecida (e^x).

_Origem: `compiler/src/runtime/core/core.c:479`_

### hphl_exp2

```c
double hphl_exp2(double v);
```

Calcula 2 elevado à potência fornecida (2^x).

_Origem: `compiler/src/runtime/core/core.c:480`_

### hphl_file_exists

```c
int64_t hphl_file_exists(const char* path);
```

Retorna 1 se um arquivo ou diretório existir no caminho especificado, ou 0 caso contrário.

_Origem: `compiler/src/runtime/core/core.c:222`_

### hphl_float_overflow_check

```c
void hphl_float_overflow_check(double* v);
```

Verifica se um valor de ponto flutuante não é infinito nem NaN, disparando erro de overflow se for inválido.

_Origem: `compiler/src/runtime/core/core.c:681`_

### hphl_format_date

```c
char* hphl_format_date(int64_t epoch, const char* fmt);
```

Formata um carimbo de data/hora (timestamp Unix) em string de acordo com o especificador de formato.

_Origem: `compiler/src/runtime/core/core.c:333`_

### hphl_gas_str

```c
char* hphl_gas_str(const char* raw);
```

Normaliza um literal de string do assembly/runtime em uma string gerenciada do HP-HL.

_Origem: `compiler/src/runtime/core/core.c:130`_

### hphl_init_args

```c
void hphl_init_args(int argc, char** argv);
```

Armazena os argumentos argc e argv da linha de comando no armazenamento global do runtime.

_Origem: `compiler/src/runtime/core/core.c:6`_

### hphl_install_crash_handler

```c
void hphl_install_crash_handler(void);
```

Instala manipuladores de exceções de hardware do sistema operacional para relatar falhas fatais.

_Origem: `compiler/src/runtime/core/core.c:592`_

### hphl_json_get

```c
char* hphl_json_get(const char* root, const char* path);
```

Recupera o valor de uma propriedade JSON pelo caminho da chave em um documento JSON.

_Origem: `compiler/src/runtime/core/core.c:299`_

### hphl_json_parse

```c
char* hphl_json_parse(const char* s);
```

Analisa sintaticamente uma string JSON em uma árvore de documento na memória.

_Origem: `compiler/src/runtime/core/core.c:297`_

### hphl_json_set

```c
char* hphl_json_set(const char* root, const char* path, const char* value);
```

Define ou atualiza o valor de uma propriedade no caminho especificado em um documento JSON.

_Origem: `compiler/src/runtime/core/core.c:318`_

### hphl_json_stringify

```c
char* hphl_json_stringify(const char* s);
```

Serializa uma estrutura de documento JSON da memória em uma string formatada.

_Origem: `compiler/src/runtime/core/core.c:298`_

### hphl_list_dir

```c
void* hphl_list_dir(const char* path);
```

Retorna uma lista dinâmica com os nomes dos arquivos e diretórios localizados na pasta especificada.

_Origem: `compiler/src/runtime/core/core.c:239`_

### hphl_log

```c
double hphl_log(double v);
```

Calcula o logaritmo natural (base e) de um número de ponto flutuante.

_Origem: `compiler/src/runtime/core/core.c:477`_

### hphl_log2

```c
double hphl_log2(double v);
```

Calcula o logaritmo binário (base 2) de um número de ponto flutuante.

_Origem: `compiler/src/runtime/core/core.c:478`_

### hphl_match_fail

```c
void hphl_match_fail(void);
```

Dispara pânico de exaustão de pattern matching em tempo de execução quando nenhum padrão coincide.

_Origem: `compiler/src/runtime/core/core.c:701`_

### hphl_mkdir

```c
int64_t hphl_mkdir(const char* path);
```

Cria um novo diretório no caminho de sistema de arquivos especificado, retornando 1 em caso de sucesso ou 0 em caso de falha.

_Origem: `compiler/src/runtime/core/core.c:232`_

### hphl_now

```c
int64_t hphl_now(void);
```

Retorna o carimbo de data/hora atual da época Unix em segundos.

_Origem: `compiler/src/runtime/core/core.c:414`_

### hphl_now_ms

```c
int64_t hphl_now_ms(void);
```

Retorna o carimbo de data/hora atual da época Unix em milissegundos.

_Origem: `compiler/src/runtime/core/core.c:418`_

### hphl_now_us

```c
int64_t hphl_now_us(void);
```

Retorna o carimbo de data/hora atual da época Unix em microssegundos.

_Origem: `compiler/src/runtime/core/core.c:431`_

### hphl_overflow_check

```c
void hphl_overflow_check(long long v, long long min, long long max);
```

Valida se um inteiro de 64 bits está dentro dos limites [min, max], disparando erro de overflow se violado.

_Origem: `compiler/src/runtime/core/core.c:668`_

### hphl_panic

```c
void hphl_panic(const char* msg);
```

Interrompe imediatamente a execução do programa e imprime uma mensagem de pânico no stderr.

_Origem: `compiler/src/runtime/core/core.c:708`_

### hphl_parse_date

```c
int64_t hphl_parse_date(const char* s, const char* fmt);
```

Analisa uma string de data formatada usando o padrão especificado e retorna o timestamp Unix em segundos.

_Origem: `compiler/src/runtime/core/core.c:346`_

### hphl_pow

```c
double hphl_pow(double b, double e);
```

Calcula a base elevada ao expoente especificado (base^expoente).

_Origem: `compiler/src/runtime/core/core.c:484`_

### hphl_print_bool

```c
void hphl_print_bool(int b);
```

Imprime um valor booleano ('true' ou 'false') na saída padrão.

_Origem: `compiler/src/runtime/core/core.c:637`_

### hphl_print_char

```c
void hphl_print_char(int c);
```

Imprime um único caractere na saída padrão.

_Origem: `compiler/src/runtime/core/core.c:642`_

### hphl_print_float

```c
void hphl_print_float(double v);
```

Imprime um número de ponto flutuante de 64 bits formatado na saída padrão.

_Origem: `compiler/src/runtime/core/core.c:632`_

### hphl_print_int

```c
void hphl_print_int(long long v);
```

Imprime um inteiro com sinal de 64 bits em formato decimal na saída padrão.

_Origem: `compiler/src/runtime/core/core.c:622`_

### hphl_print_string

```c
void hphl_print_string(const char* s);
```

Imprime uma string terminada em nulo na saída padrão.

_Origem: `compiler/src/runtime/core/core.c:647`_

### hphl_print_uint

```c
void hphl_print_uint(unsigned long long v);
```

Imprime um inteiro sem sinal de 64 bits em formato decimal na saída padrão.

_Origem: `compiler/src/runtime/core/core.c:627`_

### hphl_random

```c
double hphl_random(void);
```

Gera um número pseudoaleatório de ponto flutuante uniformemente distribuído no intervalo [0.0, 1.0).

_Origem: `compiler/src/runtime/core/core.c:359`_

### hphl_random_int

```c
int64_t hphl_random_int(int64_t lo, int64_t hi);
```

Gera um número inteiro pseudoaleatório no intervalo [lo, hi] inclusive.

_Origem: `compiler/src/runtime/core/core.c:365`_

### hphl_read_file

```c
char* hphl_read_file(const char* path);
```

Lê todo o conteúdo de um arquivo no caminho especificado para um novo buffer de texto.

_Origem: `compiler/src/runtime/core/core.c:188`_

### hphl_read_line

```c
char* hphl_read_line(void);
```

Lê uma única linha de texto da entrada padrão (stdin) até a nova linha ou EOF.

_Origem: `compiler/src/runtime/core/core.c:156`_

### hphl_read_lines

```c
void* hphl_read_lines(const char* path);
```

Lê todas as linhas do arquivo especificado retornando uma lista dinâmica de strings.

_Origem: `compiler/src/runtime/core/core.c:269`_

### hphl_remove_file

```c
int64_t hphl_remove_file(const char* path);
```

Exclui o arquivo no caminho de sistema de arquivos especificado, retornando 1 em caso de sucesso ou 0 em caso de falha.

_Origem: `compiler/src/runtime/core/core.c:228`_

### hphl_runtime_abi_version

```c
int hphl_runtime_abi_version(void);
```

Retorna o número inteiro da versão da ABI da biblioteca de runtime compilada do HP-HL.

_Origem: `compiler/src/runtime/core/core.c:10`_

### hphl_set_env

```c
int64_t hphl_set_env(const char* name, const char* value);
```

Define ou atualiza uma variável de ambiente no ambiente do processo.

_Origem: `compiler/src/runtime/core/core.c:399`_

### hphl_sin

```c
double hphl_sin(double v);
```

Calcula o seno de um ângulo fornecido em radianos.

_Origem: `compiler/src/runtime/core/core.c:470`_

### hphl_sleep_ms

```c
int64_t hphl_sleep_ms(int64_t ms);
```

Suspende a execução da thread chamadora pela duração especificada em milissegundos.

_Origem: `compiler/src/runtime/core/core.c:370`_

### hphl_stat

```c
char* hphl_stat(const char* path);
```

Retorna metadados sobre um arquivo (tamanho, modo, carimbos de data) como string formatada.

_Origem: `compiler/src/runtime/core/core.c:285`_

### hphl_str_cmp

```c
int64_t hphl_str_cmp(const char* a, const char* b);
```

Compara lexicograficamente duas strings, retornando valor negativo, zero ou positivo.

_Origem: `compiler/src/runtime/core/core.c:510`_

### hphl_str_eq

```c
int64_t hphl_str_eq(const char* a, const char* b);
```

Retorna 1 se ambas as strings tiverem conteúdo idêntico, ou 0 caso contrário.

_Origem: `compiler/src/runtime/core/core.c:503`_

### hphl_str_replace

```c
char* hphl_str_replace(const char* s, const char* from, const char* to);
```

Substitui ocorrências de uma substring por uma string de substituição.

_Origem: `compiler/src/runtime/core/core.c:451`_

### hphl_struct_copy

```c
void* hphl_struct_copy(const void* src, size_t n);
```

Aloca memória e realiza uma cópia binária de uma struct de `n` bytes.

_Origem: `compiler/src/runtime/core/core.c:852`_

### hphl_tan

```c
double hphl_tan(double v);
```

Calcula a tangente de um ângulo fornecido em radianos.

_Origem: `compiler/src/runtime/core/core.c:472`_

### hphl_throw

```c
void hphl_throw(void* payload);
```

Lança uma exceção contendo o objeto de payload especificado, iniciando o desenrolamento da pilha (stack unwinding).

_Origem: `compiler/src/runtime/core/core.c:826`_

### hphl_tls_block

```c
void* hphl_tls_block(void);
```

Retorna o ponteiro base do bloco de armazenamento local da thread atual (TLS).

_Origem: `compiler/src/runtime/core/core.c:875`_

### hphl_tls_setup

```c
void hphl_tls_setup(int bytes);
```

Aloca e inicializa a área de armazenamento local de thread (TLS) com o tamanho especificado para a thread chamadora.

_Origem: `compiler/src/runtime/core/core.c:869`_

### hphl_trunc

```c
double hphl_trunc(double v);
```

Trunca um valor de ponto flutuante em direção a zero para o valor inteiro mais próximo.

_Origem: `compiler/src/runtime/core/core.c:481`_

### hphl_unbox_i64

```c
long long hphl_unbox_i64(void* p);
```

Desempacota (unbox) um inteiro de 64 bits de um objeto gerenciado alocado no heap.

_Origem: `compiler/src/runtime/core/core.c:849`_

### hphl_write_file

```c
int64_t hphl_write_file(const char* path, const char* content);
```

Grava o conteúdo de texto em um arquivo no caminho especificado, substituindo qualquer arquivo existente.

_Origem: `compiler/src/runtime/core/core.c:208`_

## Subsistema `debug`

### hphl_dbg_enter

```c
void hphl_dbg_enter(unsigned long long fnId);
```

Notifica o runtime do depurador sobre a entrada em uma função pelo identificador da função.

_Origem: `compiler/src/runtime/debug/debug.c:1228`_

### hphl_dbg_enter_frame

```c
void hphl_dbg_enter_frame(unsigned long long fnId, unsigned long long framePtr);
```

Registra a entrada na função e o ponteiro base do quadro de pilha para o depurador interativo.

_Origem: `compiler/src/runtime/debug/debug.c:1094`_

### hphl_dbg_enter_impl

```c
void hphl_dbg_enter_impl(unsigned long long fnId, unsigned long long rbp);
```

Auxiliar de implementação de baixo nível para rastreamento de entrada em funções no depurador.

_Origem: `compiler/src/runtime/debug/debug.c:1068`_

### hphl_dbg_exc_report

```c
void hphl_dbg_exc_report(int cod, const char* msg);
```

Relata uma exceção não tratada ou condição de falha crítica aos depuradores conectados.

_Origem: `compiler/src/runtime/debug/debug.c:1234`_

### hphl_dbg_leave

```c
void hphl_dbg_leave(void);
```

Notifica o runtime do depurador de que o quadro de execução da função atual está sendo encerrado.

_Origem: `compiler/src/runtime/debug/debug.c:1086`_

### hphl_dbg_trap

```c
void hphl_dbg_trap(long long line);
```

Gancho de interceptação (trap) do depurador acionado ao atingir um breakpoint de linha de código.

_Origem: `compiler/src/runtime/debug/debug.c:1225`_

### hphl_dbg_trap_impl

```c
void hphl_dbg_trap_impl(long long line, unsigned long long rbp);
```

Auxiliar de implementação de baixo nível para traps de depurador e quadros de registradores inspecionáveis.

_Origem: `compiler/src/runtime/debug/debug.c:1018`_

## Subsistema `gc`

### hphl_gc

```c
void hphl_gc(void);
```

Força a execução imediata de um ciclo completo de coleta de lixo (garbage collection).

_Origem: `compiler/src/runtime/gc/gc.c:1267`_

### hphl_gc_add_root

```c
void hphl_gc_add_root(void* slot);
```

Registra um slot de endereço como raiz do GC para preservá-lo durante as coletas.

_Origem: `compiler/src/runtime/gc/gc.c:557`_

### hphl_gc_add_roots_batch

```c
void hphl_gc_add_roots_batch(void* slots[], int n);
```

Registra um conjunto de slots de ponteiros raiz em uma única operação em lote.

_Origem: `compiler/src/runtime/gc/gc.c:589`_

### hphl_gc_cards_clear

```c
void hphl_gc_cards_clear(void);
```

Limpa os marcadores da tabela de cartões (card table) do GC geracional.

_Origem: `compiler/src/runtime/gc/gc.c:952`_

### hphl_gc_cards_scan

```c
void hphl_gc_cards_scan(void);
```

Examina as entradas modificadas da tabela de cartões para rastrear referências entre gerações de objetos.

_Origem: `compiler/src/runtime/gc/gc.c:985`_

### hphl_gc_cleanup

```c
void hphl_gc_cleanup(void);
```

Encerra o coletor de lixo e desaloca todas as páginas de memória do heap gerenciado.

_Origem: `compiler/src/runtime/gc/gc.c:717`_

### hphl_gc_cleanup_rem

```c
void hphl_gc_cleanup_rem(void);
```

Limpa as estruturas de dados do conjunto lembrado (remembered set) do coletor geracional.

_Origem: `compiler/src/runtime/gc/gc.c:1042`_

### hphl_gc_free_managed

```c
void hphl_gc_free_managed(void* p);
```

Libera manualmente um bloco de objeto gerenciado, contornando a fase normal de varredura.

_Origem: `compiler/src/runtime/gc/gc.c:1519`_

### hphl_gc_init

```c
void hphl_gc_init(void);
```

Inicializa as estruturas do runtime e as páginas de memória do coletor de lixo.

_Origem: `compiler/src/runtime/gc/gc.c:690`_

### hphl_gc_is_managed

```c
int hphl_gc_is_managed(void* p);
```

Retorna 1 se o ponteiro pertencer a uma página de heap gerenciada pelo GC, ou 0 caso contrário.

_Origem: `compiler/src/runtime/gc/gc.c:1513`_

### hphl_gc_major

```c
long long hphl_gc_major(void);
```

Executa um ciclo de coleta de lixo maior (full heap) e retorna a quantidade de bytes liberados.

_Origem: `compiler/src/runtime/gc/gc.c:1203`_

### hphl_gc_minor

```c
long long hphl_gc_minor(void);
```

Executa um ciclo de coleta menor (geração jovem / nursery) e retorna a quantidade de bytes liberados.

_Origem: `compiler/src/runtime/gc/gc.c:1055`_

### hphl_gc_pop_roots

```c
void hphl_gc_pop_roots(int n);
```

Remove os `n` slots de ponteiros raiz mais recentes da pilha de raízes da thread.

_Origem: `compiler/src/runtime/gc/gc.c:680`_

### hphl_gc_pressure

```c
void hphl_gc_pressure(void);
```

Informa pressão de memória ao coletor, podendo disparar um ciclo de coleta preventivo.

_Origem: `compiler/src/runtime/gc/gc.c:1228`_

### hphl_gc_push_stack

```c
void hphl_gc_push_stack(void* lo_addr, void* hi_addr);
```

Registra um intervalo de endereços da pilha de execução para varredura conservadora pelo GC.

_Origem: `compiler/src/runtime/gc/gc.c:419`_

### hphl_gc_register

```c
void hphl_gc_register(void* p);
```

Registra um ponteiro de objeto recém-alocado no pool da geração jovem (nursery).

_Origem: `compiler/src/runtime/gc/gc.c:780`_

### hphl_gc_register_old

```c
void hphl_gc_register_old(void* p);
```

Registra um objeto diretamente na geração madura (tenured) do heap.

_Origem: `compiler/src/runtime/gc/gc.c:807`_

### hphl_gc_register_slots

```c
void hphl_gc_register_slots(const int64_t* offs, void* base, int n);
```

Registra deslocamentos de campos de um objeto contendo ponteiros do GC para rastreamento exato.

_Origem: `compiler/src/runtime/gc/gc.c:645`_

### hphl_gc_remove_root

```c
void hphl_gc_remove_root(void* slot);
```

Remove o registro de um slot de ponteiro raiz do rastreamento do GC.

_Origem: `compiler/src/runtime/gc/gc.c:607`_

### hphl_gc_remove_roots_batch

```c
void hphl_gc_remove_roots_batch(void* slots[], int n);
```

Remove o registro de um lote de slots de ponteiros raiz do rastreamento do GC.

_Origem: `compiler/src/runtime/gc/gc.c:612`_

### hphl_gc_stats_collections

```c
long long hphl_gc_stats_collections(void);
```

Retorna o número acumulado de ciclos de coleta de lixo executados.

_Origem: `compiler/src/runtime/gc/gc.c:546`_

### hphl_gc_stats_freed

```c
long long hphl_gc_stats_freed(void);
```

Retorna o total acumulado de bytes recuperados pelo coletor de lixo.

_Origem: `compiler/src/runtime/gc/gc.c:547`_

### hphl_gc_stats_last_freed

```c
long long hphl_gc_stats_last_freed(void);
```

Retorna a quantidade de bytes recuperados durante o ciclo de coleta mais recente.

_Origem: `compiler/src/runtime/gc/gc.c:549`_

### hphl_gc_stats_max_pause

```c
long long hphl_gc_stats_max_pause(void);
```

Retorna a duração máxima de pausa observada durante ciclos de coleta em microssegundos.

_Origem: `compiler/src/runtime/gc/gc.c:548`_

### hphl_gc_step

```c
int hphl_gc_step(int budget);
```

Executa uma etapa incremental de coleta de lixo dentro do orçamento de trabalho especificado.

_Origem: `compiler/src/runtime/gc/gc.c:359`_

### hphl_gc_sweep

```c
long long hphl_gc_sweep(void);
```

Executa a fase de varredura (sweep) do coletor de lixo, recuperando blocos não marcados.

_Origem: `compiler/src/runtime/gc/gc.c:1388`_

### hphl_gc_unregister

```c
void hphl_gc_unregister(void* p);
```

Remove o ponteiro de um objeto do gerenciamento ativo do coletor de lixo.

_Origem: `compiler/src/runtime/gc/gc.c:823`_

### hphl_gc_unregister_slots

```c
void hphl_gc_unregister_slots(const int64_t* offs, void* base, int n);
```

Remove o registro dos deslocamentos de campos previamente gravados para rastreamento exato do GC.

_Origem: `compiler/src/runtime/gc/gc.c:666`_

### hphl_set_class_desc

```c
int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);
```

Registra uma máscara de bits de descritor de classe definindo quais deslocamentos contêm referências gerenciadas.

_Origem: `compiler/src/runtime/gc/gc.c:190`_

### hphl_shared_alloc

```c
void* hphl_shared_alloc(size_t n);
```

Aloca um objeto de memória compartilhada gerenciado por contagem de referências.

_Origem: `compiler/src/runtime/gc/gc.c:1282`_

### hphl_shared_alloc_typed

```c
void* hphl_shared_alloc_typed(size_t n, int classId);
```

Aloca um objeto de memória compartilhada tipado com o identificador de classe fornecido.

_Origem: `compiler/src/runtime/gc/gc.c:1320`_

### hphl_shared_release

```c
long long hphl_shared_release(void* p);
```

Decrementa a contagem de referências de um objeto compartilhado, liberando-o quando atinge zero.

_Origem: `compiler/src/runtime/gc/gc.c:1353`_

### hphl_shared_retain

```c
void* hphl_shared_retain(void* p);
```

Incrementa a contagem de referências de um objeto compartilhado para impedir sua desalocação.

_Origem: `compiler/src/runtime/gc/gc.c:1348`_

### hphl_str_alloc

```c
void* hphl_str_alloc(size_t n);
```

Aloca um buffer de memória para string rastreado pelo coletor de lixo.

_Origem: `compiler/src/runtime/gc/gc.c:1507`_

### hphl_tc_alloc

```c
void* hphl_tc_alloc(size_t n);
```

Aloca memória a partir do caminho rápido do cache de alocação local da thread (TC).

_Origem: `compiler/src/runtime/gc/gc.c:457`_

### hphl_tc_free

```c
void hphl_tc_free(void* payload, size_t n);
```

Devolve memória ao cache de alocação local da thread.

_Origem: `compiler/src/runtime/gc/gc.c:477`_

### hphl_write_barrier

```c
void hphl_write_barrier(void* old_obj, void* field_addr);
```

Barreira de escrita geracional que atualiza a tabela de cartões ao armazenar referências em objetos existentes.

_Origem: `compiler/src/runtime/gc/gc.c:999`_

### hphl_write_barrier_slow

```c
void hphl_write_barrier_slow(void* old_obj, void* field_addr);
```

Implementação do caminho lento da barreira de escrita geracional para marcação de cartões.

_Origem: `compiler/src/runtime/gc/gc.c:1024`_

## Subsistema `img`

### hphl_img_blur

```c
void* hphl_img_blur(void* px, long long w, long long h, long long r);
```

Aplica desfoque (blur) com raio `r` a um buffer de pixels e retorna um novo buffer.

_Origem: `compiler/src/runtime/img/img.c:128`_

### hphl_img_flip_h

```c
void* hphl_img_flip_h(void* px, long long w, long long h);
```

Inverte horizontalmente um buffer de pixels e retorna um novo buffer de imagem invertida.

_Origem: `compiler/src/runtime/img/img.c:74`_

### hphl_img_grayscale

```c
void* hphl_img_grayscale(void* px);
```

Converte um buffer de pixels RGB/RGBA para tons de cinza.

_Origem: `compiler/src/runtime/img/img.c:54`_

### hphl_img_png_save

```c
long long hphl_img_png_save(const char* path, void* px, long long w, long long h);
```

Salva um buffer de pixels como arquivo de imagem PNG no caminho especificado.

_Origem: `compiler/src/runtime/img/img.c:185`_

### hphl_img_ppm_load

```c
void* hphl_img_ppm_load(const char* path);
```

Carrega uma imagem a partir de um arquivo PPM para um buffer de pixels recém-alocado.

_Origem: `compiler/src/runtime/img/img.c:278`_

### hphl_img_ppm_save

```c
long long hphl_img_ppm_save(const char* path, void* px, long long w, long long h);
```

Salva um buffer de pixels como arquivo de imagem PPM no caminho especificado.

_Origem: `compiler/src/runtime/img/img.c:157`_

### hphl_img_resize

```c
void* hphl_img_resize(void* px, long long w, long long h, long long nw, long long nh);
```

Redimensiona um buffer de pixels de (w, h) para (nw, nh) usando interpolação bilinear.

_Origem: `compiler/src/runtime/img/img.c:92`_

## Subsistema `mem`

### hphl_mem_alloc

```c
void* hphl_mem_alloc(int64_t size);
```

Aloca `size` bytes de memória bruta não inicializada no heap.

_Origem: `compiler/src/runtime/mem/mem.c:12`_

### hphl_mem_copy

```c
void hphl_mem_copy(void* dst, void* src, int64_t n);
```

Copia `n` bytes do buffer de origem para o buffer de destino.

_Origem: `compiler/src/runtime/mem/mem.c:90`_

### hphl_mem_copy_off

```c
void hphl_mem_copy_off(void* dst, int64_t dstOff, void* src, int64_t srcOff, int64_t n);
```

Copia `n` bytes entre buffers de memória com deslocamentos explícitos de origem e destino.

_Origem: `compiler/src/runtime/mem/mem.c:95`_

### hphl_mem_fill

```c
void hphl_mem_fill(void* dst, int64_t byteVal, int64_t n);
```

Preenche `n` bytes da memória de destino com o valor de byte especificado.

_Origem: `compiler/src/runtime/mem/mem.c:101`_

### hphl_mem_free

```c
void hphl_mem_free(void* p);
```

Desaloca um bloco de memória bruta do heap previamente alocado por `hphl_mem_alloc`.

_Origem: `compiler/src/runtime/mem/mem.c:17`_

### hphl_mem_peek_f32

```c
double hphl_mem_peek_f32(void* p, int64_t off);
```

Lê um valor de ponto flutuante de 32 bits da memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:73`_

### hphl_mem_peek_f64

```c
double hphl_mem_peek_f64(void* p, int64_t off);
```

Lê um valor de ponto flutuante de 64 bits da memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:84`_

### hphl_mem_peek_i32

```c
int64_t hphl_mem_peek_i32(void* p, int64_t off);
```

Lê um inteiro com sinal de 32 bits da memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:27`_

### hphl_mem_peek_i64

```c
int64_t hphl_mem_peek_i64(void* p, int64_t off);
```

Lê um inteiro com sinal de 64 bits da memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:50`_

### hphl_mem_peek_ptr

```c
void* hphl_mem_peek_ptr(void* p, int64_t off);
```

Lê um ponteiro da memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:61`_

### hphl_mem_peek_u32

```c
int64_t hphl_mem_peek_u32(void* p, int64_t off);
```

Lê um inteiro sem sinal de 32 bits da memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:39`_

### hphl_mem_poke_f32

```c
void hphl_mem_poke_f32(void* p, int64_t off, double v);
```

Escreve um valor de ponto flutuante de 32 bits na memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:67`_

### hphl_mem_poke_f64

```c
void hphl_mem_poke_f64(void* p, int64_t off, double v);
```

Escreve um valor de ponto flutuante de 64 bits na memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:79`_

### hphl_mem_poke_i32

```c
void hphl_mem_poke_i32(void* p, int64_t off, int64_t v);
```

Escreve um inteiro com sinal de 32 bits na memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:21`_

### hphl_mem_poke_i64

```c
void hphl_mem_poke_i64(void* p, int64_t off, int64_t v);
```

Escreve um inteiro com sinal de 64 bits na memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:45`_

### hphl_mem_poke_ptr

```c
void hphl_mem_poke_ptr(void* p, int64_t off, void* v);
```

Escreve um ponteiro na memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:56`_

### hphl_mem_poke_u32

```c
void hphl_mem_poke_u32(void* p, int64_t off, int64_t v);
```

Escreve um inteiro sem sinal de 32 bits na memória bruta no deslocamento de bytes especificado.

_Origem: `compiler/src/runtime/mem/mem.c:33`_

### hphl_mem_zero

```c
void hphl_mem_zero(void* dst, int64_t n);
```

Zera `n` bytes de memória no buffer de destino.

_Origem: `compiler/src/runtime/mem/mem.c:108`_

## Subsistema `net`

### hphl_accept

```c
int64_t hphl_accept(int64_t s);
```

Aceita uma conexão TCP de entrada em um socket receptor e retorna o descritor do socket cliente.

_Origem: `compiler/src/runtime/net/socket.c:74`_

### hphl_bind

```c
int64_t hphl_bind(int64_t s, int64_t port);
```

Vincula um socket de rede ao número de porta local especificado.

_Origem: `compiler/src/runtime/net/socket.c:47`_

### hphl_close_socket

```c
int64_t hphl_close_socket(int64_t s);
```

Fecha o descritor de um socket de rede aberto e libera os recursos de rede do sistema operacional.

_Origem: `compiler/src/runtime/net/socket.c:99`_

### hphl_connect

```c
int64_t hphl_connect(int64_t s, const char* host, int64_t port);
```

Estabelece uma conexão de cliente TCP com o host remoto e porta especificados.

_Origem: `compiler/src/runtime/net/socket.c:30`_

### hphl_listen

```c
int64_t hphl_listen(int64_t s, int64_t backlog);
```

Coloca um socket de rede em modo de escuta com a fila de conexões pendentes (backlog) especificada.

_Origem: `compiler/src/runtime/net/socket.c:71`_

### hphl_recv

```c
char* hphl_recv(int64_t s, int64_t len);
```

Recebe dados de um socket conectado em até `len` bytes para um buffer de string.

_Origem: `compiler/src/runtime/net/socket.c:86`_

### hphl_send

```c
int64_t hphl_send(int64_t s, const char* buf);
```

Envia uma string ou payload de bytes por um socket de rede conectado.

_Origem: `compiler/src/runtime/net/socket.c:78`_

### hphl_socket

```c
int64_t hphl_socket(int64_t af, int64_t type, int64_t proto);
```

Cria um novo socket de rede do sistema operacional com a família de endereços, tipo e protocolo especificados.

_Origem: `compiler/src/runtime/net/socket.c:25`_

## Subsistema `strings`

### hphl_ceil

```c
double hphl_ceil(double v);
```

Retorna o menor valor inteiro que não seja menor do que o número de ponto flutuante fornecido (teto).

_Origem: `compiler/src/runtime/strings/strings.c:166`_

### hphl_e

```c
double hphl_e(void);
```

Retorna a constante matemática de Euler e (aprox. 2.718281828459045).

_Origem: `compiler/src/runtime/strings/strings.c:164`_

### hphl_floor

```c
double hphl_floor(double v);
```

Retorna o maior valor inteiro que não seja maior do que o número de ponto flutuante fornecido (piso).

_Origem: `compiler/src/runtime/strings/strings.c:165`_

### hphl_fmod

```c
double hphl_fmod(double a, double b);
```

Calcula o resto da divisão de ponto flutuante entre a e b.

_Origem: `compiler/src/runtime/strings/strings.c:168`_

### hphl_is_numeric

```c
int64_t hphl_is_numeric(const char* s);
```

Retorna 1 se a string for composta exclusivamente por dígitos numéricos decimais, ou 0 caso contrário.

_Origem: `compiler/src/runtime/strings/strings.c:51`_

### hphl_max_i64

```c
int64_t hphl_max_i64(int64_t a, int64_t b);
```

Retorna o maior entre dois inteiros com sinal de 64 bits.

_Origem: `compiler/src/runtime/strings/strings.c:162`_

### hphl_min_i64

```c
int64_t hphl_min_i64(int64_t a, int64_t b);
```

Retorna o menor entre dois inteiros com sinal de 64 bits.

_Origem: `compiler/src/runtime/strings/strings.c:161`_

### hphl_parse_f64

```c
double hphl_parse_f64(const char* s);
```

Converte uma string terminada em nulo em um número de ponto flutuante de 64 bits.

_Origem: `compiler/src/runtime/strings/strings.c:46`_

### hphl_parse_int

```c
int64_t hphl_parse_int(const char* s);
```

Converte uma string terminada em nulo em um inteiro com sinal de 64 bits.

_Origem: `compiler/src/runtime/strings/strings.c:34`_

### hphl_pi

```c
double hphl_pi(void);
```

Retorna a constante matemática Pi (aprox. 3.141592653589793).

_Origem: `compiler/src/runtime/strings/strings.c:163`_

### hphl_round

```c
double hphl_round(double v);
```

Arredonda um valor de ponto flutuante para o inteiro mais próximo.

_Origem: `compiler/src/runtime/strings/strings.c:167`_

### hphl_split

```c
void* hphl_split(const char* s, const char* sep);
```

Divide uma string em uma lista dinâmica de substrings separadas pelo delimitador especificado.

_Origem: `compiler/src/runtime/strings/strings.c:1`_

### hphl_sqrt

```c
double hphl_sqrt(double v);
```

Calcula a raiz quadrada de um número de ponto flutuante.

_Origem: `compiler/src/runtime/strings/strings.c:170`_

### hphl_str_char_at

```c
char* hphl_str_char_at(const char* s, int64_t i);
```

Retorna uma string de um único caractere contendo o caractere no índice `i`.

_Origem: `compiler/src/runtime/strings/strings.c:79`_

### hphl_str_char_index

```c
int64_t hphl_str_char_index(const char* s, int64_t i);
```

Retorna o índice de bytes correspondente ao índice de caracteres `i` em uma string UTF-8.

_Origem: `compiler/src/runtime/strings/strings.c:90`_

### hphl_str_chr

```c
char* hphl_str_chr(int64_t c);
```

Converte um ponto de código Unicode/ASCII inteiro em uma string de caractere único.

_Origem: `compiler/src/runtime/strings/strings.c:103`_

### hphl_str_concat

```c
char* hphl_str_concat(const char* a, const char* b);
```

Concatena duas strings e retorna uma nova string combinada.

_Origem: `compiler/src/runtime/strings/strings.c:217`_

### hphl_str_contains

```c
int64_t hphl_str_contains(const char* s, const char* needle);
```

Retorna 1 se a substring for encontrada dentro da string de origem, ou 0 caso contrário.

_Origem: `compiler/src/runtime/strings/strings.c:123`_

### hphl_str_down

```c
char* hphl_str_down(const char* s);
```

Retorna uma cópia da string com todos os caracteres convertidos para minúsculas.

_Origem: `compiler/src/runtime/strings/strings.c:144`_

### hphl_str_ends

```c
int64_t hphl_str_ends(const char* s, const char* suf);
```

Retorna 1 se a string terminar com o sufixo especificado, ou 0 caso contrário.

_Origem: `compiler/src/runtime/strings/strings.c:130`_

### hphl_str_format

```c
char* hphl_str_format(const char* fmt, const char* arg);
```

Formata uma string utilizando um especificador de formato estilo printf e argumento de texto.

_Origem: `compiler/src/runtime/strings/strings.c:262`_

### hphl_str_format_float

```c
char* hphl_str_format_float(const char* fmt, double v);
```

Formata um número de ponto flutuante de 64 bits utilizando o especificador de formato fornecido.

_Origem: `compiler/src/runtime/strings/strings.c:286`_

### hphl_str_format_int

```c
char* hphl_str_format_int(const char* fmt, int64_t v);
```

Formata um número inteiro com sinal de 64 bits utilizando o especificador de formato fornecido.

_Origem: `compiler/src/runtime/strings/strings.c:282`_

### hphl_str_free

```c
void hphl_str_free(char* s);
```

Desaloca um buffer de string alocado.

_Origem: `compiler/src/runtime/strings/strings.c:211`_

### hphl_str_from_bool

```c
char* hphl_str_from_bool(int b);
```

Converte um valor booleano em sua representação em texto ('true' ou 'false').

_Origem: `compiler/src/runtime/strings/strings.c:195`_

### hphl_str_from_char

```c
char* hphl_str_from_char(int c);
```

Converte um código de caractere em uma string de um único caractere.

_Origem: `compiler/src/runtime/strings/strings.c:203`_

### hphl_str_from_float

```c
char* hphl_str_from_float(double v);
```

Converte um número de ponto flutuante em sua representação decimal em texto.

_Origem: `compiler/src/runtime/strings/strings.c:188`_

### hphl_str_from_int

```c
char* hphl_str_from_int(long long v);
```

Converte um inteiro com sinal de 64 bits em sua representação decimal em texto.

_Origem: `compiler/src/runtime/strings/strings.c:174`_

### hphl_str_from_uint

```c
char* hphl_str_from_uint(unsigned long long v);
```

Converte um inteiro sem sinal de 64 bits em sua representação decimal em texto.

_Origem: `compiler/src/runtime/strings/strings.c:181`_

### hphl_str_index_of

```c
int64_t hphl_str_index_of(const char* s, const char* needle);
```

Retorna o índice de base 0 da primeira ocorrência da substring, ou -1 caso não encontrada.

_Origem: `compiler/src/runtime/strings/strings.c:73`_

### hphl_str_last_index_of

```c
int64_t hphl_str_last_index_of(const char* s, const char* needle);
```

Retorna o índice de base 0 da última ocorrência da substring, ou -1 caso não encontrada.

_Origem: `compiler/src/runtime/strings/strings.c:112`_

### hphl_str_len

```c
int64_t hphl_str_len(const char* s);
```

Retorna o comprimento da string em caracteres (ou bytes para ASCII).

_Origem: `compiler/src/runtime/strings/strings.c:60`_

### hphl_str_ord

```c
int64_t hphl_str_ord(const char* s);
```

Retorna o código do primeiro caractere da string.

_Origem: `compiler/src/runtime/strings/strings.c:97`_

### hphl_str_ord_at

```c
int64_t hphl_str_ord_at(const char* s, int64_t i);
```

Retorna o código do caractere no índice especificado na string.

_Origem: `compiler/src/runtime/strings/strings.c:100`_

### hphl_str_pad_left

```c
char* hphl_str_pad_left(const char* s, int64_t n, const char* fill);
```

Preenche a string à esquerda com a string de preenchimento até atingir o comprimento `n`.

_Origem: `compiler/src/runtime/strings/strings.c:231`_

### hphl_str_pad_right

```c
char* hphl_str_pad_right(const char* s, int64_t n, const char* fill);
```

Preenche a string à direita com a string de preenchimento até atingir o comprimento `n`.

_Origem: `compiler/src/runtime/strings/strings.c:246`_

### hphl_str_starts

```c
int64_t hphl_str_starts(const char* s, const char* pre);
```

Retorna 1 se a string iniciar com o prefixo especificado, ou 0 caso contrário.

_Origem: `compiler/src/runtime/strings/strings.c:126`_

### hphl_str_sub

```c
char* hphl_str_sub(const char* s, int64_t start, int64_t n);
```

Retorna uma substring iniciando no índice `start` com comprimento `n`.

_Origem: `compiler/src/runtime/strings/strings.c:61`_

### hphl_str_trim

```c
char* hphl_str_trim(const char* s);
```

Retorna uma cópia da string sem espaços em branco no início e no final.

_Origem: `compiler/src/runtime/strings/strings.c:152`_

### hphl_str_up

```c
char* hphl_str_up(const char* s);
```

Retorna uma cópia da string com todos os caracteres convertidos para maiúsculas.

_Origem: `compiler/src/runtime/strings/strings.c:136`_
