// SPDX-License-Identifier: GPL-3.0-or-later
// Direct-memory framebuffers follow DolphinPS4's VideoOut probe (GPL-2.0-or-later).

#include <algorithm>
#include <cstring>
#include <vector>
#include <iterator>
#include <utility>
#include <stdexcept>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>
#include <unistd.h>
#include "common/logging.h"
#include "yuzu_ps4/platform.h"

namespace OrbisFrontend {
void InitializeModules() {
    const OrbisSysModuleInternal modules[] = {
        ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE, ORBIS_SYSMODULE_INTERNAL_USER_SERVICE,
        ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT, ORBIS_SYSMODULE_INTERNAL_PAD,
        ORBIS_SYSMODULE_INTERNAL_AUDIOOUT,
    };
    for (const auto module : modules) {
        const int status = sceSysmoduleLoadModuleInternal(module);
        LOG_INFO(Frontend, "PS4 module {:#x}: {:#x}", u32(module), u32(status));
        if (status < 0) throw std::runtime_error{"PS4 system module could not be loaded"};
    }
    const int result = sceUserServiceInitialize(nullptr);
    LOG_INFO(Frontend, "PS4 UserService: {:#x}", u32(result));
}

// Every signed-in PS4 user has their own controller (up to 4). Slot 0 is the user who started
// Eden; others take the first free slot when they sign in (PS button on another controller) and
// keep it until they sign out.
Controller::Controller() {
    const int result = scePadInit();
    LOG_INFO(Input, "PS4 Pad initialization: {:#x}", u32(result));
    OrbisUserServiceUserId user{-1};
    if (sceUserServiceGetInitialUser(&user) < 0)
        throw std::runtime_error{"No signed-in PS4 user"};
    if (!Open(0, user)) throw std::runtime_error{"PS4 controller could not be opened"};
    Refresh();
}
Controller::~Controller() {
    for (Pad& pad : pads)
        if (pad.handle >= 0) scePadClose(pad.handle);
}
bool Controller::Open(size_t slot, int32_t user) {
    const int handle = scePadOpen(user, 0, 0, nullptr);
    LOG_INFO(Input, "PS4 controller for user {:#x} in slot {}: handle {:#x}", u32(user), slot + 1, u32(handle));
    if (handle < 0) return false;
    char name[32]{};
    sceUserServiceGetUserName(user, name, sizeof(name));
    pads[slot] = Pad{.user = user, .handle = handle, .state = {}, .name = name};
    changed = true;
    return true;
}
void Controller::Swap(size_t a, size_t b) {
    if (a >= pads.size() || b >= pads.size() || a == b) return;
    std::swap(pads[a], pads[b]);
    LOG_INFO(Input, "PS4 controller slots {} and {} swapped", a + 1, b + 1);
    changed = true;
}
void Controller::Refresh() {
    OrbisUserServiceLoginUserIdList list{};
    if (sceUserServiceGetLoginUserIdList(&list) < 0) return;
    const auto logged_in = [&](int32_t user) {
        return std::find(std::begin(list.userId), std::end(list.userId), user) != std::end(list.userId);
    };
    for (size_t slot = 0; slot < pads.size(); ++slot) {
        Pad& pad = pads[slot];
        if (pad.handle >= 0 && !logged_in(pad.user)) {
            LOG_INFO(Input, "PS4 user {:#x} signed out: slot {} free", u32(pad.user), slot + 1);
            scePadClose(pad.handle);
            pad = Pad{};
            changed = true;
        }
    }
    for (const int32_t user : list.userId) {
        if (user < 0) continue;
        const bool known = std::any_of(pads.begin(), pads.end(), [&](const Pad& pad) {
            return pad.handle >= 0 && pad.user == user;
        });
        if (known) continue;
        const auto free = std::find_if(pads.begin(), pads.end(), [](const Pad& pad) { return pad.handle < 0; });
        if (free != pads.end()) Open(static_cast<size_t>(free - pads.begin()), user);
    }
}
bool Controller::Read() {
    if (++reads % 60 == 0) Refresh();
    previous = state.buttons;
    state = {};
    for (Pad& pad : pads) {
        const bool was_connected = pad.state.connected;
        OrbisPadData next{};
        if (pad.handle < 0 || scePadReadState(pad.handle, &next) < 0 || !next.connected) next = {};
        pad.state = next;
        if (bool(next.connected) != was_connected) changed = true;
        if (!next.connected) continue;
        // The launcher reacts to any controller; sticks come from the first one connected.
        if (!state.connected) state = next;
        state.buttons |= next.buttons;
    }
    return state.connected;
}
bool Controller::TakeChanged() {
    return std::exchange(changed, false);
}
bool Controller::Pressed(uint32_t mask) const {
    return (state.buttons & mask) && !(previous & mask);
}

namespace {
constexpr uint32_t PixelFormatArgb = 0x80000000; // A8R8G8B8_SRGB
constexpr size_t DisplayAlignment = 2 * 1024 * 1024;
// VideoOut setups tried in order until a test flip succeeds. 00.20 in PS4 Pro mode registered the
// first one but its flip failed; the reason is not known yet, so the alternatives cover size,
// pixel format and memory type.
const DisplayMode DisplayModes[] = {
    {1920, 1080, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB, ORBIS_KERNEL_WC_GARLIC, "1920x1080 ABGR garlic"},
    {1280, 720, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB, ORBIS_KERNEL_WC_GARLIC, "1280x720 ABGR garlic"},
    {1920, 1080, PixelFormatArgb, ORBIS_KERNEL_WC_GARLIC, "1920x1080 ARGB garlic"},
    {1280, 720, PixelFormatArgb, ORBIS_KERNEL_WC_GARLIC, "1280x720 ARGB garlic"},
    {1920, 1080, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB, ORBIS_KERNEL_WB_ONION, "1920x1080 ABGR onion"},
    {1280, 720, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB, ORBIS_KERNEL_WB_ONION, "1280x720 ABGR onion"},
};
std::optional<DisplayMode> g_working_mode;
}

std::optional<DisplayMode> WorkingDisplayMode() { return g_working_mode; }

Display::Display() {
    // Once a setup has worked, use only that one.
    std::vector<DisplayMode> modes;
    if (g_working_mode) modes.push_back(*g_working_mode);
    else modes.assign(std::begin(DisplayModes), std::end(DisplayModes));
    for (const DisplayMode& candidate : modes) {
        if (Open(candidate)) {
            if (!g_working_mode)
                LOG_INFO(Frontend, "PS4 launcher display: {} works", candidate.name);
            g_working_mode = candidate;
            return;
        }
        Release();
    }
    throw std::runtime_error{"PS4 launcher display: no VideoOut setup could flip (see eden.log)"};
}

bool Display::Open(const DisplayMode& candidate) {
    mode = candidate;
    allocation = (size_t(mode.width) * mode.height * 4 + DisplayAlignment - 1) & ~(DisplayAlignment - 1);
    video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (video < 0) {
        LOG_ERROR(Frontend, "PS4 launcher display {}: open failed {:#x}", mode.name, u32(video));
        return false;
    }
    for (size_t i = 0; i < pixels.size(); ++i) {
        off_t offset{};
        int result = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), allocation,
                                                   DisplayAlignment, mode.memory_type, &offset);
        if (result != 0) {
            LOG_ERROR(Frontend, "PS4 launcher display {}: allocation failed {:#x}", mode.name, u32(result));
            return false;
        }
        physical[i] = offset;
        result = sceKernelMapDirectMemory(&pixels[i], allocation,
            ORBIS_KERNEL_PROT_CPU_RW | ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE,
            0, offset, DisplayAlignment);
        if (result != 0) {
            LOG_ERROR(Frontend, "PS4 launcher display {}: mapping failed {:#x}", mode.name, u32(result));
            return false;
        }
        std::fill_n(static_cast<uint32_t*>(pixels[i]), size_t(mode.width) * mode.height, 0xff000000);
    }
    OrbisVideoOutBufferAttribute attributes{};
    sceVideoOutSetBufferAttribute(&attributes, mode.format, ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0,
                                  mode.width, mode.height, mode.width);
    const int result = sceVideoOutRegisterBuffers(video, 0, pixels.data(), pixels.size(), &attributes);
    if (result < 0) {
        LOG_ERROR(Frontend, "PS4 launcher display {}: registration failed {:#x}", mode.name, u32(result));
        return false;
    }
    return Flip();
}

