// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include "common/cpu_features.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/param_package.h"
#include "common/ps4_diagnostics.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/kernel/svc_types.h"
#include "core/hle/service/acc/profile_manager.h"
#include "common/uuid.h"
#include "core/arm/arm_interface.h"
#include "core/hle/kernel/k_process.h"
#include "core/cpu_manager.h"
#include "core/perf_stats.h"
#include "core/loader/loader.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "input_common/main.h"
#include "video_core/gpu.h"
#include "yuzu_ps4/platform.h"
#include "yuzu_ps4/ui_canvas.h"
#include "yuzu_ps4/ui_launcher.h"
#include "yuzu_ps4/ui_metadata.h"
#include "yuzu_ps4/ui_sound.h"

extern "C" int sceSystemServiceLoadExec(const char*, char* const[]);
extern "C" void ps4_heap_set_overflow_gpu_reserve(size_t bytes);
extern "C" size_t ps4_heap_overflow_size();
extern "C" int32_t sceKernelIsNeoMode();
extern "C" uint64_t sceKernelGetCpuFrequency();
extern "C" bool ps4_page_reuse;
extern "C" bool ps4_regalloc_free_moves;
extern "C" bool ps4_regalloc_r13;
extern "C" void ps4_sampler_start();
extern "C" void ps4_sampler_enable_block_map();
extern "C" void ps4_sampler_stop_threads();
extern "C" size_t ps4_sampler_take_report(char* out, size_t capacity);
extern "C" int ps4_sampler_hot_guest_pcs(uint64_t* out, int max);
extern "C" int ps4_sampler_hot_guest_pages(uint64_t* out, int max);
extern "C" int ps4_sampler_busy_core();
extern "C" void ps4_dump_thread_stacks(const char* path, const char* reason);

namespace {
constexpr char Version[] = "1.0";

struct Game {
    std::string name;
    std::string path;
    std::optional<Settings::RendererBackend> renderer;
    uint64_t size{};
    int64_t mtime{};
    std::string source; // name of the source it was found in
};

// Where games are looked for: internal storage, USB drives (/mnt/usbN) and folders the user added.
struct SourceDef {
    std::string id, name, path;
    bool removable{};
};

bool IsDirectory(const std::string& path) {
    struct stat status{};
    return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

std::vector<SourceDef> SourceDefs(const std::vector<std::string>& custom) {
    std::vector<SourceDef> defs{{"internal", "Internal storage", "/data/EdenPS4/games", false}};
    // USB 1 is always listed (it shows "Not connected"); further drives when they are plugged in.
    for (int i = 0; i < 8; ++i) {
        const std::string path = "/mnt/usb" + std::to_string(i);
        if (i == 0 || IsDirectory(path)) defs.push_back({"usb" + std::to_string(i), "USB " + std::to_string(i + 1), path, false});
    }
    for (const auto& path : custom) {
        const auto slash = path.find_last_of('/');
        defs.push_back({path, slash == std::string::npos || slash + 1 >= path.size() ? path : path.substr(slash + 1), path, true});
    }
    return defs;
}

bool IsGameFile(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    auto extension = name.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".nro" || extension == ".nso" || extension == ".nsp" || extension == ".xci";
}

// Game files in `folder` and, while depth > 0, in its subfolders (e.g. a USB drive's switch folder).
void ScanFolder(const std::string& folder, int depth, const std::string& source, std::vector<Game>& out) {
    DIR* directory = opendir(folder.c_str());
    if (!directory) return;
    std::vector<std::string> subfolders;
    while (const auto* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.' || name[0] == '$' || name == "System Volume Information") continue;
        const std::string path = folder + "/" + name;
        struct stat status{};
        if (stat(path.c_str(), &status) != 0) continue;
        if (S_ISREG(status.st_mode) && IsGameFile(name)) {
            out.push_back({name, path, std::nullopt, uint64_t(status.st_size), int64_t(status.st_mtime), source});
        } else if (S_ISDIR(status.st_mode) && depth > 0) {
            subfolders.push_back(path);
        }
    }
    closedir(directory);
    for (const auto& path : subfolders) ScanFolder(path, depth - 1, source, out);
}

std::vector<Game> ScanGames(const std::vector<SourceDef>& sources, const std::set<std::string>& disabled,
                            std::map<std::string, int>& counts) {
    std::vector<Game> games;
    counts.clear();
    for (const auto& source : sources) {
        if (disabled.count(source.id) || !IsDirectory(source.path)) continue;
        const size_t before = games.size();
        ScanFolder(source.path, 1, source.name, games);
        counts[source.id] = int(games.size() - before);
    }
    // The same file reached through two sources (a folder inside another) appears once.
    std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    games.erase(std::unique(games.begin(), games.end(), [](const auto& a, const auto& b) { return a.path == b.path; }),
                games.end());
    std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    struct stat status{};
    if (stat("/app0/homebrew/cpu-smoke.nro", &status) == 0) {
        games.push_back({"CPU self-test (no GPU)", "/app0/homebrew/cpu-smoke.nro", Settings::RendererBackend::Null,
                         uint64_t(status.st_size), 0, "Eden4"});
        games.push_back({"CPU self-test + Vulkan startup", "/app0/homebrew/cpu-smoke.nro",
                         Settings::RendererBackend::Vulkan, uint64_t(status.st_size), 0, "Eden4"});
    }
    LOG_INFO(Frontend, "PS4 HOME menu: {} games from {} sources", games.size(), counts.size());
    return games;
}

std::string ExportLogs() {
    Common::Log::Stop();
    const char* names[] = {"eden.log", "eden.previous.log", "xmb-trace.log", "boot-trace.log",
                           "crash.log", "samples.log"};
    size_t copied{};
    for (const char* mount : {"/mnt/usb0", "/mnt/usb1"}) {
        for (const char* name : names) {
            const std::string source = std::string{"/data/EdenPS4/"} + name;
            FILE* input = fopen(source.c_str(), "rb");
            if (!input) continue;
            const std::string destination = std::string{mount} + "/EdenPS4-" + name;
            FILE* output = fopen(destination.c_str(), "wb");
            if (!output) { fclose(input); continue; }
            bool success = true;
            char buffer[16384];
            while (const size_t count = fread(buffer, 1, sizeof(buffer), input)) {
                if (fwrite(buffer, 1, count, output) != count) { success = false; break; }
            }
            success = success && !ferror(input);
            if (fflush(output) != 0 || fsync(fileno(output)) != 0) success = false;
            fclose(input);
            if (fclose(output) != 0) success = false;
            if (success) ++copied;
        }
    }
    return copied ? "Logs copied to USB. You can attach the EdenPS4-*.log files." :
                    "USB unavailable. Logs remain in /data/EdenPS4/";
}

// Every ps4.ini switch: read at startup, shown in System Settings (by category), saved back.
struct Option {
    const char* key;
    const char* label;
    const char* help;
    const char* category; // System Settings page
    std::vector<std::pair<const char*, const char*>> values; // ini value, shown text; first = default
    size_t current{};
};

#define CONTROLLER_TYPES {{"pro", "Pro Controller"}, {"dual", "Dual Joy-Cons"}, {"left", "Left Joy-Con"}, \
                          {"right", "Right Joy-Con"}}
std::vector<Option> g_options = {
    {"docked", "Console mode",
     "Docked: games render at up to 1080p. Handheld: 720p, less GPU work and memory (faster).", "display",
     {{"1", "Docked"}, {"0", "Handheld"}}},
    {"output", "Video output",
     "Resolution sent to the TV. 1080p uses ~13 MiB more GPU memory; pair it with Docked.", "display",
     {{"720", "1280x720"}, {"1080", "1920x1080"}}},
    {"player1_type", "Player 1 controller", "The Switch controller this PS4 controller acts as.", "controllers",
     CONTROLLER_TYPES},
    {"player2_type", "Player 2 controller", "The Switch controller this PS4 controller acts as.", "controllers",
     CONTROLLER_TYPES},
    {"player3_type", "Player 3 controller", "The Switch controller this PS4 controller acts as.", "controllers",
     CONTROLLER_TYPES},
    {"player4_type", "Player 4 controller", "The Switch controller this PS4 controller acts as.", "controllers",
     CONTROLLER_TYPES},
    {"ui_theme", "Theme", "", "theme", {{"black", "Basic Black"}, {"white", "Basic White"}}},
    {"ui_sounds", "Menu sounds", "Sounds when you move around the HOME menu. Games are not affected.", "sound",
     {{"on", "On"}, {"off", "Off"}}},
    {"profile", "Sampling profiler",
     "Writes where CPU time goes to eden.log every 10 s. Costs a little speed.", "debug",
     {{"off", "Off"}, {"on", "On"}}},
    {"renderer", "Renderer",
     "Null draws nothing (CPU-only test).", "debug",
     {{"vulkan", "Vulkan"}, {"null", "Null"}}},
    {"inline_memory", "JIT inline memory",
     "Diagnostics: Off sends every guest memory access through a slow callback.", "debug",
     {{"1", "On"}, {"0", "Off"}}},
    {"direct_table", "JIT direct page table",
     "Diagnostics: Off decodes packed page table entries instead.", "debug",
     {{"1", "On"}, {"0", "Off"}}},
    {"page_reuse", "JIT page reuse",
     "Diagnostics: Off looks up the page table for every access.", "debug",
     {{"1", "On"}, {"0", "Off"}}},
    {"regalloc", "JIT register tweaks",
     "Diagnostics: Off uses Dynarmic's original register allocation.", "debug",
     {{"on", "On"}, {"off", "Off"}}},
};
#undef CONTROLLER_TYPES

const char* OptionValue(std::string_view key) {
    for (const Option& option : g_options)
        if (key == option.key) return option.values[option.current].first;
    return "";
}

void LoadOptions() {
    FILE* file = fopen("/data/EdenPS4/ps4.ini", "r");
    if (!file) return;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        std::string_view text{line};
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
        const size_t equals = text.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = text.substr(0, equals), value = text.substr(equals + 1);
        if (key == "user") Settings::values.current_user = std::clamp(std::atoi(std::string{value}.c_str()), 0, 7);
        for (Option& option : g_options) {
            if (key != option.key) continue;
            for (size_t i = 0; i < option.values.size(); ++i)
                if (value == option.values[i].first) option.current = i;
        }
    }
    fclose(file);
}

