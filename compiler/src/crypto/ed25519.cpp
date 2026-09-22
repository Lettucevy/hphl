// ============================================================================
// ed25519.cpp — Verificação Ed25519 (RFC 8032)
//
// Implementação educacional. NÃO constant-time. Usa bigint de 256 bits
// em little-endian (5 limbs de 64 bits).
// ============================================================================

#include "ed25519.h"
#include <string.h>
#include <stdlib.h>

// ----------------------------------------------------------------------------
// Constantes — Ed25519 (RFC 8032)
// ----------------------------------------------------------------------------

// Ordem L do subgrupo: 2^252 + 27742317777372353535851937790883648493
// = 0x1000000000000000000000000000000014DEF9DEA2F79CD65812631A5CF5D3ED
// Em LE 64-bit limbs:
static const uint64_t L_LIMBS[5] = {
    0x5812631A5CF5D3EDULL, // bits 0-63
    0x14DEF9DEA2F79CD6ULL, // bits 64-127
    0x0000000000000000ULL, // bits 128-191
    0x1000000000000000ULL, // bits 192-255 (bit 252 é bit 60 deste limb = 0x1000000000000000)
    0x0000000000000000ULL  // bits 256+
};

// Primo do campo: p = 2^255 - 19
// LE 64-bit limbs: p = 0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFED
// (limb 0..3 com bits 0-255, limb 4 = 0)
static const uint64_t P_LIMBS[5] = {
    0xFFFFFFFFFFFFFFEDULL, // bits 0-63
    0xFFFFFFFFFFFFFFFFULL, // bits 64-127
    0xFFFFFFFFFFFFFFFFULL, // bits 128-191
    0x7FFFFFFFFFFFFFFFULL, // bits 192-255 (bit 254 setado, todos os outros = 1)
    0x0000000000000000ULL  // bits 256+ (zero)
};

// Base point B (em extended coords)
//   B_y = 4/5 mod p = 0x6666666666666666666666666666666666666666666666666666666666666658
//   Em LE 64-bit limbs:
static const uint64_t B_Y_LIMBS[5] = {
    0x6666666666666658ULL, // bits 0-63 (low byte é 0x58)
    0x6666666666666666ULL, // bits 64-127
    0x6666666666666666ULL, // bits 128-191
    0x6666666666666666ULL, // bits 192-255
    0x0000000000000000ULL
};

// B_x (X coordinate of base point)
// = 0x216936D3CD6E53FEC0A4E231FDD6DC5C692CC7609525A7B2C9562D608F25D51A
// Em LE 64-bit limbs:
static const uint64_t B_X_LIMBS[5] = {
    0xC9562D608F25D51AULL, // bits 0-63
    0x692CC7609525A7B2ULL, // bits 64-127
    0xC0A4E231FDD6DC5CULL, // bits 128-191
    0x216936D3CD6E53FEULL, // bits 192-255
    0x0000000000000000ULL
};

// ----------------------------------------------------------------------------
// BigInt 256-bit: operações básicas
// ----------------------------------------------------------------------------

static void bint_zero(hphl_bint_t* x) {
    x->d[0] = x->d[1] = x->d[2] = x->d[3] = x->d[4] = 0;
}

static void bint_copy(hphl_bint_t* dst, const hphl_bint_t* src) {
    dst->d[0] = src->d[0]; dst->d[1] = src->d[1]; dst->d[2] = src->d[2];
    dst->d[3] = src->d[3]; dst->d[4] = src->d[4];
}

static void bint_from_bytes(hphl_bint_t* x, const uint8_t b[32]) {
    // little-endian: byte 0 → limb 0 low
    for (int i = 0; i < 5; i++) x->d[i] = 0;
    for (int i = 0; i < 32; i++) {
        x->d[i / 8] |= ((uint64_t)b[i]) << ((i % 8) * 8);
    }
}

static void bint_to_bytes(const hphl_bint_t* x, uint8_t b[32]) {
    for (int i = 0; i < 32; i++) {
        b[i] = (uint8_t)((x->d[i / 8] >> ((i % 8) * 8)) & 0xFF);
    }
}

static void bint_from_limbs(hphl_bint_t* x, const uint64_t limbs[5]) {
    for (int i = 0; i < 5; i++) x->d[i] = limbs[i];
}

