// SPDX-License-Identifier: GPL-3.0-or-later
// Renders the launcher's screens on a PC for checking the layout: ./preview <out-dir>
// writes <screen>.rgba files (1920x1080 RGBA). Games and pictures are generated placeholders.

#include <cmath>
#include <cstdio>
#include <string>
#include "yuzu_ps4/ui_launcher.h"

using namespace Ui;

namespace {
std::shared_ptr<const Image> Gradient(int size, uint32_t a, uint32_t b, int seed) {
    Image image{size, size, std::vector<uint32_t>(size_t(size) * size)};
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float t = (x + y) / float(2 * size);
            const float wave = 0.5f + 0.5f * std::sin((x * 0.03f + seed) + std::cos(y * 0.02f + seed));
            const auto mix = [&](int shift) {
                const float ca = (a >> shift) & 255, cb = (b >> shift) & 255;
                return uint32_t(std::clamp((ca + (cb - ca) * t) * (0.75f + 0.25f * wave), 0.0f, 255.0f));
            };
            image.pixels[size_t(y) * size + x] = 0xff000000u | (mix(0) << 16) | (mix(8) << 8) | mix(16);
        }
    return std::make_shared<Image>(std::move(image));
}

class FakeHost : public Host {
public:
    FakeHost() {
        const char* titles[] = {"Clubhouse Games: 51 Worldwide Classics", "Undertale", "Puzzle Quest", "Space Racer",
                                "Garden Story", "River City", "Tiny Tower", "Ocean Explorer", "Pixel Kart",
                                "Night Forest", "Card Duel", "Sky Islands", "Retro Arcade Collection", "Mountain Hike"};
        const uint32_t colors[][2] = {{0x2e7d32, 0xffd54f}, {0x1a1a1a, 0xd32f2f}, {0x6a1b9a, 0x4fc3f7},
                                      {0x0d47a1, 0xff7043}, {0x33691e, 0xfff176}, {0xbf360c, 0x5d4037}};
        for (int i = 0; i < 14; ++i) {
            Game game;
            game.title = titles[i];
            game.file = std::string{titles[i]} + ".nsp";
            game.size = (1ull + i % 5) << 30;
            game.source = i < 9 ? "Internal storage" : "USB 1";
            game.program_id = 0x0100000000010000ull + i * 0x1000;
            if (i != 6) { // one without an icon, like homebrew
                game.home_icon = Gradient(384, colors[i % 6][0], colors[i % 6][1], i);
                game.library_icon = Gradient(261, colors[i % 6][0], colors[i % 6][1], i);
            }
            games.push_back(game);
        }
        users = {{"Chary", 6, 1, nullptr}, {"Guest", 0, 2, nullptr}};
        options = {
            {"docked", "Console mode", "Docked: games render at up to 1080p. Handheld: 720p, faster.", "display",
             {"Docked", "Handheld"}, 0},
            {"output", "Video output", "Resolution sent to the TV.", "display", {"1280x720", "1920x1080"}, 1},
            {"player1_type", "Player 1 controller", "", "controllers", {"Pro Controller", "Dual Joy-Cons", "Handheld"}, 0},
            {"player2_type", "Player 2 controller", "", "controllers", {"Pro Controller", "Dual Joy-Cons"}, 0},
            {"ui_theme", "Theme", "", "theme", {"Basic Black", "Basic White"}, 0},
            {"ui_sounds", "Menu sounds", "Sounds when you move around the HOME menu.", "sound", {"On", "Off"}, 0},
            {"profile", "Sampling profiler", "Writes where CPU time goes to eden.log.", "debug", {"Off", "On"}, 0},
            {"renderer", "Renderer", "Null draws nothing.", "debug", {"Vulkan", "Null"}, 0},
        };
    }
    const std::vector<Game>& Games() override { return games; }
    bool Scanning() override { return false; }
    void Rescan() override {}
    std::vector<User> Users() override { return users; }
    int CurrentUser() override { return 0; }
    void SetCurrentUser(int) override {}
    bool AddUser(const std::string& name) override { users.push_back({name, 3, 4, nullptr}); return true; }
    bool RenameUser(int i, const std::string& name) override { users[i].name = name; return true; }
    bool RemoveUser(int) override { return false; }
    void SetUserLook(int i, int color, int symbol) override { users[i].color = color; users[i].symbol = symbol; }
    std::array<Pad, 4> Pads() override {
        std::array<Pad, 4> pads{};
        pads[0] = {true, "Chary", 0, Cross | L1, 0.4f, -0.2f, 0, 0};
        pads[1] = {true, "Guest", 0, 0, 0, 0, -0.6f, 0.5f};
        return pads;
    }
    void SwapPads(int, int) override {}
    std::vector<Option>& Options() override { return options; }
    void SetOption(int index, int value) override { options[index].current = value; }
    std::string Clock() override { return "23:18"; }
    std::vector<std::pair<std::string, std::string>> SystemInfo() override {
        return {{"Version", "00.25"}, {"Console", "PS4 Pro (base mode)"}, {"CPU", "8 cores, 1600 MHz"},
                {"Memory for games", "4608 MiB direct"}, {"Data folder", "/data/EdenPS4"}};
    }
    std::vector<std::pair<std::string, std::string>> FilesInfo() override {
        return {{"Keys", "Ready"}, {"Firmware", "Installed"}, {"Software", "14"}, {"Free space", "312 GB"}};
    }
    std::vector<Source> Sources() override { return sources; }
    void SetSourceEnabled(int i, bool enabled) override { sources[i].enabled = enabled; }
    bool AddSource(const std::string& path) override { sources.push_back({path, path, true, true, true, 0}); return true; }
    bool RemoveSource(int i) override {
        if (!sources[i].removable) return false;
        sources.erase(sources.begin() + i);
        return true;
    }
    std::string ExportLogs() override { return "Logs copied to USB."; }
    void Play(Sound) override {}