bool SaveOptions() {
    FILE* file = fopen("/data/EdenPS4/ps4.ini", "w");
    if (!file) return false;
    for (const Option& option : g_options)
        fprintf(file, "%s=%s\n", option.key, option.values[option.current].first);
    fprintf(file, "user=%d\n", int(Settings::values.current_user.GetValue()));
    return fclose(file) == 0;
}

void ApplyOptions() {
    const auto enabled = [](std::string_view key) {
        const std::string_view value = OptionValue(key);
        return value == "1" || value == "on";
    };
    Settings::values.use_docked_mode.SetValue(enabled("docked") ? Settings::ConsoleMode::Docked
                                                                : Settings::ConsoleMode::Handheld);
    Common::PS4Diagnostics::output_1080 = std::string_view{OptionValue("output")} == "1080";
    Common::PS4Diagnostics::profiler = enabled("profile");
    Settings::values.renderer_backend.SetValue(std::string_view{OptionValue("renderer")} == "null"
                                                   ? Settings::RendererBackend::Null
                                                   : Settings::RendererBackend::Vulkan);
    Common::PS4Diagnostics::inline_memory = enabled("inline_memory");
    Common::PS4Diagnostics::direct_table = enabled("direct_table");
    ps4_page_reuse = enabled("page_reuse");
    ps4_regalloc_free_moves = ps4_regalloc_r13 = enabled("regalloc");
    auto& players = Settings::values.players.GetValue();
    for (size_t port = 0; port < OrbisFrontend::Controller::MaxPads; ++port) {
        const std::string_view type = OptionValue(fmt::format("player{}_type", port + 1));
        players[port].controller_type = type == "dual"   ? Settings::ControllerType::DualJoyconDetached
                                        : type == "left"  ? Settings::ControllerType::LeftJoycon
                                        : type == "right" ? Settings::ControllerType::RightJoycon
                                                          : Settings::ControllerType::ProController;
    }
}

std::string OptionsSummary() {
    std::string summary;
    for (const Option& option : g_options)
        summary += fmt::format("{}={} ", option.key, option.values[option.current].first);
    return summary;
}