static void bint_set_one(hphl_bint_t* x) {
    x->d[0] = 1;
    x->d[1] = x->d[2] = x->d[3] = x->d[4] = 0;
}

// ----------------------------------------------------------------------------
// Aritmética de 256 bits: add, sub, comparação (schoolbook, LE)
// ----------------------------------------------------------------------------

static int bint_cmp(const hphl_bint_t* a, const hphl_bint_t* b) {
    for (int i = 4; i >= 0; i--) {
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    }
    return 0;
}

static int bint_is_zero(const hphl_bint_t* a) {
    return (a->d[0] | a->d[1] | a->d[2] | a->d[3] | a->d[4]) == 0;
}

// a += b  (assume a+b < 2^257)
static void bint_add(hphl_bint_t* a, const hphl_bint_t* b) {
    unsigned __int128 carry = 0;
    for (int i = 0; i < 5; i++) {
        unsigned __int128 sum = (unsigned __int128)a->d[i] + b->d[i] + carry;
        a->d[i] = (uint64_t)sum;
        carry = sum >> 64;
    }
}

// a -= b  (assume a >= b)
static void bint_sub(hphl_bint_t* a, const hphl_bint_t* b) {
    unsigned __int128 borrow = 0;
    for (int i = 0; i < 5; i++) {
        unsigned __int128 diff = (unsigned __int128)a->d[i] - b->d[i] - borrow;
        a->d[i] = (uint64_t)diff;
        borrow = (diff >> 64) & 1;
    }
}

// ----------------------------------------------------------------------------
// Multiplicação 256x256 → 512 bits (schoolbook LE com unsigned __int128)
// ----------------------------------------------------------------------------

static void bint_mul_256x256(const hphl_bint_t* a, const hphl_bint_t* b, uint64_t r[10]) {
    for (int i = 0; i < 10; i++) r[i] = 0;
    for (int i = 0; i < 5; i++) {
        unsigned __int128 carry = 0;
        for (int j = 0; j < 5; j++) {
            unsigned __int128 prod = (unsigned __int128)a->d[i] * (unsigned __int128)b->d[j];
            unsigned __int128 sum = (unsigned __int128)r[i + j] + prod + carry;
            r[i + j] = (uint64_t)sum;
            carry = sum >> 64;
        }
        int k = i + 5;
        while (carry && k < 10) {
            unsigned __int128 sum = (unsigned __int128)r[k] + carry;
            r[k] = (uint64_t)sum;
            carry = sum >> 64;
            k++;
        }
    }
}

// ----------------------------------------------------------------------------
// Redução mod p (2^255 - 19) para um valor 512-bit r[0..9]
//
// Para p = 2^255 - 19: 2^256 ≡ 38 (mod p).
// r = r_low + 38 * r_high (mod p) onde r_low = r[0..3], r_high = r[4..9].
// ----------------------------------------------------------------------------

void bint_reduce_mod_p(const uint64_t r[10], hphl_bint_t* out) {
    uint64_t t[10];
    for (int i = 0; i < 10; i++) t[i] = r[i];

    hphl_bint_t p;
    bint_from_limbs(&p, P_LIMBS);

    // Iterativamente reduz t[4..9] acumulando t_low + 38 * t_high
    for (int iter = 0; iter < 5; iter++) {
        bool has_high = false;
        for (int i = 4; i < 10; i++) {
            if (t[i] != 0) { has_high = true; break; }
        }
        if (!has_high) break;

        uint64_t hi38[7] = {0};
        unsigned __int128 carry = 0;
        for (int i = 0; i < 6; i++) {
            unsigned __int128 prod = (unsigned __int128)t[4 + i] * 38ULL + carry;
            hi38[i] = (uint64_t)prod;
            carry = prod >> 64;
        }
        hi38[6] = (uint64_t)carry;

        for (int i = 4; i < 10; i++) t[i] = 0;

        carry = 0;
        for (int i = 0; i < 7; i++) {
            unsigned __int128 sum = (unsigned __int128)t[i] + hi38[i] + carry;
            t[i] = (uint64_t)sum;
            carry = sum >> 64;
        }
    }

    hphl_bint_t v;
    for (int i = 0; i < 5; i++) v.d[i] = t[i];

    while (bint_cmp(&v, &p) >= 0) {
        bint_sub(&v, &p);
    }

    bint_copy(out, &v);
}

