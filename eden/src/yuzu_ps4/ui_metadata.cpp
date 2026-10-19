// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <sys/stat.h>
#include "common/logging.h"
#include "yuzu_ps4/ui_metadata.h"

namespace OrbisFrontend {
namespace {
constexpr char CacheDir[] = "/data/EdenPS4/cache/ui";
constexpr uint32_t CacheMagic = 0x31554445; // "EDU1"

uint64_t Hash(const Metadata::Entry& entry) {
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&](const void* data, size_t size) {
        for (size_t i = 0; i < size; ++i) h = (h ^ static_cast<const uint8_t*>(data)[i]) * 1099511628211ull;
    };
    mix(entry.path.data(), entry.path.size());
    mix(&entry.size, sizeof(entry.size));
    mix(&entry.mtime, sizeof(entry.mtime));
    return h;
}

std::string CachePath(const Metadata::Entry& entry) {
    char name[64];
    snprintf(name, sizeof(name), "/%016llx.bin", static_cast<unsigned long long>(Hash(entry)));
    return CacheDir + std::string{name};
}

struct Info {
    std::string title;
    uint64_t program_id{};
    std::vector<uint8_t> icon; // JPEG
};

bool ReadCache(const std::string& path, Info& info) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return false;
    uint32_t magic = 0, title_size = 0, icon_size = 0;
    bool ok = fread(&magic, 4, 1, file) == 1 && magic == CacheMagic && fread(&info.program_id, 8, 1, file) == 1 &&
              fread(&title_size, 4, 1, file) == 1 && title_size < 4096;
    if (ok) {
        info.title.resize(title_size);
        ok = fread(info.title.data(), 1, title_size, file) == title_size && fread(&icon_size, 4, 1, file) == 1 &&
             icon_size < (8u << 20);
    }
    if (ok) {
        info.icon.resize(icon_size);
        ok = fread(info.icon.data(), 1, icon_size, file) == icon_size;
    }
    fclose(file);
    return ok;
}

void WriteCache(const std::string& path, const Info& info) {
    mkdir("/data/EdenPS4/cache", 0777);
    mkdir(CacheDir, 0777);
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) return;
    const uint32_t title_size = uint32_t(info.title.size()), icon_size = uint32_t(info.icon.size());
    fwrite(&CacheMagic, 4, 1, file);
    fwrite(&info.program_id, 8, 1, file);
    fwrite(&title_size, 4, 1, file);
    fwrite(info.title.data(), 1, title_size, file);
    fwrite(&icon_size, 4, 1, file);
    fwrite(info.icon.data(), 1, icon_size, file);
    fclose(file);
}
} // namespace

Metadata::~Metadata() {
    Stop();
}

void Metadata::Start(std::vector<Entry> entries) {
    Stop();
    stop = false;
    busy = true;
    if (!thread.Start([this, entries = std::move(entries)]() mutable { Run(std::move(entries)); }, 1u << 20)) {
        LOG_ERROR(Frontend, "PS4 HOME menu: metadata thread could not start");
        busy = false;
    }
}

void Metadata::Stop() {
    stop = true;
    thread.Join();
    busy = false;
}

std::vector<Metadata::Result> Metadata::Take() {
    std::scoped_lock lock{mutex};
    return std::exchange(results, {});
}

void Metadata::Store(const Entry& entry, const std::string& title, uint64_t program_id,
                     const std::vector<uint8_t>& icon) {
    WriteCache(CachePath(entry), Info{title, program_id, icon});
}

void Metadata::Run(std::vector<Entry> entries) {
    size_t cached = 0;
    for (size_t index = 0; index < entries.size() && !stop; ++index) {
        const Entry& entry = entries[index];
        Info info;
        if (entry.path.empty() || !ReadCache(CachePath(entry), info)) continue;
        ++cached;
        Result result{index, info.title, info.program_id, nullptr, nullptr};
        if (!info.icon.empty()) {
            auto home = std::make_shared<Ui::Image>(Ui::DecodeImage(info.icon, 384, 384));
            if (!home->Empty()) {
                result.library = std::make_shared<Ui::Image>(Ui::Resize(*home, 261, 261));
                result.home = std::move(home);
            }
        }
        std::scoped_lock lock{mutex};
        results.push_back(std::move(result));
    }
    LOG_INFO(Frontend, "PS4 HOME menu: {} of {} games have saved icons (the others get theirs when started)",
             cached, entries.size());
    busy = false;
}

} // namespace OrbisFrontend
