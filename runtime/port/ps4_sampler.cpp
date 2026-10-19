// Sampling profiler for PS4 test builds.
//
// PS4 has no perf/Razor access for homebrew, so the app samples itself: a helper thread sends
// SIGPROF to each registered thread every millisecond, and the handler records the interrupted
// instruction pointer. Every 10 seconds the samples are aggregated and appended to
// /data/DolphinPS4/samples.log: per thread, the busiest eboot addresses (ELF virtual addresses,
// symbolize with `llvm-symbolizer --obj=<oelf> 0x...`), system module addresses ("sys") and JIT
// code addresses ("jit", 16-byte buckets). Every 60 s the busiest JIT code (64 KiB chunks) is
// copied to /data/DolphinPS4/jitcode.bin and the emulated MEM1 to mem1.bin, so the generated x86
// and the guest code behind it can be disassembled offline (blocks: the jit-perf map written by
// Common/JitRegister).
//
// Started by ps4_sampler_start() (DolphinNoGUI, ps4.ini profile=on); threads register
// themselves with ps4_sampler_register_thread().

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sched.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>

extern "C" char __text_start[];  // defined by the OpenOrbis link.x at the start of .text
extern "C" int pthread_getthreadid_np(void);  // libkernel (FreeBSD libthr)
extern "C" void ps4_boot_trace(const char* stage);

#ifndef PS4_RUNTIME_DATA_DIR
#define PS4_RUNTIME_DATA_DIR "/data/DolphinPS4"
#endif

namespace {

constexpr int kSigProf = 27;    // FreeBSD value
constexpr int kSaSiginfo = 0x40, kSaRestart = 0x0002;
constexpr int kMcontextOffset = 0x40;  // measured on hardware (see ps4_crashlog.cpp)
constexpr int kRipIndex = 20, kRspIndex = 23;
// Samples in system libraries (libkernel/libc at 0x800000000+) are attributed to the eboot
// function that called into them: the first eboot code address on the stack, tagged.
constexpr uint64_t kSystemLibs = 0x800000000ULL, kCallerTag = 1ULL << 62;
constexpr uintptr_t kTextWindow = 0x4000000;
constexpr int kMaxThreads = 12;
constexpr int kMaxSamples = 16384;  // per thread and window (10 s at 1 kHz fits)

struct ThreadSamples {
    std::atomic<pthread_t> thread{};
    char name[32] = {};
    uint64_t samples[kMaxSamples];
    std::atomic<int> count{0};
};

ThreadSamples g_threads[kMaxThreads];
std::atomic<int> g_thread_count{0};

void profHandler(int, siginfo_t*, void* context) {
    const pthread_t self = pthread_self();
    for (int i = 0; i < g_thread_count.load(std::memory_order_acquire); i++) {
        ThreadSamples& t = g_threads[i];
        if (t.thread.load(std::memory_order_relaxed) != self)
            continue;
        const auto* regs =
            reinterpret_cast<const uint64_t*>(static_cast<char*>(context) + kMcontextOffset);
        const int n = t.count.load(std::memory_order_relaxed);
        if (n < kMaxSamples) {
            uint64_t sample = regs[kRipIndex];
            // Inside a system module: keep the exact address (tagged). The module list at the
            // top of samples.log maps it to a module offset, which the decrypted modules' symbol
            // tables name. (Blaming "the first eboot address on the stack" picked up stale
            // values and named the wrong callers.)
            if (sample >= kSystemLibs && sample < 2 * kSystemLibs)
                sample |= kCallerTag;
            t.samples[n] = sample;
            t.count.store(n + 1, std::memory_order_release);
        }
        return;
    }
}

void writeLine(int fd, const char* text) {
    size_t length = strlen(text);
    while (length > 0) {
        const ssize_t written = write(fd, text, length);
        if (written <= 0)
            return;
        text += written;
        length -= static_cast<size_t>(written);
    }
}

void noteHotJit(uint64_t address, int count);

// JIT block map: host code range -> guest PC, filled by the JIT while compiling (Eden only).
struct JitBlock {
    uint64_t host;
    uint64_t info;  // guest pc (48 bits) | min(code size, 0xFFFF) << 48
};
constexpr uint32_t kMaxBlocks = 262144;
std::atomic<JitBlock*> g_blocks{nullptr};
std::atomic<uint32_t> g_block_count{0};
uint32_t g_block_order[kMaxBlocks];  // indices sorted by host address (sampler thread only)
uint32_t g_sorted_blocks = 0;

// Report text of the last windows, drained by the application into its own log.
constexpr size_t kReportCap = 192 * 1024;
char g_report[kReportCap];
size_t g_report_len = 0;
uint64_t g_hot_pcs[16];
int g_hot_pc_count = 0;
uint64_t g_hot_pages[16];
int g_hot_page_count = 0;
int g_busy_core = -1;           // CPUCore_N with the most guest-jit samples in the last window
int g_window_best_pct10 = -1;   // that share in tenths of a percent, for the window being built
std::atomic_flag g_report_lock = ATOMIC_FLAG_INIT;
void lockReport() {
    while (g_report_lock.test_and_set(std::memory_order_acquire))
        sched_yield();
}
void unlockReport() { g_report_lock.clear(std::memory_order_release); }

// Output of one window: appended here, then written to samples.log and queued for the application.
char g_window[96 * 1024];
size_t g_window_len = 0;
void emit(const char* text) {
    const size_t length = strlen(text);
    if (g_window_len + length < sizeof(g_window)) {
        memcpy(g_window + g_window_len, text, length);
        g_window_len += length;
    }
}

void sortBlocks() {
    JitBlock* blocks = g_blocks.load(std::memory_order_acquire);
    if (!blocks) {
        g_sorted_blocks = 0;
        return;
    }
    const uint32_t count = std::min(g_block_count.load(std::memory_order_acquire), kMaxBlocks);
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (blocks[i].host != 0)
            g_block_order[n++] = i;
    }
    std::sort(g_block_order, g_block_order + n,
              [blocks](uint32_t a, uint32_t b) { return blocks[a].host < blocks[b].host; });
    g_sorted_blocks = n;
}

