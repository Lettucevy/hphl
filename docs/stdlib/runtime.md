# HP-HL Runtime C API Reference

> **Auto-generated** from `compiler/src/runtime/*/*.c` by
> `tools/gen_runtime_doc.py`. Do not edit by hand; re-run the
> script after changing the runtime.

Total: **267** `hphl_*` functions.

Companion: [Standard Library](index.md) (HP-HL builtins).

## Subsystem `libc` (C99 libm, no wrapper)

### fmin

```c
double fmin(double x, double y);
```

C99 math library function, used directly by the `min(float, float)` builtin.

_Source: `libm`_

### fmax

```c
double fmax(double x, double y);
```

C99 math library function, used directly by the `max(float, float)` builtin.

_Source: `libm`_

## Subsystem `alloc`

### hphl_arena_alloc

```c
void* hphl_arena_alloc(void** arena, size_t n);
```

Allocates a contiguous chunk of `n` bytes from the bump-pointer arena without per-object GC overhead.

_Source: `compiler/src/runtime/alloc/alloc.c:17`_

### hphl_arena_free_all

```c
void hphl_arena_free_all(void* arena);
```

Frees all memory previously allocated within the specified arena in a single constant-time operation.

_Source: `compiler/src/runtime/alloc/alloc.c:34`_

### hphl_arena_reset

```c
void hphl_arena_reset(void** arena);
```

Resets the arena allocation pointer back to the start of its buffer, invalidating previous allocations without unmapping pages.

_Source: `compiler/src/runtime/alloc/alloc.c:46`_

### hphl_pool_alloc

```c
void* hphl_pool_alloc(size_t n);
```

Allocates a fixed-size memory block from the thread-safe pool allocator.

_Source: `compiler/src/runtime/alloc/alloc.c:66`_

### hphl_pool_free

```c
void hphl_pool_free(void* p, size_t n);
```

Returns a previously allocated block back to the memory pool allocator.

_Source: `compiler/src/runtime/alloc/alloc.c:83`_

## Subsystem `collections`

### hphl_join

```c
char* hphl_join(void* listHandle, const char* sep);
```

Concatenates all string elements of a dynamic list into a single string separated by the given delimiter.

_Source: `compiler/src/runtime/collections/collections.c:77`_

### hphl_list_add_f

```c
void hphl_list_add_f(void* p, double v);
```

Appends a 64-bit floating-point value to the end of a dynamic list.

_Source: `compiler/src/runtime/collections/collections.c:70`_

### hphl_list_add_i

```c
void hphl_list_add_i(void* p, long long v);
```

Appends a 64-bit signed integer value to the end of a dynamic list.

_Source: `compiler/src/runtime/collections/collections.c:64`_

### hphl_list_check

```c
void hphl_list_check(long long index, void* p);
```

Validates that an element index is within dynamic list bounds, raising a runtime panic if out of range.

_Source: `compiler/src/runtime/collections/collections.c:140`_

### hphl_list_data

```c
void* hphl_list_data(void* p);
```

Returns the raw memory pointer to the element storage buffer of the dynamic list, or NULL if empty.

_Source: `compiler/src/runtime/collections/collections.c:111`_

### hphl_list_free

```c
void hphl_list_free(void* p);
```

Deallocates memory used by the dynamic list and releases internal storage buffers.

_Source: `compiler/src/runtime/collections/collections.c:43`_

### hphl_list_len

```c
long long hphl_list_len(void* p);
```

Returns the number of elements currently stored in the dynamic list.

_Source: `compiler/src/runtime/collections/collections.c:108`_

### hphl_list_new

```c
void* hphl_list_new(void);
```

Allocates and initializes a new empty dynamic list with default initial capacity.

_Source: `compiler/src/runtime/collections/collections.c:12`_

### hphl_list_slice

```c
void* hphl_list_slice(void* p, long long from, long long count, long long elemSize);
```

Creates a new shallow copy of a subslice of the list starting from index `from` spanning `count` elements.

_Source: `compiler/src/runtime/collections/collections.c:115`_

### hphl_list_with_cap

```c
void* hphl_list_with_cap(long long n);
```

Allocates and initializes a new dynamic list with an explicit pre-allocated capacity `n` to avoid reallocations during append operations.

_Source: `compiler/src/runtime/collections/collections.c:24`_

### hphl_map_clear

```c
void hphl_map_clear(void *p);
```

Removes all key-value entries from the hash map.

_Source: `compiler/src/runtime/collections/collections.c:251`_

### hphl_map_contains

```c
long long hphl_map_contains(void *p, long long k);
```

Returns 1 if the hash map contains an entry for the specified key, 0 otherwise.

_Source: `compiler/src/runtime/collections/collections.c:233`_

### hphl_map_free

```c
void hphl_map_free(void *p);
```

Deallocates the hash map and releases all internal bucket memory.

_Source: `compiler/src/runtime/collections/collections.c:193`_

### hphl_map_get

```c
long long hphl_map_get(void *p, long long k);
```

Retrieves the value associated with the specified key, or 0 if not present.

_Source: `compiler/src/runtime/collections/collections.c:224`_

### hphl_map_len

```c
long long hphl_map_len(void *p);
```

Returns the number of key-value pairs currently stored in the hash map.

_Source: `compiler/src/runtime/collections/collections.c:252`_

### hphl_map_new

```c
void* hphl_map_new(long long keyKind);
```

Allocates and initializes a new empty hash map configured for the specified key kind with quadratic probing.

_Source: `compiler/src/runtime/collections/collections.c:182`_

### hphl_map_put

```c
void hphl_map_put(void *p, long long k, long long v);
```

Inserts or updates a key-value mapping in the hash map.

_Source: `compiler/src/runtime/collections/collections.c:211`_

### hphl_map_remove

```c
long long hphl_map_remove(void *p, long long k);
```

Removes the key-value mapping for the specified key from the hash map.

_Source: `compiler/src/runtime/collections/collections.c:242`_

### hphl_write_lines

```c
int64_t hphl_write_lines(const char* path, void* listHandle);
```

Writes each string in the provided dynamic list as a separate line to the specified file path.

_Source: `compiler/src/runtime/collections/collections.c:96`_

## Subsystem `concurrency`

### hphl_atomic_add_i64

```c
long long hphl_atomic_add_i64(volatile long long* p, long long delta);
```

Atomically adds a 64-bit integer to the target memory location and returns the previous value.

_Source: `compiler/src/runtime/concurrency/concurrency.c:744`_

### hphl_atomic_cas_i64

```c
long long hphl_atomic_cas_i64(volatile long long* p, long long expected, long long desired);
```

Atomically compares the value at target address with expected, replacing it with desired if equal (compare-and-swap).

_Source: `compiler/src/runtime/concurrency/concurrency.c:752`_

### hphl_atomic_load_i64

```c
long long hphl_atomic_load_i64(volatile long long* p);
```

Atomically reads and returns a 64-bit integer from the target memory location with acquire memory ordering.

_Source: `compiler/src/runtime/concurrency/concurrency.c:736`_

### hphl_atomic_store_i64

```c
void hphl_atomic_store_i64(volatile long long* p, long long v);
```

Atomically stores a 64-bit integer value into the target memory location with release memory ordering.

_Source: `compiler/src/runtime/concurrency/concurrency.c:740`_

### hphl_atomic_sub_i64

