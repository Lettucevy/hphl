#pragma once
#include "runtime.h"
void hphl_panic(const char* msg);
void hphl_dbg_exc_report(int cod, const char* msg);
void hphl_install_crash_handler(void);
void* hphl_exc_new(void);
void  hphl_exc_free(void* p);
void  hphl_exc_push(void* p);
int   hphl_exc_begin(void* rec);
void  hphl_exc_end(void* p);
void* hphl_exc_payload(void* p);
void  hphl_throw(void* payload);
void* hphl_box_i64(long long v);
long long hphl_unbox_i64(void* p);
/* COMPAT-2 (Sprint 4): versão de ABI do runtime. Programa aborta se
 * não bater com HPHL_RUNTIME_ABI_VERSION do header. */
int hphl_runtime_abi_version(void);
void* hphl_list_new(void);
void hphl_list_add_i(void*, long long);
long long hphl_list_len(void*);
void* hphl_list_data(void*);
void hphl_gc_register(void*);
void hphl_gc_unregister(void*);
long long hphl_gc_sweep(void);
int hphl_gc_step(int budget);
void hphl_gc_cards_clear(void);
void hphl_write_barrier(void* old_obj, void* field_addr);
void hphl_write_barrier_slow(void* old_obj, void* field_addr);
/* M29: conservative stack scan + thread cache (BDWGC/rpmalloc-inspired) */
void hphl_gc_push_stack(void* lo_addr, void* hi_addr);
void* hphl_tc_alloc(size_t n);
void  hphl_tc_free(void* p, size_t n);
/* M29: batch GC root registration (evita 64 bytes de stack por root) */
void hphl_gc_add_roots_batch(void** slots, int n);
void hphl_gc_remove_roots_batch(void** slots, int n);
/* M29 FIX: versão baseada em offsets (endereços calculados em runtime) */
/* Offsets are emitted as assembler .quad values. Do not use long here:
 * Windows uses LLP64, where long is only 32 bits. */
void hphl_gc_register_slots(const int64_t* offs, void* base, int n);
void hphl_gc_unregister_slots(const int64_t* offs, void* base, int n);
void hphl_gc_pop_roots(int n);
/* M31: strings gerenciadas pelo GC (header + registro; sweep coleta) */
void* hphl_str_alloc(size_t n);
int hphl_gc_is_managed(void* p);
void hphl_gc_free_managed(void* p);