// Guest PC of the block containing a JIT address, or ~0 if none.
uint64_t guestPcOf(uint64_t address) {
    JitBlock* blocks = g_blocks.load(std::memory_order_acquire);
    uint32_t lo = 0, hi = g_sorted_blocks;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (blocks[g_block_order[mid]].host <= address)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return ~0ULL;
    const JitBlock& block = blocks[g_block_order[lo - 1]];
    const uint64_t size = block.info >> 48;
    return address < block.host + (size ? size : 1) ? (block.info & 0xFFFFFFFFFFFFULL) : ~0ULL;
}

struct Entry {
    uint64_t key;
    int count;
};

// Sorts keys[0..n) and prints the `lines` most frequent as "  <label> <pct>% 0x<key>".
// Returns the most frequent key (or ~0).
uint64_t printTop(const char* label, uint64_t* keys, int n, int total, int lines, uint64_t key_mask = ~0ULL) {
    static Entry entries[kMaxSamples];
    if (n == 0)
        return ~0ULL;
    std::sort(keys, keys + n);
    int unique = 0;
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && keys[j] == keys[i])
            j++;
        entries[unique++] = {keys[i], j - i};
        i = j;
    }
    std::sort(entries, entries + unique, [](const Entry& a, const Entry& b) { return a.count > b.count; });
    char line[96];
    for (int i = 0; i < unique && i < lines && entries[i].count >= 2; i++) {
        snprintf(line, sizeof(line), "  %s %5.1f%% 0x%llx\n", label, 100.0 * entries[i].count / total,
                 static_cast<unsigned long long>(entries[i].key & key_mask));
        emit(line);
    }
    return entries[0].key;
}

