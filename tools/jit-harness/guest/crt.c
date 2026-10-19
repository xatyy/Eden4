#include <stddef.h>
#include <stdint.h>
// Freestanding libc pieces. Plain byte loops: the point is to run zstd's own code.
void* memcpy(void* d, const void* s, size_t n) { uint8_t* a = d; const uint8_t* b = s; while (n--) *a++ = *b++; return d; }
void* memmove(void* d, const void* s, size_t n) {
    uint8_t* a = d; const uint8_t* b = s;
    if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
void* memset(void* d, int c, size_t n) { uint8_t* a = d; while (n--) *a++ = (uint8_t)c; return d; }
int memcmp(const void* x, const void* y, size_t n) {
    const uint8_t* a = x; const uint8_t* b = y;
    for (; n; --n, ++a, ++b) if (*a != *b) return *a < *b ? -1 : 1;
    return 0;
}
size_t strlen(const char* s) { size_t n = 0; while (s[n]) ++n; return n; }
void* malloc(size_t n) { (void)n; return 0; }
void* calloc(size_t a, size_t b) { (void)a; (void)b; return 0; }
void free(void* p) { (void)p; }
