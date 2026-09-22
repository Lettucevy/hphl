# -*- coding: utf-8 -*-
"""Bilingual descriptions (EN and PT) for all HP-HL Runtime C API functions."""

DESCRIPTIONS = {
    # Subsystem: libc (libm)
    "fmin": {
        "en": "C99 math library function, used directly by the `min(float, float)` builtin.",
        "pt": "Função da biblioteca matemática C99, utilizada diretamente pelo builtin `min(float, float)`."
    },
    "fmax": {
        "en": "C99 math library function, used directly by the `max(float, float)` builtin.",
        "pt": "Função da biblioteca matemática C99, utilizada diretamente pelo builtin `max(float, float)`."
    },

    # Subsystem: alloc
    "hphl_arena_alloc": {
        "en": "Allocates a contiguous chunk of `n` bytes from the bump-pointer arena without per-object GC overhead.",
        "pt": "Aloca um bloco contíguo de `n` bytes na arena de ponteiro linear (bump-pointer) sem sobrecarga de GC por objeto."
    },
    "hphl_arena_free_all": {
        "en": "Frees all memory previously allocated within the specified arena in a single constant-time operation.",
        "pt": "Libera toda a memória alocada na arena especificada em uma única operação de tempo constante."
    },
    "hphl_arena_reset": {
        "en": "Resets the arena allocation pointer back to the start of its buffer, invalidating previous allocations without unmapping pages.",
        "pt": "Reinicia o ponteiro de alocação da arena para o início do buffer, invalidando alocações anteriores sem desmapear páginas da memória."
    },
    "hphl_pool_alloc": {
        "en": "Allocates a fixed-size memory block from the thread-safe pool allocator.",
        "pt": "Aloca um bloco de memória de tamanho fixo a partir do alocador de pool seguro para threads."
    },
    "hphl_pool_free": {
        "en": "Returns a previously allocated block back to the memory pool allocator.",
        "pt": "Devolve um bloco previamente alocado ao alocador de pool de memória."
    },

    # Subsystem: collections
    "hphl_join": {
        "en": "Concatenates all string elements of a dynamic list into a single string separated by the given delimiter.",
        "pt": "Concatena todos os elementos de texto de uma lista dinâmica em uma única string separada pelo delimitador fornecido."
    },
    "hphl_list_add_f": {
        "en": "Appends a 64-bit floating-point value to the end of a dynamic list.",
        "pt": "Adiciona um valor de ponto flutuante de 64 bits ao final de uma lista dinâmica."
    },
    "hphl_list_add_i": {
        "en": "Appends a 64-bit signed integer value to the end of a dynamic list.",
        "pt": "Adiciona um valor inteiro com sinal de 64 bits ao final de uma lista dinâmica."
    },
    "hphl_list_check": {
        "en": "Validates that an element index is within dynamic list bounds, raising a runtime panic if out of range.",
        "pt": "Valida se o índice fornecido está dentro dos limites da lista dinâmica, disparando pânico de execução se estiver fora do intervalo."
    },
    "hphl_list_data": {
        "en": "Returns the raw memory pointer to the element storage buffer of the dynamic list, or NULL if empty.",
        "pt": "Retorna o ponteiro de memória bruta para o buffer de armazenamento de elementos da lista dinâmica, ou NULL se vazia."
    },
    "hphl_list_free": {
        "en": "Deallocates memory used by the dynamic list and releases internal storage buffers.",
        "pt": "Desaloca a memória utilizada pela lista dinâmica e libera seus buffers internos de armazenamento."
    },
    "hphl_list_len": {
        "en": "Returns the number of elements currently stored in the dynamic list.",
        "pt": "Retorna o número de elementos atualmente armazenados na lista dinâmica."
    },
    "hphl_list_new": {
        "en": "Allocates and initializes a new empty dynamic list with default initial capacity.",
        "pt": "Aloca e inicializa uma nova lista dinâmica vazia com a capacidade inicial padrão."
    },
    "hphl_list_slice": {
        "en": "Creates a new shallow copy of a subslice of the list starting from index `from` spanning `count` elements.",
        "pt": "Cria uma nova fatia rasa (shallow copy) da lista a partir do índice `from` contendo `count` elementos."
    },
    "hphl_list_with_cap": {
        "en": "Allocates and initializes a new dynamic list with an explicit pre-allocated capacity `n` to avoid reallocations during append operations.",
        "pt": "Aloca e inicializa uma nova lista dinâmica com capacidade inicial pré-alocada `n`, evitando realocações durante inserções."
    },
    "hphl_map_clear": {
        "en": "Removes all key-value entries from the hash map.",
        "pt": "Remove todas as entradas de chave e valor do mapa hash."
    },
    "hphl_map_contains": {
        "en": "Returns 1 if the hash map contains an entry for the specified key, 0 otherwise.",
        "pt": "Retorna 1 se o mapa hash contiver uma entrada para a chave especificada, ou 0 caso contrário."
    },
    "hphl_map_free": {
        "en": "Deallocates the hash map and releases all internal bucket memory.",
        "pt": "Desaloca o mapa hash e libera toda a memória interna dos buckets."
    },
    "hphl_map_get": {
        "en": "Retrieves the value associated with the specified key, or 0 if not present.",
        "pt": "Recupera o valor associado à chave especificada, ou 0 caso ela não esteja presente."
    },
    "hphl_map_len": {
        "en": "Returns the number of key-value pairs currently stored in the hash map.",
        "pt": "Retorna o número de pares chave-valor atualmente armazenados no mapa hash."
    },
    "hphl_map_new": {
        "en": "Allocates and initializes a new empty hash map configured for the specified key kind with quadratic probing.",
        "pt": "Aloca e inicializa um novo mapa hash vazio configurado para o tipo de chave especificado usando sondagem quadrática."
    },
    "hphl_map_put": {
        "en": "Inserts or updates a key-value mapping in the hash map.",
        "pt": "Insere ou atualiza um mapeamento de chave e valor no mapa hash."
    },
    "hphl_map_remove": {
        "en": "Removes the key-value mapping for the specified key from the hash map.",
        "pt": "Remove o mapeamento de chave e valor da chave especificada no mapa hash."
    },
    "hphl_write_lines": {
        "en": "Writes each string in the provided dynamic list as a separate line to the specified file path.",
        "pt": "Escreve cada string da lista dinâmica fornecida como uma linha separada no caminho de arquivo especificado."
    },

    # Subsystem: concurrency
    "hphl_atomic_add_i64": {
        "en": "Atomically adds a 64-bit integer to the target memory location and returns the previous value.",
        "pt": "Adiciona atomicamente um inteiro de 64 bits ao endereço de memória de destino e retorna o valor anterior."
    },
    "hphl_atomic_cas_i64": {
        "en": "Atomically compares the value at target address with expected, replacing it with desired if equal (compare-and-swap).",
        "pt": "Compara atomicamente o valor no endereço de destino com o esperado, substituindo-o pelo desejado se forem iguais (compare-and-swap)."
    },
    "hphl_atomic_load_i64": {
        "en": "Atomically reads and returns a 64-bit integer from the target memory location with acquire memory ordering.",
        "pt": "Lê e retorna atomicamente um inteiro de 64 bits do endereço de memória de destino com ordenação de memória acquire."
    },
    "hphl_atomic_store_i64": {
        "en": "Atomically stores a 64-bit integer value into the target memory location with release memory ordering.",
        "pt": "Armazena atomicamente um valor inteiro de 64 bits no endereço de memória de destino com ordenação de memória release."
    },
    "hphl_atomic_sub_i64": {
        "en": "Atomically subtracts a 64-bit integer from the target memory location and returns the previous value.",
        "pt": "Subtrai atomicamente um inteiro de 64 bits do endereço de memória de destino e retorna o valor anterior."
    },
    "hphl_barrier_destroy": {
        "en": "Destroys a thread synchronization barrier and releases underlying OS synchronization primitives.",
        "pt": "Destrói uma barreira de sincronização de threads e libera os recursos subjacentes do sistema operacional."
    },
    "hphl_barrier_new": {
        "en": "Initializes a synchronization barrier configured for a fixed number of participating threads.",
        "pt": "Inicializa uma barreira de sincronização configurada para um número fixo de threads participantes."
    },
    "hphl_barrier_wait": {
        "en": "Blocks the calling thread until all participating threads reach the synchronization barrier.",
        "pt": "Bloqueia a thread chamadora até que todas as threads participantes alcancem a barreira de sincronização."
    },
    "hphl_cancel_task": {
        "en": "Requests cooperative cancellation of a background task by setting its cancellation flag.",
        "pt": "Solicita o cancelamento cooperativo de uma tarefa em segundo plano ativando seu sinalizador de cancelamento."
    },
    "hphl_channel_new": {
        "en": "Creates a new thread-safe, typed message passing channel with lock-free ring buffer.",
        "pt": "Cria um novo canal de passagem de mensagens tipado e seguro para threads com buffer circular sem travas."
    },
    "hphl_channel_receive": {
        "en": "Receives and dequeues a message payload from the channel, blocking if the channel is empty.",
        "pt": "Recebe e desenfileira um elemento de mensagem do canal, bloqueando se o canal estiver vazio."
    },
    "hphl_channel_send": {
        "en": "Enqueues a message payload onto the channel and notifies waiting reader threads.",
        "pt": "Enfileira um elemento de mensagem no canal e notifica threads leitoras em espera."
    },
    "hphl_condvar_broadcast": {
        "en": "Wakes all threads currently waiting on the specified condition variable.",
        "pt": "Acorda todas as threads atualmente em espera na variável de condição especificada."
    },
    "hphl_condvar_destroy": {
        "en": "Destroys the condition variable and frees associated OS resources.",
        "pt": "Destrói a variável de condição e libera os recursos associados do sistema operacional."
    },
    "hphl_condvar_new": {
        "en": "Allocates and initializes a new condition variable.",
        "pt": "Aloca e inicializa uma nova variável de condição."
    },
    "hphl_condvar_signal": {
        "en": "Wakes at least one thread currently waiting on the specified condition variable.",
        "pt": "Acorda pelo menos uma thread atualmente em espera na variável de condição especificada."
    },
    "hphl_condvar_wait": {
        "en": "Atomically releases the associated mutex and suspends execution until signaled.",
        "pt": "Libera atomicamente o mutex associado e suspende a execução da thread até receber sinal."
    },
    "hphl_event_destroy": {
        "en": "Destroys the synchronization event handle and releases kernel resources.",
        "pt": "Destrói o manipulador de evento de sincronização e libera os recursos do kernel."
    },
    "hphl_event_new": {
        "en": "Creates a manual or auto-reset synchronization event.",
        "pt": "Cria um evento de sincronização manual ou de redefinição automática."
    },
    "hphl_event_reset": {
        "en": "Resets the synchronization event to an unsignaled state.",
        "pt": "Redefine o evento de sincronização para o estado não sinalizado."
    },
    "hphl_event_set": {
        "en": "Sets the event to a signaled state, unblocking waiting threads.",
        "pt": "Define o evento para o estado sinalizado, desbloqueando threads em espera."
    },
    "hphl_event_wait": {
        "en": "Waits for the synchronization event to become signaled.",
        "pt": "Aguarda até que o evento de sincronização seja sinalizado."
    },
    "hphl_join_tasks": {
        "en": "Waits for all active thread pool tasks to finish before proceeding.",
        "pt": "Aguarda a conclusão de todas as tarefas ativas do pool de threads antes de prosseguir."
    },
    "hphl_lock_begin": {
        "en": "Enters a reentrant critical section lock.",
        "pt": "Entra em uma seção crítica reentrante (lock)."
    },
    "hphl_lock_end": {
        "en": "Exits a reentrant critical section lock.",
        "pt": "Sai de uma seção crítica reentrante (unlock)."
    },
    "hphl_mutex_destroy": {
        "en": "Destroys a mutual exclusion lock and frees underlying kernel handles.",
        "pt": "Destrói um bloqueio de exclusão mútua (mutex) e libera os manipuladores do kernel subjacentes."
    },
    "hphl_mutex_lock": {
        "en": "Acquires an exclusive lock on the mutex, blocking if currently locked by another thread.",
        "pt": "Adquire bloqueio exclusivo no mutex, bloqueando se estiver atualmente travado por outra thread."
    },
    "hphl_mutex_new": {
        "en": "Allocates and initializes a new recursive mutex.",
        "pt": "Aloca e inicializa um novo mutex recursivo."
    },
    "hphl_mutex_unlock": {
        "en": "Releases the exclusive lock on the specified mutex.",
        "pt": "Libera o bloqueio exclusivo no mutex especificado."
    },
    "hphl_region_begin": {
        "en": "Opens a parallel structured region boundary.",
        "pt": "Abre o limite de uma região estruturada paralela."
    },
    "hphl_region_join": {
        "en": "Synchronizes and waits for all tasks within the current parallel region boundary to complete.",
        "pt": "Sincroniza e aguarda a conclusão de todas as tarefas dentro do escopo da região paralela atual."
    },
    "hphl_semaphore_destroy": {
        "en": "Destroys a counting semaphore and releases OS resources.",
        "pt": "Destrói um semáforo contador e libera os recursos do sistema operacional."
    },
    "hphl_semaphore_new": {
        "en": "Creates a counting semaphore with the specified initial and maximum counts.",
        "pt": "Cria um semáforo contador com a contagem inicial e máxima especificadas."
    },
    "hphl_semaphore_signal": {
        "en": "Increments the counting semaphore value, waking waiting threads.",
        "pt": "Incrementa o valor do semáforo contador, acordando threads em espera."
    },
    "hphl_semaphore_wait": {
        "en": "Decrements the counting semaphore value, blocking if the count is zero.",
        "pt": "Decrementa o valor do semáforo contador, bloqueando se a contagem for zero."
    },
    "hphl_spawn_task_ex": {
        "en": "Spawns a new task onto the thread pool work-stealing queue with execution environment and result storage.",
        "pt": "Enfileira uma nova tarefa na fila de work-stealing do pool de threads com ambiente de execução e armazenamento de retorno."
    },
    "hphl_task_iscancelled": {
        "en": "Checks if cancellation has been requested for the currently executing task.",
        "pt": "Verifica se o cancelamento foi solicitado para a tarefa em execução no momento."
    },
    "hphl_track_barrier": {
        "en": "Registers a barrier with the runtime leak detection and tracking registry.",
        "pt": "Registra uma barreira no sistema de rastreamento de vazamentos de recursos do runtime."
    },
    "hphl_track_event": {
        "en": "Registers an event with the runtime leak detection and tracking registry.",
        "pt": "Registra um evento no sistema de rastreamento de vazamentos de recursos do runtime."
    },
    "hphl_track_semaphore": {
        "en": "Registers a semaphore with the runtime leak detection and tracking registry.",
        "pt": "Registra um semáforo no sistema de rastreamento de vazamentos de recursos do runtime."
    },
    "hphl_untrack_barrier": {
        "en": "Removes a barrier from the runtime leak detection registry.",
        "pt": "Remove uma barreira do registro de detecção de vazamentos do runtime."
    },
    "hphl_untrack_event": {
        "en": "Removes an event from the runtime leak detection registry.",
        "pt": "Remove um evento do registro de detecção de vazamentos do runtime."
    },
    "hphl_untrack_semaphore": {
        "en": "Removes a semaphore from the runtime leak detection registry.",
        "pt": "Remove um semáforo do registro de detecção de vazamentos do runtime."
    },
    "hphl_wait_task": {
        "en": "Blocks until the specified task handle completes, assisting worker queue execution while waiting.",
        "pt": "Bloqueia até que a tarefa especificada termine, auxiliando na execução da fila de tarefas durante a espera."
    },
    "hphl_worker_count": {
        "en": "Returns the current number of active worker threads in the runtime thread pool.",
        "pt": "Retorna o número atual de threads trabalhadoras ativas no pool de threads do runtime."
    },

    # Subsystem: core
    "hphl_abort": {
        "en": "Aborts process execution immediately with an error diagnostic and abnormal termination code.",
        "pt": "Interrompe a execução do processo imediatamente com diagnóstico de erro e código de terminação anormal."
    },
    "hphl_abs_f64": {
        "en": "Returns the absolute value of a 64-bit floating-point number.",
        "pt": "Retorna o valor absoluto de um número de ponto flutuante de 64 bits."
    },
    "hphl_abs_i64": {
        "en": "Returns the absolute value of a 64-bit signed integer.",
        "pt": "Retorna o valor absoluto de um inteiro com sinal de 64 bits."
    },
    "hphl_acos": {
        "en": "Computes the principal arc cosine of a floating-point value in radians.",
        "pt": "Calcula o arco cosseno principal de um valor de ponto flutuante em radianos."
    },
    "hphl_append_file": {
        "en": "Appends string data to the end of a file at the specified path.",
        "pt": "Acrescenta dados de texto ao final do arquivo no caminho especificado."
    },
    "hphl_args": {
        "en": "Returns a dynamic list containing the command-line argument strings passed to the program.",
        "pt": "Retorna uma lista dinâmica contendo os argumentos de linha de comando passados ao programa."
    },
    "hphl_asin": {
        "en": "Computes the principal arc sine of a floating-point value in radians.",
        "pt": "Calcula o arco seno principal de um valor de ponto flutuante em radianos."
    },
    "hphl_assert": {
        "en": "Evaluates a boolean condition and raises a runtime assertion panic with the given message if false.",
        "pt": "Avalia uma condição booleana e dispara pânico de asserção em tempo de execução se for falsa."
    },
    "hphl_async_yield": {
        "en": "Cooperatively yields execution of the current coroutine or green task to allow other tasks to run.",
        "pt": "Cede cooperativamente a execução da corrotina ou tarefa atual permitindo o escalonamento de outras tarefas."
    },
    "hphl_atan": {
        "en": "Computes the principal arc tangent of a floating-point value in radians.",
        "pt": "Calcula o arco tangente principal de um valor de ponto flutuante em radianos."
    },
    "hphl_atan2": {
        "en": "Computes the arc tangent of y/x using the signs of both arguments to determine the quadrant.",
        "pt": "Calcula o arco tangente de y/x utilizando os sinais dos argumentos para determinar o quadrante correto."
    },
    "hphl_bounds_check": {
        "en": "Validates that an array or memory buffer index is within valid bounds [0, size - 1].",
        "pt": "Valida se um índice de array ou buffer está dentro dos limites válidos [0, tamanho - 1]."
    },
    "hphl_box_i64": {
        "en": "Boxes a primitive 64-bit signed integer into a heap-allocated managed object.",
        "pt": "Empacota (box) um inteiro primitivo de 64 bits em um objeto gerenciado alocado no heap."
    },
    "hphl_clock_ns": {
        "en": "Returns the current high-resolution monotonic time in nanoseconds.",
        "pt": "Retorna o tempo monotônico atual de alta resolução em nanossegundos."
    },
    "hphl_coop_run": {
        "en": "Executes the cooperative scheduler event loop until all scheduled cooperative tasks complete.",
        "pt": "Executa o laço do escalonador cooperativo até que todas as tarefas agendadas terminem."
    },
    "hphl_coop_spawn": {
        "en": "Spawns a new lightweight cooperative task function with the specified environment context.",
        "pt": "Cria e agenda uma nova tarefa leve cooperativa com o contexto de ambiente especificado."
    },
    "hphl_cos": {
        "en": "Computes the cosine of an angle expressed in radians.",
        "pt": "Calcula o cosseno de um ângulo fornecido em radianos."
    },
    "hphl_crash_handler": {
        "en": "Global operating system crash and exception handler that dumps stack traces on fatal hardware faults.",
        "pt": "Manipulador global de falhas do sistema operacional que gera diagnósticos de pilha em falhas fatais."
    },
    "hphl_divide_by_zero": {
        "en": "Triggers a runtime panic diagnostic when a division by zero occurs.",
        "pt": "Dispara diagnóstico de pânico em tempo de execução quando ocorre uma divisão por zero."
    },
    "hphl_env_var": {
        "en": "Retrieves the value of an environment variable as a string, or returns an empty string if unset.",
        "pt": "Obtém o valor de uma variável de ambiente como string, ou retorna string vazia caso não definida."
    },
    "hphl_exc_begin": {
        "en": "Initializes a structured exception frame record before entering a protected try block.",
        "pt": "Inicializa um registro de quadro de exceção estruturada antes de entrar em um bloco try protegido."
    },
    "hphl_exc_end": {
        "en": "Unwinds and tears down the top exception handler frame upon leaving a try block.",
        "pt": "Desfaz e encerra o quadro de manipulador de exceção do topo ao sair de um bloco try."
    },
    "hphl_exc_free": {
        "en": "Deallocates an active exception wrapper object.",
        "pt": "Desaloca um objeto de encapsulamento de exceção ativo."
    },
    "hphl_exc_new": {
        "en": "Allocates a new empty exception structure.",
        "pt": "Aloca uma nova estrutura de exceção."
    },
    "hphl_exc_payload": {
        "en": "Extracts the payload object pointer associated with an in-flight exception.",
        "pt": "Extrai o ponteiro do objeto de payload associado a uma exceção em andamento."
    },
    "hphl_exc_push": {
        "en": "Pushes an exception landing pad frame onto the thread-local exception stack.",
        "pt": "Empilha um quadro de pouso (landing pad) de exceção na pilha local da thread."
    },
    "hphl_exc_restore": {
        "en": "Restores CPU registers and stack pointer to resume execution in the matching catch handler.",
        "pt": "Restaura os registradores e a pilha da CPU para retomar a execução no bloco catch correspondente."
    },
    "hphl_exit_prog": {
        "en": "Terminates the program normally and returns the specified exit code to the operating system.",
        "pt": "Encerra o programa normalmente e retorna o código de saída especificado ao sistema operacional."
    },
    "hphl_exp": {
        "en": "Computes Euler's number e raised to the given power (e^x).",
        "pt": "Calcula a constante de Euler e elevada à potência fornecida (e^x)."
    },
    "hphl_exp2": {
        "en": "Computes 2 raised to the given power (2^x).",
        "pt": "Calcula 2 elevado à potência fornecida (2^x)."
    },
    "hphl_file_exists": {
        "en": "Returns 1 if a file or directory exists at the specified path, 0 otherwise.",
        "pt": "Retorna 1 se um arquivo ou diretório existir no caminho especificado, ou 0 caso contrário."
    },
    "hphl_float_overflow_check": {
        "en": "Verifies that a floating-point value is not infinite or NaN, raising a runtime overflow error if invalid.",
        "pt": "Verifica se um valor de ponto flutuante não é infinito nem NaN, disparando erro de overflow se for inválido."
    },
    "hphl_floor": {
        "en": "Returns the largest integer value not greater than the input floating-point number.",
        "pt": "Retorna o maior valor inteiro que não seja maior que o número de ponto flutuante de entrada."
    },
    "hphl_fmod": {
        "en": "Computes the floating-point remainder of dividing a by b.",
        "pt": "Calcula o resto da divisão de ponto flutuante entre a e b."
    },
    "hphl_format_date": {
        "en": "Formats a Unix epoch timestamp into a string representation according to the format specifier.",
        "pt": "Formata um carimbo de data/hora (timestamp Unix) em string de acordo com o especificador de formato."
    },
    "hphl_gas_str": {
        "en": "Normalizes an assembly/runtime string literal into an HP-HL managed string.",
        "pt": "Normaliza um literal de string do assembly/runtime em uma string gerenciada do HP-HL."
    },
    "hphl_init_args": {
        "en": "Stores command-line argc and argv into global runtime storage for later retrieval.",
        "pt": "Armazena os argumentos argc e argv da linha de comando no armazenamento global do runtime."
    },
    "hphl_install_crash_handler": {
        "en": "Installs operating system hardware exception and signal handlers to report fatal errors.",
        "pt": "Instala manipuladores de exceções de hardware do sistema operacional para relatar falhas fatais."
    },
    "hphl_json_get": {
        "en": "Retrieves a JSON property value by key path from a parsed JSON document.",
        "pt": "Recupera o valor de uma propriedade JSON pelo caminho da chave em um documento JSON."
    },
    "hphl_json_parse": {
        "en": "Parses a JSON string into an in-memory document tree representation.",
        "pt": "Analisa sintaticamente uma string JSON em uma árvore de documento na memória."
    },
    "hphl_json_set": {
        "en": "Sets or updates a property value at the specified key path in a JSON document.",
        "pt": "Define ou atualiza o valor de uma propriedade no caminho especificado em um documento JSON."
    },
    "hphl_json_stringify": {
        "en": "Serializes an in-memory JSON document structure into a formatted string.",
        "pt": "Serializa uma estrutura de documento JSON da memória em uma string formatada."
    },
    "hphl_list_dir": {
        "en": "Returns a dynamic list of file and directory names located within the specified folder path.",
        "pt": "Retorna uma lista dinâmica com os nomes dos arquivos e diretórios localizados na pasta especificada."
    },
    "hphl_log": {
        "en": "Computes the natural (base-e) logarithm of a floating-point number.",
        "pt": "Calcula o logaritmo natural (base e) de um número de ponto flutuante."
    },
    "hphl_log2": {
        "en": "Computes the binary (base-2) logarithm of a floating-point number.",
        "pt": "Calcula o logaritmo binário (base 2) de um número de ponto flutuante."
    },
    "hphl_match_fail": {
        "en": "Triggers a runtime pattern matching exhaustion panic when no pattern branch matches.",
        "pt": "Dispara pânico de exaustão de pattern matching em tempo de execução quando nenhum padrão coincide."
    },
    "hphl_mkdir": {
        "en": "Creates a new directory at the specified filesystem path, returning 1 on success or 0 on failure.",
        "pt": "Cria um novo diretório no caminho de sistema de arquivos especificado, retornando 1 em caso de sucesso ou 0 em caso de falha."
    },
    "hphl_now": {
        "en": "Returns the current Unix epoch timestamp in seconds.",
        "pt": "Retorna o carimbo de data/hora atual da época Unix em segundos."
    },
    "hphl_now_ms": {
        "en": "Returns the current Unix epoch timestamp in milliseconds.",
        "pt": "Retorna o carimbo de data/hora atual da época Unix em milissegundos."
    },
    "hphl_now_us": {
        "en": "Returns the current Unix epoch timestamp in microseconds.",
        "pt": "Retorna o carimbo de data/hora atual da época Unix em microssegundos."
    },
    "hphl_overflow_check": {
        "en": "Validates that a 64-bit integer is within [min, max] bounds, raising an overflow error if violated.",
        "pt": "Valida se um inteiro de 64 bits está dentro dos limites [min, max], disparando erro de overflow se violado."
    },
    "hphl_panic": {
        "en": "Halts program execution immediately and prints a diagnostic panic message to stderr.",
        "pt": "Interrompe imediatamente a execução do programa e imprime uma mensagem de pânico no stderr."
    },
    "hphl_parse_date": {
        "en": "Parses a formatted date string using the specified format pattern into a Unix timestamp in seconds.",
        "pt": "Analisa uma string de data formatada usando o padrão especificado e retorna o timestamp Unix em segundos."
    },
    "hphl_parse_f64": {
        "en": "Parses a 64-bit floating-point number from a string.",
        "pt": "Converte uma string em um número de ponto flutuante de 64 bits."
    },
    "hphl_parse_int": {
        "en": "Parses a 64-bit signed integer from a string.",
        "pt": "Converte uma string em um número inteiro com sinal de 64 bits."
    },
    "pow": {
        "en": "Computes base raised to the specified exponent (base^exponent).",
        "pt": "Calcula a base elevada ao expoente especificado (base^expoente)."
    },
    "hphl_pow": {
        "en": "Computes base raised to the specified exponent (base^exponent).",
        "pt": "Calcula a base elevada ao expoente especificado (base^expoente)."
    },
    "hphl_print_bool": {
        "en": "Prints a boolean value ('true' or 'false') to standard output.",
        "pt": "Imprime um valor booleano ('true' ou 'false') na saída padrão."
    },
    "hphl_print_char": {
        "en": "Prints a single character to standard output.",
        "pt": "Imprime um único caractere na saída padrão."
    },
    "hphl_print_float": {
        "en": "Prints a 64-bit floating-point number formatted to standard output.",
        "pt": "Imprime um número de ponto flutuante de 64 bits formatado na saída padrão."
    },
    "hphl_print_int": {
        "en": "Prints a 64-bit signed integer in decimal format to standard output.",
        "pt": "Imprime um inteiro com sinal de 64 bits em formato decimal na saída padrão."
    },
    "hphl_print_string": {
        "en": "Prints a null-terminated string to standard output.",
        "pt": "Imprime uma string terminada em nulo na saída padrão."
    },
    "hphl_print_uint": {
        "en": "Prints an unsigned 64-bit integer in decimal format to standard output.",
        "pt": "Imprime um inteiro sem sinal de 64 bits em formato decimal na saída padrão."
    },
    "hphl_random": {
        "en": "Generates a pseudo-random floating-point number uniformly distributed in [0.0, 1.0).",
        "pt": "Gera um número pseudoaleatório de ponto flutuante uniformemente distribuído no intervalo [0.0, 1.0)."
    },
    "hphl_random_int": {
        "en": "Generates a cryptographically seeded pseudo-random integer in the range [lo, hi] inclusive.",
        "pt": "Gera um número inteiro pseudoaleatório no intervalo [lo, hi] inclusive."
    },
    "hphl_read_file": {
        "en": "Reads the entire contents of a file at the specified path into a newly allocated string buffer.",
        "pt": "Lê todo o conteúdo de um arquivo no caminho especificado para um novo buffer de texto."
    },
    "hphl_read_line": {
        "en": "Reads a single line of text from standard input (stdin) until newline or EOF.",
        "pt": "Lê uma única linha de texto da entrada padrão (stdin) até a nova linha ou EOF."
    },
    "hphl_read_lines": {
        "en": "Reads all lines from the specified file path into a dynamic list of strings.",
        "pt": "Lê todas as linhas do arquivo especificado retornando uma lista dinâmica de strings."
    },
    "hphl_remove_file": {
        "en": "Deletes the file at the specified filesystem path, returning 1 on success or 0 on failure.",
        "pt": "Exclui o arquivo no caminho de sistema de arquivos especificado, retornando 1 em caso de sucesso ou 0 em caso de falha."
    },
    "hphl_runtime_abi_version": {
        "en": "Returns the ABI version integer of the HP-HL compiled runtime library.",
        "pt": "Retorna o número inteiro da versão da ABI da biblioteca de runtime compilada do HP-HL."
    },
    "hphl_set_env": {
        "en": "Sets or updates an environment variable in the process environment.",
        "pt": "Define ou atualiza uma variável de ambiente no ambiente do processo."
    },
    "hphl_sin": {
        "en": "Computes the sine of an angle expressed in radians.",
        "pt": "Calcula o seno de um ângulo fornecido em radianos."
    },
    "hphl_sleep_ms": {
        "en": "Suspends execution of the calling thread for the specified duration in milliseconds.",
        "pt": "Suspende a execução da thread chamadora pela duração especificada em milissegundos."
    },
    "hphl_stat": {
        "en": "Returns metadata information about a file (size, mode, timestamps) as a formatted string.",
        "pt": "Retorna metadados sobre um arquivo (tamanho, modo, carimbos de data) como string formatada."
    },
    "hphl_str_cmp": {
        "en": "Compares two strings lexicographically, returning negative if a < b, 0 if equal, and positive if a > b.",
        "pt": "Compara lexicograficamente duas strings, retornando negativo se a < b, 0 se iguais, e positivo se a > b."
    },
    "hphl_str_eq": {
        "en": "Compares two strings for byte-level equality, returning 1 if identical or 0 otherwise.",
        "pt": "Compara a igualdade de bytes entre duas strings, retornando 1 se forem idênticas ou 0 caso contrário."
    },
    "hphl_str_replace": {
        "en": "Returns a new string with all occurrences of `from` replaced by `to`.",
        "pt": "Retorna uma nova string com todas as ocorrências de `from` substituídas por `to`."
    },
    "hphl_struct_copy": {
        "en": "Allocates memory and performs a bitwise copy of a struct of `n` bytes.",
        "pt": "Aloca memória e realiza uma cópia binária de uma struct de `n` bytes."
    },
    "hphl_tan": {
        "en": "Computes the tangent of an angle expressed in radians.",
        "pt": "Calcula a tangente de um ângulo fornecido em radianos."
    },
    "hphl_throw": {
        "en": "Throws an exception containing the specified payload object, initiating stack unwinding.",
        "pt": "Lança uma exceção contendo o objeto de payload especificado, iniciando o desenrolamento da pilha (stack unwinding)."
    },
    "hphl_tls_block": {
        "en": "Returns the base pointer of the current thread's thread-local storage (TLS) block.",
        "pt": "Retorna o ponteiro base do bloco de armazenamento local da thread atual (TLS)."
    },
    "hphl_tls_setup": {
        "en": "Allocates and initializes the thread-local storage area of `bytes` size for the calling thread.",
        "pt": "Aloca e inicializa a área de armazenamento local de thread (TLS) com o tamanho especificado para a thread chamadora."
    },
    "hphl_trunc": {
        "en": "Truncates a floating-point value towards zero to its nearest integral value.",
        "pt": "Trunca um valor de ponto flutuante em direção a zero para o valor inteiro mais próximo."
    },
    "hphl_unbox_i64": {
        "en": "Unboxes a 64-bit integer from a heap-allocated managed object.",
        "pt": "Desempacota (unbox) um inteiro de 64 bits de um objeto gerenciado alocado no heap."
    },
    "hphl_write_file": {
        "en": "Writes string content to a file at the specified path, replacing any existing file.",
        "pt": "Grava o conteúdo de texto em um arquivo no caminho especificado, substituindo qualquer arquivo existente."
    },

    # Subsystem: debug
    "hphl_dbg_enter": {
        "en": "Notifies debugger runtime of function entry by function identifier.",
        "pt": "Notifica o runtime do depurador sobre a entrada em uma função pelo identificador da função."
    },
    "hphl_dbg_enter_frame": {
        "en": "Records function entry and stack frame base pointer for the interactive debugger.",
        "pt": "Registra a entrada na função e o ponteiro base do quadro de pilha para o depurador interativo."
    },
    "hphl_dbg_enter_impl": {
        "en": "Low-level implementation helper for debugger function entry tracking.",
        "pt": "Auxiliar de implementação de baixo nível para rastreamento de entrada em funções no depurador."
    },
    "hphl_dbg_exc_report": {
        "en": "Reports an unhandled exception or crash condition to attached debuggers.",
        "pt": "Relata uma exceção não tratada ou condição de falha crítica aos depuradores conectados."
    },
    "hphl_dbg_leave": {
        "en": "Notifies debugger runtime that the current function execution frame is exiting.",
        "pt": "Notifica o runtime do depurador de que o quadro de execução da função atual está sendo encerrado."
    },
    "hphl_dbg_trap": {
        "en": "Debugger trap hook triggered when reaching a source line breakpoint.",
        "pt": "Gancho de interceptação (trap) do depurador acionado ao atingir um breakpoint de linha de código."
    },
    "hphl_dbg_trap_impl": {
        "en": "Low-level implementation helper for debugger line traps and inspectable register frames.",
        "pt": "Auxiliar de implementação de baixo nível para traps de depurador e quadros de registradores inspecionáveis."
    },

    # Subsystem: gc
    "hphl_gc": {
        "en": "Forces an immediate complete garbage collection cycle.",
        "pt": "Força a execução imediata de um ciclo completo de coleta de lixo (garbage collection)."
    },
    "hphl_gc_add_root": {
        "en": "Registers an address slot as a GC root reference so it is preserved during collections.",
        "pt": "Registra um slot de endereço como raiz do GC para preservá-lo durante as coletas."
    },
    "hphl_gc_add_roots_batch": {
        "en": "Registers an array of root pointer slots in a single batch operation.",
        "pt": "Registra um conjunto de slots de ponteiros raiz em uma única operação em lote."
    },
    "hphl_gc_cards_clear": {
        "en": "Clears the generational GC card table markers.",
        "pt": "Limpa os marcadores da tabela de cartões (card table) do GC geracional."
    },
    "hphl_gc_cards_scan": {
        "en": "Scans dirty generational card table entries to mark inter-generational object pointers.",
        "pt": "Examina as entradas modificadas da tabela de cartões para rastrear referências entre gerações de objetos."
    },
    "hphl_gc_cleanup": {
        "en": "Shuts down the garbage collector and deallocates all managed heap memory pages.",
        "pt": "Encerra o coletor de lixo e desaloca todas as páginas de memória do heap gerenciado."
    },
    "hphl_gc_cleanup_rem": {
        "en": "Cleans up the remembered set data structures of the generational garbage collector.",
        "pt": "Limpa as estruturas de dados do conjunto lembrado (remembered set) do coletor geracional."
    },
    "hphl_gc_free_managed": {
        "en": "Manually frees a managed object block, bypassing the normal sweep phase.",
        "pt": "Libera manualmente um bloco de objeto gerenciado, contornando a fase normal de varredura."
    },
    "hphl_gc_init": {
        "en": "Initializes the garbage collector runtime structures and memory heaps.",
        "pt": "Inicializa as estruturas do runtime e as páginas de memória do coletor de lixo."
    },
    "hphl_gc_is_managed": {
        "en": "Returns 1 if the pointer belongs to a heap page managed by the GC, 0 otherwise.",
        "pt": "Retorna 1 se o ponteiro pertencer a uma página de heap gerenciada pelo GC, ou 0 caso contrário."
    },
    "hphl_gc_major": {
        "en": "Executes a major (full-heap) garbage collection cycle and returns the number of freed bytes.",
        "pt": "Executa um ciclo de coleta de lixo maior (full heap) e retorna a quantidade de bytes liberados."
    },
    "hphl_gc_minor": {
        "en": "Executes a minor (young-generation nursery) collection cycle and returns the number of freed bytes.",
        "pt": "Executa um ciclo de coleta menor (geração jovem / nursery) e retorna a quantidade de bytes liberados."
    },
    "hphl_gc_pop_roots": {
        "en": "Pops the topmost `n` registered root pointer slots from the thread root stack.",
        "pt": "Remove os `n` slots de ponteiros raiz mais recentes da pilha de raízes da thread."
    },
    "hphl_gc_pressure": {
        "en": "Reports allocation memory pressure to the collector, potentially triggering collection.",
        "pt": "Informa pressão de memória ao coletor, podendo disparar um ciclo de coleta preventivo."
    },
    "hphl_gc_push_stack": {
        "en": "Registers an address range of the execution stack to be scanned conservatively by the GC.",
        "pt": "Registra um intervalo de endereços da pilha de execução para varredura conservadora pelo GC."
    },
    "hphl_gc_register": {
        "en": "Registers a newly allocated object pointer into the young-generation nursery pool.",
        "pt": "Registra um ponteiro de objeto recém-alocado no pool da geração jovem (nursery)."
    },
    "hphl_gc_register_old": {
        "en": "Registers an object directly into the mature tenured heap generation.",
        "pt": "Registra um objeto diretamente na geração madura (tenured) do heap."
    },
    "hphl_gc_register_slots": {
        "en": "Registers object field offsets containing GC pointers for precise struct tracing.",
        "pt": "Registra deslocamentos de campos de um objeto contendo ponteiros do GC para rastreamento exato."
    },
    "hphl_gc_remove_root": {
        "en": "Deregisters a root pointer slot from GC tracking.",
        "pt": "Remove o registro de um slot de ponteiro raiz do rastreamento do GC."
    },
    "hphl_gc_remove_roots_batch": {
        "en": "Deregisters a batch of root pointer slots from GC tracking.",
        "pt": "Remove o registro de um lote de slots de ponteiros raiz do rastreamento do GC."
    },
    "hphl_gc_stats_collections": {
        "en": "Returns the cumulative number of garbage collection cycles executed.",
        "pt": "Retorna o número acumulado de ciclos de coleta de lixo executados."
    },
    "hphl_gc_stats_freed": {
        "en": "Returns the cumulative total number of bytes reclaimed by the garbage collector.",
        "pt": "Retorna o total acumulado de bytes recuperados pelo coletor de lixo."
    },
    "hphl_gc_stats_last_freed": {
        "en": "Returns the number of bytes reclaimed during the most recent collection cycle.",
        "pt": "Retorna a quantidade de bytes recuperados durante o ciclo de coleta mais recente."
    },
    "hphl_gc_stats_max_pause": {
        "en": "Returns the maximum pause duration observed during collection cycles in microseconds.",
        "pt": "Retorna a duração máxima de pausa observada durante ciclos de coleta em microssegundos."
    },
    "hphl_gc_step": {
        "en": "Performs an incremental garbage collection step within the specified work budget.",
        "pt": "Executa uma etapa incremental de coleta de lixo dentro do orçamento de trabalho especificado."
    },
    "hphl_gc_sweep": {
        "en": "Performs the sweep phase of garbage collection, reclaiming unmarked memory blocks.",
        "pt": "Executa a fase de varredura (sweep) do coletor de lixo, recuperando blocos não marcados."
    },
    "hphl_gc_unregister": {
        "en": "Removes an object pointer from active garbage collection management.",
        "pt": "Remove o ponteiro de um objeto do gerenciamento ativo do coletor de lixo."
    },
    "hphl_gc_unregister_slots": {
        "en": "Deregisters struct field pointer offsets previously recorded for precise GC tracing.",
        "pt": "Remove o registro dos deslocamentos de campos previamente gravados para rastreamento exato do GC."
    },
    "hphl_set_class_desc": {
        "en": "Registers a class descriptor bitmask defining which byte offsets contain managed references.",
        "pt": "Registra uma máscara de bits de descritor de classe definindo quais deslocamentos contêm referências gerenciadas."
    },
    "hphl_shared_alloc": {
        "en": "Allocates a reference-counted shared memory object.",
        "pt": "Aloca um objeto de memória compartilhada gerenciado por contagem de referências."
    },
    "hphl_shared_alloc_typed": {
        "en": "Allocates a typed reference-counted shared memory object with the given class identifier.",
        "pt": "Aloca um objeto de memória compartilhada tipado com o identificador de classe fornecido."
    },
    "hphl_shared_release": {
        "en": "Decrements reference count on a shared object, freeing it when count reaches zero.",
        "pt": "Decrementa a contagem de referências de um objeto compartilhado, liberando-o quando atinge zero."
    },
    "hphl_shared_retain": {
        "en": "Increments reference count on a shared object to prevent deallocation.",
        "pt": "Incrementa a contagem de referências de um objeto compartilhado para impedir sua desalocação."
    },
    "hphl_str_alloc": {
        "en": "Allocates a string memory buffer tracked by the garbage collector.",
        "pt": "Aloca um buffer de memória para string rastreado pelo coletor de lixo."
    },
    "hphl_tc_alloc": {
        "en": "Allocates memory from the thread-local allocation cache (TC) fast path.",
        "pt": "Aloca memória a partir do caminho rápido do cache de alocação local da thread (TC)."
    },
    "hphl_tc_free": {
        "en": "Returns memory back to the thread-local allocation cache.",
        "pt": "Devolve memória ao cache de alocação local da thread."
    },
    "hphl_write_barrier": {
        "en": "Generational write barrier updating card table when storing references into existing objects.",
        "pt": "Barreira de escrita geracional que atualiza a tabela de cartões ao armazenar referências em objetos existentes."
    },
    "hphl_write_barrier_slow": {
        "en": "Slow path implementation of the generational write barrier card marking.",
        "pt": "Implementação do caminho lento da barreira de escrita geracional para marcação de cartões."
    },

    # Subsystem: img
    "hphl_img_blur": {
        "en": "Applies a box or Gaussian blur with radius `r` to pixel buffer and returns new pixel buffer.",
        "pt": "Aplica desfoque (blur) com raio `r` a um buffer de pixels e retorna um novo buffer."
    },
    "hphl_img_flip_h": {
        "en": "Flips pixel buffer horizontally and returns a newly allocated flipped image buffer.",
        "pt": "Inverte horizontalmente um buffer de pixels e retorna um novo buffer de imagem invertida."
    },
    "hphl_img_grayscale": {
        "en": "Converts an RGB/RGBA pixel buffer to grayscale intensity values.",
        "pt": "Converte um buffer de pixels RGB/RGBA para tons de cinza."
    },
    "hphl_img_png_save": {
        "en": "Saves a pixel buffer to a PNG image file at the specified path.",
        "pt": "Salva um buffer de pixels como arquivo de imagem PNG no caminho especificado."
    },
    "hphl_img_ppm_load": {
        "en": "Loads an image from a PPM file into a newly allocated pixel buffer.",
        "pt": "Carrega uma imagem a partir de um arquivo PPM para um buffer de pixels recém-alocado."
    },
    "hphl_img_ppm_save": {
        "en": "Saves a pixel buffer to a PPM image file at the specified path.",
        "pt": "Salva um buffer de pixels como arquivo de imagem PPM no caminho especificado."
    },
    "hphl_img_resize": {
        "en": "Resizes an image pixel buffer from (w, h) to (nw, nh) using bilinear interpolation.",
        "pt": "Redimensiona um buffer de pixels de (w, h) para (nw, nh) usando interpolação bilinear."
    },

    # Subsystem: mem
    "hphl_mem_alloc": {
        "en": "Allocates `size` bytes of uninitialized raw heap memory.",
        "pt": "Aloca `size` bytes de memória bruta não inicializada no heap."
    },
    "hphl_mem_copy": {
        "en": "Copies `n` bytes from source buffer to destination buffer.",
        "pt": "Copia `n` bytes do buffer de origem para o buffer de destino."
    },
    "hphl_mem_copy_off": {
        "en": "Copies `n` bytes between memory buffers with explicit source and destination offsets.",
        "pt": "Copia `n` bytes entre buffers de memória com deslocamentos explícitos de origem e destino."
    },
    "hphl_mem_fill": {
        "en": "Fills `n` bytes of destination memory with the specified byte value.",
        "pt": "Preenche `n` bytes da memória de destino com o valor de byte especificado."
    },
    "hphl_mem_free": {
        "en": "Deallocates a block of raw heap memory previously allocated by `hphl_mem_alloc`.",
        "pt": "Desaloca um bloco de memória bruta do heap previamente alocado por `hphl_mem_alloc`."
    },
    "hphl_mem_peek_f32": {
        "en": "Reads a 32-bit floating-point value from raw memory at the specified byte offset.",
        "pt": "Lê um valor de ponto flutuante de 32 bits da memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_peek_f64": {
        "en": "Reads a 64-bit floating-point value from raw memory at the specified byte offset.",
        "pt": "Lê um valor de ponto flutuante de 64 bits da memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_peek_i32": {
        "en": "Reads a signed 32-bit integer from raw memory at the specified byte offset.",
        "pt": "Lê um inteiro com sinal de 32 bits da memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_peek_i64": {
        "en": "Reads a signed 64-bit integer from raw memory at the specified byte offset.",
        "pt": "Lê um inteiro com sinal de 64 bits da memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_peek_ptr": {
        "en": "Reads a pointer value from raw memory at the specified byte offset.",
        "pt": "Lê um ponteiro da memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_peek_u32": {
        "en": "Reads an unsigned 32-bit integer from raw memory at the specified byte offset.",
        "pt": "Lê um inteiro sem sinal de 32 bits da memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_poke_f32": {
        "en": "Writes a 32-bit floating-point value to raw memory at the specified byte offset.",
        "pt": "Escreve um valor de ponto flutuante de 32 bits na memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_poke_f64": {
        "en": "Writes a 64-bit floating-point value to raw memory at the specified byte offset.",
        "pt": "Escreve um valor de ponto flutuante de 64 bits na memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_poke_i32": {
        "en": "Writes a signed 32-bit integer to raw memory at the specified byte offset.",
        "pt": "Escreve um inteiro com sinal de 32 bits na memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_poke_i64": {
        "en": "Writes a signed 64-bit integer to raw memory at the specified byte offset.",
        "pt": "Escreve um inteiro com sinal de 64 bits na memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_poke_ptr": {
        "en": "Writes a pointer value to raw memory at the specified byte offset.",
        "pt": "Escreve um ponteiro na memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_poke_u32": {
        "en": "Writes an unsigned 32-bit integer to raw memory at the specified byte offset.",
        "pt": "Escreve um inteiro sem sinal de 32 bits na memória bruta no deslocamento de bytes especificado."
    },
    "hphl_mem_zero": {
        "en": "Sets `n` bytes of memory at destination buffer to zero.",
        "pt": "Zera `n` bytes de memória no buffer de destino."
    },

    # Subsystem: net
    "hphl_accept": {
        "en": "Accepts an incoming TCP connection on a listening socket and returns the client socket descriptor.",
        "pt": "Aceita uma conexão TCP de entrada em um socket receptor e retorna o descritor do socket cliente."
    },
    "hphl_bind": {
        "en": "Binds a network socket to the specified local port number.",
        "pt": "Vincula um socket de rede ao número de porta local especificado."
    },
    "hphl_close_socket": {
        "en": "Closes an open network socket descriptor and releases OS networking resources.",
        "pt": "Fecha o descritor de um socket de rede aberto e libera os recursos de rede do sistema operacional."
    },
    "hphl_connect": {
        "en": "Establishes a TCP client connection to the specified remote host and port.",
        "pt": "Estabelece uma conexão de cliente TCP com o host remoto e porta especificados."
    },
    "hphl_listen": {
        "en": "Sets a network socket into listening mode with the specified connection backlog queue size.",
        "pt": "Coloca um socket de rede em modo de escuta com a fila de conexões pendentes (backlog) especificada."
    },
    "hphl_recv": {
        "en": "Receives incoming bytes from a connected socket up to `len` bytes into a string buffer.",
        "pt": "Recebe dados de um socket conectado em até `len` bytes para um buffer de string."
    },
    "hphl_send": {
        "en": "Sends a string or byte payload over a connected network socket.",
        "pt": "Envia uma string ou payload de bytes por um socket de rede conectado."
    },
    "hphl_socket": {
        "en": "Creates a new OS network socket endpoint with the specified address family, type, and protocol.",
        "pt": "Cria um novo socket de rede do sistema operacional com a família de endereços, tipo e protocolo especificados."
    },

    # Subsystem: strings
    "hphl_ceil": {
        "en": "Returns the smallest integral value not less than the input float.",
        "pt": "Retorna o menor valor inteiro que não seja menor do que o número de ponto flutuante fornecido (teto)."
    },
    "hphl_e": {
        "en": "Returns Euler's mathematical constant e (approx. 2.718281828459045).",
        "pt": "Retorna a constante matemática de Euler e (aprox. 2.718281828459045)."
    },
    "hphl_floor": {
        "en": "Returns the largest integral value not greater than the input float.",
        "pt": "Retorna o maior valor inteiro que não seja maior do que o número de ponto flutuante fornecido (piso)."
    },
    "hphl_fmod": {
        "en": "Computes the floating-point remainder of dividing a by b.",
        "pt": "Calcula o resto da divisão de ponto flutuante entre a e b."
    },
    "hphl_is_numeric": {
        "en": "Returns 1 if the string consists entirely of numeric decimal digits, 0 otherwise.",
        "pt": "Retorna 1 se a string for composta exclusivamente por dígitos numéricos decimais, ou 0 caso contrário."
    },
    "hphl_max_i64": {
        "en": "Returns the greater of two 64-bit signed integers.",
        "pt": "Retorna o maior entre dois inteiros com sinal de 64 bits."
    },
    "hphl_min_i64": {
        "en": "Returns the lesser of two 64-bit signed integers.",
        "pt": "Retorna o menor entre dois inteiros com sinal de 64 bits."
    },
    "hphl_parse_f64": {
        "en": "Parses a 64-bit floating-point number from a null-terminated string.",
        "pt": "Converte uma string terminada em nulo em um número de ponto flutuante de 64 bits."
    },
    "hphl_parse_int": {
        "en": "Parses a 64-bit signed integer from a null-terminated string.",
        "pt": "Converte uma string terminada em nulo em um inteiro com sinal de 64 bits."
    },
    "hphl_pi": {
        "en": "Returns the mathematical constant Pi (approx. 3.141592653589793).",
        "pt": "Retorna a constante matemática Pi (aprox. 3.141592653589793)."
    },
    "hphl_round": {
        "en": "Rounds a floating-point value to the nearest integer, rounding halfway cases away from zero.",
        "pt": "Arredonda um valor de ponto flutuante para o inteiro mais próximo."
    },
    "hphl_split": {
        "en": "Splits a string into a dynamic list of substrings separated by the specified delimiter.",
        "pt": "Divide uma string em uma lista dinâmica de substrings separadas pelo delimitador especificado."
    },
    "hphl_sqrt": {
        "en": "Computes the square root of a floating-point number.",
        "pt": "Calcula a raiz quadrada de um número de ponto flutuante."
    },
    "hphl_str_char_at": {
        "en": "Returns a single-character string containing the character at index `i`.",
        "pt": "Retorna uma string de um único caractere contendo o caractere no índice `i`."
    },
    "hphl_str_char_index": {
        "en": "Returns the byte index corresponding to character index `i` in a UTF-8 string.",
        "pt": "Retorna o índice de bytes correspondente ao índice de caracteres `i` em uma string UTF-8."
    },
    "hphl_str_chr": {
        "en": "Converts an integer Unicode/ASCII code point into a single-character string.",
        "pt": "Converte um ponto de código Unicode/ASCII inteiro em uma string de caractere único."
    },
    "hphl_str_cmp": {
        "en": "Compares two strings lexicographically, returning negative, zero, or positive integer.",
        "pt": "Compara lexicograficamente duas strings, retornando valor negativo, zero ou positivo."
    },
    "hphl_str_concat": {
        "en": "Concatenates two strings and returns a newly allocated combined string.",
        "pt": "Concatena duas strings e retorna uma nova string combinada."
    },
    "hphl_str_contains": {
        "en": "Returns 1 if the substring is found within the source string, 0 otherwise.",
        "pt": "Retorna 1 se a substring for encontrada dentro da string de origem, ou 0 caso contrário."
    },
    "hphl_str_down": {
        "en": "Returns a copy of the string with all characters converted to lowercase.",
        "pt": "Retorna uma cópia da string com todos os caracteres convertidos para minúsculas."
    },
    "hphl_str_ends": {
        "en": "Returns 1 if the string ends with the specified suffix, 0 otherwise.",
        "pt": "Retorna 1 se a string terminar com o sufixo especificado, ou 0 caso contrário."
    },
    "hphl_str_eq": {
        "en": "Returns 1 if both strings have identical contents, 0 otherwise.",
        "pt": "Retorna 1 se ambas as strings tiverem conteúdo idêntico, ou 0 caso contrário."
    },
    "hphl_str_format": {
        "en": "Formats a string using a printf-style format specifier and string argument.",
        "pt": "Formata uma string utilizando um especificador de formato estilo printf e argumento de texto."
    },
    "hphl_str_format_float": {
        "en": "Formats a 64-bit floating-point number using the given format specifier.",
        "pt": "Formata um número de ponto flutuante de 64 bits utilizando o especificador de formato fornecido."
    },
    "hphl_str_format_int": {
        "en": "Formats a 64-bit signed integer using the given format specifier.",
        "pt": "Formata um número inteiro com sinal de 64 bits utilizando o especificador de formato fornecido."
    },
    "hphl_str_free": {
        "en": "Deallocates an allocated string buffer.",
        "pt": "Desaloca um buffer de string alocado."
    },
    "hphl_str_from_bool": {
        "en": "Converts a boolean value to its string representation ('true' or 'false').",
        "pt": "Converte um valor booleano em sua representação em texto ('true' ou 'false')."
    },
    "hphl_str_from_char": {
        "en": "Converts a character code to a single-character string.",
        "pt": "Converte um código de caractere em uma string de um único caractere."
    },
    "hphl_str_from_float": {
        "en": "Converts a floating-point number to its decimal string representation.",
        "pt": "Converte um número de ponto flutuante em sua representação decimal em texto."
    },
    "hphl_str_from_int": {
        "en": "Converts a 64-bit signed integer to its decimal string representation.",
        "pt": "Converte um inteiro com sinal de 64 bits em sua representação decimal em texto."
    },
    "hphl_str_from_uint": {
        "en": "Converts an unsigned 64-bit integer to its decimal string representation.",
        "pt": "Converte um inteiro sem sinal de 64 bits em sua representação decimal em texto."
    },
    "hphl_str_index_of": {
        "en": "Returns the 0-based index of the first occurrence of the substring, or -1 if not found.",
        "pt": "Retorna o índice de base 0 da primeira ocorrência da substring, ou -1 caso não encontrada."
    },
    "hphl_str_last_index_of": {
        "en": "Returns the 0-based index of the last occurrence of the substring, or -1 if not found.",
        "pt": "Retorna o índice de base 0 da última ocorrência da substring, ou -1 caso não encontrada."
    },
    "hphl_str_len": {
        "en": "Returns the length of the string in characters (or bytes for ASCII).",
        "pt": "Retorna o comprimento da string em caracteres (ou bytes para ASCII)."
    },
    "hphl_str_ord": {
        "en": "Returns the character code of the first character of the string.",
        "pt": "Retorna o código do primeiro caractere da string."
    },
    "hphl_str_ord_at": {
        "en": "Returns the character code at the specified index in the string.",
        "pt": "Retorna o código do caractere no índice especificado na string."
    },
    "hphl_str_pad_left": {
        "en": "Pads the string on the left side with the fill string until reaching length `n`.",
        "pt": "Preenche a string à esquerda com a string de preenchimento até atingir o comprimento `n`."
    },
    "hphl_str_pad_right": {
        "en": "Pads the string on the right side with the fill string until reaching length `n`.",
        "pt": "Preenche a string à direita com a string de preenchimento até atingir o comprimento `n`."
    },
    "hphl_str_replace": {
        "en": "Replaces occurrences of a substring with a replacement string.",
        "pt": "Substitui ocorrências de uma substring por uma string de substituição."
    },
    "hphl_str_starts": {
        "en": "Returns 1 if the string begins with the specified prefix, 0 otherwise.",
        "pt": "Retorna 1 se a string iniciar com o prefixo especificado, ou 0 caso contrário."
    },
    "hphl_str_sub": {
        "en": "Returns a substring starting at `start` index with length `n`.",
        "pt": "Retorna uma substring iniciando no índice `start` com comprimento `n`."
    },
    "hphl_str_trim": {
        "en": "Returns a copy of the string with leading and trailing whitespace removed.",
        "pt": "Retorna uma cópia da string sem espaços em branco no início e no final."
    },
    "hphl_str_up": {
        "en": "Returns a copy of the string with all characters converted to uppercase.",
        "pt": "Retorna uma cópia da string com todos os caracteres convertidos para maiúsculas."
    },
}