// ----------------------------------------------------------------------------
// Redução mod L (2^252 + 27742317777372353535851937790883648493)
// Divisão binária bit-a-bit exata de r[0..9] por L.
// ----------------------------------------------------------------------------

void bint_reduce_mod_L(const uint64_t r[10], hphl_bint_t* out) {
    hphl_bint_t L;
    bint_from_limbs(&L, L_LIMBS);

    // rem em 10 limbs (512 bits) para acomodar crescimento durante shift-left
    uint64_t rem[10] = {0};

    for (int bit = 511; bit >= 0; bit--) {
        int word_idx = bit / 64;
        int bit_idx = bit % 64;
        uint64_t in_bit = (r[word_idx] >> bit_idx) & 1ULL;

        // rem <<= 1, OR in_bit
        uint64_t carry = in_bit;
        for (int i = 0; i < 10; i++) {
            uint64_t next_carry = rem[i] >> 63;
            rem[i] = (rem[i] << 1) | carry;
            carry = next_carry;
        }

        // Se rem >= L, subtrai. Comparação de 512 bits com 256 bits.
        // Como L < 2^256 e rem < 2^512, basta comparar high 256 bits de rem
        // com 0, e se for 0, comparar low 256 bits com L.
        bool high_nonzero = false;
        for (int i = 5; i < 10; i++) {
            if (rem[i] != 0) { high_nonzero = true; break; }
        }
        bool ge = high_nonzero;
        if (!high_nonzero) {
            for (int i = 4; i >= 0; i--) {
                if (rem[i] != L.d[i]) { ge = rem[i] > L.d[i]; break; }
            }
        }

        if (ge) {
            // Subtrai L de rem (low 256 bits, propaga borrow para high)
            uint64_t borrow = 0;
            for (int i = 0; i < 5; i++) {
                uint64_t diff = rem[i] - L.d[i] - borrow;
                borrow = (rem[i] < L.d[i] + borrow) ? 1 : 0;
                rem[i] = diff;
            }
            // Propaga borrow para high limbs
            for (int i = 5; i < 10 && borrow; i++) {
                uint64_t diff = rem[i] - borrow;
                borrow = (rem[i] < borrow) ? 1 : 0;
                rem[i] = diff;
            }
        }
    }

    // Resultado em low 256 bits
    for (int i = 0; i < 5; i++) out->d[i] = rem[i];
    for (int i = 5; i < 5; i++) {}  // noop
}

// ----------------------------------------------------------------------------
// Modular multiplication: a * b mod p
// ----------------------------------------------------------------------------

void bint_mod_mul_p(const hphl_bint_t* a, const hphl_bint_t* b, hphl_bint_t* out) {
    uint64_t r[10];
    bint_mul_256x256(a, b, r);
    bint_reduce_mod_p(r, out);
}

static void bint_mod_mul_L(const hphl_bint_t* a, const hphl_bint_t* b, hphl_bint_t* out) {
    uint64_t r[10];
    bint_mul_256x256(a, b, r);
    bint_reduce_mod_L(r, out);
}

// ----------------------------------------------------------------------------
// Field operations mod p: add, sub, neg, inv (Fermat)
// ----------------------------------------------------------------------------

static void field_add(hphl_bint_t* a, const hphl_bint_t* b) {
    hphl_bint_t p;
    bint_from_limbs(&p, P_LIMBS);
    bint_add(a, b);
    if (bint_cmp(a, &p) >= 0) bint_sub(a, &p);
}

static void field_sub(hphl_bint_t* a, const hphl_bint_t* b) {
    hphl_bint_t p;
    bint_from_limbs(&p, P_LIMBS);
    // se a < b, a += p antes
    if (bint_cmp(a, b) < 0) bint_add(a, &p);
    bint_sub(a, b);
}

static void field_mul(hphl_bint_t* a, const hphl_bint_t* b) {
    bint_mod_mul_p(a, b, a);
}

