/* -------------------------------------------------------------------------
 * img.* — builtins de manipulação de imagens (M_RV1 IMG).
 *
 * Modelo: imagem = list<int> RGB flat ([r0,g0,b0, r1,g1,b1, ...], linha
 * a linha) + largura/altura como int64. Todas as operações devolvem
 * lista NOVA (sem mutação); tamanhos inconsistentes são completados
 * com zero (sem panic).
 * ------------------------------------------------------------------------- */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

static long long img_clamp_ll(long long v, long long lo, long long hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static int img_clamp_u8(long long v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (int)v;
}

/* copia os pixels p/ buffer RGB temporário (w*h*3, zero-fill se curto).
 * Devolve NULL se w/h inválidos. */
static long long* img_fetch(void* px, long long w, long long h) {
  if (w <= 0 || h <= 0) return NULL;
  if (w > 100000 || h > 100000) return NULL;
  if (w > 0 && h > (long long)SIZE_MAX / (size_t)w / 3) return NULL;
  size_t n = (size_t)w * (size_t)h * 3;
  long long* buf = (long long*)calloc(n, sizeof(long long));
  if (!buf) return NULL;
  long long have = hphl_list_len(px);
  long long* items = (long long*)hphl_list_data(px);
  if (items && have > 0) {
    long long m = have < (long long)n ? have : (long long)n;
    for (long long i = 0; i < m; i++) buf[i] = items[i];
  }
  return buf;
}

static void* img_wrap(long long* buf, long long n) {
  void* out = hphl_list_new();
  if (!out) { free(buf); return NULL; }
  for (long long i = 0; i < n; i++) hphl_list_add_i(out, buf[i]);
  free(buf);
  return out;
}

/* grayscale: luminância Rec.601 */
void* hphl_img_grayscale(void* px) {
  long long n = hphl_list_len(px);
  long long* items = (long long*)hphl_list_data(px);
  void* out = hphl_list_new();
  if (!out) return NULL;
  long long triples = n / 3;
  for (long long i = 0; i < triples; i++) {
    long long r = items ? items[i * 3] : 0;
    long long g = items ? items[i * 3 + 1] : 0;
    long long b = items ? items[i * 3 + 2] : 0;
    long long y = (299 * r + 587 * g + 114 * b + 500) / 1000;
    y = img_clamp_ll(y, 0, 255);
    hphl_list_add_i(out, y);
    hphl_list_add_i(out, y);
    hphl_list_add_i(out, y);
  }
  return out;
}

/* flip horizontal (espelho esquerda-direita) */
void* hphl_img_flip_h(void* px, long long w, long long h) {
  long long* buf = img_fetch(px, w, h);
  if (!buf) return hphl_list_new();
  size_t n = (size_t)w * (size_t)h * 3;
  long long* out = (long long*)malloc(n * sizeof(long long));
  if (!out) { free(buf); return hphl_list_new(); }
  for (long long y = 0; y < h; y++) {
    for (long long x = 0; x < w; x++) {
      long long sx = w - 1 - x;
      for (int k = 0; k < 3; k++)
        out[(y * w + x) * 3 + k] = buf[(y * w + sx) * 3 + k];
    }
  }
  free(buf);
  return img_wrap(out, (long long)n);
}

/* resize bilinear para nw x nh */
void* hphl_img_resize(void* px, long long w, long long h,
                       long long nw, long long nh) {
  long long* buf = img_fetch(px, w, h);
  if (!buf || nw <= 0 || nh <= 0) {
    if (buf) free(buf);
    return hphl_list_new();
  }
  size_t n = (size_t)nw * (size_t)nh * 3;
  long long* out = (long long*)malloc(n * sizeof(long long));
  if (!out) { free(buf); return hphl_list_new(); }
  for (long long y = 0; y < nh; y++) {
    double sy = (nh == 1) ? 0.0 : (double)y * (h - 1) / (nh - 1);
    long long y0 = (long long)sy;
    long long y1 = y0 + 1 < h ? y0 + 1 : h - 1;
    double fy = sy - y0;
    for (long long x = 0; x < nw; x++) {
      double sx = (nw == 1) ? 0.0 : (double)x * (w - 1) / (nw - 1);
      long long x0 = (long long)sx;
      long long x1 = x0 + 1 < w ? x0 + 1 : w - 1;
      double fx = sx - x0;
      for (int k = 0; k < 3; k++) {
        double a = (double)buf[(y0 * w + x0) * 3 + k];
        double b = (double)buf[(y0 * w + x1) * 3 + k];
        double c = (double)buf[(y1 * w + x0) * 3 + k];
        double d = (double)buf[(y1 * w + x1) * 3 + k];
        double v = a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) +
                   c * (1 - fx) * fy + d * fx * fy;
        out[(y * nw + x) * 3 + k] = img_clamp_u8((long long)(v + 0.5));
      }
    }
  }
  free(buf);
  return img_wrap(out, (long long)n);
}