```c
long long hphl_atomic_sub_i64(volatile long long* p, long long delta);
```

Atomically subtracts a 64-bit integer from the target memory location and returns the previous value.

_Source: `compiler/src/runtime/concurrency/concurrency.c:748`_

### hphl_barrier_destroy

```c
void hphl_barrier_destroy(void* p);
```

Destroys a thread synchronization barrier and releases underlying OS synchronization primitives.

_Source: `compiler/src/runtime/concurrency/concurrency.c:855`_

### hphl_barrier_new

```c
void* hphl_barrier_new(long long n);
```

Initializes a synchronization barrier configured for a fixed number of participating threads.

_Source: `compiler/src/runtime/concurrency/concurrency.c:826`_

### hphl_barrier_wait

```c
void hphl_barrier_wait(void* p);
```

Blocks the calling thread until all participating threads reach the synchronization barrier.

_Source: `compiler/src/runtime/concurrency/concurrency.c:839`_

### hphl_cancel_task

```c
void hphl_cancel_task(void* t);
```

Requests cooperative cancellation of a background task by setting its cancellation flag.

_Source: `compiler/src/runtime/concurrency/concurrency.c:359`_

### hphl_channel_new

```c
void* hphl_channel_new(long long cap);
```

Creates a new thread-safe, typed message passing channel with lock-free ring buffer.

_Source: `compiler/src/runtime/concurrency/concurrency.c:512`_

### hphl_channel_receive

```c
long long hphl_channel_receive(void* p);
```

Receives and dequeues a message payload from the channel, blocking if the channel is empty.

_Source: `compiler/src/runtime/concurrency/concurrency.c:583`_

### hphl_channel_send

```c
void hphl_channel_send(void* p, long long v);
```

Enqueues a message payload onto the channel and notifies waiting reader threads.

_Source: `compiler/src/runtime/concurrency/concurrency.c:530`_

### hphl_condvar_broadcast

```c
void hphl_condvar_broadcast(void* cv);
```

Wakes all threads currently waiting on the specified condition variable.

_Source: `compiler/src/runtime/concurrency/concurrency.c:713`_

### hphl_condvar_destroy

```c
void hphl_condvar_destroy(void* cv);
```

Destroys the condition variable and frees associated OS resources.

_Source: `compiler/src/runtime/concurrency/concurrency.c:722`_

### hphl_condvar_new

```c
void* hphl_condvar_new(void);
```

Allocates and initializes a new condition variable.

_Source: `compiler/src/runtime/concurrency/concurrency.c:668`_

### hphl_condvar_signal

```c
void hphl_condvar_signal(void* cv);
```

Wakes at least one thread currently waiting on the specified condition variable.

_Source: `compiler/src/runtime/concurrency/concurrency.c:704`_

### hphl_condvar_wait

```c
void hphl_condvar_wait(void* cv, void* mutex);
```

Atomically releases the associated mutex and suspends execution until signaled.

_Source: `compiler/src/runtime/concurrency/concurrency.c:690`_

### hphl_event_destroy

```c
void hphl_event_destroy(void* p);
```

Destroys the synchronization event handle and releases kernel resources.

_Source: `compiler/src/runtime/concurrency/concurrency.c:812`_

### hphl_event_new

```c
void* hphl_event_new(void);
```

Creates a manual or auto-reset synchronization event.

_Source: `compiler/src/runtime/concurrency/concurrency.c:790`_

### hphl_event_reset

```c
void hphl_event_reset(void* p);
```

Resets the synchronization event to an unsignaled state.

_Source: `compiler/src/runtime/concurrency/concurrency.c:808`_

### hphl_event_set

```c
void hphl_event_set(void* p);
```

Sets the event to a signaled state, unblocking waiting threads.

_Source: `compiler/src/runtime/concurrency/concurrency.c:804`_

### hphl_event_wait

```c
void hphl_event_wait(void* p);
```

Waits for the synchronization event to become signaled.

_Source: `compiler/src/runtime/concurrency/concurrency.c:800`_

### hphl_join_tasks

```c
void hphl_join_tasks(void);
```

Waits for all active thread pool tasks to finish before proceeding.

_Source: `compiler/src/runtime/concurrency/concurrency.c:415`_

### hphl_lock_begin

```c
void hphl_lock_begin(void* obj);
```

Enters a reentrant critical section lock.

_Source: `compiler/src/runtime/concurrency/concurrency.c:23`_

### hphl_lock_end

```c
void hphl_lock_end(void* obj);
```

Exits a reentrant critical section lock.

_Source: `compiler/src/runtime/concurrency/concurrency.c:42`_

### hphl_mutex_destroy

```c
void hphl_mutex_destroy(void* p);
```

Destroys a mutual exclusion lock and frees underlying kernel handles.

_Source: `compiler/src/runtime/concurrency/concurrency.c:649`_

### hphl_mutex_lock

```c
void hphl_mutex_lock(void* p);
```

Acquires an exclusive lock on the mutex, blocking if currently locked by another thread.

_Source: `compiler/src/runtime/concurrency/concurrency.c:633`_

### hphl_mutex_new

```c
void* hphl_mutex_new(void);
```

Allocates and initializes a new recursive mutex.

_Source: `compiler/src/runtime/concurrency/concurrency.c:610`_

### hphl_mutex_unlock

```c
void hphl_mutex_unlock(void* p);
```

Releases the exclusive lock on the specified mutex.

_Source: `compiler/src/runtime/concurrency/concurrency.c:641`_

### hphl_region_begin

```c
void hphl_region_begin(void);
```

Opens a parallel structured region boundary.

_Source: `compiler/src/runtime/concurrency/concurrency.c:440`_

### hphl_region_join

```c
void hphl_region_join(void);
```

Synchronizes and waits for all tasks within the current parallel region boundary to complete.

_Source: `compiler/src/runtime/concurrency/concurrency.c:465`_

### hphl_semaphore_destroy

```c
void hphl_semaphore_destroy(void* p);
```

Destroys a counting semaphore and releases OS resources.

_Source: `compiler/src/runtime/concurrency/concurrency.c:784`_

### hphl_semaphore_new

```c
void* hphl_semaphore_new(long long n);
```

Creates a counting semaphore with the specified initial and maximum counts.

_Source: `compiler/src/runtime/concurrency/concurrency.c:766`_

### hphl_semaphore_signal

```c
void hphl_semaphore_signal(void* p);
```

Increments the counting semaphore value, waking waiting threads.

_Source: `compiler/src/runtime/concurrency/concurrency.c:780`_

### hphl_semaphore_wait

```c
void hphl_semaphore_wait(void* p);
```

Decrements the counting semaphore value, blocking if the count is zero.

_Source: `compiler/src/runtime/concurrency/concurrency.c:776`_

### hphl_spawn_task_ex

```c
void* hphl_spawn_task_ex(void (*fn)(void* env, void* res), void* env);
```

Spawns a new task onto the thread pool work-stealing queue with execution environment and result storage.

_Source: `compiler/src/runtime/concurrency/concurrency.c:311`_

### hphl_task_iscancelled

```c
long long hphl_task_iscancelled(void);
```

Checks if cancellation has been requested for the currently executing task.

_Source: `compiler/src/runtime/concurrency/concurrency.c:364`_

### hphl_track_barrier

