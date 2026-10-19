// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <orbis/AudioOut.h>
#include "common/logging.h"
#include "yuzu_ps4/ui_sound.h"

namespace OrbisFrontend {
namespace {
constexpr int Rate = 48000;
constexpr int Frames = 256;

struct Tone {
    float start_hz, end_hz, seconds, delay, volume;
};

// Sums decaying sine tones into one mono clip.
std::vector<int16_t> Synth(std::initializer_list<Tone> tones) {
    float length = 0;
    for (const auto& t : tones) length = std::max(length, t.delay + t.seconds);
    std::vector<float> mix(size_t(length * Rate) + 1);
    for (const auto& t : tones) {
        const size_t begin = size_t(t.delay * Rate), count = size_t(t.seconds * Rate);
        float phase = 0;
        for (size_t i = 0; i < count && begin + i < mix.size(); ++i) {
            const float progress = float(i) / count;
            const float hz = t.start_hz + (t.end_hz - t.start_hz) * progress;
            phase += 6.2831853f * hz / Rate;
            const float attack = std::min(1.0f, i / (0.004f * Rate));
            const float envelope = attack * std::exp(-5.0f * progress);
            mix[begin + i] += std::sin(phase) * envelope * t.volume;
        }
    }
    std::vector<int16_t> out(mix.size());
    for (size_t i = 0; i < mix.size(); ++i) out[i] = int16_t(std::clamp(mix[i], -1.0f, 1.0f) * 32767);
    return out;
}
} // namespace

UiSound::UiSound() {
    // Same order as Ui::Sound.
    sounds = {
        Synth({{1250, 1150, 0.035f, 0, 0.16f}}),                                         // Focus
        Synth({{880, 880, 0.05f, 0, 0.22f}, {1320, 1320, 0.07f, 0.045f, 0.22f}}),        // Select
        Synth({{1320, 1320, 0.05f, 0, 0.18f}, {880, 880, 0.07f, 0.045f, 0.18f}}),        // Back
        Synth({{523, 523, 0.35f, 0, 0.16f}, {659, 659, 0.35f, 0.04f, 0.14f},
               {784, 784, 0.40f, 0.08f, 0.14f}, {1047, 1047, 0.45f, 0.12f, 0.10f}}),     // Launch
        Synth({{220, 200, 0.14f, 0, 0.25f}, {440, 400, 0.14f, 0, 0.08f}}),               // Error
        Synth({{1500, 1500, 0.025f, 0, 0.16f}}),                                         // Toggle
        Synth({{600, 1200, 0.09f, 0, 0.18f}}),                                           // Open
        Synth({{300, 260, 0.05f, 0, 0.22f}}),                                            // Limit
    };
}

UiSound::~UiSound() {
    Stop();
}

void UiSound::Start() {
    if (running) return;
    sceAudioOutInit(); // already initialised after a game: harmless
    port = sceAudioOutOpen(0xff, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, Frames, Rate, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (port < 0) {
        LOG_WARNING(Frontend, "PS4 menu sounds: AudioOut port not available ({:#x})", u32(port));
        return;
    }
    running = true;
    if (!thread.Start([this] { Run(); }, 512u << 10)) {
        running = false;
        sceAudioOutClose(port);
        port = -1;
    }
}

void UiSound::Stop() {
    if (!running) return;
    running = false;
    thread.Join();
    sceAudioOutClose(port);
    port = -1;
    std::scoped_lock lock{mutex};
    voices.clear();
}

void UiSound::Play(Ui::Sound sound) {
    if (!enabled || !running) return;
    std::scoped_lock lock{mutex};
    if (voices.size() < 8) voices.push_back({&sounds[size_t(sound)], 0});
}

void UiSound::Run() {
    std::vector<int16_t> buffer(Frames * 2);
    while (running) {
        {
            std::scoped_lock lock{mutex};
            std::fill(buffer.begin(), buffer.end(), int16_t{0});
            for (auto& voice : voices) {
                for (int i = 0; i < Frames && voice.position < voice.samples->size(); ++i, ++voice.position) {
                    const int sample = (*voice.samples)[voice.position];
                    for (int c = 0; c < 2; ++c)
                        buffer[i * 2 + c] = int16_t(std::clamp(buffer[i * 2 + c] + sample, -32768, 32767));
                }
            }
            voices.erase(std::remove_if(voices.begin(), voices.end(),
                                        [](const Voice& v) { return v.position >= v.samples->size(); }),
                         voices.end());
        }
        // Blocks until the previous block has played (~5 ms), which paces this thread.
        sceAudioOutOutput(port, buffer.data());
    }
    sceAudioOutOutput(port, nullptr);
}

} // namespace OrbisFrontend