/* box blur de raio r (janela (2r+1)^2, bordas por clamp) */
void* hphl_img_blur(void* px, long long w, long long h, long long r) {
  if (r < 0) r = 0;
  if (r > 64) r = 64;
  long long* buf = img_fetch(px, w, h);
  if (!buf) return hphl_list_new();
  size_t n = (size_t)w * (size_t)h * 3;
  long long* out = (long long*)malloc(n * sizeof(long long));
  if (!out) { free(buf); return hphl_list_new(); }
  for (long long y = 0; y < h; y++) {
    for (long long x = 0; x < w; x++) {
      for (int k = 0; k < 3; k++) {
        long long acc = 0, cnt = 0;
        for (long long j = y - r; j <= y + r; j++) {
          long long jj = img_clamp_ll(j, 0, h - 1);
          for (long long i = x - r; i <= x + r; i++) {
            long long ii = img_clamp_ll(i, 0, w - 1);
            acc += buf[(jj * w + ii) * 3 + k];
            cnt++;
          }
        }
        out[(y * w + x) * 3 + k] = img_clamp_u8((acc + cnt / 2) / cnt);
      }
    }
  }
  free(buf);
  return img_wrap(out, (long long)n);
}

/* PPM P3 (texto) em arquivo. Devolve 1 ok / 0 erro. */
long long hphl_img_ppm_save(const char* path, void* px, long long w,
                            long long h) {
  if (!path || w <= 0 || h <= 0) return 0;
  long long* buf = img_fetch(px, w, h);
  if (!buf) return 0;
  FILE* f = fopen(path, "w");
  if (!f) { free(buf); return 0; }
  fprintf(f, "P3\n%lld %lld\n255\n", w, h);
  for (long long y = 0; y < h; y++) {
    for (long long x = 0; x < w; x++) {
      long long i = (y * w + x) * 3;
      fprintf(f, "%d %d %d\n", img_clamp_u8(buf[i]),
              img_clamp_u8(buf[i + 1]), img_clamp_u8(buf[i + 2]));
    }
  }
  fclose(f);
  free(buf);
  return 1;
}