```c
void hphl_track_barrier(void* p);
```

Registers a barrier with the runtime leak detection and tracking registry.

_Source: `compiler/src/runtime/concurrency/concurrency.c:936`_

### hphl_track_event

```c
void hphl_track_event(void* p);
```

Registers an event with the runtime leak detection and tracking registry.

_Source: `compiler/src/runtime/concurrency/concurrency.c:935`_

### hphl_track_semaphore

```c
void hphl_track_semaphore(void* p);
```

Registers a semaphore with the runtime leak detection and tracking registry.

_Source: `compiler/src/runtime/concurrency/concurrency.c:934`_

### hphl_untrack_barrier

```c
void hphl_untrack_barrier(void* p);
```

Removes a barrier from the runtime leak detection registry.

_Source: `compiler/src/runtime/concurrency/concurrency.c:939`_

### hphl_untrack_event

```c
void hphl_untrack_event(void* p);
```

Removes an event from the runtime leak detection registry.

_Source: `compiler/src/runtime/concurrency/concurrency.c:938`_

### hphl_untrack_semaphore

```c
void hphl_untrack_semaphore(void* p);
```

Removes a semaphore from the runtime leak detection registry.

_Source: `compiler/src/runtime/concurrency/concurrency.c:937`_

### hphl_wait_task

```c
long long hphl_wait_task(HphlTask* t);
```

Blocks until the specified task handle completes, assisting worker queue execution while waiting.

_Source: `compiler/src/runtime/concurrency/concurrency.c:382`_

### hphl_worker_count

```c
long long hphl_worker_count(void);
```

Returns the current number of active worker threads in the runtime thread pool.

_Source: `compiler/src/runtime/concurrency/concurrency.c:372`_

## Subsystem `core`

### hphl_abort

```c
void hphl_abort(void);
```

Aborts process execution immediately with an error diagnostic and abnormal termination code.

_Source: `compiler/src/runtime/core/core.c:406`_

### hphl_abs_f64

```c
double hphl_abs_f64(double v);
```

Returns the absolute value of a 64-bit floating-point number.

_Source: `compiler/src/runtime/core/core.c:482`_

### hphl_abs_i64

```c
int64_t hphl_abs_i64(int64_t v);
```

Returns the absolute value of a 64-bit signed integer.

_Source: `compiler/src/runtime/core/core.c:483`_

### hphl_acos

```c
double hphl_acos(double v);
```

Computes the principal arc cosine of a floating-point value in radians.

_Source: `compiler/src/runtime/core/core.c:474`_

### hphl_append_file

```c
int64_t hphl_append_file(const char* path, const char* content);
```

Appends string data to the end of a file at the specified path.

_Source: `compiler/src/runtime/core/core.c:215`_

### hphl_args

```c
void* hphl_args(void);
```

Returns a dynamic list containing the command-line argument strings passed to the program.

_Source: `compiler/src/runtime/core/core.c:388`_

### hphl_asin

```c
double hphl_asin(double v);
```

Computes the principal arc sine of a floating-point value in radians.

_Source: `compiler/src/runtime/core/core.c:473`_

### hphl_assert

```c
void hphl_assert(long long cond, const char* msg);
```

Evaluates a boolean condition and raises a runtime assertion panic with the given message if false.

_Source: `compiler/src/runtime/core/core.c:717`_

### hphl_async_yield

```c
void hphl_async_yield(void);
```

Cooperatively yields execution of the current coroutine or green task to allow other tasks to run.

_Source: `compiler/src/runtime/core/core.c:95`_

### hphl_atan

```c
double hphl_atan(double v);
```

Computes the principal arc tangent of a floating-point value in radians.

_Source: `compiler/src/runtime/core/core.c:475`_

### hphl_atan2

```c
double hphl_atan2(double y, double x);
```

Computes the arc tangent of y/x using the signs of both arguments to determine the quadrant.

_Source: `compiler/src/runtime/core/core.c:476`_

### hphl_bounds_check

```c
void hphl_bounds_check(long long index, long long size);
```

Validates that an array or memory buffer index is within valid bounds [0, size - 1].

_Source: `compiler/src/runtime/core/core.c:655`_

### hphl_box_i64

```c
void* hphl_box_i64(long long v);
```

Boxes a primitive 64-bit signed integer into a heap-allocated managed object.

_Source: `compiler/src/runtime/core/core.c:839`_

### hphl_clock_ns

```c
int64_t hphl_clock_ns(void);
```

Returns the current high-resolution monotonic time in nanoseconds.

_Source: `compiler/src/runtime/core/core.c:485`_

### hphl_coop_run

```c
void hphl_coop_run(void);
```

Executes the cooperative scheduler event loop until all scheduled cooperative tasks complete.

_Source: `compiler/src/runtime/core/core.c:59`_

### hphl_coop_spawn

```c
void* hphl_coop_spawn(void (*fn)(void*, void*), void* env, void* res);
```

Spawns a new lightweight cooperative task function with the specified environment context.

_Source: `compiler/src/runtime/core/core.c:47`_

### hphl_cos

```c
double hphl_cos(double v);
```

Computes the cosine of an angle expressed in radians.

_Source: `compiler/src/runtime/core/core.c:471`_

### hphl_crash_handler

```c
LONG WINAPI hphl_crash_handler(EXCEPTION_POINTERS* ep);
```

Global operating system crash and exception handler that dumps stack traces on fatal hardware faults.

_Source: `compiler/src/runtime/core/core.c:519`_

### hphl_divide_by_zero

```c
void hphl_divide_by_zero(void);
```

Triggers a runtime panic diagnostic when a division by zero occurs.

_Source: `compiler/src/runtime/core/core.c:693`_

### hphl_env_var

```c
char* hphl_env_var(const char* name);
```

Retrieves the value of an environment variable as a string, or returns an empty string if unset.

_Source: `compiler/src/runtime/core/core.c:376`_

### hphl_exc_begin

```c
int hphl_exc_begin(void* rec);
```

Initializes a structured exception frame record before entering a protected try block.

_Source: `compiler/src/runtime/core/core.c:816`_

### hphl_exc_end

```c
void hphl_exc_end(void* p);
```

Unwinds and tears down the top exception handler frame upon leaving a try block.

_Source: `compiler/src/runtime/core/core.c:762`_

### hphl_exc_free

```c
void hphl_exc_free(void* p);
```

Deallocates an active exception wrapper object.

_Source: `compiler/src/runtime/core/core.c:754`_

### hphl_exc_new

```c
void* hphl_exc_new(void);
```

Allocates a new empty exception structure.

_Source: `compiler/src/runtime/core/core.c:745`_

### hphl_exc_payload

```c
void* hphl_exc_payload(void* p);
```

Extracts the payload object pointer associated with an in-flight exception.

_Source: `compiler/src/runtime/core/core.c:767`_

### hphl_exc_push

```c
void hphl_exc_push(void* p);
```

Pushes an exception landing pad frame onto the thread-local exception stack.

_Source: `compiler/src/runtime/core/core.c:756`_

### hphl_exc_restore

```c
void hphl_exc_restore(void* rec);
```

Restores CPU registers and stack pointer to resume execution in the matching catch handler.

_Source: `compiler/src/runtime/core/core.c:820`_

### hphl_exit_prog

```c
int64_t hphl_exit_prog(int64_t code);
```

