// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <string_view>
#include <orbis/Pad.h>

namespace OrbisFrontend {
void InitializeModules();
class Controller {
public:
    static constexpr size_t MaxPads = 4; // PS4: at most 4 signed-in users, one controller each
    Controller();
    ~Controller();
    /// Reads every controller (and every ~60 calls, who is signed in). Returns whether any is connected.
    bool Read();
    /// Pressed on any controller since the previous Read.
    bool Pressed(uint32_t mask) const;
    /// All controllers combined (buttons of all, sticks of the first connected).
    const OrbisPadData& State() const { return state; }
    /// One controller; `connected` is false for an empty slot or a controller that is off.
    const OrbisPadData& State(size_t slot) const { return pads[slot].state; }
    /// True once after a controller connected, disconnected, signed in or out.
    bool TakeChanged();
    /// The PS4 user who owns the controller in this slot ("" for an empty slot).
    const std::string& UserName(size_t slot) const { return pads[slot].name; }
    /// Swaps two slots, so those players trade controllers.
    void Swap(size_t a, size_t b);
private:
    struct Pad {
        int32_t user{-1};
        int handle{-1};
        OrbisPadData state{};
        std::string name;
    };
    bool Open(size_t slot, int32_t user);
    void Refresh();
    std::array<Pad, MaxPads> pads{};
    uint64_t reads{};
    bool changed{};
    uint32_t previous{};
    OrbisPadData state{};
};

struct DisplayMode {
    int width;
    int height;
    uint32_t format;
    int memory_type;
    const char* name;
};
/// The VideoOut setup the launcher found working (empty before the first Display).
std::optional<DisplayMode> WorkingDisplayMode();

class Display {
public:
    static constexpr int CanvasWidth = 1920;
    static constexpr int CanvasHeight = 1080;
    Display();
    ~Display();
    /// Shows a 1920x1080 canvas (0xAABBGGRR pixels) and waits for the flip.
    void Present(const uint32_t* canvas);
private:
    bool Open(const DisplayMode& candidate);
    bool Flip();
    DisplayMode mode{};
    size_t allocation{};
    int video{-1};
    std::array<void*, 2> pixels{};
    std::array<int64_t, 2> physical{{-1, -1}};
    size_t back{};
    int64_t flips{};
    void Release() noexcept;
};
}
