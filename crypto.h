#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <string.h>

/* Simple XOR-based encryption for demonstration/lightweight use.
   In a production environment, use a robust library like mbedTLS or libsodium. */

static inline void crypto_xor(char* data, int len, const char* key) {
    if (!key || !*key) return;
    size_t key_len = strlen(key);
    if (key_len == 0) return;

    for (int i = 0; i < len; i++) {
        data[i] ^= key[i % key_len];
    }
}

#endif