Terminates the program normally and returns the specified exit code to the operating system.

_Source: `compiler/src/runtime/core/core.c:383`_

### hphl_exp

```c
double hphl_exp(double v);
```

Computes Euler's number e raised to the given power (e^x).

_Source: `compiler/src/runtime/core/core.c:479`_

### hphl_exp2

```c
double hphl_exp2(double v);
```

Computes 2 raised to the given power (2^x).

_Source: `compiler/src/runtime/core/core.c:480`_

### hphl_file_exists

```c
int64_t hphl_file_exists(const char* path);
```

Returns 1 if a file or directory exists at the specified path, 0 otherwise.

_Source: `compiler/src/runtime/core/core.c:222`_

### hphl_float_overflow_check

```c
void hphl_float_overflow_check(double* v);
```

Verifies that a floating-point value is not infinite or NaN, raising a runtime overflow error if invalid.

_Source: `compiler/src/runtime/core/core.c:681`_

### hphl_format_date

```c
char* hphl_format_date(int64_t epoch, const char* fmt);
```

Formats a Unix epoch timestamp into a string representation according to the format specifier.

_Source: `compiler/src/runtime/core/core.c:333`_

### hphl_gas_str

```c
char* hphl_gas_str(const char* raw);
```

Normalizes an assembly/runtime string literal into an HP-HL managed string.

_Source: `compiler/src/runtime/core/core.c:130`_

### hphl_init_args

```c
void hphl_init_args(int argc, char** argv);
```

Stores command-line argc and argv into global runtime storage for later retrieval.

_Source: `compiler/src/runtime/core/core.c:6`_

### hphl_install_crash_handler

```c
void hphl_install_crash_handler(void);
```

Installs operating system hardware exception and signal handlers to report fatal errors.

_Source: `compiler/src/runtime/core/core.c:592`_

### hphl_json_get

```c
char* hphl_json_get(const char* root, const char* path);
```

Retrieves a JSON property value by key path from a parsed JSON document.

_Source: `compiler/src/runtime/core/core.c:299`_

### hphl_json_parse

```c
char* hphl_json_parse(const char* s);
```

Parses a JSON string into an in-memory document tree representation.

_Source: `compiler/src/runtime/core/core.c:297`_

### hphl_json_set

```c
char* hphl_json_set(const char* root, const char* path, const char* value);
```

Sets or updates a property value at the specified key path in a JSON document.

_Source: `compiler/src/runtime/core/core.c:318`_

### hphl_json_stringify

```c
char* hphl_json_stringify(const char* s);
```

Serializes an in-memory JSON document structure into a formatted string.

_Source: `compiler/src/runtime/core/core.c:298`_

### hphl_list_dir

```c
void* hphl_list_dir(const char* path);
```

Returns a dynamic list of file and directory names located within the specified folder path.

_Source: `compiler/src/runtime/core/core.c:239`_

### hphl_log

```c
double hphl_log(double v);
```

Computes the natural (base-e) logarithm of a floating-point number.

_Source: `compiler/src/runtime/core/core.c:477`_

### hphl_log2

```c
double hphl_log2(double v);
```

Computes the binary (base-2) logarithm of a floating-point number.

_Source: `compiler/src/runtime/core/core.c:478`_

### hphl_match_fail

```c
void hphl_match_fail(void);
```

Triggers a runtime pattern matching exhaustion panic when no pattern branch matches.

_Source: `compiler/src/runtime/core/core.c:701`_

### hphl_mkdir

```c
int64_t hphl_mkdir(const char* path);
```

Creates a new directory at the specified filesystem path, returning 1 on success or 0 on failure.

_Source: `compiler/src/runtime/core/core.c:232`_

### hphl_now

```c
int64_t hphl_now(void);
```

Returns the current Unix epoch timestamp in seconds.

_Source: `compiler/src/runtime/core/core.c:414`_

### hphl_now_ms

```c
int64_t hphl_now_ms(void);
```

Returns the current Unix epoch timestamp in milliseconds.

_Source: `compiler/src/runtime/core/core.c:418`_

### hphl_now_us

```c
int64_t hphl_now_us(void);
```

Returns the current Unix epoch timestamp in microseconds.

_Source: `compiler/src/runtime/core/core.c:431`_

### hphl_overflow_check

```c
void hphl_overflow_check(long long v, long long min, long long max);
```

Validates that a 64-bit integer is within [min, max] bounds, raising an overflow error if violated.

_Source: `compiler/src/runtime/core/core.c:668`_

### hphl_panic

```c
void hphl_panic(const char* msg);
```

Halts program execution immediately and prints a diagnostic panic message to stderr.

_Source: `compiler/src/runtime/core/core.c:708`_

### hphl_parse_date

```c
int64_t hphl_parse_date(const char* s, const char* fmt);
```

Parses a formatted date string using the specified format pattern into a Unix timestamp in seconds.

_Source: `compiler/src/runtime/core/core.c:346`_

### hphl_pow

```c
double hphl_pow(double b, double e);
```

Computes base raised to the specified exponent (base^exponent).

_Source: `compiler/src/runtime/core/core.c:484`_

### hphl_print_bool

```c
void hphl_print_bool(int b);
```

Prints a boolean value ('true' or 'false') to standard output.

_Source: `compiler/src/runtime/core/core.c:637`_

### hphl_print_char

```c
void hphl_print_char(int c);
```

Prints a single character to standard output.

_Source: `compiler/src/runtime/core/core.c:642`_

### hphl_print_float

```c
void hphl_print_float(double v);
```

Prints a 64-bit floating-point number formatted to standard output.

_Source: `compiler/src/runtime/core/core.c:632`_

### hphl_print_int

```c
void hphl_print_int(long long v);
```

Prints a 64-bit signed integer in decimal format to standard output.

_Source: `compiler/src/runtime/core/core.c:622`_

### hphl_print_string

```c
void hphl_print_string(const char* s);
```

Prints a null-terminated string to standard output.

_Source: `compiler/src/runtime/core/core.c:647`_

### hphl_print_uint

```c
void hphl_print_uint(unsigned long long v);
```

Prints an unsigned 64-bit integer in decimal format to standard output.

_Source: `compiler/src/runtime/core/core.c:627`_

### hphl_random

```c
double hphl_random(void);
```

Generates a pseudo-random floating-point number uniformly distributed in [0.0, 1.0).

_Source: `compiler/src/runtime/core/core.c:359`_

### hphl_random_int

```c
int64_t hphl_random_int(int64_t lo, int64_t hi);
```

Generates a cryptographically seeded pseudo-random integer in the range [lo, hi] inclusive.

_Source: `compiler/src/runtime/core/core.c:365`_

### hphl_read_file

```c
char* hphl_read_file(const char* path);
```

Reads the entire contents of a file at the specified path into a newly allocated string buffer.

_Source: `compiler/src/runtime/core/core.c:188`_

### hphl_read_line

```c
char* hphl_read_line(void);
```

Reads a single line of text from standard input (stdin) until newline or EOF.

_Source: `compiler/src/runtime/core/core.c:156`_

### hphl_read_lines

```c
void* hphl_read_lines(const char* path);
```

Reads all lines from the specified file path into a dynamic list of strings.

_Source: `compiler/src/runtime/core/core.c:269`_

### hphl_remove_file

```c
int64_t hphl_remove_file(const char* path);
```

