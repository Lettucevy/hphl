/* mem.c — FFI v2: memória raw para interop com C/Vulkan.
 *
 * Permite montar structs C byte a byte (ex.: VkInstanceCreateInfo) e
 * passar seus endereços para `extern` via `ptr` + `addr_of`.
 * Ponteiros aqui NÃO são gerenciados pelo GC: o usuário aloca/libera.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* aloca size bytes zerados; devolve NULL se size <= 0 ou sem memória */
void* hphl_mem_alloc(int64_t size) {
  if (size <= 0) return NULL;
  return calloc((size_t)1, (size_t)size);
}

void hphl_mem_free(void* p) {
  if (p) free(p);
}

void hphl_mem_poke_i32(void* p, int64_t off, int64_t v) {
  if (!p) return;
  int32_t x = (int32_t)v;
  memcpy((char*)p + off, &x, 4);
}

int64_t hphl_mem_peek_i32(void* p, int64_t off) {
  int32_t x = 0;
  if (p) memcpy(&x, (const char*)p + off, 4);
  return (int64_t)x;
}

void hphl_mem_poke_u32(void* p, int64_t off, int64_t v) {
  if (!p) return;
  uint32_t x = (uint32_t)v;
  memcpy((char*)p + off, &x, 4);
}

int64_t hphl_mem_peek_u32(void* p, int64_t off) {
  uint32_t x = 0;
  if (p) memcpy(&x, (const char*)p + off, 4);
  return (int64_t)x;
}

void hphl_mem_poke_i64(void* p, int64_t off, int64_t v) {
  if (!p) return;
  memcpy((char*)p + off, &v, 8);
}

int64_t hphl_mem_peek_i64(void* p, int64_t off) {
  int64_t x = 0;
  if (p) memcpy(&x, (const char*)p + off, 8);
  return x;
}

void hphl_mem_poke_ptr(void* p, int64_t off, void* v) {
  if (!p) return;
  memcpy((char*)p + off, &v, sizeof(void*));
}

void* hphl_mem_peek_ptr(void* p, int64_t off) {
  void* x = NULL;
  if (p) memcpy(&x, (const char*)p + off, sizeof(void*));
  return x;
}

void hphl_mem_poke_f32(void* p, int64_t off, double v) {
  if (!p) return;
  float x = (float)v;
  memcpy((char*)p + off, &x, 4);
}

double hphl_mem_peek_f32(void* p, int64_t off) {
  float x = 0.0f;
  if (p) memcpy(&x, (const char*)p + off, 4);
  return (double)x;
}

void hphl_mem_poke_f64(void* p, int64_t off, double v) {
  if (!p) return;
  memcpy((char*)p + off, &v, 8);
}

double hphl_mem_peek_f64(void* p, int64_t off) {
  double x = 0.0;
  if (p) memcpy(&x, (const char*)p + off, 8);
  return x;
}

void hphl_mem_copy(void* dst, void* src, int64_t n) {
  if (!dst || !src || n <= 0) return;
  memcpy(dst, src, (size_t)n);
}

void hphl_mem_copy_off(void* dst, int64_t dstOff, void* src, int64_t srcOff,
                       int64_t n) {
  if (!dst || !src || n <= 0 || dstOff < 0 || srcOff < 0) return;
  memcpy((char*)dst + dstOff, (const char*)src + srcOff, (size_t)n);
}

void hphl_mem_fill(void* dst, int64_t byteVal, int64_t n) {
  if (!dst || n <= 0) return;
  memset(dst, (int)(byteVal & 0xFF), (size_t)n);
}

/* mem_zero(dst, n): zera n bytes a partir de dst (assinatura sem ambiguidade
 * de offset, ao contrario de mem_fill(dst, byteVal, n)). */
void hphl_mem_zero(void* dst, int64_t n) {
  if (!dst || n <= 0) return;
  memset(dst, 0, (size_t)n);
}