void SetDefaults() {
    Settings::values.cpu_backend.SetValue(Settings::CpuBackend::Dynarmic);
    Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
    Settings::values.sink_id.SetValue(Settings::AudioEngine::Ps4);
    Settings::values.use_disk_shader_cache.SetValue(false);
    Settings::values.async_presentation.SetValue(false);
    Settings::values.log_flush_line.SetValue(true);
    Settings::values.log_filter.SetValue("*:Info");
    Settings::values.udp_input_servers.SetValue("");
    LoadOptions();
    // Player N reads virtual gamepad port N-1, fed by PS4 controller slot N (see UpdateInput).
    auto& players = Settings::values.players.GetValue();
    for (size_t port = 0; port < OrbisFrontend::Controller::MaxPads; ++port) {
        auto& player = players[port];
        player.connected = port == 0;
        player.vibration_enabled = false;
        for (size_t i = 0; i < player.buttons.size(); ++i) {
            Common::ParamPackage params;
            params.Set("engine", "virtual_gamepad");
            params.Set("guid", "00000000000000000000000000000000");
            params.Set("port", static_cast<int>(port));
            params.Set("pad", 0);
            params.Set("button", static_cast<int>(i));
            player.buttons[i] = params.Serialize();
        }
        for (size_t i = 0; i < player.analogs.size(); ++i) {
            Common::ParamPackage params;
            params.Set("engine", "virtual_gamepad");
            params.Set("guid", "00000000000000000000000000000000");
            params.Set("port", static_cast<int>(port));
            params.Set("pad", 0);
            params.Set("axis_x", static_cast<int>(i * 2));
            params.Set("axis_y", static_cast<int>(i * 2 + 1));
            params.Set("deadzone", 0.15f);
            player.analogs[i] = params.Serialize();
        }
    }
    for (size_t port = OrbisFrontend::Controller::MaxPads; port < players.size(); ++port)
        players[port].connected = false;
    ApplyOptions();
}

// Switch players 2-4 follow the PS4 controllers in slots 2-4. Player 1 stays connected even while its
// controller is off, so games do not stop for the controller applet. With a running system, players
// whose state changed are reconnected in the emulated HID.
void SyncPlayers(const OrbisFrontend::Controller& controller, Core::System* system) {
    auto& players = Settings::values.players.GetValue();
    for (size_t port = 1; port < OrbisFrontend::Controller::MaxPads; ++port) {
        const bool connected = controller.State(port).connected;
        if (players[port].connected == connected) continue;
        players[port].connected = connected;
        LOG_INFO(Input, "PS4 controller slot {}: Switch player {} {}", port + 1, port + 1,
                 connected ? "connected" : "disconnected");
        if (system)
            system->HIDCore().GetEmulatedControllerByIndex(port)->ReloadFromSettings();
    }
}

class Window final : public Core::Frontend::EmuWindow {
public:
    Window() {
        window_info.type = Core::Frontend::WindowSystemType::Orbis;
        window_info.render_surface = nullptr;
        const bool full_hd = Common::PS4Diagnostics::output_1080 || Common::PS4Diagnostics::videoout_needs_1080;
        NotifyClientAreaSizeChanged({full_hd ? 1920u : 1280u, full_hd ? 1080u : 720u});
        UpdateCurrentFramebufferLayout(full_hd ? 1920 : 1280, full_hd ? 1080 : 720);
    }
    ~Window() override = default;
    bool IsShown() const override { return true; }
    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<Core::Frontend::GraphicsContext>();
    }
};

void UpdateInput(InputCommon::InputSubsystem& input, const OrbisPadData& pad, size_t port) {
    using Button = InputCommon::VirtualGamepad::VirtualButton;
    const std::pair<uint32_t, Button> buttons[] = {
        {ORBIS_PAD_BUTTON_CROSS, Button::ButtonA}, {ORBIS_PAD_BUTTON_CIRCLE, Button::ButtonB},
        {ORBIS_PAD_BUTTON_TRIANGLE, Button::ButtonX}, {ORBIS_PAD_BUTTON_SQUARE, Button::ButtonY},
        {ORBIS_PAD_BUTTON_L1, Button::TriggerL}, {ORBIS_PAD_BUTTON_R1, Button::TriggerR},
        {ORBIS_PAD_BUTTON_L2, Button::TriggerZL}, {ORBIS_PAD_BUTTON_R2, Button::TriggerZR},
        {ORBIS_PAD_BUTTON_L3, Button::StickL}, {ORBIS_PAD_BUTTON_R3, Button::StickR},
        {ORBIS_PAD_BUTTON_OPTIONS, Button::ButtonPlus}, {ORBIS_PAD_BUTTON_TOUCH_PAD, Button::ButtonMinus},
        {ORBIS_PAD_BUTTON_UP, Button::ButtonUp}, {ORBIS_PAD_BUTTON_DOWN, Button::ButtonDown},
        {ORBIS_PAD_BUTTON_LEFT, Button::ButtonLeft}, {ORBIS_PAD_BUTTON_RIGHT, Button::ButtonRight},
    };
    auto* gamepad = input.GetVirtualGamepad();
    for (const auto& [mask, button] : buttons) gamepad->SetButtonState(port, button, pad.connected && (pad.buttons & mask));
    const auto axis = [](uint8_t value) { return std::clamp((int(value) - 128) / 127.0f, -1.0f, 1.0f); };
    gamepad->SetStickPosition(port, 0, pad.connected ? axis(pad.leftStick.x) : 0.0f,
                                       pad.connected ? -axis(pad.leftStick.y) : 0.0f);
    gamepad->SetStickPosition(port, 1, pad.connected ? axis(pad.rightStick.x) : 0.0f,
                                       pad.connected ? -axis(pad.rightStick.y) : 0.0f);
}

