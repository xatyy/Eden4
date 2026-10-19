// Host test for the PS4 JIT memory paths: runs an aarch64 zstd decompressor (guest/) under the same
// Dynarmic configuration as Eden-PS4 in several modes and compares the results.
#include <sys/mman.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>
#include <string>

#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"
#include "dynarmic/interface/exclusive_monitor.h"

extern "C" {
extern bool ps4_regalloc_free_moves;
extern bool ps4_regalloc_r13;
extern bool ps4_page_reuse;
extern long ps4_regalloc_move_limit;
extern bool ps4_regalloc_full_scan;
extern long ps4_trace_block;
extern unsigned long long ps4_page_reuse_groups, ps4_page_reuse_accesses;
}

using u64 = std::uint64_t;
using u8 = std::uint8_t;
using u32 = std::uint32_t;
constexpr u64 BASE = 0x2000000000ULL;
constexpr u64 MEM_SIZE = 64ull << 20;
constexpr u64 CHUNK_ENTRIES = 8192;

struct Result { u64 status, in_size, out_size, checksum, calls, error; };

struct Env : Dynarmic::A64::UserCallbacks {
    u8* host = nullptr;
    std::vector<u64> map;     // guest page -> host byte offset (shuffled when --shuffle)
    Dynarmic::A64::Jit* jit = nullptr;
    u64 callbacks = 0, faults = 0, counter = 0;
    bool done = false;
    u64 tick_budget = 1ull << 60, ticks_used = 0;

    // Byte pointer for one guest byte (pages may be backed discontiguously).
    u8* Byte(u64 vaddr) {
        if (vaddr < BASE || vaddr >= BASE + MEM_SIZE) { if (faults++ < 5) std::fprintf(stderr, "fault vaddr=%llx\n", (unsigned long long)vaddr); return nullptr; }
        const u64 off = vaddr - BASE;
        return host + map[off >> 12] + (off & 0xfff);
    }
    // Accesses are done byte-wise so a page crossing is always handled page by page.
    u64 MemoryRead(u64 vaddr, size_t size) override {
        ++callbacks;
        u64 v = 0;
        for (size_t i = 0; i < size; ++i) { u8* p = Byte(vaddr + i); if (p) v |= u64(*p) << (8 * i); }
        return v;
    }
    Dynarmic::A64::Vector MemoryRead128(u64 vaddr) override {
        ++callbacks;
        Dynarmic::A64::Vector v{};
        u8 raw[16] = {};
        for (size_t i = 0; i < 16; ++i) if (u8* p = Byte(vaddr + i)) raw[i] = *p;
        std::memcpy(v.data(), raw, 16);
        return v;
    }
    void MemoryWrite(u64 vaddr, u64 value, size_t size) override {
        ++callbacks;
        for (size_t i = 0; i < size; ++i) if (u8* p = Byte(vaddr + i)) *p = u8(value >> (8 * i));
    }
    void MemoryWrite128(u64 vaddr, Dynarmic::A64::Vector value) override {
        ++callbacks;
        u8 raw[16];
        std::memcpy(raw, value.data(), 16);
        for (size_t i = 0; i < 16; ++i) if (u8* p = Byte(vaddr + i)) *p = raw[i];
    }
    u8* Ptr(u64 vaddr, size_t size) {   // exclusives: only within one page
        if (((vaddr & 0xfff) + size) > 0x1000) return nullptr;
        return Byte(vaddr);
    }
    template <typename T> bool Exclusive(u64 vaddr, T value, T expected) {
        u8* p = Ptr(vaddr, sizeof(T));
        if (!p) return false;
        T cur; std::memcpy(&cur, p, sizeof(T));
        if (cur != expected) return false;
        std::memcpy(p, &value, sizeof(T));
        return true;
    }
    bool MemoryWriteExclusive8(u64 a, u8 v, u8 e) override { return Exclusive(a, v, e); }
    bool MemoryWriteExclusive16(u64 a, std::uint16_t v, std::uint16_t e) override { return Exclusive(a, v, e); }
    bool MemoryWriteExclusive32(u64 a, std::uint32_t v, std::uint32_t e) override { return Exclusive(a, v, e); }
    bool MemoryWriteExclusive64(u64 a, u64 v, u64 e) override { return Exclusive(a, v, e); }
    void CallSVC(std::uint32_t) override { done = true; jit->HaltExecution(); }
    void ExceptionRaised(u64 pc, Dynarmic::A64::Exception e) override {
        std::fprintf(stderr, "exception %d at %llx\n", int(e), (unsigned long long)pc);
        done = true; jit->HaltExecution();
    }
    void AddTicks(u64 t) override { ticks_used += t; }
    u64 GetTicksRemaining() override { return ticks_used >= tick_budget ? 0 : tick_budget - ticks_used; }
    u64 GetCNTPCT() override { return counter += 100; }
};