Deletes the file at the specified filesystem path, returning 1 on success or 0 on failure.

_Source: `compiler/src/runtime/core/core.c:228`_

### hphl_runtime_abi_version

```c
int hphl_runtime_abi_version(void);
```

Returns the ABI version integer of the HP-HL compiled runtime library.

_Source: `compiler/src/runtime/core/core.c:10`_

### hphl_set_env

```c
int64_t hphl_set_env(const char* name, const char* value);
```

Sets or updates an environment variable in the process environment.

_Source: `compiler/src/runtime/core/core.c:399`_

### hphl_sin

```c
double hphl_sin(double v);
```

Computes the sine of an angle expressed in radians.

_Source: `compiler/src/runtime/core/core.c:470`_

### hphl_sleep_ms

```c
int64_t hphl_sleep_ms(int64_t ms);
```

Suspends execution of the calling thread for the specified duration in milliseconds.

_Source: `compiler/src/runtime/core/core.c:370`_

### hphl_stat

```c
char* hphl_stat(const char* path);
```

Returns metadata information about a file (size, mode, timestamps) as a formatted string.

_Source: `compiler/src/runtime/core/core.c:285`_

### hphl_str_cmp

```c
int64_t hphl_str_cmp(const char* a, const char* b);
```

Compares two strings lexicographically, returning negative, zero, or positive integer.

_Source: `compiler/src/runtime/core/core.c:510`_

### hphl_str_eq

```c
int64_t hphl_str_eq(const char* a, const char* b);
```

Returns 1 if both strings have identical contents, 0 otherwise.

_Source: `compiler/src/runtime/core/core.c:503`_

### hphl_str_replace

```c
char* hphl_str_replace(const char* s, const char* from, const char* to);
```

Replaces occurrences of a substring with a replacement string.

_Source: `compiler/src/runtime/core/core.c:451`_

### hphl_struct_copy

```c
void* hphl_struct_copy(const void* src, size_t n);
```

Allocates memory and performs a bitwise copy of a struct of `n` bytes.

_Source: `compiler/src/runtime/core/core.c:852`_

### hphl_tan

```c
double hphl_tan(double v);
```

Computes the tangent of an angle expressed in radians.

_Source: `compiler/src/runtime/core/core.c:472`_

### hphl_throw

```c
void hphl_throw(void* payload);
```

Throws an exception containing the specified payload object, initiating stack unwinding.

_Source: `compiler/src/runtime/core/core.c:826`_

### hphl_tls_block

```c
void* hphl_tls_block(void);
```

Returns the base pointer of the current thread's thread-local storage (TLS) block.

_Source: `compiler/src/runtime/core/core.c:875`_

### hphl_tls_setup

```c
void hphl_tls_setup(int bytes);
```

Allocates and initializes the thread-local storage area of `bytes` size for the calling thread.

_Source: `compiler/src/runtime/core/core.c:869`_

### hphl_trunc

```c
double hphl_trunc(double v);
```

Truncates a floating-point value towards zero to its nearest integral value.

_Source: `compiler/src/runtime/core/core.c:481`_

### hphl_unbox_i64

```c
long long hphl_unbox_i64(void* p);
```

Unboxes a 64-bit integer from a heap-allocated managed object.

_Source: `compiler/src/runtime/core/core.c:849`_

### hphl_write_file

```c
int64_t hphl_write_file(const char* path, const char* content);
```

Writes string content to a file at the specified path, replacing any existing file.

_Source: `compiler/src/runtime/core/core.c:208`_

## Subsystem `debug`

### hphl_dbg_enter

```c
void hphl_dbg_enter(unsigned long long fnId);
```

Notifies debugger runtime of function entry by function identifier.

_Source: `compiler/src/runtime/debug/debug.c:1228`_

### hphl_dbg_enter_frame

```c
void hphl_dbg_enter_frame(unsigned long long fnId, unsigned long long framePtr);
```

Records function entry and stack frame base pointer for the interactive debugger.

_Source: `compiler/src/runtime/debug/debug.c:1094`_

### hphl_dbg_enter_impl

```c
void hphl_dbg_enter_impl(unsigned long long fnId, unsigned long long rbp);
```

Low-level implementation helper for debugger function entry tracking.

_Source: `compiler/src/runtime/debug/debug.c:1068`_

### hphl_dbg_exc_report

```c
void hphl_dbg_exc_report(int cod, const char* msg);
```

Reports an unhandled exception or crash condition to attached debuggers.

_Source: `compiler/src/runtime/debug/debug.c:1234`_

### hphl_dbg_leave

```c
void hphl_dbg_leave(void);
```

Notifies debugger runtime that the current function execution frame is exiting.

_Source: `compiler/src/runtime/debug/debug.c:1086`_

### hphl_dbg_trap

```c
void hphl_dbg_trap(long long line);
```

Debugger trap hook triggered when reaching a source line breakpoint.

_Source: `compiler/src/runtime/debug/debug.c:1225`_

### hphl_dbg_trap_impl

```c
void hphl_dbg_trap_impl(long long line, unsigned long long rbp);
```

Low-level implementation helper for debugger line traps and inspectable register frames.

_Source: `compiler/src/runtime/debug/debug.c:1018`_

## Subsystem `gc`

### hphl_gc

```c
void hphl_gc(void);
```

Forces an immediate complete garbage collection cycle.

_Source: `compiler/src/runtime/gc/gc.c:1267`_

### hphl_gc_add_root

```c
void hphl_gc_add_root(void* slot);
```

Registers an address slot as a GC root reference so it is preserved during collections.

_Source: `compiler/src/runtime/gc/gc.c:557`_

### hphl_gc_add_roots_batch

```c
void hphl_gc_add_roots_batch(void* slots[], int n);
```

Registers an array of root pointer slots in a single batch operation.

_Source: `compiler/src/runtime/gc/gc.c:589`_

### hphl_gc_cards_clear

```c
void hphl_gc_cards_clear(void);
```

Clears the generational GC card table markers.

_Source: `compiler/src/runtime/gc/gc.c:952`_

### hphl_gc_cards_scan

```c
void hphl_gc_cards_scan(void);
```

Scans dirty generational card table entries to mark inter-generational object pointers.

_Source: `compiler/src/runtime/gc/gc.c:985`_

### hphl_gc_cleanup

```c
void hphl_gc_cleanup(void);
```

Shuts down the garbage collector and deallocates all managed heap memory pages.

_Source: `compiler/src/runtime/gc/gc.c:717`_

### hphl_gc_cleanup_rem

```c
void hphl_gc_cleanup_rem(void);
```

Cleans up the remembered set data structures of the generational garbage collector.

_Source: `compiler/src/runtime/gc/gc.c:1042`_

### hphl_gc_free_managed

```c
void hphl_gc_free_managed(void* p);
```

Manually frees a managed object block, bypassing the normal sweep phase.

_Source: `compiler/src/runtime/gc/gc.c:1519`_

### hphl_gc_init

```c
void hphl_gc_init(void);
```

Initializes the garbage collector runtime structures and memory heaps.

_Source: `compiler/src/runtime/gc/gc.c:690`_

### hphl_gc_is_managed

```c
int hphl_gc_is_managed(void* p);
```

Returns 1 if the pointer belongs to a heap page managed by the GC, 0 otherwise.

