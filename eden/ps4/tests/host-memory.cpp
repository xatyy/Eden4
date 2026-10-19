// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the actual OpenOrbis HostMemory implementation against a deterministic kernel shim.
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <new>
#include <thread>
#include <vector>
#include "common/host_memory.h"
#include "common/page_table.h"
#include "common/logging.h"

namespace {
struct Block { size_t size; void* address{}; };
std::map<off_t, Block> blocks;
off_t next{};
int allocations{}, releases{}, unmaps{};
bool fail_allocate{}, fail_map{};
}
extern "C" size_t sceKernelGetDirectMemorySize() { return size_t{4608} << 20; }
extern "C" int32_t sceKernelAllocateDirectMemory(off_t, off_t, size_t size, size_t alignment,
                                                 int32_t, off_t* output) {
    assert(alignment == 0x4000 && size % alignment == 0);
    if (fail_allocate) return -12;
    *output = next++;
    blocks.emplace(*output, Block{size});
    ++allocations;
    return 0;
}
extern "C" int32_t sceKernelMapDirectMemory(void** output, size_t size, int32_t, int32_t,
                                             off_t physical, size_t alignment) {
    assert(blocks.at(physical).size == size);
    if (fail_map) return -12;
    assert(posix_memalign(output, alignment, size) == 0);
    blocks.at(physical).address = *output;
    return 0;
}
extern "C" int32_t sceKernelMunmap(void* pointer, size_t size) {
    for (auto& [_, block] : blocks) {
        if (block.address != pointer) continue;
        assert(block.size == size);
        free(pointer);
        block.address = nullptr;
        ++unmaps;
        return 0;
    }
    std::abort();
}
extern "C" int32_t sceKernelReleaseDirectMemory(off_t physical, size_t size) {
    assert(blocks.at(physical).size == size && !blocks.at(physical).address);
    blocks.erase(physical);
    ++releases;
    return 0;
}
namespace Common::Log {
void FmtLogMessageImpl(Class, Level, const char*, unsigned, const char*, fmt::string_view,
                       const fmt::format_args&) {}
}
void AssertFailSoftImpl() { std::abort(); }
[[noreturn]] void AssertFatalImpl() { std::abort(); }

int main() {
    {
        Common::SparseLargeVector<u32> table(size_t{1} << 27);
        assert(table.size() == size_t{1} << 27);
        assert(table.data() == nullptr && table[123456789] == 0);
        table.Set(16383, 7);
        table.Set(16384, 42);
        table.Set(table.size() - 1, 99);
        assert(table[16383] == 7 && table[16384] == 42);
        table.ZeroRegion(16383, 16385);
        assert(table[16383] == 0 && table[16384] == 0);
        assert(table[table.size() - 1] == 99);
        std::vector<std::thread> threads;
        for (size_t i = 0; i < 8; ++i)
            threads.emplace_back([&, i] { table.Set(40000 + i, static_cast<u32>(i + 1)); });
        for (auto& thread : threads) thread.join();
        for (size_t i = 0; i < 8; ++i) assert(table[40000 + i] == i + 1);
        table.ResizeAndClear(table.size());
        assert(table[40000] == 0 && table[table.size() - 1] == 0);
        Common::SparseLargeVector<Common::PageTable::PageEntryData> entries(100000);
        assert(entries[80000].Type() == Common::PageType::Unmapped);
        entries.GetAndFault(80000).Store(false, Common::PageType::Memory, 19, 0x100004000ULL);
        assert(entries[80000].Pointer() == 0x100004000ULL && entries[80000].Block() == 19);
        entries.ZeroRegion(80000, 80001);
        assert(entries[80000].Type() == Common::PageType::Unmapped);
    }
    {
        Common::HostMemory memory(0x4001, size_t{1} << 39);
        assert(blocks.begin()->second.size == 0x8000);
        assert(memory.BackingBasePointer() && !memory.VirtualBasePointer());
        assert(!memory.IsInVirtualRange(nullptr));
        memory.BackingBasePointer()[0x4000] = 42;
        Common::HostMemory moved(std::move(memory));
        assert(!memory.BackingBasePointer());
        assert(moved.BackingBasePointer()[0x4000] == 42);
        Common::HostMemory assigned(0x4000, 0x4000);
        assigned = std::move(moved);
        assert(!moved.BackingBasePointer());
        assert(assigned.BackingBasePointer()[0x4000] == 42);
        assigned = std::move(assigned);
    }
    assert(blocks.empty() && allocations == 2 && unmaps == 2 && releases == 2);
    fail_allocate = true;
    try { Common::HostMemory memory(0x4000, 0); std::abort(); }
    catch (const std::bad_alloc&) {}
    fail_allocate = false;
    assert(blocks.empty() && releases == 2);
    fail_map = true;
    try { Common::HostMemory memory(0x4000, 0); std::abort(); }
    catch (const std::bad_alloc&) {}
    fail_map = false;
    assert(blocks.empty() && releases == 3 && unmaps == 2);
    for (size_t size : {size_t{0}, SIZE_MAX}) {
        try { Common::HostMemory memory(size, 0); std::abort(); }
        catch (const std::bad_alloc&) {}
    }
    assert(blocks.empty() && allocations == 3 && releases == 3);
    std::cout << "PASS: PS4 sparse tables, concurrent publication, zero/reset, atomic entries, page alignment, ownership, move operations, allocation/map failure cleanup, size bounds\n";
}