// Aggregates one thread's window and appends it to g_window. Samples are classified as
// eboot C++ code (16-byte buckets, ELF addresses), system libraries (exact addresses), JIT code
// resolved to the guest block (guest PC) or "glue" (dispatcher/prelude), and other.
void report(ThreadSamples& t, double window_start) {
    static uint64_t s[kMaxSamples], eboot_keys[kMaxSamples], sys_keys[kMaxSamples],
        guest_keys[kMaxSamples], page_keys[kMaxSamples];
    const int n = std::min(t.count.load(std::memory_order_acquire), kMaxSamples);
    memcpy(s, t.samples, sizeof(uint64_t) * n);
    t.count.store(0, std::memory_order_release);
    if (n == 0)
        return;
    const uintptr_t text = reinterpret_cast<uintptr_t>(__text_start);
    int eboot = 0, sys = 0, guest = 0, glue = 0, other = 0;
    for (int i = 0; i < n; i++) {
        const uint64_t address = s[i] & ~kCallerTag;
        if (s[i] & kCallerTag) {
            sys_keys[sys++] = address & ~0xFULL;
        } else if (address >= text && address < text + kTextWindow) {
            eboot_keys[eboot++] = (address - text) & ~0xFULL;
        } else if (address >= 0x300000000ULL && address < 0x400000000ULL) {  // JIT caches (flexible memory)
            const uint64_t pc = guestPcOf(address);
            if (pc == ~0ULL) {
                glue++;
            } else {
                guest_keys[guest] = pc;
                page_keys[guest] = pc & ~0xFFFULL;
                guest++;
            }
        } else {
            other++;
        }
    }
    char line[160];
    snprintf(line, sizeof(line),
             "== %.0f s, %s: %d samples: guest-jit %.1f%%, jit-glue %.1f%%, eboot %.1f%%, system %.1f%%, other %.1f%%\n",
             window_start, t.name, n, 100.0 * guest / n, 100.0 * glue / n, 100.0 * eboot / n,
             100.0 * sys / n, 100.0 * other / n);
    emit(line);
    // Idle threads sit in one system wait; only busy ones get the long listing.
    if (eboot + guest + glue < n / 10) {
        printTop("sys", sys_keys, sys, n, 2);
        return;
    }
    const uint64_t top_pc = printTop("guest", guest_keys, guest, n, 30);
    if (strncmp(t.name, "CPUCore_", 8) == 0 && t.name[8] >= '0' && t.name[8] <= '9' && guest * 1000 / n > g_window_best_pct10) {
        g_window_best_pct10 = guest * 1000 / n;
        lockReport();
        g_busy_core = t.name[8] - '0';
        unlockReport();
    }
    const uint64_t top_page = printTop("gpage", page_keys, guest, n, 8);
    if (top_page != ~0ULL && guest > n / 20) {
        lockReport();
        if (g_hot_page_count < 16) g_hot_pages[g_hot_page_count++] = top_page;
        unlockReport();
    }
    printTop("eboot", eboot_keys, eboot, n, 40);
    printTop("sys", sys_keys, sys, n, 12);
    if (guest > n / 20 && top_pc != ~0ULL) {
        lockReport();
        if (g_hot_pc_count < 16)
            g_hot_pcs[g_hot_pc_count++] = top_pc;
        unlockReport();
    }
}

// 64 KiB chunks of JIT code sampled since the last dump (dumpJitCode).
constexpr int kMaxHotChunks = 96;
uint64_t g_hot_chunks[kMaxHotChunks];
int g_hot_chunk_count = 0;

void noteHotJit(uint64_t address, int count) {
    if (count < 3)
        return;
    const uint64_t chunk = address & ~0xFFFFULL;
    for (int i = 0; i < g_hot_chunk_count; i++) {
        if (g_hot_chunks[i] == chunk)
            return;
    }
    if (g_hot_chunk_count < kMaxHotChunks)
        g_hot_chunks[g_hot_chunk_count++] = chunk;
}

double now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// Loaded modules and their segments, so "sys" samples can be mapped to module offsets.
struct ModuleSegment {
    void* address;
    uint32_t size;
    int32_t prot;
};
struct ModuleInfo {
    size_t size;
    char name[256];
    ModuleSegment segments[4];
    uint32_t segment_count;
    uint8_t fingerprint[20];
};
extern "C" int32_t sceKernelGetModuleList(int32_t* handles, size_t count, size_t* available);
extern "C" int32_t sceKernelGetModuleInfo(int32_t handle, ModuleInfo* info);

