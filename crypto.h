#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <string.h>

/* Simple XOR-based encryption for demonstration/lightweight use.
   In a production environment, use a robust library like mbedTLS or libsodium. */

static inline void crypto_xor(char* data, int len, const char* key) {
    if (!key || !*key) return;
    static size_t cached_key_len = 0;
    static const char* last_key = NULL;

    if (key != last_key) {
        cached_key_len = strlen(key);
        last_key = key;
    }

    if (cached_key_len == 0) return;

    for (int i = 0; i < len; i++) {
        data[i] ^= key[i % cached_key_len];
    }
}

#endif