_Source: `compiler/src/runtime/gc/gc.c:1513`_

### hphl_gc_major

```c
long long hphl_gc_major(void);
```

Executes a major (full-heap) garbage collection cycle and returns the number of freed bytes.

_Source: `compiler/src/runtime/gc/gc.c:1203`_

### hphl_gc_minor

```c
long long hphl_gc_minor(void);
```

Executes a minor (young-generation nursery) collection cycle and returns the number of freed bytes.

_Source: `compiler/src/runtime/gc/gc.c:1055`_

### hphl_gc_pop_roots

```c
void hphl_gc_pop_roots(int n);
```

Pops the topmost `n` registered root pointer slots from the thread root stack.

_Source: `compiler/src/runtime/gc/gc.c:680`_

### hphl_gc_pressure

```c
void hphl_gc_pressure(void);
```

Reports allocation memory pressure to the collector, potentially triggering collection.

_Source: `compiler/src/runtime/gc/gc.c:1228`_

### hphl_gc_push_stack

```c
void hphl_gc_push_stack(void* lo_addr, void* hi_addr);
```

Registers an address range of the execution stack to be scanned conservatively by the GC.

_Source: `compiler/src/runtime/gc/gc.c:419`_

### hphl_gc_register

```c
void hphl_gc_register(void* p);
```

Registers a newly allocated object pointer into the young-generation nursery pool.

_Source: `compiler/src/runtime/gc/gc.c:780`_

### hphl_gc_register_old

```c
void hphl_gc_register_old(void* p);
```

Registers an object directly into the mature tenured heap generation.

_Source: `compiler/src/runtime/gc/gc.c:807`_

### hphl_gc_register_slots

```c
void hphl_gc_register_slots(const int64_t* offs, void* base, int n);
```

Registers object field offsets containing GC pointers for precise struct tracing.

_Source: `compiler/src/runtime/gc/gc.c:645`_

### hphl_gc_remove_root

```c
void hphl_gc_remove_root(void* slot);
```

Deregisters a root pointer slot from GC tracking.

_Source: `compiler/src/runtime/gc/gc.c:607`_

### hphl_gc_remove_roots_batch

```c
void hphl_gc_remove_roots_batch(void* slots[], int n);
```

Deregisters a batch of root pointer slots from GC tracking.

_Source: `compiler/src/runtime/gc/gc.c:612`_

### hphl_gc_stats_collections

```c
long long hphl_gc_stats_collections(void);
```

Returns the cumulative number of garbage collection cycles executed.

_Source: `compiler/src/runtime/gc/gc.c:546`_

### hphl_gc_stats_freed

```c
long long hphl_gc_stats_freed(void);
```

Returns the cumulative total number of bytes reclaimed by the garbage collector.

_Source: `compiler/src/runtime/gc/gc.c:547`_

### hphl_gc_stats_last_freed

```c
long long hphl_gc_stats_last_freed(void);
```

Returns the number of bytes reclaimed during the most recent collection cycle.

_Source: `compiler/src/runtime/gc/gc.c:549`_

### hphl_gc_stats_max_pause

```c
long long hphl_gc_stats_max_pause(void);
```

Returns the maximum pause duration observed during collection cycles in microseconds.

_Source: `compiler/src/runtime/gc/gc.c:548`_

### hphl_gc_step

```c
int hphl_gc_step(int budget);
```

Performs an incremental garbage collection step within the specified work budget.

_Source: `compiler/src/runtime/gc/gc.c:359`_

### hphl_gc_sweep

```c
long long hphl_gc_sweep(void);
```

Performs the sweep phase of garbage collection, reclaiming unmarked memory blocks.

_Source: `compiler/src/runtime/gc/gc.c:1388`_

### hphl_gc_unregister

```c
void hphl_gc_unregister(void* p);
```

Removes an object pointer from active garbage collection management.

_Source: `compiler/src/runtime/gc/gc.c:823`_

### hphl_gc_unregister_slots

```c
void hphl_gc_unregister_slots(const int64_t* offs, void* base, int n);
```

Deregisters struct field pointer offsets previously recorded for precise GC tracing.

_Source: `compiler/src/runtime/gc/gc.c:666`_

### hphl_set_class_desc

```c
int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);
```

Registers a class descriptor bitmask defining which byte offsets contain managed references.

_Source: `compiler/src/runtime/gc/gc.c:190`_

### hphl_shared_alloc

```c
void* hphl_shared_alloc(size_t n);
```

Allocates a reference-counted shared memory object.

_Source: `compiler/src/runtime/gc/gc.c:1282`_

### hphl_shared_alloc_typed

```c
void* hphl_shared_alloc_typed(size_t n, int classId);
```

Allocates a typed reference-counted shared memory object with the given class identifier.

_Source: `compiler/src/runtime/gc/gc.c:1320`_

### hphl_shared_release

```c
long long hphl_shared_release(void* p);
```

Decrements reference count on a shared object, freeing it when count reaches zero.

_Source: `compiler/src/runtime/gc/gc.c:1353`_

### hphl_shared_retain

```c
void* hphl_shared_retain(void* p);
```

Increments reference count on a shared object to prevent deallocation.

_Source: `compiler/src/runtime/gc/gc.c:1348`_

### hphl_str_alloc

```c
void* hphl_str_alloc(size_t n);
```

Allocates a string memory buffer tracked by the garbage collector.

_Source: `compiler/src/runtime/gc/gc.c:1507`_

### hphl_tc_alloc

```c
void* hphl_tc_alloc(size_t n);
```

Allocates memory from the thread-local allocation cache (TC) fast path.

_Source: `compiler/src/runtime/gc/gc.c:457`_

### hphl_tc_free

```c
void hphl_tc_free(void* payload, size_t n);
```

Returns memory back to the thread-local allocation cache.

_Source: `compiler/src/runtime/gc/gc.c:477`_

### hphl_write_barrier

```c
void hphl_write_barrier(void* old_obj, void* field_addr);
```

Generational write barrier updating card table when storing references into existing objects.

_Source: `compiler/src/runtime/gc/gc.c:999`_

### hphl_write_barrier_slow

```c
void hphl_write_barrier_slow(void* old_obj, void* field_addr);
```

Slow path implementation of the generational write barrier card marking.

_Source: `compiler/src/runtime/gc/gc.c:1024`_

## Subsystem `img`

### hphl_img_blur

```c
void* hphl_img_blur(void* px, long long w, long long h, long long r);
```

Applies a box or Gaussian blur with radius `r` to pixel buffer and returns new pixel buffer.

_Source: `compiler/src/runtime/img/img.c:128`_

### hphl_img_flip_h

```c
void* hphl_img_flip_h(void* px, long long w, long long h);
```

Flips pixel buffer horizontally and returns a newly allocated flipped image buffer.

_Source: `compiler/src/runtime/img/img.c:74`_

### hphl_img_grayscale

```c
void* hphl_img_grayscale(void* px);
```

Converts an RGB/RGBA pixel buffer to grayscale intensity values.

_Source: `compiler/src/runtime/img/img.c:54`_

### hphl_img_png_save

```c
long long hphl_img_png_save(const char* path, void* px, long long w, long long h);
```

Saves a pixel buffer to a PNG image file at the specified path.

_Source: `compiler/src/runtime/img/img.c:185`_

### hphl_img_ppm_load