// Page table with Eden's layout: a directory of chunks, each CHUNK_ENTRIES packed entries then
// CHUNK_ENTRIES direct values.
struct Table {
    std::vector<u64*> dir = std::vector<u64*>((1ull << (39 - 12)) / CHUNK_ENTRIES, nullptr);
    void Map(u64 vaddr, u64 delta) {
        const u64 page = vaddr >> 12;
        u64*& chunk = dir[page / CHUNK_ENTRIES];
        if (!chunk) chunk = static_cast<u64*>(std::calloc(2 * CHUNK_ENTRIES, sizeof(u64)));
        const u64 idx = page % CHUNK_ENTRIES;
        // packed: marked bit 0, type bits 1-2 (Memory = 1), page = pointer >> 12 (45 bits) at bit 12
        chunk[idx] = (1ull << 1) | ((((delta >> 12) & ((1ull << 45) - 1))) << 12);
        chunk[CHUNK_ENTRIES + idx] = delta;
    }
};

int main(int argc, char** argv) {
    std::string mode = "direct", input, guest = "guest/out/guest.bin";
    bool torture = false, shuffle = false, dump_state = false;
    std::string save_path, load_path; u64 patch_addr = 0;
    u64 tick_limit = 1ull << 60;
    bool reuse = true, regalloc = true, hostlow = false, freemoves = true, r13 = true;
    u64 chunk = 4096;
    std::string expect;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](const char* k) { return a.rfind(k, 0) == 0 ? a.substr(std::strlen(k)) : std::string(); };
        if (!val("--mode=").empty()) mode = val("--mode=");
        else if (!val("--input=").empty()) input = val("--input=");
        else if (!val("--expect=").empty()) expect = val("--expect=");
        else if (!val("--chunk=").empty()) chunk = std::stoull(val("--chunk="));
        else if (a == "--noreuse") reuse = false;
        else if (a == "--torture") torture = true;
        else if (a == "--shuffle") shuffle = true;
        else if (!val("--ticks=").empty()) tick_limit = std::stoull(val("--ticks="));
        else if (a == "--state") dump_state = true;
        else if (!val("--save=").empty()) save_path = val("--save=");
        else if (!val("--load=").empty()) load_path = val("--load=");
        else if (!val("--patch=").empty()) patch_addr = std::stoull(val("--patch="), nullptr, 16);
        else if (!val("--guest=").empty()) guest = val("--guest=");
        else if (a == "--noregalloc") regalloc = false;
        else if (a == "--nofreemoves") freemoves = false;
        else if (a == "--nor13") r13 = false;
        else if (a == "--fullscan") ps4_regalloc_full_scan = true;
        else if (!val("--traceblock=").empty()) ps4_trace_block = std::stol(val("--traceblock="));
        else if (!val("--movelimit=").empty()) ps4_regalloc_move_limit = std::stol(val("--movelimit="));
        else if (a == "--hostlow") hostlow = true;
    }
    ps4_page_reuse = reuse;
    ps4_regalloc_free_moves = regalloc && freemoves;
    ps4_regalloc_r13 = regalloc && r13;

    Env env;
    void* want = hostlow ? reinterpret_cast<void*>(0x10000000ull) : nullptr;
    void* m = mmap(want, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | (hostlow ? MAP_FIXED_NOREPLACE : 0), -1, 0);
    if (m == MAP_FAILED) { std::perror("mmap"); return 2; }
    env.host = static_cast<u8*>(m);

    std::ifstream f(torture ? std::string("guest/out/torture.bin") : guest, std::ios::binary);
    std::vector<char> code((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::ifstream fi(input, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
    if (torture) data.assign(1, 0);
    if (code.empty() || data.empty()) { std::fprintf(stderr, "missing guest.bin or input\n"); return 2; }
    std::memcpy(env.host, code.data(), code.size());
    std::memcpy(env.host + 0x00200000, data.data(), data.size());
    u64 params[3] = {data.size(), 0x01A00000, chunk};   // output capacity 26 MiB
    std::memcpy(env.host + 0x02510000, params, sizeof(params));

    // Guest page -> host page, optionally shuffled so neighbouring guest pages are not neighbours on the host.
    const u64 pages = MEM_SIZE >> 12;
    env.map.resize(pages);
    for (u64 i = 0; i < pages; ++i) env.map[i] = i << 12;
    if (shuffle) {
        u64 seed = 0x1234567;
        for (u64 i = pages - 1; i > 0; --i) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            std::swap(env.map[i], env.map[(seed >> 33) % (i + 1)]);
        }
    }
    // Code and data of the guest were copied linearly above: redo it through the map.
    {
        std::vector<u8> flat(MEM_SIZE);
        std::memcpy(flat.data(), env.host, MEM_SIZE);
        for (u64 i = 0; i < pages; ++i) std::memcpy(env.host + env.map[i], flat.data() + (i << 12), 4096);
    }
    Table table;
    for (u64 off = 0; off < MEM_SIZE; off += 4096) {
        const u64 delta = reinterpret_cast<u64>(env.host) + env.map[off >> 12] - (BASE + off);   // host page - guest page
        table.Map(BASE + off, delta);
    }

    Dynarmic::ExclusiveMonitor monitor(1);
    u64 tpidr = 0, tpidrro = 0;
    Dynarmic::A64::UserConfig cfg;
    cfg.callbacks = &env;
    cfg.global_monitor = &monitor;
    cfg.tpidr_el0 = &tpidr;
    cfg.tpidrro_el0 = &tpidrro;
    cfg.dczid_el0 = 4;
    cfg.ctr_el0 = 0x8444c004;
    cfg.define_unpredictable_behaviour = true;
    cfg.enable_cycle_counting = true;
    cfg.unsafe_optimizations = true;
    cfg.optimizations |= Dynarmic::OptimizationFlag::Unsafe_UnfuseFMA;
    cfg.optimizations |= Dynarmic::OptimizationFlag::Unsafe_IgnoreGlobalMonitor;
    cfg.code_cache_size = 64u << 20;
    cfg.page_table_address_space_bits = 39;
    cfg.silently_mirror_page_table = false;
    cfg.absolute_offset_page_table = true;
    cfg.detect_misaligned_access_via_page_table = 16 | 32 | 64 | 128;
    cfg.only_detect_misalignment_via_page_table_on_page_boundary = true;
    if (mode == "packed") {
        cfg.page_table = reinterpret_cast<void**>(table.dir.data());
        cfg.page_table_two_level_chunk_bits = 13;
        cfg.page_table_pointer_mask = ((1ull << 44) - 1) << 12;
        cfg.page_table_marked_bit = 0;
        cfg.page_table_sign_extension = 8;
    } else if (mode == "direct") {
        cfg.page_table = reinterpret_cast<void**>(table.dir.data());
        cfg.page_table_two_level_chunk_bits = 13;
        cfg.page_table_two_level_value_offset = 65536;
    }   // "ref": no page table, callbacks only

    env.tick_budget = tick_limit;
    Dynarmic::A64::Jit jit(cfg);
    env.jit = &jit;
    jit.SetPC(BASE);
    jit.SetSP(BASE + 0x00200000 - 0x100);
    if (!load_path.empty()) {
        std::ifstream sf(load_path, std::ios::binary);
        std::array<u64, 31> regs{}; u64 sp = 0, pc = 0; u32 pstate = 0;
        sf.read(reinterpret_cast<char*>(regs.data()), sizeof(regs));
        sf.read(reinterpret_cast<char*>(&sp), 8); sf.read(reinterpret_cast<char*>(&pc), 8); sf.read(reinterpret_cast<char*>(&pstate), 4);
        std::vector<char> mem(MEM_SIZE);
        sf.read(mem.data(), MEM_SIZE);
        for (u64 i = 0; i < pages; ++i) std::memcpy(env.host + env.map[i], mem.data() + (i << 12), 4096);
        jit.SetRegisters(regs); jit.SetSP(sp); jit.SetPC(pc); jit.SetPstate(pstate);
        if (patch_addr) { const u32 svc0 = 0xd4000001; u64 off = patch_addr - BASE; std::memcpy(env.host + env.map[off >> 12] + (off & 0xfff), &svc0, 4); }
    }
    std::atomic<bool> finished{false};
    std::thread watchdog([&] {
        for (int i = 0; i < 600 && !finished; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!finished) { std::fprintf(stderr, "TIMEOUT\n"); env.done = true; jit.HaltExecution(); }
    });
    const auto t0 = std::chrono::steady_clock::now();
    if (dump_state) {
        jit.Run();   // runs until the tick budget is used up (or the guest finishes)
        u64 h = 1469598103934665603ULL;
        for (u64 i = 0; i < MEM_SIZE; i += 8) { u64 w; std::memcpy(&w, env.host + i, 8); h = (h ^ w) * 1099511628211ULL; }
        std::printf("STATE pc=%llx sp=%llx ticks=%llu memhash=%016llx regs:", (unsigned long long)jit.GetPC(), (unsigned long long)jit.GetSP(), (unsigned long long)env.ticks_used, (unsigned long long)h);
        for (int i = 0; i < 31; ++i) std::printf(" %llx", (unsigned long long)jit.GetRegister(i));
        std::printf(" nzcv=%x\n", jit.GetPstate());
        if (!save_path.empty()) {
            std::ofstream of(save_path, std::ios::binary);
            auto regs = jit.GetRegisters(); u64 sp = jit.GetSP(), pc = jit.GetPC(); u32 ps = jit.GetPstate();
            of.write(reinterpret_cast<const char*>(regs.data()), sizeof(regs));
            of.write(reinterpret_cast<const char*>(&sp), 8); of.write(reinterpret_cast<const char*>(&pc), 8); of.write(reinterpret_cast<const char*>(&ps), 4);
            std::vector<char> mem(MEM_SIZE);
            for (u64 i = 0; i < pages; ++i) std::memcpy(mem.data() + (i << 12), env.host + env.map[i], 4096);
            of.write(mem.data(), MEM_SIZE);
        }
        finished = true; watchdog.join();
        return 0;
    }
    while (!env.done) jit.Run();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    finished = true;
    watchdog.join();

    auto read_guest = [&](u64 off, void* dst, size_t n) {
        for (size_t i = 0; i < n; ++i) static_cast<u8*>(dst)[i] = env.host[env.map[(off + i) >> 12] + ((off + i) & 0xfff)];
    };
    if (torture) {
        u64 w[3];
        read_guest(0x02500000, w, sizeof(w));
        std::printf("torture mode=%s reuse=%d fm=%d r13=%d hostlow=%d: status=%llu sum=%016llx acc=%016llx cb=%llu faults=%llu time=%.2fs\n",
                    mode.c_str(), reuse, int(ps4_regalloc_free_moves), int(ps4_regalloc_r13), hostlow, (unsigned long long)w[0],
                    (unsigned long long)w[1], (unsigned long long)w[2], (unsigned long long)env.callbacks, (unsigned long long)env.faults, secs);
        std::printf("page reuse: %llu groups, %llu accesses\n", ps4_page_reuse_groups, ps4_page_reuse_accesses);
        return w[0] == 2 ? 0 : 1;
    }
    Result r;
    read_guest(0x02500000, &r, sizeof(r));
    char sum[32];
    std::snprintf(sum, sizeof(sum), "%016llx", (unsigned long long)r.checksum);
    std::printf("page reuse: %llu groups, %llu accesses\n", ps4_page_reuse_groups, ps4_page_reuse_accesses);
    const bool ok = r.status == 2 && (expect.empty() || expect == sum);
    std::printf("mode=%s reuse=%d regalloc=%d fm=%d r13=%d hostlow=%d shuf=%d: status=%llu out=%llu checksum=%s calls=%llu error=%llu cb=%llu faults=%llu time=%.2fs %s\n",
                mode.c_str(), reuse, regalloc, int(ps4_regalloc_free_moves), int(ps4_regalloc_r13), hostlow, int(shuffle), (unsigned long long)r.status, (unsigned long long)r.out_size, sum,
                (unsigned long long)r.calls, (unsigned long long)r.error, (unsigned long long)env.callbacks,
                (unsigned long long)env.faults, secs, ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
