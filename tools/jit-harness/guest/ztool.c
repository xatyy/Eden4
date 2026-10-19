// Host tool: builds deterministic test data, compresses it with zstd, prints the raw size and XXH64.
#define XXH_STATIC_LINKING_ONLY
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "zstd.h"
#include "xxhash.h"

static uint64_t s = 0x9E3779B97F4A7C15ULL;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 16); }

int main(int argc, char** argv) {
    size_t raw = (size_t)atoi(argv[1]) << 10;   // KiB
    int level = atoi(argv[2]);
    uint8_t* d = malloc(raw);
    size_t i = 0;
    while (i < raw) {
        switch (rnd() % 4) {
        case 0: { // text-like
            size_t n = 64 + rnd() % 512;
            for (size_t k = 0; k < n && i < raw; ++k, ++i) d[i] = "etaoin shrdlu cmfwyp"[rnd() % 20];
        } break;
        case 1: { // random
            size_t n = 16 + rnd() % 256;
            for (size_t k = 0; k < n && i < raw; ++k, ++i) d[i] = (uint8_t)rnd();
        } break;
        case 2: { // repeat of earlier data
            if (i > 1024) {
                size_t from = rnd() % (i - 512), n = 16 + rnd() % 400;
                for (size_t k = 0; k < n && i < raw; ++k, ++i) d[i] = d[from + k];
            }
        } break;
        default: { // table of 32-bit counters
            size_t n = 8 + rnd() % 64;
            uint32_t v = rnd();
            for (size_t k = 0; k < n && i + 4 <= raw; ++k, i += 4) { v += rnd() % 7; memcpy(d + i, &v, 4); }
        } break;
        }
    }
    size_t cap = ZSTD_compressBound(raw);
    uint8_t* c = malloc(cap);
    size_t cs = ZSTD_compress(c, cap, d, raw, level);
    if (ZSTD_isError(cs)) return 1;
    FILE* f = fopen(argv[3], "wb"); fwrite(c, 1, cs, f); fclose(f);
    printf("raw=%zu comp=%zu xxh64=%016llx\n", raw, cs, (unsigned long long)XXH64(d, raw, 0));
    return 0;
}