// Copies the profiler's finished windows into eden.log and dumps the guest code at its hottest blocks.
// The reads below use ReadBlockUnsafe: the checked ReadBlock flushes GPU-cached pages, which must not
// happen from this thread (00.11/00.12 froze a few seconds after the first frame).
// Registers, nearby memory and stack of the busiest emulated core, and the code pages around the hottest
// guest page (once each), logged as hex so a stuck guest loop can be analysed offline.
void DumpGuestState(Core::System& system) {
    static std::vector<u64> dumped_pages;
    Kernel::KProcess* process = system.ApplicationProcess();
    if (!process)
        return;
    auto& memory = system.ApplicationMemory();
    const auto hex = [](const u8* data, size_t size) {
        std::string text;
        for (size_t i = 0; i < size; ++i)
            text += fmt::format("{:02x}", data[i]);
        return text;
    };
    const int busy = ps4_sampler_busy_core();
    for (size_t core = 0; core < 4; ++core) {
        Core::ArmInterface* arm = process->GetArmInterface(core);
        if (!arm)
            continue;
        Kernel::Svc::ThreadContext ctx{};
        arm->GetContext(ctx);
        if (ctx.pc == 0)
            continue;
        std::string line = fmt::format("pc={:x} sp={:x} fp={:x} lr={:x} nzcv={:x} tpidr={:x}", ctx.pc, ctx.sp, ctx.fp,
                                       ctx.lr, ctx.pstate, ctx.tpidr);
        for (size_t i = 0; i < ctx.r.size(); ++i)
            line += fmt::format(" x{}={:x}", i, ctx.r[i]);
        LOG_INFO(Frontend, "PROFILE regs core {}: {}", core, line);
        if (int(core) != busy)
            continue;
        // 64 bytes behind each register that looks like a pointer, then the top of the stack.
        std::vector<u64> pointers(ctx.r.begin(), ctx.r.end());
        pointers.push_back(ctx.fp);
        pointers.push_back(ctx.lr);
        for (size_t i = 0; i < pointers.size(); ++i) {
            u8 buffer[96];
            if (pointers[i] >= 0x10000 && memory.IsValidVirtualAddressRange(pointers[i], sizeof(buffer)) &&
                memory.ReadBlockUnsafe(pointers[i], buffer, sizeof(buffer)))
                LOG_INFO(Frontend, "PROFILE mem core {} reg {} @0x{:x}: {}", core, i, pointers[i], hex(buffer, sizeof(buffer)));
        }
        u8 stack[512];
        if (memory.IsValidVirtualAddressRange(ctx.sp, sizeof(stack)) && memory.ReadBlockUnsafe(ctx.sp, stack, sizeof(stack)))
            LOG_INFO(Frontend, "PROFILE stack core {} @0x{:x}: {}", core, ctx.sp, hex(stack, sizeof(stack)));
    }
    u64 pages[16];
    const int count = ps4_sampler_hot_guest_pages(pages, 16);
    for (int i = 0; i < count; i++) {
        for (s64 delta = -2; delta <= 4; ++delta) {
            const u64 page = pages[i] + static_cast<u64>(delta) * 0x1000;
            if (std::find(dumped_pages.begin(), dumped_pages.end(), page) != dumped_pages.end())
                continue;
            u8 buffer[0x1000];
            if (!memory.IsValidVirtualAddressRange(page, sizeof(buffer)) || !memory.ReadBlockUnsafe(page, buffer, sizeof(buffer)))
                continue;
            dumped_pages.push_back(page);
            for (size_t offset = 0; offset < sizeof(buffer); offset += 128)
                LOG_INFO(Frontend, "PROFILE page 0x{:x}: {}", page + offset, hex(buffer + offset, 128));
        }
    }
}

void DrainProfiler(Core::System& system) {
    static char text[96 * 1024];
    static std::vector<u64> dumped;
    while (const size_t length = ps4_sampler_take_report(text, sizeof(text))) {
        std::string_view rest{text, length};
        while (!rest.empty()) {
            const size_t end = rest.find('\n');
            LOG_INFO(Frontend, "PROFILE {}", rest.substr(0, end));
            rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
        }
    }
    DumpGuestState(system);
    uint64_t pcs[16];
    const int count = ps4_sampler_hot_guest_pcs(pcs, 16);
    for (int i = 0; i < count; i++) {
        if (std::find(dumped.begin(), dumped.end(), pcs[i]) != dumped.end())
            continue;
        dumped.push_back(pcs[i]);
        u32 code[48];
        auto& memory = system.ApplicationMemory();
        if (!memory.IsValidVirtualAddressRange(pcs[i], sizeof(code)) || !memory.ReadBlockUnsafe(pcs[i], code, sizeof(code)))
            continue;
        std::string hex;
        for (const u32 word : code)
            hex += fmt::format("{:08x} ", word);
        LOG_INFO(Frontend, "PROFILE code 0x{:x}: {}", pcs[i], hex);
    }
}

