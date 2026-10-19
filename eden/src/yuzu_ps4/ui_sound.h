// SPDX-License-Identifier: GPL-3.0-or-later
// HOME menu sounds: short tones synthesized at start, mixed on one AudioOut port by a thread that
// only runs while the launcher is shown.
#pragma once

#include <atomic>
#include <mutex>
#include "yuzu_ps4/ui_thread.h"
#include <vector>
#include "yuzu_ps4/ui_launcher.h"

namespace OrbisFrontend {

class UiSound {
public:
    UiSound();
    ~UiSound();
    void Start();
    void Stop();
    void Play(Ui::Sound sound);
    void SetEnabled(bool enabled) { this->enabled = enabled; }

private:
    void Run();
    struct Voice {
        const std::vector<int16_t>* samples;
        size_t position;
    };
    std::vector<std::vector<int16_t>> sounds; // mono, 48 kHz, one per Ui::Sound
    std::vector<Voice> voices;
    std::mutex mutex;
    StackThread thread;
    std::atomic<bool> running{false};
    bool enabled{true};
    int port{-1};
};

} // namespace OrbisFrontend