static void png_u32(unsigned char* p, unsigned long v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

/* PNG truecolor 8-bit (filtro 0) via zlib. Devolve 1 ok / 0 erro. */
long long hphl_img_png_save(const char* path, void* px, long long w,
                            long long h) {
  if (!path || w <= 0 || h <= 0 || w > 100000 || h > 100000) return 0;
  long long* buf = img_fetch(px, w, h);
  if (!buf) return 0;
  size_t rowBytes = (size_t)w * 3;
  size_t rawSize = (size_t)h * (rowBytes + 1);
  unsigned char* raw = (unsigned char*)malloc(rawSize);
  if (!raw) { free(buf); return 0; }
  for (long long y = 0; y < h; y++) {
    unsigned char* row = raw + (size_t)y * (rowBytes + 1);
    row[0] = 0;
    for (long long x = 0; x < w; x++) {
      long long i = (y * w + x) * 3;
      row[1 + x * 3] = (unsigned char)img_clamp_u8(buf[i]);
      row[1 + x * 3 + 1] = (unsigned char)img_clamp_u8(buf[i + 1]);
      row[1 + x * 3 + 2] = (unsigned char)img_clamp_u8(buf[i + 2]);
    }
  }
  free(buf);
  uLong compSize = compressBound((uLong)rawSize);
  unsigned char* comp = (unsigned char*)malloc(compSize);
  if (!comp) { free(raw); return 0; }
  if (compress(comp, &compSize, raw, (uLong)rawSize) != Z_OK) {
    free(raw);
    free(comp);
    return 0;
  }
  free(raw);
  FILE* f = fopen(path, "wb");
  if (!f) { free(comp); return 0; }
  static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  unsigned char head[8 + 8 + 13 + 4]; /* sig + len+tipo + IHDR + crc */
  memcpy(head, sig, 8);
  png_u32(head + 8, 13);
  memcpy(head + 12, "IHDR", 4);
  png_u32(head + 16, (unsigned long)w);
  png_u32(head + 20, (unsigned long)h);
  head[24] = 8;
  head[25] = 2;
  head[26] = 0;
  head[27] = 0;
  head[28] = 0;
  png_u32(head + 29, crc32(crc32(0L, (const Bytef*)"IHDR", 4), head + 16, 13));
  unsigned char iend[12];
  png_u32(iend, 0);
  memcpy(iend + 4, "IEND", 4);
  png_u32(iend + 8, crc32(0L, (const Bytef*)"IEND", 4));
  int ok = 1;
  if (fwrite(head, 1, sizeof head, f) != sizeof head) ok = 0;
  if (ok) {
    unsigned char dh[8];
    png_u32(dh, (unsigned long)compSize);
    memcpy(dh + 4, "IDAT", 4);
    if (fwrite(dh, 1, 8, f) != 8) ok = 0;
  }
  if (ok && fwrite(comp, 1, compSize, f) != compSize) ok = 0;
  if (ok) {
    unsigned char dcrc[4];
    png_u32(dcrc, crc32(crc32(0L, (const Bytef*)"IDAT", 4), comp,
                        (uInt)compSize));
    if (fwrite(dcrc, 1, 4, f) != 4) ok = 0;
  }
  if (ok && fwrite(iend, 1, sizeof iend, f) != sizeof iend) ok = 0;
  fclose(f);
  free(comp);
  return ok ? 1 : 0;
}

/* leitor de int (pula ws e comentários '#...' do PPM) */
static int img_next_int(const char** p, long long* v) {
  const char* s = *p;
  for (;;) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '#') {
      while (*s && *s != '\n') s++;
      continue;
    }
    break;
  }
  if (!*s) return 0;
  int neg = 0;
  if (*s == '-') { neg = 1; s++; }
  else if (*s == '+') s++;
  if (*s < '0' || *s > '9') return 0;
  long long acc = 0;
  while (*s >= '0' && *s <= '9') { acc = acc * 10 + (*s - '0'); s++; }
  *v = neg ? -acc : acc;
  *p = s;
  return 1;
}

/* PPM P3 -> lista [w, h, r,g,b...]. Lista vazia se erro. */
void* hphl_img_ppm_load(const char* path) {
  void* out = hphl_list_new();
  if (!out || !path) return out ? out : hphl_list_new();
  FILE* f = fopen(path, "r");
  if (!f) return out;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > (1 << 26)) { fclose(f); return out; }
  char* txt = (char*)malloc((size_t)sz + 1);
  if (!txt) { fclose(f); return out; }
  size_t rd = fread(txt, 1, (size_t)sz, f);
  fclose(f);
  txt[rd] = 0;
  const char* p = txt;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (p[0] != 'P' || p[1] != '3') { free(txt); return out; }
  p += 2;
  long long w = 0, h = 0, mx = 0;
  if (!img_next_int(&p, &w) || !img_next_int(&p, &h) ||
      !img_next_int(&p, &mx)) {
    free(txt);
    return out;
  }
  if (w <= 0 || h <= 0 || w > 100000 || h > 100000 || mx <= 0 || mx > 65535) {
    free(txt);
    return out;
  }
  hphl_list_add_i(out, w);
  hphl_list_add_i(out, h);
  for (long long i = 0; i < w * h * 3; i++) {
    long long v = 0;
    if (!img_next_int(&p, &v)) break;
    if (mx != 255) v = v * 255 / mx;
    hphl_list_add_i(out, img_clamp_u8(v));
  }
  free(txt);
  return out;
}