// inversão via Fermat: a^(p-2) mod p usando square-and-multiply
static void field_inv(hphl_bint_t* a) {
    hphl_bint_t result, base;
    hphl_bint_t p;
    bint_from_limbs(&p, P_LIMBS);
    // exp = p - 2
    hphl_bint_t exp;
    bint_copy(&exp, &p);
    exp.d[0] -= 2;
    bint_copy(&base, a);
    bint_from_limbs(&result, (const uint64_t[]) {1,0,0,0,0});
    // square-and-multiply
    for (int bit = 0; bit < 256; bit++) {
        int limb_idx = bit / 64;
        int bit_idx = bit % 64;
        int exp_bit = (exp.d[limb_idx] >> bit_idx) & 1;
        if (exp_bit) {
            bint_mod_mul_p(&result, &base, &result);
        }
        if (bit < 255) {
            bint_mod_mul_p(&base, &base, &base);
        }
    }
    bint_copy(a, &result);
}

// ----------------------------------------------------------------------------
// Ed25519 point arithmetic (extended coords)
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Constantes da curva Ed25519 (RFC 8032)
// ----------------------------------------------------------------------------

// d = -121665 / 121666 mod p
// = 0x52036CEE2B6FFE738CC740797779E89800700A4D4141D8AB75EB4DCA135978A3
static const uint64_t D_LIMBS[5] = {
    0x75EB4DCA135978A3ULL, // bits 0-63
    0x00700A4D4141D8ABULL, // bits 64-127
    0x8CC740797779E898ULL, // bits 128-191
    0x52036CEE2B6FFE73ULL, // bits 192-255
    0x0000000000000000ULL
};

// 2d mod p
// = 0x2406D9DC56DFFCE7198E80F2EEF3D13000E0149A8283B156EBD69B9426B2F159
static const uint64_t TWO_D_LIMBS[5] = {
    0xEBD69B9426B2F159ULL, // bits 0-63
    0x00E0149A8283B156ULL, // bits 64-127
    0x198E80F2EEF3D130ULL, // bits 128-191
    0x2406D9DC56DFFCE7ULL, // bits 192-255
    0x0000000000000000ULL
};

// I = sqrt(-1) mod p = 2^((p-1)/4) mod p
// = 0x2B8324804FC1DF0B2B4D00993DFBD7A72F431806AD2FE478C4EE1B274A0EA0B0
static const uint64_t I_LIMBS[5] = {
    0xC4EE1B274A0EA0B0ULL, // bits 0-63
    0x2F431806AD2FE478ULL, // bits 64-127
    0x2B4D00993DFBD7A7ULL, // bits 128-191
    0x2B8324804FC1DF0BULL, // bits 192-255
    0x0000000000000000ULL
};

static void bint_pow_mod_p(const hphl_bint_t* base, const hphl_bint_t* exp, hphl_bint_t* out) {
    hphl_bint_t res, b;
    bint_set_one(&res);
    bint_copy(&b, base);
    for (int bit = 0; bit < 256; bit++) {
        int limb = bit / 64;
        int bit_idx = bit % 64;
        if ((exp->d[limb] >> bit_idx) & 1) {
            field_mul(&res, &b);
        }
        if (bit < 255) {
            field_mul(&b, &b);
        }
    }
    bint_copy(out, &res);
}

// RFC 8032 §5.1.3: recupera coordenada x a partir de y e do sign bit x_bit
static bool recover_x(hphl_bint_t* x, const hphl_bint_t* y, int x_bit) {
    hphl_bint_t y2, u, v, v_inv, uv, x_candidate, check;
    hphl_bint_t p;
    bint_from_limbs(&p, P_LIMBS);

    // y2 = y^2
    bint_copy(&y2, y); field_mul(&y2, y);
    // u = y^2 - 1
    bint_copy(&u, &y2);
    hphl_bint_t one; bint_set_one(&one);
    field_sub(&u, &one);
    // v = d * y^2 + 1
    hphl_bint_t d_const;
    bint_from_limbs(&d_const, D_LIMBS);
    bint_copy(&v, &d_const); field_mul(&v, &y2);
    field_add(&v, &one);

    // uv = u / v
    bint_copy(&v_inv, &v); field_inv(&v_inv);
    bint_copy(&uv, &u); field_mul(&uv, &v_inv);

    // exp = (p+3)/8 = 2^252 - 2
    hphl_bint_t exp;
    bint_from_limbs(&exp, (const uint64_t[]){
        0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
        0x0FFFFFFFFFFFFFFFULL, 0x0ULL
    });

    bint_pow_mod_p(&uv, &exp, &x_candidate);

    // check = v * x_candidate^2
    bint_copy(&check, &x_candidate); field_mul(&check, &x_candidate);
    field_mul(&check, &v);

    if (bint_cmp(&check, &u) != 0) {
        // Multiplica por I = sqrt(-1)
        hphl_bint_t I;
        bint_from_limbs(&I, I_LIMBS);
        field_mul(&x_candidate, &I);
    }

    // Ajusta sinal com base em x_bit (lsb de x)
    int current_bit = x_candidate.d[0] & 1;
    if (current_bit != x_bit) {
        hphl_bint_t neg_x;
        bint_copy(&neg_x, &p);
        field_sub(&neg_x, &x_candidate);
        bint_copy(x, &neg_x);
    } else {
        bint_copy(x, &x_candidate);
    }
    return true;
}

