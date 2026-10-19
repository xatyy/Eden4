// SPDX-License-Identifier: GPL-3.0-or-later
// Game titles, IDs and icons for the HOME menu, from a cache in /data/EdenPS4/cache/ui. The cache is
// written when a game starts (Store), by the loader Eden already has then: reading NSP/XCI files in
// the launcher itself (a bare Core::System on another thread) ended the process in 00.26.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include "yuzu_ps4/ui_thread.h"
#include <vector>
#include "yuzu_ps4/ui_canvas.h"

namespace OrbisFrontend {

class Metadata {
public:
    struct Entry {
        std::string path;
        uint64_t size{};
        int64_t mtime{};
    };
    struct Result {
        size_t index;
        std::string title;
        uint64_t program_id{};
        std::shared_ptr<const Ui::Image> home, library;
    };

    ~Metadata();
    void Start(std::vector<Entry> entries);
    /// Saves a game's metadata for the next time the menu opens.
    static void Store(const Entry& entry, const std::string& title, uint64_t program_id,
                      const std::vector<uint8_t>& icon);
    void Stop();
    std::vector<Result> Take();
    bool Busy() const { return busy; }

private:
    void Run(std::vector<Entry> entries);
    StackThread thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> busy{false};
    std::mutex mutex;
    std::vector<Result> results;
};

} // namespace OrbisFrontend