void writeModuleList(int fd) {
    int32_t handles[256];
    size_t count = 0;
    static size_t s_listed = 0;  // relisted when modules get loaded (e.g. RADV's GnmDriver)
    if (sceKernelGetModuleList(handles, 256, &count) != 0 || count == s_listed)
        return;
    s_listed = count;
    char line[512];
    for (size_t m = 0; m < count && m < 256; m++) {
        ModuleInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelGetModuleInfo(handles[m], &info) != 0)
            continue;
        int len = snprintf(line, sizeof(line), "module %s", info.name);
        for (uint32_t g = 0; g < info.segment_count && g < 4; g++)
            len += snprintf(line + len, sizeof(line) - len, " seg %p+0x%x/%d",
                            info.segments[g].address, info.segments[g].size, info.segments[g].prot);
        snprintf(line + len, sizeof(line) - len, "\n");
        writeLine(fd, line);
    }
}

}  // namespace

extern "C" int32_t sceKernelQueryMemoryProtection(void* address, void** start, void** end,
                                                  uint32_t* protection);
// Emulated MEM1 (set by Core/HW/Memmap.cpp), dumped with the JIT code.
extern "C" void* ps4_profile_guest_ram;
extern "C" uint32_t ps4_profile_guest_ram_size;
extern "C" {
void* ps4_profile_guest_ram = nullptr;
uint32_t ps4_profile_guest_ram_size = 0;
}