    std::vector<Game> games;
    std::vector<User> users;
    std::vector<Option> options;
    std::vector<Source> sources{{"Internal storage", "/data/EdenPS4/games", true, true, false, 9},
                                {"USB 1", "/mnt/usb0", true, true, false, 5},
                                {"USB 2", "/mnt/usb1", true, false, false, 0},
                                {"Homebrew", "/data/homebrew", false, true, true, 0}};
};

void Dump(Canvas& canvas, const std::string& path) {
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) return;
    fwrite(canvas.Pixels(), 4, size_t(Canvas::Width) * Canvas::Height, file);
    fclose(file);
}

// Feeds a key sequence, then lets animations settle.
void Press(Launcher& launcher, std::initializer_list<uint32_t> keys) {
    for (const uint32_t key : keys) {
        launcher.Update(0.016f, key, key);
        launcher.Update(0.016f, 0, 0);
    }
    for (int i = 0; i < 60; ++i) launcher.Update(0.016f, 0, 0);
}
} // namespace

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    Canvas canvas;
    const auto shot = [&](const char* name, std::initializer_list<uint32_t> keys, bool white = false) {
        FakeHost host;
        if (white) host.options[4].current = 1;
        Launcher launcher{host};
        Press(launcher, keys);
        launcher.Render(canvas);
        Dump(canvas, out + "/" + name + ".rgba");
    };
    shot("home", {Right});
    shot("home_white", {Right}, true);
    shot("home_shortcuts", {Down, Right, Right, Right});
    shot("home_users", {Up});
    shot("home_scrolled", {Right, Right, Right, Right, Right, Right});
    shot("library", {Right, Right, Right, Right, Right, Right, Right, Right, Right, Right, Right, Right, Cross, Right});
    shot("settings", {Down, Right, Right, Right, Right, Cross, Down, Right});
    shot("settings_users", {Down, Right, Right, Right, Right, Cross, Right});
    shot("settings_system", {Down, Right, Right, Right, Right, Cross, Up});
    shot("choice", {Down, Right, Right, Right, Right, Cross, Down, Down, Right, Cross});
    shot("controllers", {Down, Right, Right, Right, Cross, Down});
    shot("controller_test", {Down, Right, Right, Right, Cross, Down, Cross});
    shot("order", {Down, Right, Right, Right, Cross, Cross, Right, Cross});
    shot("users", {Down, Right, Right, Cross});
    shot("avatar", {Down, Right, Right, Cross, Options, Right, Down});
    shot("keyboard", {Down, Right, Right, Cross, Triangle, Down, Right, Right});
    shot("about", {Down, Cross});
    shot("files", {Down, Right, Cross, Down});
    shot("files_add", {Down, Right, Cross, Down, Down, Down, Down, Cross});
    shot("power", {Down, Right, Right, Right, Right, Right, Cross, Right});
    shot("game_info", {Triangle});
    {
        FakeHost host;
        Launcher launcher{host};
        launcher.RenderLoading(canvas, 0);
        Dump(canvas, out + "/loading.rgba");
    }
    // Edge cases: nothing installed, no users, and a long random walk through every screen.
    for (int variant = 0; variant < 3; ++variant) {
        FakeHost host;
        if (variant == 0) host.games.clear();
        if (variant == 1) host.users.clear();
        if (variant == 2)
            for (auto& game : host.games) { game.home_icon.reset(); game.library_icon.reset(); }
        Launcher launcher{host};
        uint32_t seed = 12345 + variant;
        const uint32_t keys[] = {Up, Down, Left, Right, Cross, Circle, Square, Triangle, Options, L1, R1};
        for (int step = 0; step < 2000; ++step) {
            seed = seed * 1103515245u + 12345u;
            const uint32_t key = keys[(seed >> 16) % 11];
            const Request request = launcher.Update(0.016f, key == Circle && (seed & 0x100) ? 0 : key, key);
            if (request.kind != Request::None) launcher.Returned("");
            launcher.Render(canvas);
        }
    }
    return 0;
}
