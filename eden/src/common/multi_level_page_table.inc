// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "common/assert.h"
#include "common/multi_level_page_table.h"

#ifdef __OPENORBIS__
extern "C" void* ps4_reserve_lazy(std::size_t size);
extern "C" int ps4_commit_lazy(void* address, std::size_t size);
extern "C" void ps4_release_lazy(void* address, std::size_t size);
#endif

namespace Common {

template <typename BaseAddr>
MultiLevelPageTable<BaseAddr>::MultiLevelPageTable(std::size_t address_space_bits_, std::size_t first_level_bits_, std::size_t page_bits_)
    : address_space_bits{address_space_bits_}
    , first_level_bits{first_level_bits_}
    , page_bits{page_bits_}
{
    if (page_bits == 0) {
        return;
    }
    first_level_shift = address_space_bits - first_level_bits;
    first_level_chunk_size = (1ULL << (first_level_shift - page_bits)) * sizeof(BaseAddr);
    alloc_size = (1ULL << (address_space_bits - page_bits)) * sizeof(BaseAddr);
    std::size_t first_level_size = 1ULL << first_level_bits;
    first_level_map.resize(first_level_size, nullptr);
#ifdef _WIN32
    void* base{VirtualAlloc(nullptr, alloc_size, MEM_RESERVE, PAGE_READWRITE)};
#elif defined(__OPENORBIS__)
    // Reserved only; AllocateLevel backs each first-level chunk (64 KiB for the GPU table) when it
    // is first used, as on Windows. A plain mmap would take all 128 MiB at once.
    void* base{(first_level_chunk_size % 16384) == 0 ? ps4_reserve_lazy(alloc_size) : nullptr};
    lazy = base != nullptr;
    if (!base) {
        base = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED)
            base = nullptr;
    }
#else
    void* base{mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)};
    if (base == MAP_FAILED)
        base = nullptr;
#endif
    ASSERT(base);
    base_ptr = reinterpret_cast<BaseAddr*>(base);
}

template <typename BaseAddr>
MultiLevelPageTable<BaseAddr>::~MultiLevelPageTable() noexcept {
    if (!base_ptr) {
        return;
    }
#ifdef _WIN32
    ASSERT(VirtualFree(base_ptr, 0, MEM_RELEASE));
#elif defined(__OPENORBIS__)
    if (lazy)
        ps4_release_lazy(base_ptr, alloc_size);
    else
        ASSERT(munmap(base_ptr, alloc_size) == 0);
#else
    ASSERT(munmap(base_ptr, alloc_size) == 0);
#endif
}

template <typename BaseAddr>
void MultiLevelPageTable<BaseAddr>::ReserveRange(u64 start, std::size_t size) {
    const u64 new_start = start >> first_level_shift;
    const u64 new_end = (start + size) >> first_level_shift;
    for (u64 i = new_start; i <= new_end; i++)
        if (!first_level_map[i])
            AllocateLevel(i);
}

template <typename BaseAddr>
void MultiLevelPageTable<BaseAddr>::AllocateLevel(u64 index) {
    void* ptr = reinterpret_cast<char *>(base_ptr) + index * first_level_chunk_size;
#ifdef _WIN32
    void* base = VirtualAlloc(ptr, first_level_chunk_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(base);
#elif defined(__OPENORBIS__)
    void* base = ptr;
    if (lazy)
        ASSERT(ps4_commit_lazy(ptr, first_level_chunk_size) == 0);
#else
    void* base = ptr;
#endif
    first_level_map[index] = base;
}

} // namespace Common
