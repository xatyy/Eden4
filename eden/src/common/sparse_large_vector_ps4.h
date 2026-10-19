// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <memory>

namespace Common {

// Flexible memory on PS4 cannot back a desktop-sized flat page table. Keep a
// small pointer directory and publish zero-initialized chunks on first write.
// Mirrored: every chunk holds a second array of ChunkEntries values right after the first, so an
// entry's mirror value lies a constant MirrorOffsetBytes after it (the page table's direct table).
template <typename T, bool Mirrored = false>
class SparseLargeVector final {
public:
    SparseLargeVector() = default;
    explicit SparseLargeVector(size_t count) { ResizeAndClear(count); }
    ~SparseLargeVector() { Clear(); }
    SparseLargeVector(const SparseLargeVector&) = delete;
    SparseLargeVector& operator=(const SparseLargeVector&) = delete;

    void ResizeAndClear(size_t count) {
        const size_t new_chunk_count = count / ChunkEntries + (count % ChunkEntries != 0);
        auto directory = std::make_unique<std::atomic<T*>[]>(new_chunk_count);
        Clear();
        count_ = count;
        chunk_count = new_chunk_count;
        chunks = std::move(directory);
    }

    T& GetAndFault(size_t index) {
        ASSERT(index < count_);
        const size_t slot = index / ChunkEntries;
        T* chunk = chunks[slot].load(std::memory_order_acquire);
        if (!chunk) {
            auto candidate = std::make_unique<T[]>(ChunkEntries * (Mirrored ? 2 : 1));
            T* expected = nullptr;
            if (chunks[slot].compare_exchange_strong(expected, candidate.get(),
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                chunk = candidate.release();
            } else {
                chunk = expected;
            }
        }
        return chunk[index % ChunkEntries];
    }

    const T& GetOrDefault(size_t index) const {
        ASSERT(index < count_);
        const T* chunk = chunks[index / ChunkEntries].load(std::memory_order_acquire);
        return chunk ? chunk[index % ChunkEntries] : zero;
    }

    void Set(size_t index, const T& value) { GetAndFault(index) = value; }
    T& GetUnchecked(size_t index) { return GetAndFault(index); }
    const T& operator[](size_t index) const { return GetOrDefault(index); }
    size_t size() const noexcept { return count_; }

    // A null flat pointer makes Dynarmic use its memory callbacks on this target.
    const T* data() const noexcept { return nullptr; }

    // Two-level lookup for Dynarmic: directory()[index >> ChunkBits()] is a chunk or null,
    // chunk[index & (ChunkEntries - 1)] is the entry. Chunks are never freed or moved until Clear.
    const void* directory() const noexcept { return chunks.get(); }
    static constexpr size_t MirrorOffsetBytes = Mirrored ? (65536 / sizeof(T)) * sizeof(T) : 0;
    static constexpr unsigned ChunkBits() noexcept {
        return static_cast<unsigned>(std::countr_zero(ChunkEntries));
    }

    void CommitRegion(size_t start, size_t end) {
        ASSERT(start <= end && end <= count_);
        for (size_t index = start; index < end;) {
            GetAndFault(index);
            index += std::min(end - index, ChunkEntries - index % ChunkEntries);
        }
    }

    void ZeroRegion(size_t start, size_t end) {
        ASSERT(start <= end && end <= count_);
        while (start < end) {
            const size_t offset = start % ChunkEntries;
            const size_t length = std::min(end - start, ChunkEntries - offset);
            if (T* chunk = chunks[start / ChunkEntries].load(std::memory_order_acquire)) {
                // The mirror first: the direct value must never be more permissive than the entry.
                if constexpr (Mirrored)
                    std::memset(chunk + ChunkEntries + offset, 0, length * sizeof(T));
                std::memset(chunk + offset, 0, length * sizeof(T));
            }
            start += length;
        }
    }

private:
    void Clear() noexcept {
        for (size_t i = 0; i < chunk_count; ++i)
            delete[] chunks[i].load(std::memory_order_relaxed);
        chunks.reset();
        chunk_count = 0;
        count_ = 0;
    }
    static constexpr size_t ChunkEntries = 65536 / sizeof(T);
    static_assert(ChunkEntries > 0 && std::has_single_bit(ChunkEntries));
    static_assert(std::atomic<T*>::is_always_lock_free && sizeof(std::atomic<T*>) == sizeof(T*));
    size_t count_{};
    size_t chunk_count{};
    std::unique_ptr<std::atomic<T*>[]> chunks;
    const T zero{};
};
} // namespace Common
