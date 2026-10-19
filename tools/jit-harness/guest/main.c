#define ZSTD_STATIC_LINKING_ONLY
#define XXH_STATIC_LINKING_ONLY
#include <stdint.h>
#include "zstd.h"
#include "xxhash.h"

// Guest layout, filled in by the harness (see harness.cpp): the test is linked at BASE.
#ifndef BASE
#define BASE 0x2000000000ULL
#endif
#define INPUT   ((const uint8_t*)(BASE + 0x00200000))
#define OUTPUT  ((uint8_t*)(BASE + 0x00600000))
#define WORK    ((void*)(BASE + 0x02000000))
#define WORKSZ  (4u << 20)
struct Result { uint64_t status, in_size, out_size, checksum, calls, error; };
#define RESULT  ((volatile struct Result*)(BASE + 0x02500000))
#define PARAMS  ((const uint64_t*)(BASE + 0x02510000))   // [0] compressed size, [1] output capacity, [2] chunk size

void run(void) {
    volatile struct Result* r = RESULT;
    const uint64_t in_size = PARAMS[0], out_cap = PARAMS[1], chunk = PARAMS[2];
    r->status = 1;
    ZSTD_DStream* ds = ZSTD_initStaticDStream(WORK, WORKSZ);
    if (!ds) { r->status = 100; return; }
    ZSTD_inBuffer in = { INPUT, 0, 0 };
    ZSTD_outBuffer out = { OUTPUT, (size_t)out_cap, 0 };
    uint64_t calls = 0;
    size_t ret = 1;
    while (ret != 0) {
        if (in.pos == in.size) {
            if (in.size >= in_size) { r->status = 101; break; }
            size_t next = in.size + (size_t)chunk;
            if (next > in_size) next = (size_t)in_size;
            in.size = next;
        }
        ret = ZSTD_decompressStream(ds, &out, &in);
        ++calls;
        if (ZSTD_isError(ret)) { r->error = (uint64_t)ZSTD_getErrorCode(ret); r->status = 102; break; }
        if (calls > 100000000ull) { r->status = 103; break; }
    }
    r->calls = calls;
    r->in_size = in.pos;
    r->out_size = out.pos;
    r->checksum = XXH64(OUTPUT, out.pos, 0);
    if (r->status == 1) r->status = 2;
}

void __attribute__((noreturn)) start_c(void) {
    run();
    for (;;) __asm__ volatile("svc #0");
}