bool Display::Flip() {
    const int64_t argument = ++flips;
    const int result = sceVideoOutSubmitFlip(video, static_cast<int>(back), ORBIS_VIDEO_OUT_FLIP_VSYNC, argument);
    if (result < 0) {
        LOG_ERROR(Frontend, "PS4 launcher display {}: flip failed {:#x}", mode.name, u32(result));
        return false;
    }
    for (int attempt = 0; attempt < 1000; ++attempt) {
        OrbisVideoOutFlipStatus status{};
        const int query = sceVideoOutGetFlipStatus(video, &status);
        if (query < 0) {
            LOG_ERROR(Frontend, "PS4 launcher display {}: flip status failed {:#x}", mode.name, u32(query));
            return false;
        }
        if (status.flipArg >= argument) { back ^= 1; return true; }
        usleep(1000);
    }
    LOG_ERROR(Frontend, "PS4 launcher display {}: flip timed out", mode.name);
    return false;
}

Display::~Display() { Release(); }
void Display::Release() noexcept {
    if (video >= 0) { sceVideoOutClose(video); video = -1; }
    for (size_t i = 0; i < pixels.size(); ++i) {
        if (pixels[i]) { sceKernelMunmap(pixels[i], allocation); pixels[i] = nullptr; }
        if (physical[i] >= 0) {
            sceKernelReleaseDirectMemory(physical[i], allocation);
            physical[i] = -1;
        }
    }
    back = 0;
}
void Display::Present(const uint32_t* canvas) {
    auto* out = static_cast<uint32_t*>(pixels[back]);
    const bool swap = mode.format == PixelFormatArgb;
    const auto convert = [swap](uint32_t pixel) {
        return swap ? (pixel & 0xff00ff00) | ((pixel & 0xff) << 16) | ((pixel >> 16) & 0xff) : pixel;
    };
    if (mode.width == CanvasWidth && mode.height == CanvasHeight) {
        if (!swap) {
            std::memcpy(out, canvas, size_t(CanvasWidth) * CanvasHeight * 4);
        } else {
            for (size_t i = 0; i < size_t(CanvasWidth) * CanvasHeight; ++i) out[i] = convert(canvas[i]);
        }
    } else {
        // 1920x1080 -> 1280x720: every 3x3 source block becomes 2x2 (weights 2/3 and 1/3).
        std::vector<uint32_t> row(size_t(mode.width) * 3);
        for (int by = 0; by < CanvasHeight / 3; ++by) {
            for (int r = 0; r < 3; ++r) {
                const uint32_t* src = canvas + size_t(by * 3 + r) * CanvasWidth;
                for (int bx = 0; bx < CanvasWidth / 3; ++bx) {
                    const uint32_t a = src[bx * 3], b = src[bx * 3 + 1], c = src[bx * 3 + 2];
                    const auto mix = [](uint32_t p, uint32_t q) {
                        uint32_t v = 0xff000000u;
                        for (int shift : {0, 8, 16})
                            v |= ((((p >> shift) & 255) * 2 + ((q >> shift) & 255)) / 3) << shift;
                        return v;
                    };
                    row[size_t(r) * mode.width + bx * 2] = mix(a, b);
                    row[size_t(r) * mode.width + bx * 2 + 1] = mix(c, b);
                }
            }
            for (int half = 0; half < 2; ++half) {
                uint32_t* dst = out + size_t(by * 2 + half) * mode.width;
                const uint32_t* p = &row[size_t(half == 0 ? 0 : 2) * mode.width];
                const uint32_t* q = &row[size_t(1) * mode.width];
                for (int x = 0; x < mode.width; ++x) {
                    uint32_t v = 0xff000000u;
                    for (int shift : {0, 8, 16})
                        v |= ((((p[x] >> shift) & 255) * 2 + ((q[x] >> shift) & 255)) / 3) << shift;
                    dst[x] = convert(v);
                }
            }
        }
    }
    if (!Flip())
        throw std::runtime_error{"PS4 launcher display flip failed"};
}
}