// ----------------------------------------------------------------------------
// Ed25519 point arithmetic (extended coords)
// ----------------------------------------------------------------------------

static void point_zero(hphl_point_t* p) {
    bint_zero(&p->X);
    bint_set_one(&p->Y);
    bint_set_one(&p->Z);
    bint_zero(&p->T);
}

// P + Q (extended coords, complete addition formula)
static void point_add(hphl_point_t* r, const hphl_point_t* p, const hphl_point_t* q) {
    hphl_bint_t a, b, c, d, e, f, g, h;
    // a = (Y1 - X1) * (Y2 - X2)
    bint_copy(&a, &p->Y); field_sub(&a, &p->X);
    bint_copy(&b, &q->Y); field_sub(&b, &q->X);
    field_mul(&a, &b);
    // b = (Y1 + X1) * (Y2 + X2)
    bint_copy(&b, &p->Y); field_add(&b, &p->X);
    bint_copy(&c, &q->Y); field_add(&c, &q->X);
    field_mul(&b, &c);
    // c = T1 * 2d * T2
    hphl_bint_t two_d;
    bint_from_limbs(&two_d, TWO_D_LIMBS);
    bint_copy(&c, &p->T); field_mul(&c, &two_d); field_mul(&c, &q->T);
    // d = Z1 * 2 * Z2
    bint_copy(&d, &p->Z); field_mul(&d, &q->Z);
    field_add(&d, &d);
    // e = b - a
    bint_copy(&e, &b); field_sub(&e, &a);
    // f = d - c
    bint_copy(&f, &d); field_sub(&f, &c);
    // g = d + c
    bint_copy(&g, &d); field_add(&g, &c);
    // h = b + a
    bint_copy(&h, &b); field_add(&h, &a);
    // X3 = e * f
    bint_copy(&r->X, &e); field_mul(&r->X, &f);
    // Y3 = g * h
    bint_copy(&r->Y, &g); field_mul(&r->Y, &h);
    // T3 = e * h
    bint_copy(&r->T, &e); field_mul(&r->T, &h);
    // Z3 = f * g
    bint_copy(&r->Z, &f); field_mul(&r->Z, &g);
}

// 2P
static void point_double(hphl_point_t* r, const hphl_point_t* p) {
    hphl_point_t q;
    q = *p;
    point_add(r, p, &q);
}

// n * P (double-and-add, scalar em LE)
static void point_scalar_mult(hphl_point_t* r, const hphl_bint_t* n, const hphl_point_t* p) {
    point_zero(r);
    int top_bit = -1;
    for (int b = 255; b >= 0; b--) {
        int word = b / 64;
        int bit = b % 64;
        if ((n->d[word] >> bit) & 1) {
            top_bit = b;
            break;
        }
    }
    if (top_bit < 0) return;

    for (int b = top_bit; b >= 0; b--) {
        point_double(r, r);
        int word = b / 64;
        int bit = b % 64;
        if ((n->d[word] >> bit) & 1) {
            point_add(r, r, p);
        }
    }
}