```c
void* hphl_img_ppm_load(const char* path);
```

Loads an image from a PPM file into a newly allocated pixel buffer.

_Source: `compiler/src/runtime/img/img.c:278`_

### hphl_img_ppm_save

```c
long long hphl_img_ppm_save(const char* path, void* px, long long w, long long h);
```

Saves a pixel buffer to a PPM image file at the specified path.

_Source: `compiler/src/runtime/img/img.c:157`_

### hphl_img_resize

```c
void* hphl_img_resize(void* px, long long w, long long h, long long nw, long long nh);
```

Resizes an image pixel buffer from (w, h) to (nw, nh) using bilinear interpolation.

_Source: `compiler/src/runtime/img/img.c:92`_

## Subsystem `mem`

### hphl_mem_alloc

```c
void* hphl_mem_alloc(int64_t size);
```

Allocates `size` bytes of uninitialized raw heap memory.

_Source: `compiler/src/runtime/mem/mem.c:12`_

### hphl_mem_copy

```c
void hphl_mem_copy(void* dst, void* src, int64_t n);
```

Copies `n` bytes from source buffer to destination buffer.

_Source: `compiler/src/runtime/mem/mem.c:90`_

### hphl_mem_copy_off

```c
void hphl_mem_copy_off(void* dst, int64_t dstOff, void* src, int64_t srcOff, int64_t n);
```

Copies `n` bytes between memory buffers with explicit source and destination offsets.

_Source: `compiler/src/runtime/mem/mem.c:95`_

### hphl_mem_fill

```c
void hphl_mem_fill(void* dst, int64_t byteVal, int64_t n);
```

Fills `n` bytes of destination memory with the specified byte value.

_Source: `compiler/src/runtime/mem/mem.c:101`_

### hphl_mem_free

```c
void hphl_mem_free(void* p);
```

Deallocates a block of raw heap memory previously allocated by `hphl_mem_alloc`.

_Source: `compiler/src/runtime/mem/mem.c:17`_

### hphl_mem_peek_f32

```c
double hphl_mem_peek_f32(void* p, int64_t off);
```

Reads a 32-bit floating-point value from raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:73`_

### hphl_mem_peek_f64

```c
double hphl_mem_peek_f64(void* p, int64_t off);
```

Reads a 64-bit floating-point value from raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:84`_

### hphl_mem_peek_i32

```c
int64_t hphl_mem_peek_i32(void* p, int64_t off);
```

Reads a signed 32-bit integer from raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:27`_

### hphl_mem_peek_i64

```c
int64_t hphl_mem_peek_i64(void* p, int64_t off);
```

Reads a signed 64-bit integer from raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:50`_

### hphl_mem_peek_ptr

```c
void* hphl_mem_peek_ptr(void* p, int64_t off);
```

Reads a pointer value from raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:61`_

### hphl_mem_peek_u32

```c
int64_t hphl_mem_peek_u32(void* p, int64_t off);
```

Reads an unsigned 32-bit integer from raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:39`_

### hphl_mem_poke_f32

```c
void hphl_mem_poke_f32(void* p, int64_t off, double v);
```

Writes a 32-bit floating-point value to raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:67`_

### hphl_mem_poke_f64

```c
void hphl_mem_poke_f64(void* p, int64_t off, double v);
```

Writes a 64-bit floating-point value to raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:79`_

### hphl_mem_poke_i32

```c
void hphl_mem_poke_i32(void* p, int64_t off, int64_t v);
```

Writes a signed 32-bit integer to raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:21`_

### hphl_mem_poke_i64

```c
void hphl_mem_poke_i64(void* p, int64_t off, int64_t v);
```

Writes a signed 64-bit integer to raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:45`_

### hphl_mem_poke_ptr

```c
void hphl_mem_poke_ptr(void* p, int64_t off, void* v);
```