void RunGame(const Game& game, OrbisFrontend::Controller& controller) {
    LOG_INFO(Frontend, "Launching {}", game.path);
    LOG_INFO(Frontend, "PS4 options: {}", OptionsSummary());
    Common::PS4Diagnostics::Reset();
    if (Common::PS4Diagnostics::profiler) {
        ps4_sampler_enable_block_map();
        ps4_sampler_start();
    }
    // 4 GiB of guest RAM leaves ~512 MiB of direct memory; keep it for the GPU, not the C heap.
    ps4_heap_set_overflow_gpu_reserve(256ull << 20);
    Common::PS4Diagnostics::LogDirectMemory("before guest RAM");
    const auto previous_renderer = Settings::values.renderer_backend.GetValue();
    Settings::values.renderer_backend.SetValue(game.renderer.value_or(previous_renderer));
    SCOPE_EXIT { Settings::values.renderer_backend.SetValue(previous_renderer); };
    InputCommon::InputSubsystem input;
    input.Initialize();
    SCOPE_EXIT { input.Shutdown(); };
    controller.Read();
    void(controller.TakeChanged());
    SyncPlayers(controller, nullptr);
    Window window;
    Core::System system;
    system.Initialize();
    system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
    system.ApplySettings();
    Service::AM::FrontendAppletParameters parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    const auto result = system.Load(window, game.path, parameters);
    if (result != Core::SystemResultStatus::Success) {
        LOG_ERROR(Frontend, "Eden load failed: status={:#x}, path={}", u32(result), game.path);
        throw std::runtime_error{"Could not load this file. See /data/EdenPS4/eden.log"};
    }
    // The HOME menu's title and icon for this game, from the loader Eden has now anyway.
    if (!game.renderer) {
        std::string title;
        u64 program_id{};
        std::vector<u8> icon;
        auto& loader = system.GetAppLoader();
        void(loader.ReadTitle(title));
        void(loader.ReadProgramId(program_id));
        void(loader.ReadIcon(icon));
        OrbisFrontend::Metadata::Store({game.path, game.size, game.mtime}, title, program_id, icon);
        LOG_INFO(Frontend, "PS4 HOME menu: saved \"{}\" ({:016X}, icon {} bytes)", title, program_id, icon.size());
    }
    std::atomic<bool> running{true};
    system.RegisterExitCallback([&] { running = false; });
    system.GPU().Start();
    system.GetCpuManager().OnGpuReady();
    void(system.Run());
    LOG_INFO(Frontend, "Emulation running; L3 + R3 + Options requests shutdown");
    Common::PS4Diagnostics::LogDirectMemory("after renderer startup");
    auto next_status = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    constexpr uint32_t exit_buttons = ORBIS_PAD_BUTTON_L3 | ORBIS_PAD_BUTTON_R3 | ORBIS_PAD_BUTTON_OPTIONS;
    // Hang watchdog: if this loop stops, or frames stop after they had started, write every named thread's
    // stack once to /data/EdenPS4/hang.log (no guest memory is touched, so it works while emulation is stuck).
    std::atomic<u64> frontend_ticks{0};
    std::atomic<bool> watchdog_stop{false};
    std::thread watchdog([&] {
        u64 last_ticks = 0, last_composites = 0;
        int frontend_stale = 0, frames_stale = 0;
        bool dumped = false;
        while (!watchdog_stop) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
            const u64 ticks = frontend_ticks.load();
            const u64 composites = Common::PS4Diagnostics::composites.load();
            frontend_stale = ticks == last_ticks ? frontend_stale + 1 : 0;
            frames_stale = composites > 0 && composites == last_composites ? frames_stale + 1 : 0;
            last_ticks = ticks;
            last_composites = composites;
            if (!dumped && (frontend_stale >= 15 || frames_stale >= 20)) {
                dumped = true;
                ps4_dump_thread_stacks("/data/EdenPS4/hang.log",
                                       frontend_stale >= 15 ? "frontend loop stopped for 15 s" : "no new frame for 20 s");
            }
        }
    });
    SCOPE_EXIT {
        watchdog_stop = true;
        watchdog.join();
    };
    while (running) {
        frontend_ticks.fetch_add(1, std::memory_order_relaxed);
        controller.Read();
        bool exit_requested = false;
        for (size_t port = 0; port < OrbisFrontend::Controller::MaxPads; ++port) {
            const OrbisPadData& pad = controller.State(port);
            UpdateInput(input, pad, port);
            exit_requested |= pad.connected && (pad.buttons & exit_buttons) == exit_buttons;
        }
        input.PumpEvents();
        if (controller.TakeChanged())
            SyncPlayers(controller, &system);
        if (exit_requested) {
            LOG_INFO(Input, "PS4 exit shortcut received");
            break;
        }
        if (std::chrono::steady_clock::now() >= next_status) {
            LOG_INFO(Frontend,
                     "PS4 progress: composites={}, flips={}, audio buffers={}, pad={}, stage={}, "
                     "overflow heap={} MiB, memory callbacks={}",
                     Common::PS4Diagnostics::composites.load(), Common::PS4Diagnostics::flips.load(),
                     Common::PS4Diagnostics::audio_buffers.load(), bool(controller.State().connected),
                     Common::PS4Diagnostics::stage.load(), ps4_heap_overflow_size() >> 20,
                     Common::PS4Diagnostics::MemoryCallbackTotal());
            Common::PS4Diagnostics::LogDirectMemory("heartbeat");
            const auto perf = system.GetAndResetPerfStats();
            LOG_INFO(Frontend, "PS4 performance: game {:.1f} fps, emulation speed {:.0f}%, frame time {:.1f} ms",
                     perf.average_game_fps, perf.emulation_speed * 100.0, perf.frametime * 1000.0);
            if (Common::PS4Diagnostics::profiler)
                DrainProfiler(system);
            next_status = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        }
        usleep(8000);
    }
    LOG_INFO(Frontend, "Stopping emulation");
    if (Common::PS4Diagnostics::profiler) {
        ps4_sampler_stop_threads();
        DrainProfiler(system);
    }
    void(system.Pause());
    LOG_INFO(Frontend, "Emulation paused; shutting down process");
    system.ShutdownMainProcess();
    LOG_INFO(Frontend, "Emulation shutdown complete");
    Common::Log::Stop();
}

// ---- HOME menu ---------------------------------------------------------------------------------

constexpr char UiStatePath[] = "/data/EdenPS4/ui-state.ini";
constexpr char AvatarDir[] = "/data/EdenPS4/nand/system/save/8000000000000010/su/avators/";

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::vector<uint8_t> data;
    if (FILE* file = fopen(path.c_str(), "rb")) {
        uint8_t buffer[16384];
        while (const size_t count = fread(buffer, 1, sizeof(buffer), file)) data.insert(data.end(), buffer, buffer + count);
        fclose(file);
    }
    return data;
}

uint32_t ToUiButtons(const OrbisPadData& pad) {
    if (!pad.connected) return 0;
    static const std::pair<uint32_t, uint32_t> map[] = {
        {ORBIS_PAD_BUTTON_UP, Ui::Up}, {ORBIS_PAD_BUTTON_DOWN, Ui::Down}, {ORBIS_PAD_BUTTON_LEFT, Ui::Left},
        {ORBIS_PAD_BUTTON_RIGHT, Ui::Right}, {ORBIS_PAD_BUTTON_CROSS, Ui::Cross}, {ORBIS_PAD_BUTTON_CIRCLE, Ui::Circle},
        {ORBIS_PAD_BUTTON_SQUARE, Ui::Square}, {ORBIS_PAD_BUTTON_TRIANGLE, Ui::Triangle},
        {ORBIS_PAD_BUTTON_OPTIONS, Ui::Options}, {ORBIS_PAD_BUTTON_L1, Ui::L1}, {ORBIS_PAD_BUTTON_R1, Ui::R1},
        {ORBIS_PAD_BUTTON_L2, Ui::L2}, {ORBIS_PAD_BUTTON_R2, Ui::R2}, {ORBIS_PAD_BUTTON_L3, Ui::L3},
        {ORBIS_PAD_BUTTON_R3, Ui::R3}, {ORBIS_PAD_BUTTON_TOUCH_PAD, Ui::Touchpad}};
    uint32_t bits = 0;
    for (const auto& [ps4, ui] : map)
        if (pad.buttons & ps4) bits |= ui;
    return bits;
}