// Compara dois pontos (extended coords) — normaliza por Z
static bool point_equal(const hphl_point_t* p, const hphl_point_t* q) {
    // p == q iff X1*Z2 == X2*Z1 AND Y1*Z2 == Y2*Z1
    hphl_bint_t l1, l2, r1, r2;
    bint_mod_mul_p(&p->X, &q->Z, &l1);
    bint_mod_mul_p(&q->X, &p->Z, &l2);
    bint_mod_mul_p(&p->Y, &q->Z, &r1);
    bint_mod_mul_p(&q->Y, &p->Z, &r2);
    return bint_cmp(&l1, &l2) == 0 && bint_cmp(&r1, &r2) == 0;
}

// ----------------------------------------------------------------------------
// SHA-512 wrapper
// ----------------------------------------------------------------------------

// Declaração externa do runtime
extern void hphl_sha512(const uint8_t* in, size_t in_len, uint8_t out[64]);

// ----------------------------------------------------------------------------
// Ed25519 verify (RFC 8032 §5.1.7)
// ----------------------------------------------------------------------------

bool hphl_ed25519_verify(const uint8_t pub[32], const uint8_t sig[64],
                        const uint8_t* msg, size_t msg_len) {
    hphl_bint_t A_y, A_x, R_y, R_x, h, S;
    hphl_point_t B, A, R, hA, sB, R_plus_hA;

    // 1. Decode pub -> A_y, A_x
    uint8_t pub_clean[32];
    memcpy(pub_clean, pub, 32);
    int a_x_bit = (pub_clean[31] >> 7) & 1;
    pub_clean[31] &= 0x7F;
    bint_from_bytes(&A_y, pub_clean);

    if (!recover_x(&A_x, &A_y, a_x_bit)) return false;

    bint_copy(&A.X, &A_x);
    bint_copy(&A.Y, &A_y);
    bint_set_one(&A.Z);
    {
        hphl_bint_t tmp;
        bint_mod_mul_p(&A.X, &A.Y, &tmp);
        bint_copy(&A.T, &tmp);
    }

    // 2. Decode R = sig[0..31], S = sig[32..63]
    uint8_t R_clean[32];
    memcpy(R_clean, sig, 32);
    int r_x_bit = (R_clean[31] >> 7) & 1;
    R_clean[31] &= 0x7F;
    bint_from_bytes(&R_y, R_clean);
    bint_from_bytes(&S, sig + 32);

    // S < L check
    hphl_bint_t L;
    bint_from_limbs(&L, L_LIMBS);
    if (bint_cmp(&S, &L) >= 0) return false;

    if (!recover_x(&R_x, &R_y, r_x_bit)) return false;

    bint_copy(&R.X, &R_x);
    bint_copy(&R.Y, &R_y);
    bint_set_one(&R.Z);
    {
        hphl_bint_t tmp;
        bint_mod_mul_p(&R.X, &R.Y, &tmp);
        bint_copy(&R.T, &tmp);
    }

    // 3. Decode Base point B
    bint_from_limbs(&B.Y, B_Y_LIMBS);
    bint_from_limbs(&B.X, B_X_LIMBS);
    bint_set_one(&B.Z);
    {
        hphl_bint_t tmp;
        bint_mod_mul_p(&B.X, &B.Y, &tmp);
        bint_copy(&B.T, &tmp);
    }

    // 4. h = SHA-512(R || A || M) mod L
    size_t buf_len = 32 + 32 + msg_len;
    uint8_t* buf = (uint8_t*)malloc(buf_len);
    if (!buf) return false;
    memcpy(buf, sig, 32);      // raw R (with sign bit)
    memcpy(buf + 32, pub, 32); // raw A (with sign bit)
    if (msg_len > 0) memcpy(buf + 64, msg, msg_len);

    uint8_t digest[64];
    hphl_sha512(buf, buf_len, digest);
    free(buf);

    uint64_t h_raw[10] = {0};
    for (int i = 0; i < 64; i++) {
        h_raw[i / 8] |= ((uint64_t)digest[i]) << ((i % 8) * 8);
    }
    bint_reduce_mod_L(h_raw, &h);

    // 5. [S]B
    point_scalar_mult(&sB, &S, &B);

    // 6. [h]A
    point_scalar_mult(&hA, &h, &A);

    // 7. R + hA
    point_add(&R_plus_hA, &R, &hA);

    // 8. Check: sB == R + hA
    return point_equal(&sB, &R_plus_hA);
}