Writes a pointer value to raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:56`_

### hphl_mem_poke_u32

```c
void hphl_mem_poke_u32(void* p, int64_t off, int64_t v);
```

Writes an unsigned 32-bit integer to raw memory at the specified byte offset.

_Source: `compiler/src/runtime/mem/mem.c:33`_

### hphl_mem_zero

```c
void hphl_mem_zero(void* dst, int64_t n);
```

Sets `n` bytes of memory at destination buffer to zero.

_Source: `compiler/src/runtime/mem/mem.c:108`_

## Subsystem `net`

### hphl_accept

```c
int64_t hphl_accept(int64_t s);
```

Accepts an incoming TCP connection on a listening socket and returns the client socket descriptor.

_Source: `compiler/src/runtime/net/socket.c:74`_

### hphl_bind

```c
int64_t hphl_bind(int64_t s, int64_t port);
```

Binds a network socket to the specified local port number.

_Source: `compiler/src/runtime/net/socket.c:47`_

### hphl_close_socket

```c
int64_t hphl_close_socket(int64_t s);
```

Closes an open network socket descriptor and releases OS networking resources.

_Source: `compiler/src/runtime/net/socket.c:99`_

### hphl_connect

```c
int64_t hphl_connect(int64_t s, const char* host, int64_t port);
```

Establishes a TCP client connection to the specified remote host and port.

_Source: `compiler/src/runtime/net/socket.c:30`_

### hphl_listen

```c
int64_t hphl_listen(int64_t s, int64_t backlog);
```

Sets a network socket into listening mode with the specified connection backlog queue size.

_Source: `compiler/src/runtime/net/socket.c:71`_

### hphl_recv

```c
char* hphl_recv(int64_t s, int64_t len);
```

Receives incoming bytes from a connected socket up to `len` bytes into a string buffer.

_Source: `compiler/src/runtime/net/socket.c:86`_

### hphl_send

```c
int64_t hphl_send(int64_t s, const char* buf);
```

Sends a string or byte payload over a connected network socket.

_Source: `compiler/src/runtime/net/socket.c:78`_

### hphl_socket

```c
int64_t hphl_socket(int64_t af, int64_t type, int64_t proto);
```

Creates a new OS network socket endpoint with the specified address family, type, and protocol.

_Source: `compiler/src/runtime/net/socket.c:25`_

## Subsystem `strings`

### hphl_ceil

```c
double hphl_ceil(double v);
```

Returns the smallest integral value not less than the input float.

_Source: `compiler/src/runtime/strings/strings.c:166`_

### hphl_e

```c
double hphl_e(void);
```

Returns Euler's mathematical constant e (approx. 2.718281828459045).

_Source: `compiler/src/runtime/strings/strings.c:164`_

### hphl_floor

```c
double hphl_floor(double v);
```

Returns the largest integral value not greater than the input float.

_Source: `compiler/src/runtime/strings/strings.c:165`_

### hphl_fmod

```c
double hphl_fmod(double a, double b);
```

Computes the floating-point remainder of dividing a by b.

_Source: `compiler/src/runtime/strings/strings.c:168`_

### hphl_is_numeric

```c
int64_t hphl_is_numeric(const char* s);
```

Returns 1 if the string consists entirely of numeric decimal digits, 0 otherwise.

_Source: `compiler/src/runtime/strings/strings.c:51`_

### hphl_max_i64

```c
int64_t hphl_max_i64(int64_t a, int64_t b);
```

Returns the greater of two 64-bit signed integers.

_Source: `compiler/src/runtime/strings/strings.c:162`_

### hphl_min_i64

```c
int64_t hphl_min_i64(int64_t a, int64_t b);
```

Returns the lesser of two 64-bit signed integers.

_Source: `compiler/src/runtime/strings/strings.c:161`_

### hphl_parse_f64

```c
double hphl_parse_f64(const char* s);
```

Parses a 64-bit floating-point number from a null-terminated string.

_Source: `compiler/src/runtime/strings/strings.c:46`_

### hphl_parse_int

```c
int64_t hphl_parse_int(const char* s);
```

Parses a 64-bit signed integer from a null-terminated string.

_Source: `compiler/src/runtime/strings/strings.c:34`_

### hphl_pi

```c
double hphl_pi(void);
```

Returns the mathematical constant Pi (approx. 3.141592653589793).

_Source: `compiler/src/runtime/strings/strings.c:163`_

### hphl_round

```c
double hphl_round(double v);
```

Rounds a floating-point value to the nearest integer, rounding halfway cases away from zero.

_Source: `compiler/src/runtime/strings/strings.c:167`_

### hphl_split

```c
void* hphl_split(const char* s, const char* sep);
```

Splits a string into a dynamic list of substrings separated by the specified delimiter.

_Source: `compiler/src/runtime/strings/strings.c:1`_

### hphl_sqrt

```c
double hphl_sqrt(double v);
```

Computes the square root of a floating-point number.

_Source: `compiler/src/runtime/strings/strings.c:170`_

### hphl_str_char_at

```c
char* hphl_str_char_at(const char* s, int64_t i);
```

Returns a single-character string containing the character at index `i`.

_Source: `compiler/src/runtime/strings/strings.c:79`_

### hphl_str_char_index

```c
int64_t hphl_str_char_index(const char* s, int64_t i);
```

Returns the byte index corresponding to character index `i` in a UTF-8 string.

_Source: `compiler/src/runtime/strings/strings.c:90`_

### hphl_str_chr

```c
char* hphl_str_chr(int64_t c);
```

Converts an integer Unicode/ASCII code point into a single-character string.

_Source: `compiler/src/runtime/strings/strings.c:103`_

### hphl_str_concat

```c
char* hphl_str_concat(const char* a, const char* b);
```

Concatenates two strings and returns a newly allocated combined string.

_Source: `compiler/src/runtime/strings/strings.c:217`_

### hphl_str_contains

```c
int64_t hphl_str_contains(const char* s, const char* needle);
```

Returns 1 if the substring is found within the source string, 0 otherwise.

_Source: `compiler/src/runtime/strings/strings.c:123`_

### hphl_str_down

```c
char* hphl_str_down(const char* s);
```

Returns a copy of the string with all characters converted to lowercase.

_Source: `compiler/src/runtime/strings/strings.c:144`_

### hphl_str_ends

```c
int64_t hphl_str_ends(const char* s, const char* suf);
```

Returns 1 if the string ends with the specified suffix, 0 otherwise.

_Source: `compiler/src/runtime/strings/strings.c:130`_

### hphl_str_format

```c
char* hphl_str_format(const char* fmt, const char* arg);
```

Formats a string using a printf-style format specifier and string argument.

_Source: `compiler/src/runtime/strings/strings.c:262`_

### hphl_str_format_float

```c
char* hphl_str_format_float(const char* fmt, double v);
```

Formats a 64-bit floating-point number using the given format specifier.

_Source: `compiler/src/runtime/strings/strings.c:286`_

### hphl_str_format_int

```c
char* hphl_str_format_int(const char* fmt, int64_t v);
```

Formats a 64-bit signed integer using the given format specifier.

_Source: `compiler/src/runtime/strings/strings.c:282`_

### hphl_str_free

```c
void hphl_str_free(char* s);
```

Deallocates an allocated string buffer.

_Source: `compiler/src/runtime/strings/strings.c:211`_

### hphl_str_from_bool

```c
char* hphl_str_from_bool(int b);
```

Converts a boolean value to its string representation ('true' or 'false').

_Source: `compiler/src/runtime/strings/strings.c:195`_

### hphl_str_from_char

```c
char* hphl_str_from_char(int c);
```

Converts a character code to a single-character string.

_Source: `compiler/src/runtime/strings/strings.c:203`_

### hphl_str_from_float

```c
char* hphl_str_from_float(double v);
```

Converts a floating-point number to its decimal string representation.

_Source: `compiler/src/runtime/strings/strings.c:188`_

### hphl_str_from_int

```c
char* hphl_str_from_int(long long v);
```

Converts a 64-bit signed integer to its decimal string representation.

_Source: `compiler/src/runtime/strings/strings.c:174`_

### hphl_str_from_uint

```c
char* hphl_str_from_uint(unsigned long long v);
```

Converts an unsigned 64-bit integer to its decimal string representation.

_Source: `compiler/src/runtime/strings/strings.c:181`_

### hphl_str_index_of

```c
int64_t hphl_str_index_of(const char* s, const char* needle);
```

Returns the 0-based index of the first occurrence of the substring, or -1 if not found.

_Source: `compiler/src/runtime/strings/strings.c:73`_

### hphl_str_last_index_of

```c
int64_t hphl_str_last_index_of(const char* s, const char* needle);
```

Returns the 0-based index of the last occurrence of the substring, or -1 if not found.

_Source: `compiler/src/runtime/strings/strings.c:112`_

### hphl_str_len

```c
int64_t hphl_str_len(const char* s);
```

Returns the length of the string in characters (or bytes for ASCII).

_Source: `compiler/src/runtime/strings/strings.c:60`_

### hphl_str_ord

```c
int64_t hphl_str_ord(const char* s);
```

Returns the character code of the first character of the string.

_Source: `compiler/src/runtime/strings/strings.c:97`_

### hphl_str_ord_at

```c
int64_t hphl_str_ord_at(const char* s, int64_t i);
```

Returns the character code at the specified index in the string.

_Source: `compiler/src/runtime/strings/strings.c:100`_

### hphl_str_pad_left

```c
char* hphl_str_pad_left(const char* s, int64_t n, const char* fill);
```

Pads the string on the left side with the fill string until reaching length `n`.

_Source: `compiler/src/runtime/strings/strings.c:231`_

### hphl_str_pad_right

```c
char* hphl_str_pad_right(const char* s, int64_t n, const char* fill);
```

Pads the string on the right side with the fill string until reaching length `n`.

_Source: `compiler/src/runtime/strings/strings.c:246`_

### hphl_str_starts

```c
int64_t hphl_str_starts(const char* s, const char* pre);
```

Returns 1 if the string begins with the specified prefix, 0 otherwise.

_Source: `compiler/src/runtime/strings/strings.c:126`_

### hphl_str_sub

```c
char* hphl_str_sub(const char* s, int64_t start, int64_t n);
```

Returns a substring starting at `start` index with length `n`.

_Source: `compiler/src/runtime/strings/strings.c:61`_

### hphl_str_trim

```c
char* hphl_str_trim(const char* s);
```

Returns a copy of the string with leading and trailing whitespace removed.

_Source: `compiler/src/runtime/strings/strings.c:152`_

### hphl_str_up

```c
char* hphl_str_up(const char* s);
```

Returns a copy of the string with all characters converted to uppercase.

_Source: `compiler/src/runtime/strings/strings.c:136`_
