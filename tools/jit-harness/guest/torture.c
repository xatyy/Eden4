// Memory-access torture test for the JIT: random mixes of loads and stores of every size, aligned and
// not, paired and vector accesses, pre/post-indexed forms, exclusive loops, around page boundaries.
#include <stdint.h>
#include <stddef.h>
#include <arm_neon.h>

#ifndef BASE
#define BASE 0x2000000000ULL
#endif
#define REGION ((uint8_t*)(BASE + 0x01000000))
#define REGION_SIZE (1u << 20)
#define RESULT ((volatile uint64_t*)(BASE + 0x02500000))

static uint64_t rng = 0x243F6A8885A308D3ULL;
static inline uint64_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

#define R8(p,o)  (*(volatile uint8_t*)((p)+(o)))
#define R16(p,o) (*(volatile uint16_t*)((p)+(o)))
#define R32(p,o) (*(volatile uint32_t*)((p)+(o)))
#define R64(p,o) (*(volatile uint64_t*)((p)+(o)))
#define RQ(p,o)  (*(volatile uint8x16_t*)((p)+(o)))

static uint64_t acc;

__attribute__((noinline)) static void group_a(uint8_t* p, uint64_t v) {
    acc += R64(p, 0); acc ^= R64(p, 8); R64(p, 16) = v; acc += R32(p, 24); R32(p, 28) = (uint32_t)v;
    acc ^= R16(p, 32); R16(p, 34) = (uint16_t)(v >> 8); acc += R8(p, 36); R8(p, 37) = (uint8_t)(v >> 16);
    acc += R64(p, 41); R64(p, 49) = v ^ acc; acc ^= R32(p, 57); R32(p, 62) = (uint32_t)acc;
}
__attribute__((noinline)) static void group_b(uint8_t* p, uint64_t v) {
    uint8x16_t q = RQ(p, 0); RQ(p, 48) = q; acc += vgetq_lane_u64(vreinterpretq_u64_u8(q), 1);
    RQ(p, 100) = vdupq_n_u8((uint8_t)v); acc ^= R64(p, 108); R64(p, -8) = v; acc += R64(p, -16);
    acc ^= R32(p, -20); R16(p, -22) = (uint16_t)v; R8(p, -23) = (uint8_t)v;
    acc += R64(p, 200); R64(p, 248) = acc; acc ^= RQ(p, 232)[3];
}
__attribute__((noinline)) static void group_c(uint8_t* p, uint64_t v) {
    uint64_t a, b;
    __asm__ volatile("ldp %0, %1, [%2, #16]" : "=r"(a), "=r"(b) : "r"(p) : "memory");
    acc += a ^ b;
    __asm__ volatile("stp %0, %1, [%2, #32]" :: "r"(v), "r"(acc), "r"(p) : "memory");
    __asm__ volatile("ldp %0, %1, [%2, #-32]" : "=r"(a), "=r"(b) : "r"(p) : "memory");
    acc ^= a + b;
    uint64x2_t qa, qb;
    __asm__ volatile("ldp %q0, %q1, [%2, #64]" : "=w"(qa), "=w"(qb) : "r"(p) : "memory");
    acc += qa[0] ^ qb[1];
    __asm__ volatile("stp %q0, %q1, [%2, #96]" :: "w"(qb), "w"(qa), "r"(p) : "memory");
    uint32_t s, t;
    __asm__ volatile("ldp %w0, %w1, [%2, #0]" : "=r"(s), "=r"(t) : "r"(p + 7) : "memory");
    acc += s * 3 + t;
}
__attribute__((noinline)) static void group_d(uint8_t* p, uint64_t v) {
    uint64_t a, b; uint8_t* q = p;
    __asm__ volatile("ldr %0, [%1, #8]!" : "=r"(a), "+r"(q) :: "memory");
    __asm__ volatile("ldr %0, [%1], #16" : "=r"(b), "+r"(q) :: "memory");
    __asm__ volatile("str %0, [%1, #-8]!" :: "r"(v), "r"(q) : "memory");
    __asm__ volatile("strb %w0, [%1], #1" :: "r"(v), "r"(q) : "memory");
    acc += a ^ b ^ (uint64_t)(q - p);
}
__attribute__((noinline)) static void group_e(uint8_t* p, uint64_t v) {
    // exclusive increment loops
    uint64_t x; uint32_t st;
    do { __asm__ volatile("ldaxr %0, [%2]\n add %0, %0, %3\n stlxr %w1, %0, [%2]" : "=&r"(x), "=&r"(st) : "r"((uint8_t*)(((uintptr_t)(p + 8)) & ~7ull)), "r"(v) : "memory"); } while (st);
    uint32_t w;
    do { __asm__ volatile("ldxr %w0, [%2]\n eor %w0, %w0, %w3\n stxr %w1, %w0, [%2]" : "=&r"(w), "=&r"(st) : "r"((uint8_t*)(((uintptr_t)(p + 20)) & ~3ull)), "r"((uint32_t)v) : "memory"); } while (st);
    uint32_t h;
    do { __asm__ volatile("ldaxrh %w0, [%2]\n add %w0, %w0, #3\n stlxrh %w1, %w0, [%2]" : "=&r"(h), "=&r"(st) : "r"((uint8_t*)(((uintptr_t)(p + 34)) & ~1ull)), "r"(0) : "memory"); } while (st);
    uint32_t bb;
    do { __asm__ volatile("ldxrb %w0, [%2]\n add %w0, %w0, #1\n stxrb %w1, %w0, [%2]" : "=&r"(bb), "=&r"(st) : "r"(p + 45), "r"(0) : "memory"); } while (st);
    acc += x + w + h + bb;
}
__attribute__((noinline)) static void group_f(uint8_t* p, uint64_t v) {
    // wide spread: more than one page worth of offsets
    acc += R64(p, 0); R64(p, 512) = v; acc ^= R64(p, 1024); R32(p, 2048) = (uint32_t)v; acc += R32(p, 4000);
    R64(p, 4088) = acc; acc ^= R64(p, 4096 + 8); R16(p, 4094) = (uint16_t)v; acc += R8(p, 4095); acc ^= R8(p, 4096);
}

void run(void) {
    uint8_t* region = REGION;
    for (uint32_t i = 0; i < REGION_SIZE; i += 8) *(uint64_t*)(region + i) = next();
    for (int iter = 0; iter < 60000; ++iter) {
        uint64_t r = next();
        uint32_t off = 4096 + (uint32_t)((r >> 8) % (REGION_SIZE - 3 * 4096));
        if (r & 1) off = (off & ~4095u) + 4096 - (uint32_t)((r >> 40) % 70);     // straddle a 4K boundary
        else if (r & 2) off = (off & ~4095u) + (uint32_t)((r >> 40) % 64);        // just after a boundary
        uint8_t* p = region + off;
        uint64_t v = next();
        switch ((r >> 48) % 6) {
        case 0: group_a(p, v); break;
        case 1: group_b(p, v); break;
        case 2: group_c(p, v); break;
        case 3: group_d(p, v); break;
        case 4: group_e(p, v); break;
        default: group_f(p, v); break;
        }
    }
    uint64_t sum = 1469598103934665603ULL;
    for (uint32_t i = 0; i < REGION_SIZE; i += 8) { sum ^= *(uint64_t*)(region + i); sum *= 1099511628211ULL; }
    RESULT[0] = 2; RESULT[1] = sum; RESULT[2] = acc;
}

void __attribute__((noreturn)) start_c(void) {
    run();
    for (;;) __asm__ volatile("svc #0");
}
