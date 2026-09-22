// ============================================================================
// ed25519.h — Verificação Ed25519 (RFC 8032) para o package manager HP-HL
//
// Implementa: SHA-512 + aritmética bigint (256 bits) + aritmética de pontos
// na curva Ed25519. Suporta verificação de assinaturas de 64 bytes contra
// mensagens e chaves públicas de 32 bytes.
//
// NÃO é constant-time (educational). Não usar em produção adversarial.
// ============================================================================

#ifndef HPHL_ED25519_H
#define HPHL_ED25519_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Tipos públicos
typedef struct {
    uint64_t d[5]; // little-endian: d[0] = bits 0-63, d[4] = bits 192-255
} hphl_bint_t;

typedef struct {
    hphl_bint_t X, Y, Z, T; // extended coords: x = X/Z, y = Y/Z, xy = T/Z
} hphl_point_t;

// API principal
// Verifica assinatura Ed25519 (64 bytes: R || S) sobre mensagem `msg` (size bytes)
// usando public key `pub` (32 bytes). Retorna true se válida, false caso contrário.
//
// Implementa RFC 8032 §3.3: S*B == R + H(R||A||M)*A
// Onde: B = base point, A = pub, R = sig[0..32], S = sig[32..64]
bool hphl_ed25519_verify(const uint8_t pub[32], const uint8_t sig[64],
                        const uint8_t* msg, size_t msg_len);

// Auxiliar: SHA-512 wrapper (delega para hphl_sha512 em runtime.c)
void hphl_sha512(const uint8_t* in, size_t in_len, uint8_t out[64]);

#ifdef __cplusplus
}
#endif

#endif // HPHL_ED25519_H