float StickAxis(uint8_t value) {
    return std::clamp((int(value) - 128) / 127.0f, -1.0f, 1.0f);
}

// The HOME menu's view of the PS4 side: games, users, controllers and ps4.ini.
class PsHost final : public Ui::Host {
public:
    explicit PsHost(OrbisFrontend::Controller& controller_) : controller{controller_} {
        LoadState();
        BuildOptions();
    }

    /// Before showing the menu: scan games, read users, start sounds and the metadata reader.
    void Begin() {
        RefreshGames();
        LOG_INFO(Frontend, "PS4 HOME menu: users");
        RefreshUsers();
        LOG_INFO(Frontend, "PS4 HOME menu: sounds");
        sound.SetEnabled(std::string_view{OptionValue("ui_sounds")} != "off");
        sound.Start();
    }

    /// Before a game starts: stop every thread and give back the pictures' memory.
    void End() {
        metadata.Stop();
        sound.Stop();
        for (auto& game : games) {
            game.home_icon.reset();
            game.library_icon.reset();
        }
        users.clear();
        profiles.reset();
    }

    void Poll() {
        for (auto& result : metadata.Take()) {
            if (result.index >= games.size()) continue;
            auto& game = games[result.index];
            if (!result.title.empty()) game.title = result.title;
            game.program_id = result.program_id;
            game.home_icon = std::move(result.home);
            game.library_icon = std::move(result.library);
        }
    }

    const Game& Internal(int index) const { return internal[index]; }

    void Played(int index) {
        const std::string file = internal[index].name;
        recent.erase(std::remove(recent.begin(), recent.end(), file), recent.end());
        recent.insert(recent.begin(), file);
        if (recent.size() > 32) recent.resize(32);
        SaveState();
    }

    const std::vector<Ui::Game>& Games() override { return games; }
    bool Scanning() override { return metadata.Busy(); }
    void Rescan() override { RefreshGames(); }

    std::vector<Ui::User> Users() override { return users; }
    int CurrentUser() override { return int(Settings::values.current_user.GetValue()); }
    void SetCurrentUser(int index) override {
        if (!profiles || !profiles->UserExistsIndex(index)) return;
        if (const auto old = profiles->GetUser(CurrentUser())) profiles->CloseUser(*old);
        Settings::values.current_user = index;
        if (const auto uuid = profiles->GetUser(index)) profiles->OpenUser(*uuid);
        SaveOptions();
    }
    bool AddUser(const std::string& name) override {
        if (!profiles || profiles->GetUserCount() >= Service::Account::MAX_USERS) return false;
        if (profiles->CreateNewUser(Common::UUID::MakeRandom(), name).IsError()) return false;
        profiles->WriteUserSaveFile();
        RefreshUsers();
        return true;
    }
    bool RenameUser(int index, const std::string& name) override {
        const auto uuid = profiles ? profiles->GetUser(index) : std::nullopt;
        Service::Account::ProfileBase base{};
        if (!uuid || !profiles->GetProfileBase(*uuid, base)) return false;
        base.username.fill(0);
        std::copy_n(name.begin(), std::min(name.size(), base.username.size()), base.username.begin());
        if (!profiles->SetProfileBase(*uuid, base)) return false;
        profiles->WriteUserSaveFile();
        RefreshUsers();
        return true;
    }
    bool RemoveUser(int index) override {
        const auto uuid = profiles ? profiles->GetUser(index) : std::nullopt;
        if (!uuid || profiles->GetUserCount() <= 1) return false;
        profiles->CloseUser(*uuid);
        if (!profiles->RemoveUser(*uuid)) return false;
        profiles->WriteUserSaveFile();
        if (CurrentUser() >= int(profiles->GetUserCount())) Settings::values.current_user = 0;
        if (const auto current = profiles->GetUser(CurrentUser())) profiles->OpenUser(*current);
        SaveOptions();
        RefreshUsers();
        return true;
    }
    void SetUserLook(int index, int color, int symbol) override {
        const auto uuid = profiles ? profiles->GetUser(index) : std::nullopt;
        if (!uuid) return;
        looks[uuid->FormattedString()] = {color, symbol};
        SaveState();
        RefreshUsers();
    }

    std::array<Ui::Pad, 4> Pads() override {
        std::array<Ui::Pad, 4> pads{};
        for (size_t i = 0; i < pads.size(); ++i) {
            const OrbisPadData& state = controller.State(i);
            pads[i].connected = state.connected;
            pads[i].user = controller.UserName(i);
            pads[i].buttons = ToUiButtons(state);
            pads[i].lx = state.connected ? StickAxis(state.leftStick.x) : 0;
            pads[i].ly = state.connected ? StickAxis(state.leftStick.y) : 0;
            pads[i].rx = state.connected ? StickAxis(state.rightStick.x) : 0;
            pads[i].ry = state.connected ? StickAxis(state.rightStick.y) : 0;
        }
        return pads;
    }
    void SwapPads(int a, int b) override { controller.Swap(size_t(a), size_t(b)); }

    std::vector<Ui::Option>& Options() override { return options; }
    void SetOption(int index, int value) override {
        Option& option = g_options[option_index[index]];
        option.current = size_t(std::clamp(value, 0, int(option.values.size()) - 1));
        ApplyOptions();
        SaveOptions();
        sound.SetEnabled(std::string_view{OptionValue("ui_sounds")} != "off");
        BuildOptions();
    }

    std::string Clock() override {
        // libc's time (as DolphinPS4's menus): 00.25-00.27 ended on the first frame, where libSceRtc was first called.
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        return fmt::format("{}:{:02}", local.tm_hour, local.tm_min);
    }