namespace {

bool isReadable(uint64_t address, uint64_t size) {
    void* start = nullptr;
    void* end = nullptr;
    uint32_t protection = 0;
    return sceKernelQueryMemoryProtection(reinterpret_cast<void*>(address), &start, &end,
                                          &protection) == 0 &&
           (protection & 1) && address + size <= reinterpret_cast<uintptr_t>(end);
}

// jitcode.bin: records of {u64 address, u64 size, bytes}. Copied through a buffer first so the
// kernel never reads JIT memory for the file write.
void dumpJitCode() {
    const int fd = open(PS4_RUNTIME_DATA_DIR "/jitcode.bin", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        static uint8_t buffer[0x10000];
        for (int i = 0; i < g_hot_chunk_count; i++) {
            if (!isReadable(g_hot_chunks[i], sizeof(buffer)))
                continue;
            memcpy(buffer, reinterpret_cast<const void*>(g_hot_chunks[i]), sizeof(buffer));
            const uint64_t header[2] = {g_hot_chunks[i], sizeof(buffer)};
            write(fd, header, sizeof(header));
            write(fd, buffer, sizeof(buffer));
        }
        close(fd);
    }
    g_hot_chunk_count = 0;
    if (ps4_profile_guest_ram && ps4_profile_guest_ram_size) {
        const int ram = open(PS4_RUNTIME_DATA_DIR "/mem1.bin", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (ram >= 0) {
            static uint8_t copy[0x100000];
            for (uint32_t offset = 0; offset < ps4_profile_guest_ram_size; offset += sizeof(copy)) {
                const uint32_t size =
                    std::min<uint32_t>(sizeof(copy), ps4_profile_guest_ram_size - offset);
                memcpy(copy, static_cast<const uint8_t*>(ps4_profile_guest_ram) + offset, size);
                write(ram, copy, size);
            }
            close(ram);
        }
    }
}

std::atomic<bool> g_sampler_on{false};

void* samplerThread(void*) {
    const int fd = open(PS4_RUNTIME_DATA_DIR "/samples.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return nullptr;
    writeModuleList(fd);
    const double start = now();
    double window_start = start, last_dump = start;
    const timespec period = {0, 1000000}, paused = {0, 100000000};
    for (;;) {
        // Profiler switched off in the menus: no signals, no new windows until it is back on.
        if (!g_sampler_on.load(std::memory_order_relaxed)) {
            nanosleep(&paused, nullptr);
            window_start = now();
            continue;
        }
        nanosleep(&period, nullptr);
        const int threads = g_thread_count.load(std::memory_order_acquire);
        for (int i = 0; i < threads; i++)
            pthread_kill(g_threads[i].thread.load(std::memory_order_relaxed), kSigProf);
        if (now() - window_start >= 10.0) {
            writeModuleList(fd);
            g_window_len = 0;
            g_window_best_pct10 = -1;
            sortBlocks();
            for (int i = 0; i < threads; i++)
                report(g_threads[i], window_start - start);
            if (g_window_len) {
                write(fd, g_window, g_window_len);
                lockReport();
                const size_t room = kReportCap - g_report_len;
                const size_t take = std::min(room, g_window_len);
                memcpy(g_report + g_report_len, g_window, take);
                g_report_len += take;
                unlockReport();
            }
            window_start = now();
            if (window_start - last_dump >= 60.0) {
                dumpJitCode();
                last_dump = window_start = now();
                writeLine(fd, "(jitcode.bin and mem1.bin updated)\n");
            }
        }
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Hang dumps: every thread that matters registers here (ps4_watch_thread, also done by
// ps4_sampler_register_thread). ps4_dump_thread_stacks() signals each one; the handler records
// its instruction pointer and every eboot code address on its stack (return addresses, newest
// first). Used by DolphinNoGUI's watchdog when emulation stops advancing.
namespace {
constexpr int kSigDump = 30;  // SIGUSR1 (FreeBSD)
constexpr int kMaxWatched = 40, kMaxFrames = 48;

struct WatchedThread {
    std::atomic<pthread_t> thread{};
    char name[32] = {};  // copied: some callers pass temporaries
    // Highest stack address the dump may read: the thread's stack position when it registered
    // (plus its caller's frame). Reading a fixed amount above rsp ran off the top of small thread
    // stacks (the Wiimote scanner's) and crashed the app in the middle of a hang dump.
    uintptr_t stack_limit = 0;
    int tid = 0;  // kernel thread id (pthread_getthreadid_np): mutex objects record their owner's
    uint64_t rip = 0;
    uint64_t frames[kMaxFrames];
    int frame_count = 0;
    // Blocked in _umtx_op (the kernel's wait for mutexes, condition variables, semaphores): its
    // arguments - object, operation, value, second object (condition variable waits: the mutex) -
    // and the first 16 bytes of both objects (a FreeBSD mutex starts with its owner's thread id).
    bool in_umtx = false;
    uint64_t umtx_obj = 0, umtx_op = 0, umtx_val = 0, umtx_obj2 = 0;
    uint32_t umtx_words[4] = {}, umtx_words2[4] = {};
    std::atomic<int> done{0};
};
WatchedThread g_watched[kMaxWatched];
std::atomic<int> g_watched_count{0};

void dumpHandler(int, siginfo_t*, void* context) {
    const pthread_t self = pthread_self();
    const uintptr_t text = reinterpret_cast<uintptr_t>(__text_start);
    for (int i = 0; i < std::min(g_watched_count.load(std::memory_order_acquire), kMaxWatched); i++) {
        WatchedThread& t = g_watched[i];
        if (t.thread.load(std::memory_order_relaxed) != self)
            continue;
        const auto* regs =
            reinterpret_cast<const uint64_t*>(static_cast<char*>(context) + kMcontextOffset);
        t.rip = regs[kRipIndex];
        // libkernel's _umtx_op stub: "mov $0x1c6, %rax; mov %rcx, %r10; syscall" (12 bytes), the
        // interrupted thread sits right after the syscall. FreeBSD mcontext: rdi 1, rsi 2, rdx 3,
        // r10 10. Only then are the argument registers known to be the wait's.
        t.in_umtx = false;
        if (t.rip >= kSystemLibs && t.rip < 2 * kSystemLibs) {
            const auto* code = reinterpret_cast<const uint8_t*>(t.rip - 12);
            static const uint8_t kUmtxStub[12] = {0x48, 0xc7, 0xc0, 0xc6, 0x01, 0x00,
                                                  0x00, 0x49, 0x89, 0xca, 0x0f, 0x05};
            if (memcmp(code, kUmtxStub, sizeof(kUmtxStub)) == 0) {
                // Registers only: the objects are read by the dumping thread, after checking that
                // they are mapped (a register that wasn't a pointer crashed v02.37 here).
                t.in_umtx = true;
                t.umtx_obj = regs[1];
                t.umtx_op = regs[2];
                t.umtx_val = regs[3];
                t.umtx_obj2 = regs[10];
            }
        }
        // Only scan a stack that is plausibly this registration's: a thread that registered and
        // exited can leave its pthread handle to a new thread, whose stack is elsewhere (a v02.34
        // hang dump crashed scanning from one thread's rsp to another's stack limit).
        const uint64_t rsp = regs[kRspIndex];
        const bool same_thread = pthread_getthreadid_np() == t.tid;
        const bool sane = t.stack_limit > rsp && t.stack_limit - rsp <= (1u << 20);
        const auto* stack = reinterpret_cast<const uint64_t*>(rsp);
        const auto* limit =
            reinterpret_cast<const uint64_t*>(same_thread && sane ? t.stack_limit : rsp);
        int n = 0;
        for (const uint64_t* p = stack; p < limit && n < kMaxFrames; p++) {
            if (*p >= text && *p < text + kTextWindow)
                t.frames[n++] = *p - text;
        }
        t.frame_count = n;
        t.done.store(1, std::memory_order_release);
        return;
    }
}

void installDumpHandler() {
    static std::atomic<bool> installed{false};
    if (installed.exchange(true))
        return;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    void (*handler)(int, siginfo_t*, void*) = dumpHandler;
    memcpy(&action, &handler, sizeof(handler));
    action.sa_flags = kSaSiginfo | kSaRestart;
    sigemptyset(&action.sa_mask);
    sigaction(kSigDump, &action, nullptr);
}
}  // namespace

extern "C" void ps4_heap_private_arena(const char* name);  // ps4_runtime.cpp

extern "C" void ps4_watch_thread(const char* name) {
    installDumpHandler();
    ps4_heap_private_arena(name);
    const pthread_t self = pthread_self();
    for (int i = 0; i < std::min(g_watched_count.load(), kMaxWatched); i++) {
        if (g_watched[i].thread.load() == self)
            return;  // already watched (e.g. registered for sampling and named)
    }
    const int index = g_watched_count.fetch_add(1);
    if (index >= kMaxWatched)
        return;
    snprintf(g_watched[index].name, sizeof(g_watched[index].name), "%s", name ? name : "?");
    // Up to and including this function's own return address (frame + 16): threads that register
    // first thing (e.g. the analytics reporter) sit within 256 bytes of their stack's top, and a
    // larger margin read past it. Where a thread is stuck lies deeper than this anyway.
    g_watched[index].stack_limit = reinterpret_cast<uintptr_t>(__builtin_frame_address(0)) + 16;
    g_watched[index].tid = pthread_getthreadid_np();
    g_watched[index].thread.store(self);
}

extern "C" int32_t sceKernelQueryMemoryProtection(void* address, void** start, void** end,
                                                  uint32_t* protection);

// Copies 16 bytes at `address` if they lie in readable memory (else leaves zeros).
static void readIfMapped(uint64_t address, uint32_t (&words)[4]) {
    memset(words, 0, sizeof(words));
    void* start = nullptr;
    void* end = nullptr;
    uint32_t protection = 0;
    if (address < 0x10000 ||
        sceKernelQueryMemoryProtection(reinterpret_cast<void*>(address), &start, &end,
                                       &protection) != 0 ||
        !(protection & 1) || address + 16 > reinterpret_cast<uintptr_t>(end))
        return;
    memcpy(words, reinterpret_cast<const void*>(address), sizeof(words));
}

// Writes every watched thread's stack to `path` (appending), headed by `reason`.
extern "C" void ps4_dump_thread_stacks(const char* path, const char* reason) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    char line[256];
    snprintf(line, sizeof(line), "==== %s (eboot addresses relative to .text, symbolize with "
                                 "llvm-symbolizer --obj=<oelf>)\n", reason);
    writeLine(fd, line);
    const uintptr_t text = reinterpret_cast<uintptr_t>(__text_start);
    const int count = std::min(g_watched_count.load(std::memory_order_acquire), kMaxWatched);
    for (int i = 0; i < count; i++) {
        WatchedThread& t = g_watched[i];
        t.done.store(0);
        if (pthread_kill(t.thread.load(), kSigDump) != 0) {
            snprintf(line, sizeof(line), "-- %s: signal failed (thread gone?)\n", t.name);
            writeLine(fd, line);
            continue;
        }
        const timespec pause = {0, 1000000};
        for (int w = 0; w < 200 && !t.done.load(std::memory_order_acquire); w++)
            nanosleep(&pause, nullptr);
        if (!t.done.load(std::memory_order_acquire)) {
            snprintf(line, sizeof(line), "-- %s: no answer (signals blocked?)\n", t.name);
            writeLine(fd, line);
            continue;
        }
        const bool in_eboot = t.rip >= text && t.rip < text + kTextWindow;
        snprintf(line, sizeof(line), "-- %s (thread id %d): rip %s0x%llx\n", t.name, t.tid,
                 in_eboot ? "elf " : "(system/JIT) ",
                 static_cast<unsigned long long>(in_eboot ? t.rip - text : t.rip));
        writeLine(fd, line);
        if (t.in_umtx) {
            readIfMapped(t.umtx_obj, t.umtx_words);
            readIfMapped(t.umtx_obj2, t.umtx_words2);
            snprintf(line, sizeof(line),
                     "   blocked in _umtx_op(obj %#llx, op %llu, val %#llx, obj2 %#llx); obj: %08x "
                     "%08x %08x %08x; obj2: %08x %08x %08x %08x\n",
                     static_cast<unsigned long long>(t.umtx_obj),
                     static_cast<unsigned long long>(t.umtx_op),
                     static_cast<unsigned long long>(t.umtx_val),
                     static_cast<unsigned long long>(t.umtx_obj2), t.umtx_words[0], t.umtx_words[1],
                     t.umtx_words[2], t.umtx_words[3], t.umtx_words2[0], t.umtx_words2[1],
                     t.umtx_words2[2], t.umtx_words2[3]);
            writeLine(fd, line);
        }
        writeLine(fd, "   stack:");
        for (int f = 0; f < t.frame_count; f++) {
            snprintf(line, sizeof(line), " 0x%llx", static_cast<unsigned long long>(t.frames[f]));
            writeLine(fd, line);
        }
        writeLine(fd, "\n");
    }
    close(fd);
}

// Per-thread CPU time for the monitor: busy threads (CPU thread 0, video thread 1) read their own
// CPU clock now and then; the monitor divides the growth by wall time. Shows how close a thread
// is to its limit and whether something else shares its core (the sampler can't see that).
namespace {
constexpr clockid_t kThreadCpuClock = 14;  // FreeBSD CLOCK_THREAD_CPUTIME_ID
std::atomic<uint64_t> g_thread_cpu_ns[2];
}  // namespace

extern "C" void ps4_thread_cpu_sample(int slot) {
    timespec ts;
    if (slot < 0 || slot > 1 || clock_gettime(kThreadCpuClock, &ts) != 0)
        return;
    g_thread_cpu_ns[slot].store(static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                                    static_cast<uint64_t>(ts.tv_nsec),
                                std::memory_order_relaxed);
}

// Wait-time counters for the monitor (ns): 0 CPU thread waiting for the GPU thread (FlushGpu),
// 1 GPU loop sleeping for lack of work, 2 video thread waiting for the Vulkan recording thread,
// 3 vkWaitForFences.
namespace {
std::atomic<long long> g_wait_ns[6];  // 4: video-thread pipeline compile ns, 5: their count
}  // namespace

extern "C" void ps4_wait_add(int slot, long long ns) {
    if (slot >= 0 && slot < 6 && ns > 0)
        g_wait_ns[slot].fetch_add(ns, std::memory_order_relaxed);
}

extern "C" long long ps4_wait_total(int slot) {
    return slot >= 0 && slot < 6 ? g_wait_ns[slot].load(std::memory_order_relaxed) : 0;
}

extern "C" double ps4_thread_cpu_seconds(int slot) {
    if (slot < 0 || slot > 1)
        return 0;
    return g_thread_cpu_ns[slot].load(std::memory_order_relaxed) / 1e9;
}

std::atomic_flag g_register_lock = ATOMIC_FLAG_INIT;

extern "C" void ps4_sampler_register_thread(const char* name) {
    ps4_watch_thread(name);
    while (g_register_lock.test_and_set(std::memory_order_acquire))
        sched_yield();
    const int index = g_thread_count.load();
    if (index < kMaxThreads) {
        snprintf(g_threads[index].name, sizeof(g_threads[index].name), "%s", name ? name : "?");
        g_threads[index].thread.store(pthread_self());
        g_thread_count.store(index + 1, std::memory_order_release);
    }
    g_register_lock.clear(std::memory_order_release);
}

// Forgets all sampled threads (call before they exit: signalling a dead thread is undefined).
extern "C" void ps4_sampler_stop_threads() {
    g_sampler_on.store(false);
    const timespec wait = {0, 30000000};
    nanosleep(&wait, nullptr);  // lets the sampler thread finish a round
    g_thread_count.store(0, std::memory_order_release);
}

// Allocates the JIT block map; the JIT reports each compiled block through ps4_jit_block_register.
extern "C" void ps4_sampler_enable_block_map() {
    if (g_blocks.load())
        return;
    g_block_count.store(0);
    g_blocks.store(static_cast<JitBlock*>(calloc(kMaxBlocks, sizeof(JitBlock))), std::memory_order_release);
}

extern "C" void ps4_jit_block_register(uintptr_t host, uint32_t size, uint64_t guest_pc) {
    JitBlock* blocks = g_blocks.load(std::memory_order_acquire);
    if (!blocks)
        return;
    const uint32_t index = g_block_count.fetch_add(1);
    if (index >= kMaxBlocks)
        return;
    blocks[index].info = (guest_pc & 0xFFFFFFFFFFFFULL) | (static_cast<uint64_t>(std::min<uint32_t>(size, 0xFFFF)) << 48);
    blocks[index].host = host;
}

// Copies queued report text (NUL terminated) into `out`; returns its length (0 if none).
extern "C" size_t ps4_sampler_take_report(char* out, size_t capacity) {
    lockReport();
    size_t length = std::min(g_report_len, capacity > 0 ? capacity - 1 : 0);
    // Whole lines only.
    while (length > 0 && length < g_report_len && g_report[length - 1] != '\n')
        length--;
    memcpy(out, g_report, length);
    if (capacity)
        out[length] = 0;
    memmove(g_report, g_report + length, g_report_len - length);
    g_report_len -= length;
    unlockReport();
    return length;
}

// Core (0-3) whose emulated CPU thread spent the most time in translated code in the last window, or -1.
extern "C" int ps4_sampler_busy_core() {
    lockReport();
    const int core = g_busy_core;
    unlockReport();
    return core;
}

// Guest 4 KiB pages that were the busiest of a busy thread since the last call.
extern "C" int ps4_sampler_hot_guest_pages(uint64_t* out, int max) {
    lockReport();
    const int count = std::min(g_hot_page_count, max);
    memcpy(out, g_hot_pages, sizeof(uint64_t) * count);
    g_hot_page_count = 0;
    unlockReport();
    return count;
}

// Guest PCs of the busiest JIT blocks since the last call.
extern "C" int ps4_sampler_hot_guest_pcs(uint64_t* out, int max) {
    lockReport();
    const int count = std::min(g_hot_pc_count, max);
    memcpy(out, g_hot_pcs, sizeof(uint64_t) * count);
    g_hot_pc_count = 0;
    unlockReport();
    return count;
}

extern "C" void ps4_sampler_start() {
    static std::atomic<bool> s_started{false};
    g_sampler_on.store(true);
    if (s_started.exchange(true))
        return;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    void (*handler)(int, siginfo_t*, void*) = profHandler;
    memcpy(&action, &handler, sizeof(handler));  // the handler union's member names differ
    action.sa_flags = kSaSiginfo | kSaRestart;
    sigemptyset(&action.sa_mask);
    sigaction(kSigProf, &action, nullptr);

    pthread_t thread;
    if (pthread_create(&thread, nullptr, samplerThread, nullptr) == 0)
        ps4_boot_trace("sampler: started, writing /data/DolphinPS4/samples.log every 10 s");
}

// The menus' Profiler switch: starts the sampler the first time, then pauses / resumes it.
extern "C" void ps4_sampler_set_enabled(int on) {
    if (on)
        ps4_sampler_start();
    else if (g_sampler_on.exchange(false))
        ps4_boot_trace("sampler: paused");
}