    std::vector<std::pair<std::string, std::string>> SystemInfo() override {
        std::string user = "-";
        if (CurrentUser() < int(users.size())) user = users[CurrentUser()].name;
        return {{"Version", Version},
                {"Console mode", sceKernelIsNeoMode() ? "PS4 Pro mode" : "PS4 (base mode)"},
                {"CPU", fmt::format("{} cores, {} MHz", sysconf(_SC_NPROCESSORS_ONLN), sceKernelGetCpuFrequency() / 1000000)},
                {"Memory for games", fmt::format("{} MiB direct memory", sceKernelGetDirectMemorySize() >> 20)},
                {"Current user", user},
                {"Data folder", "/data/EdenPS4"}};
    }

    std::vector<std::pair<std::string, std::string>> FilesInfo() override {
        struct stat status{};
        const bool keys = stat("/data/EdenPS4/keys/prod.keys", &status) == 0;
        bool firmware = false;
        if (DIR* directory = opendir("/data/EdenPS4/nand/system/Contents/registered")) {
            while (const auto* entry = readdir(directory))
                if (entry->d_name[0] != '.') { firmware = true; break; }
            closedir(directory);
        }
        std::vector<std::pair<std::string, std::string>> info{
            {"Keys", keys ? "Ready" : "Missing"},
            {"Firmware", firmware ? "Installed" : "Missing"},
            {"Software", std::to_string(games.size())}};
        return info;
    }
    std::vector<Ui::Source> Sources() override {
        std::vector<Ui::Source> list;
        for (const auto& def : SourceDefs(custom_sources)) {
            const auto count = source_counts.find(def.id);
            list.push_back({def.name, def.path, !disabled_sources.count(def.id), IsDirectory(def.path), def.removable,
                            count == source_counts.end() ? 0 : count->second});
        }
        return list;
    }
    void SetSourceEnabled(int index, bool enabled) override {
        const auto defs = SourceDefs(custom_sources);
        if (index < 0 || index >= int(defs.size())) return;
        if (enabled) disabled_sources.erase(defs[index].id);
        else disabled_sources.insert(defs[index].id);
        SaveState();
        RefreshGames();
    }
    bool AddSource(const std::string& path) override {
        std::string folder = path;
        while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
        if (!IsDirectory(folder)) return false;
        if (std::find(custom_sources.begin(), custom_sources.end(), folder) == custom_sources.end())
            custom_sources.push_back(folder);
        SaveState();
        RefreshGames();
        return true;
    }
    bool RemoveSource(int index) override {
        const auto defs = SourceDefs(custom_sources);
        if (index < 0 || index >= int(defs.size()) || !defs[index].removable) return false;
        custom_sources.erase(std::remove(custom_sources.begin(), custom_sources.end(), defs[index].path),
                             custom_sources.end());
        disabled_sources.erase(defs[index].id);
        SaveState();
        RefreshGames();
        return true;
    }
    std::string ExportLogs() override { return ::ExportLogs(); }
    void Play(Ui::Sound which) override { sound.Play(which); }

private:
    void BuildOptions() {
        options.clear();
        option_index.clear();
        for (size_t i = 0; i < g_options.size(); ++i) {
            const Option& option = g_options[i];
            Ui::Option view{option.key, option.label, option.help, option.category, {}, int(option.current)};
            for (const auto& value : option.values) view.values.push_back(value.second);
            options.push_back(std::move(view));
            option_index.push_back(i);
        }
    }

    void RefreshGames() {
        metadata.Stop();
        auto found = ScanGames(SourceDefs(custom_sources), disabled_sources, source_counts);
        // Recently played first (newest first), then the rest by file name.
        std::stable_sort(found.begin(), found.end(), [&](const Game& a, const Game& b) {
            const auto rank = [&](const Game& g) {
                const auto it = std::find(recent.begin(), recent.end(), g.name);
                return it == recent.end() ? recent.size() : size_t(it - recent.begin());
            };
            return rank(a) < rank(b);
        });
        internal = std::move(found);
        games.clear();
        std::vector<OrbisFrontend::Metadata::Entry> entries;
        for (const Game& game : internal) {
            Ui::Game view;
            view.file = game.name;
            view.path = game.path;
            view.size = game.size;
            view.source = game.source;
            const auto dot = game.name.find_last_of('.');
            view.title = game.renderer ? game.name : game.name.substr(0, dot);
            games.push_back(std::move(view));
            // The self-tests have no metadata worth reading (an empty path is skipped).
            entries.push_back({game.renderer ? std::string{} : game.path, game.size, game.mtime});
        }
        metadata.Start(std::move(entries));
    }

    void RefreshUsers() {
        profiles = std::make_unique<Service::Account::ProfileManager>();
        users.clear();
        for (size_t i = 0; i < profiles->GetUserCount(); ++i) {
            const auto uuid = profiles->GetUser(i);
            Service::Account::ProfileBase base{};
            if (!uuid || !profiles->GetProfileBase(*uuid, base)) continue;
            Ui::User user;
            user.name.assign(reinterpret_cast<const char*>(base.username.data()),
                             strnlen(reinterpret_cast<const char*>(base.username.data()), base.username.size()));
            const auto look = looks.find(uuid->FormattedString());
            user.color = look != looks.end() ? look->second.first : int(i * 5 % 12);
            user.symbol = look != looks.end() ? look->second.second : 0;
            const auto photo = ReadFile(AvatarDir + uuid->FormattedString() + ".jpg");
            if (!photo.empty()) {
                auto image = std::make_shared<Ui::Image>(Ui::DecodeImage(photo, 256, 256));
                if (!image->Empty()) user.photo = std::move(image);
            }
            users.push_back(std::move(user));
        }
    }

    void LoadState() {
        FILE* file = fopen(UiStatePath, "r");
        if (!file) return;
        char line[512];
        while (fgets(line, sizeof(line), file)) {
            std::string text{line};
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
            const size_t equals = text.find('=');
            if (equals == std::string::npos) continue;
            const std::string key = text.substr(0, equals), value = text.substr(equals + 1);
            if (key == "recent") {
                recent.push_back(value);
            } else if (key == "source") {
                custom_sources.push_back(value);
            } else if (key == "source_off") {
                disabled_sources.insert(value);
            } else if (key.rfind("look.", 0) == 0) {
                int color = 0, symbol = 0;
                if (sscanf(value.c_str(), "%d,%d", &color, &symbol) == 2) looks[key.substr(5)] = {color, symbol};
            }
        }
        fclose(file);
    }

    void SaveState() {
        FILE* file = fopen(UiStatePath, "w");
        if (!file) return;
        for (const auto& file_name : recent) fprintf(file, "recent=%s\n", file_name.c_str());
        for (const auto& folder : custom_sources) fprintf(file, "source=%s\n", folder.c_str());
        for (const auto& id : disabled_sources) fprintf(file, "source_off=%s\n", id.c_str());
        for (const auto& [uuid, look] : looks) fprintf(file, "look.%s=%d,%d\n", uuid.c_str(), look.first, look.second);
        fclose(file);
    }

    OrbisFrontend::Controller& controller;
    std::vector<Game> internal;
    std::vector<Ui::Game> games;
    std::vector<std::string> recent;
    std::vector<std::string> custom_sources;
    std::set<std::string> disabled_sources;
    std::map<std::string, int> source_counts;
    std::map<std::string, std::pair<int, int>> looks;
    std::unique_ptr<Service::Account::ProfileManager> profiles;
    std::vector<Ui::User> users;
    std::vector<Ui::Option> options;
    std::vector<size_t> option_index;
    OrbisFrontend::UiSound sound;
    OrbisFrontend::Metadata metadata;
};

// Turns controller state into menu input: new presses, plus auto-repeat for held directions and the
// left stick.
class MenuInput {
public:
    void Update(const OrbisPadData& pad, float dt, uint32_t& pressed, uint32_t& held) {
        held = ToUiButtons(pad);
        if (pad.connected) {
            const float x = StickAxis(pad.leftStick.x), y = StickAxis(pad.leftStick.y);
            if (x < -0.6f) held |= Ui::Left;
            if (x > 0.6f) held |= Ui::Right;
            if (y < -0.6f) held |= Ui::Up;
            if (y > 0.6f) held |= Ui::Down;
        }
        pressed = held & ~previous;
        constexpr uint32_t directions = Ui::Up | Ui::Down | Ui::Left | Ui::Right;
        if ((held & directions) && (held & directions) == (previous & directions)) {
            repeat += dt;
            if (repeat > 0.38f) {
                pressed |= held & directions;
                repeat -= 0.075f;
            }
        } else {
            repeat = 0;
        }
        previous = held;
    }

private:
    uint32_t previous{};
    float repeat{};
};

void Launcher(OrbisFrontend::Controller& controller) {
    PsHost host{controller};
    Ui::Launcher ui{host};
    MenuInput input;
    std::string message;
    while (true) {
        auto display = std::make_unique<OrbisFrontend::Display>();
        if (const auto mode = OrbisFrontend::WorkingDisplayMode())
            Common::PS4Diagnostics::videoout_argb = mode->format == 0x80000000;
        // PS4 Pro mode refused 1280x720 VideoOut buffers (00.21); base mode takes them.
        Common::PS4Diagnostics::videoout_needs_1080 = sceKernelIsNeoMode() != 0;
        auto canvas = std::make_unique<Ui::Canvas>();
        host.Begin();
        LOG_INFO(Frontend, "PS4 HOME menu: {} games, {} users", host.Games().size(), host.Users().size());
        auto last = std::chrono::steady_clock::now();
        int launch = -1;
        u64 frames = 0;
        while (true) {
            controller.Read();
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.1f);
            last = now;
            uint32_t pressed = 0, held = 0;
            input.Update(controller.State(), dt, pressed, held);
            host.Poll();
            if (frames == 0) LOG_INFO(Frontend, "PS4 HOME menu: first update");
            const auto request = ui.Update(dt, pressed, held);
            if (request.kind == Ui::Request::Exit) {
                host.End();
                return;
            }
            if (request.kind == Ui::Request::Launch) {
                launch = request.game;
                break;
            }
            if (frames == 0) LOG_INFO(Frontend, "PS4 HOME menu: first render");
            ui.Render(*canvas);
            if (frames == 0) LOG_INFO(Frontend, "PS4 HOME menu: first present");
            display->Present(canvas->Pixels());
            if (++frames == 1) LOG_INFO(Frontend, "PS4 HOME menu: first frame shown");
        }
        ui.RenderLoading(*canvas, launch);
        display->Present(canvas->Pixels());
        // The game's display keeps showing this while the game boots.
        delete Common::PS4Diagnostics::loading_picture.exchange(
            new std::vector<u32>(canvas->Pixels(), canvas->Pixels() + size_t(Ui::Canvas::Width) * Ui::Canvas::Height));
        const Game game = host.Internal(launch);
        host.Played(launch);
        host.End();
        canvas.reset();
        display.reset();
        try {
            RunGame(game, controller);
            message = "";
        } catch (const std::exception& error) {
            LOG_ERROR(Frontend, "Launch failure: {}", error.what());
            message = error.what();
            Common::Log::Stop();
        }
        ui.Returned(message);
    }
}
}

int main() {
    mkdir("/data/EdenPS4", 0777);
    mkdir("/data/EdenPS4/games", 0777);
    SetDefaults();
    Common::Log::Initialize();
    Common::Log::Start();
    LOG_INFO(Frontend, "Eden4 {} starting", Version);
    LOG_INFO(Frontend, "Host CPU: {}, AVX={}, AVX2={}, page size={}",
             Common::g_cpu_caps.cpu_string, Common::g_cpu_caps.avx,
             Common::g_cpu_caps.avx2, sysconf(_SC_PAGESIZE));
    LOG_INFO(Frontend, "PS4 Pro mode: {}, CPUs: {}, CPU clock: {} MHz, direct memory: {} MiB",
             sceKernelIsNeoMode() ? "on" : "off", sysconf(_SC_NPROCESSORS_ONLN),
             sceKernelGetCpuFrequency() / 1000000, sceKernelGetDirectMemorySize() >> 20);
    try {
        Common::FS::CreateEdenPaths();
        OrbisFrontend::InitializeModules();
        OrbisFrontend::Controller controller;
        Launcher(controller);
    } catch (const std::exception& error) {
        LOG_CRITICAL(Frontend, "PS4 frontend failure: {}", error.what());
    }
    Common::Log::Stop();
    sceSystemServiceLoadExec("exit", nullptr);
    return 0;
}

#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"
