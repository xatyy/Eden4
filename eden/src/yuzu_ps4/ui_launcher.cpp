// SPDX-License-Identifier: GPL-3.0-or-later
// Layout and behaviour follow SwitchLauncher (kasobos67, GPL-3.0-or-later), redrawn natively: no
// console artwork, fonts or sounds are used (see ps4/generate-ui-assets.py).

#include <algorithm>
#include <cmath>
#include <functional>
#include "yuzu_ps4/ui_launcher.h"

namespace Ui {
namespace {

struct Palette {
    Color base, text, title, copy, meta, line, row, accent, shortcut, warn, blank, dim;
};
constexpr Palette Dark{{0x2d2d2d}, {0xf5f5f5}, {0xffffff}, {0xcfcfcf}, {0xbdbdbd}, {0x555555},
                       {0x414141}, {0x30d5c8}, {0x505050}, {0xffb36a}, {0x383838}, {0x000000}};
constexpr Palette Light{{0xebebeb}, {0x252525}, {0x252525}, {0x646464}, {0x646464}, {0xc7c7c7},
                        {0xffffff}, {0x00a9c6}, {0xffffff}, {0xd06a00}, {0xdcdcdc}, {0x000000}};

constexpr Color AvatarColors[] = {{0xe8505b}, {0xf28c38}, {0xf2c230}, {0x8cc63f}, {0x2fb67c}, {0x2bb3c0},
                                  {0x3e8ede}, {0x5b6ee1}, {0x9b5de5}, {0xe05ab4}, {0x8d6e63}, {0x6f7c85}};
constexpr const char* AvatarSymbols[] = {"person", "star", "heart", "moon", "sun", "note",
                                          "bolt", "flower", "crown", "diamond", "cloud", "smile"};
constexpr int AvatarCount = 12;
constexpr int HomeSlots = 12;
constexpr float TileStep = 405, TileSize = 384, TileTop = 291, StripLeft = 160.5f;

enum class Screen { Home, Library, Settings, Controllers, Users, Avatar, About, Files };
enum class Zone { Users, Games, Shortcuts };
enum class Modal { None, GameInfo, Choice, Power, Keyboard, ConfirmRemove, Order, Test };

const char* const ShortcutNames[] = {"About", "Game Files", "Users", "Controllers", "System Settings", "Power"};
const char* const ShortcutIcons[] = {"info", "folder", "users", "controller", "gear_72", "power_72"};
const char* const KeyboardRows[] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm_.-"};
enum KeyboardMode { AddUserName, RenameUser, AddFolder };

// Folder paths need '/' where names have an apostrophe.
char KeyChar(int row, int col, bool shift, int mode) {
    char ch = KeyboardRows[row][col];
    if (ch == '\'' && mode == AddFolder) ch = '/';
    if (shift && ch >= 'a' && ch <= 'z') ch = char(ch - 32);
    return ch;
}

float Ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

std::string FormatSize(uint64_t bytes) {
    char text[32];
    if (bytes >= (1ull << 30)) snprintf(text, sizeof(text), "%.1f GB", bytes / double(1ull << 30));
    else snprintf(text, sizeof(text), "%.0f MB", bytes / double(1ull << 20));
    return text;
}

struct Row {
    std::string label, value;
    bool accent{};
    std::function<void()> action;   // Cross
    int option{-1};                 // index into Host::Options(), or -1
};

} // namespace

struct Launcher::State {
    Screen screen{Screen::Home};
    float transition{1};
    float time{};
    Zone zone{Zone::Games};
    int game_focus{}, shortcut_focus{}, user_focus{};
    float strip{}, strip_target{};
    int library{};
    float scroll{}, scroll_target{};
    bool sort_by_file{};
    std::vector<int> library_order;
    int category{};
    bool content{};
    int row{};
    int controllers_row{};
    int user_page{};
    int avatar_stage{}, avatar_selected{}, avatar_symbol{}, avatar_color{};
    Modal modal{Modal::None};
    float modal_time{};
    int modal_row{};
    int choice_option{-1};
    std::string keyboard_text;
    int keyboard_row{1}, keyboard_col{};
    bool keyboard_shift{};
    int keyboard_mode{};
    int order_cursor{}, order_pick{-1};
    float circle_hold{};
    int info_game{-1};
    float launch_time{-1};
    int launch_game{-1};
    std::string message;
    float message_time{};
    int file_row{};
    Screen settings_return{Screen::Home};
};

namespace {

// ---- drawing helpers -------------------------------------------------------------------------

struct Ctx {
    Canvas& c;
    const Palette& p;
    float time;
};

void FocusRect(Ctx& x, float bx, float by, float w, float h) {
    const float pulse = 0.5f + 0.5f * std::sin(x.time * 6.2832f / 1.2f);
    x.c.StrokeRoundRect(bx - 7.5f, by - 7.5f, w + 15, h + 15, 4, 5.5f, x.p.accent);
    x.c.StrokeRoundRect(bx - 6, by - 6, w + 12, h + 12, 3, 2, {0xffffff}, 0.15f + 0.35f * pulse);
}

void FocusCircle(Ctx& x, float cx, float cy, float r) {
    const float pulse = 0.5f + 0.5f * std::sin(x.time * 6.2832f / 1.2f);
    x.c.StrokeCircle(cx, cy, r + 8, 5.5f, x.p.accent);
    x.c.StrokeCircle(cx, cy, r + 6.5f, 2, {0xffffff}, 0.15f + 0.35f * pulse);
}

void Avatar(Ctx& x, const User& user, float cx, float cy, float r) {
    if (user.photo && !user.photo->Empty()) {
        x.c.DrawImageCircle(*user.photo, cx, cy, r);
        return;
    }
    x.c.FillCircle(cx, cy, r, AvatarColors[std::clamp(user.color, 0, AvatarCount - 1)]);
    const std::string symbol = std::string{"avatar_"} + AvatarSymbols[std::clamp(user.symbol, 0, AvatarCount - 1)];
    x.c.MaskScaled(symbol, cx, cy, r * 1.25f, {0xffffff});
}

void AvatarSquare(Ctx& x, const User& user, float left, float top, float size) {
    if (user.photo && !user.photo->Empty()) {
        x.c.DrawImageScaled(*user.photo, left, top, size, size);
        return;
    }
    x.c.FillRect(left, top, size, size, AvatarColors[std::clamp(user.color, 0, AvatarCount - 1)]);
    const std::string symbol = std::string{"avatar_"} + AvatarSymbols[std::clamp(user.symbol, 0, AvatarCount - 1)];
    x.c.MaskScaled(symbol, left + size / 2, top + size / 2, size * 0.62f, {0xffffff});
}

// Button hints, right-aligned like the console's footer.
using Hints = std::vector<std::pair<std::string, std::string>>;
float HintGlyph(Ctx& x, const std::string& key, float left, float cy) {
    if (key == "l1" || key == "r1") {
        x.c.StrokeRoundRect(left, cy - 17, 52, 34, 6, 1.5f, x.p.text, 0.5f);
        x.c.Text(key == "l1" ? "L1" : "R1", left + 26, cy + 8, 20, x.p.text, Align::Center);
        return 52;
    }
    x.c.Mask("btn_" + key, left, cy - 18, x.p.text);
    return 36;
}
float HintWidth(Ctx& x, const std::pair<std::string, std::string>& hint) {
    const float glyph = (hint.first == "l1" || hint.first == "r1") ? 52 : 36;
    return glyph + 10 + x.c.Measure(hint.second, 30);
}

void Footer(Ctx& x, Host& host, const Hints& hints) {
    x.c.HLine(45, 1875, 971, 1.5f, x.p.line);
    const auto pads = host.Pads();
    for (int i = 0; i < 4; ++i) x.c.Mask("pad", 108 + 66 * i, 1007, x.p.title, pads[i].connected ? 1.0f : 0.16f);
    float width = 0;
    for (size_t i = 0; i < hints.size(); ++i) width += HintWidth(x, hints[i]) + (i + 1 < hints.size() ? 34 : 0);
    float left = 1836 - width;
    for (const auto& hint : hints) {
        left += HintGlyph(x, hint.first, left, 1025) + 10;
        x.c.Text(hint.second, left, 1036, 30, x.p.text);
        left += x.c.Measure(hint.second, 30) + 34;
    }
}

void Frame(Ctx& x, const std::string& title, const char* icon) {
    if (icon) x.c.MaskCentered(icon, 126, 100, x.p.text);
    x.c.Text(title, icon ? 178 : 96, 114, 40, x.p.title, Align::Left, 1430);
    x.c.HLine(96, 1824, 157, 1.5f, x.p.line);
}

void Plate(Ctx& x, float left, float top, float w, float h, bool selected, bool focused = true) {
    if (!selected) return;
    x.c.FillRect(left, top, w, h, x.p.row);
    if (focused) FocusRect(x, left, top, w, h);
}

// A settings-style list: label left, value right, a line under each row.
void DrawRows(Ctx& x, const std::vector<Row>& rows, float left, float top, float w, float pitch, int selected,
              int visible, bool focused) {
    const int first = std::clamp(selected - visible + 1, 0, std::max(0, int(rows.size()) - visible));
    x.c.PushClip(left - 14, top - 14, w + 28, visible * pitch + 28);
    for (int i = first; i < std::min<int>(rows.size(), first + visible); ++i) {
        const float y = top + (i - first) * pitch;
        const bool here = i == selected;
        if (here) {
            Plate(x, left, y, w, pitch - 2, true, focused);
        } else {
            x.c.HLine(left, left + w, y + pitch - 1, 1, x.p.line, 0.8f);
        }
        const float value_w = std::min(w * 0.5f, x.c.Measure(rows[i].value, 30));
        x.c.Text(rows[i].value, left + w - 36, y + pitch / 2 + 10, 30, rows[i].accent ? x.p.accent : x.p.copy,
                 Align::Right, w * 0.5f);
        x.c.Text(rows[i].label, left + 36, y + pitch / 2 + 10, 30, x.p.text, Align::Left, w - value_w - 108);
    }
    x.c.PopClip();
    if (int(rows.size()) > visible) {
        const float track = visible * pitch, handle = track * visible / rows.size();
        x.c.FillRoundRect(left + w + 14, top, 9, track, 4.5f, x.p.line, 0.35f);
        x.c.FillRoundRect(left + w + 14, top + (track - handle) * first / std::max(1, int(rows.size()) - visible), 9,
                          handle, 4.5f, x.p.meta, 0.8f);
    }
}

void TextBlock(Ctx& x, const std::string& text, float left, float top, int size, Color color, float w, float pitch,
               int max_lines = 6) {
    int n = 0;
    for (const auto& line : x.c.Wrap(text, size, w)) {
        if (n++ >= max_lines) break;
        x.c.Text(line, left, top, size, color);
        top += pitch;
    }
}

void Dim(Ctx& x, float amount) {
    x.c.FillRect(0, 0, Canvas::Width, Canvas::Height, x.p.dim, 0.68f * amount);
}

void Panel(Ctx& x, float left, float top, float w, float h) {
    x.c.Shadow(left, top + 2, w, h, 0.5f);
    x.c.FillRect(left, top, w, h, x.p.base);
}

} // namespace

// ------------------------------------------------------------------------------------------------

Launcher::Launcher(Host& host_) : s{std::make_unique<State>()}, host{host_} {
    s->transition = 0;
}
Launcher::~Launcher() = default;

void Launcher::Returned(const std::string& message) {
    s->screen = Screen::Home;
    s->zone = host.Games().empty() ? Zone::Shortcuts : Zone::Games;
    s->game_focus = 0;
    s->strip_target = 0;
    s->modal = Modal::None;
    s->launch_time = -1;
    s->transition = 0;
    s->message = message;
    s->message_time = 0;
}

namespace {
// The options with this category, in table order.
std::vector<int> OptionsIn(Host& host, const std::string& category) {
    std::vector<int> list;
    auto& options = host.Options();
    for (size_t i = 0; i < options.size(); ++i)
        if (options[i].category == category) list.push_back(int(i));
    return list;
}

int FindOption(Host& host, const std::string& key) {
    auto& options = host.Options();
    for (size_t i = 0; i < options.size(); ++i)
        if (options[i].key == key) return int(i);
    return -1;
}

void SortLibrary(std::vector<int>& order, const std::vector<Game>& games, bool by_file) {
    order.resize(games.size());
    for (size_t i = 0; i < games.size(); ++i) order[i] = int(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return by_file ? games[a].file < games[b].file : games[a].title < games[b].title;
    });
}

bool IsOn(const std::string& value) {
    return value == "On";
}
} // namespace

// ---- rows of the settings pages -----------------------------------------------------------------

namespace {
struct Category {
    const char* name;
    const char* options; // option category, or nullptr for a special page
};
const Category Categories[] = {{"Users", nullptr},          {"Display", "display"}, {"Controllers", "controllers"},
                               {"Themes", "theme"},         {"Sound", "sound"},     {"Advanced", "debug"},
                               {"System", nullptr}};
constexpr int CategoryCount = sizeof(Categories) / sizeof(Categories[0]);
} // namespace

// ---- update -------------------------------------------------------------------------------------

Request Launcher::Update(float dt, uint32_t pressed, uint32_t held) {
    State& st = *s;
    st.time += dt;
    st.transition += dt;
    st.modal_time += dt;
    st.message_time += dt;
    if (st.message_time > 4) st.message.clear();
    const auto& games = host.Games();
    const auto open = [&](Screen screen) {
        st.screen = screen;
        st.transition = 0;
        st.row = 0;
        st.content = false;
        host.Play(screen == Screen::Home ? Sound::Back : Sound::Open);
    };
    const auto say = [&](std::string text) {
        st.message = std::move(text);
        st.message_time = 0;
    };
    const auto close_modal = [&](Sound sound) {
        st.modal = Modal::None;
        host.Play(sound);
    };
    const auto choose_option = [&](int index) {
        auto& option = host.Options()[index];
        if (option.values.size() == 2) {
            host.SetOption(index, 1 - option.current);
            host.Play(Sound::Toggle);
        } else {
            st.modal = Modal::Choice;
            st.modal_time = 0;
            st.choice_option = index;
            st.modal_row = option.current;
            host.Play(Sound::Open);
        }
    };

    // Launch animation: the request goes out once it has played.
    if (st.launch_time >= 0) {
        st.launch_time += dt;
        if (st.launch_time > 0.5f) {
            const int game = st.launch_game;
            st.launch_time = -1;
            return {Request::Launch, game};
        }
        return {};
    }
    const auto start = [&](int game) {
        if (game < 0 || game >= int(games.size())) return;
        st.launch_time = 0;
        st.launch_game = game;
        st.modal = Modal::None;
        host.Play(Sound::Launch);
    };

    // ---- modals ----
    if (st.modal != Modal::None) {
        switch (st.modal) {
        case Modal::GameInfo:
            if (pressed & Circle) close_modal(Sound::Back);
            else if (pressed & Cross) start(st.info_game);
            break;
        case Modal::Choice: {
            auto& option = host.Options()[st.choice_option];
            const int count = int(option.values.size());
            if (pressed & Up) { st.modal_row = std::max(0, st.modal_row - 1); host.Play(Sound::Focus); }
            if (pressed & Down) { st.modal_row = std::min(count - 1, st.modal_row + 1); host.Play(Sound::Focus); }
            if (pressed & Circle) close_modal(Sound::Back);
            if (pressed & Cross) {
                host.SetOption(st.choice_option, st.modal_row);
                close_modal(Sound::Select);
            }
            break;
        }
        case Modal::Power:
            if (pressed & (Left | Right)) { st.modal_row = 1 - st.modal_row; host.Play(Sound::Focus); }
            if (pressed & Circle) close_modal(Sound::Back);
            if (pressed & Cross) {
                if (st.modal_row == 1) return {Request::Exit, -1};
                close_modal(Sound::Back);
            }
            break;
        case Modal::ConfirmRemove:
            if (pressed & (Left | Right)) { st.modal_row = 1 - st.modal_row; host.Play(Sound::Focus); }
            if (pressed & Circle) close_modal(Sound::Back);
            if (pressed & Cross) {
                if (st.modal_row == 1) {
                    if (host.RemoveUser(st.user_page)) {
                        st.user_page = std::max(0, st.user_page - 1);
                        say("User removed.");
                        close_modal(Sound::Select);
                    } else {
                        say("The last user cannot be removed.");
                        close_modal(Sound::Error);
                    }
                } else {
                    close_modal(Sound::Back);
                }
            }
            break;
        case Modal::Keyboard: {
            // Rows 0-3: characters (10 keys); row 4: Shift(0-1) Space(2-5) Delete(6-7) OK(8-9).
            const size_t limit = st.keyboard_mode == AddFolder ? 120 : 20;
            const auto type = [&](char ch) {
                if (st.keyboard_text.size() < limit) st.keyboard_text.push_back(ch);
                else host.Play(Sound::Error);
            };
            const auto finish = [&] {
                std::string name = st.keyboard_text;
                while (!name.empty() && name.back() == ' ') name.pop_back();
                if (name.empty()) { host.Play(Sound::Error); return; }
                if (st.keyboard_mode == AddFolder) {
                    const bool ok = host.AddSource(name);
                    say(ok ? "Folder added. Looking for software..." : "That folder could not be opened.");
                    if (ok) st.file_row = int(host.Sources().size()) - 1;
                    close_modal(ok ? Sound::Select : Sound::Error);
                    return;
                }
                const bool rename = st.keyboard_mode == RenameUser;
                const bool ok = rename ? host.RenameUser(st.user_page, name) : host.AddUser(name);
                if (ok && !rename) st.user_page = int(host.Users().size()) - 1;
                say(ok ? (rename ? "Name changed." : "User added.") : "Could not save the user.");
                close_modal(ok ? Sound::Select : Sound::Error);
            };
            if (pressed & Up) { st.keyboard_row = (st.keyboard_row + 4) % 5; host.Play(Sound::Focus); }
            if (pressed & Down) { st.keyboard_row = (st.keyboard_row + 1) % 5; host.Play(Sound::Focus); }
            if (pressed & (Left | Right)) {
                const int step = (pressed & Right) ? 1 : -1;
                if (st.keyboard_row == 4) {
                    // The bottom row has four wide keys starting at columns 0, 2, 6 and 8.
                    constexpr int starts[] = {0, 2, 6, 8};
                    const int key = st.keyboard_col < 2 ? 0 : st.keyboard_col < 6 ? 1 : st.keyboard_col < 8 ? 2 : 3;
                    st.keyboard_col = starts[(key + step + 4) % 4];
                } else {
                    st.keyboard_col = (st.keyboard_col + step + 10) % 10;
                }
                host.Play(Sound::Focus);
            }
            if (pressed & Square) {
                if (!st.keyboard_text.empty()) st.keyboard_text.pop_back();
                host.Play(Sound::Back);
            }
            if (pressed & Triangle) type(' ');
            if (pressed & L1) st.keyboard_shift = !st.keyboard_shift;
            if (pressed & Options) finish();
            else if (pressed & Circle) close_modal(Sound::Back);
            else if (pressed & Cross) {
                if (st.keyboard_row < 4) {
                    type(KeyChar(st.keyboard_row, st.keyboard_col, st.keyboard_shift, st.keyboard_mode));
                    host.Play(Sound::Select);
                } else if (st.keyboard_col < 2) {
                    st.keyboard_shift = !st.keyboard_shift;
                    host.Play(Sound::Toggle);
                } else if (st.keyboard_col < 6) {
                    type(' ');
                    host.Play(Sound::Select);
                } else if (st.keyboard_col < 8) {
                    if (!st.keyboard_text.empty()) st.keyboard_text.pop_back();
                    host.Play(Sound::Back);
                } else {
                    finish();
                }
            }
            break;
        }
        case Modal::Order:
            if (pressed & Left) { st.order_cursor = (st.order_cursor + 3) % 4; host.Play(Sound::Focus); }
            if (pressed & Right) { st.order_cursor = (st.order_cursor + 1) % 4; host.Play(Sound::Focus); }
            if (pressed & Circle) {
                if (st.order_pick >= 0) { st.order_pick = -1; host.Play(Sound::Back); }
                else close_modal(Sound::Back);
            }
            if (pressed & Cross) {
                if (st.order_pick < 0) {
                    st.order_pick = st.order_cursor;
                    host.Play(Sound::Select);
                } else {
                    if (st.order_pick != st.order_cursor) {
                        host.SwapPads(st.order_pick, st.order_cursor);
                        say("Players " + std::to_string(st.order_pick + 1) + " and " +
                            std::to_string(st.order_cursor + 1) + " swapped.");
                    }
                    st.order_pick = -1;
                    host.Play(Sound::Toggle);
                }
            }
            break;
        case Modal::Test:
            // Every button is being tested: hold Circle to leave.
            st.circle_hold = (held & Circle) ? st.circle_hold + dt : 0;
            if (st.circle_hold > 1.0f) {
                st.circle_hold = 0;
                close_modal(Sound::Back);
            }
            break;
        case Modal::None:
            break;
        }
        return {};
    }

    // ---- screens ----
    switch (st.screen) {
    case Screen::Home: {
        const int tiles = std::min<int>(games.size(), HomeSlots) + (int(games.size()) > HomeSlots ? 1 : 0);
        const int users = int(host.Users().size());
        if (st.zone == Zone::Games && tiles == 0) st.zone = Zone::Shortcuts;
        if (pressed & Up) {
            if (st.zone == Zone::Shortcuts && tiles > 0) st.zone = Zone::Games;
            else if (st.zone == Zone::Shortcuts || st.zone == Zone::Games) {
                st.zone = Zone::Users;
                st.user_focus = std::clamp(host.CurrentUser(), 0, std::max(0, users - 1));
            } else { host.Play(Sound::Limit); break; }
            host.Play(Sound::Focus);
        }
        if (pressed & Down) {
            if (st.zone == Zone::Users) st.zone = tiles > 0 ? Zone::Games : Zone::Shortcuts;
            else if (st.zone == Zone::Games) st.zone = Zone::Shortcuts;
            else { host.Play(Sound::Limit); break; }
            host.Play(Sound::Focus);
        }
        int* focus = st.zone == Zone::Users ? &st.user_focus : st.zone == Zone::Games ? &st.game_focus : &st.shortcut_focus;
        const int count = st.zone == Zone::Users ? users : st.zone == Zone::Games ? tiles : 6;
        if (pressed & Left) {
            if (*focus > 0) { --*focus; host.Play(Sound::Focus); } else host.Play(Sound::Limit);
        }
        if (pressed & Right) {
            if (*focus + 1 < count) { ++*focus; host.Play(Sound::Focus); } else host.Play(Sound::Limit);
        }
        st.game_focus = std::clamp(st.game_focus, 0, std::max(0, tiles - 1));
        const bool all_tile = int(games.size()) > HomeSlots && st.game_focus == tiles - 1;
        if (pressed & Cross) {
            if (st.zone == Zone::Games) {
                if (all_tile) open(Screen::Library);
                else start(st.game_focus);
            } else if (st.zone == Zone::Users) {
                st.user_page = st.user_focus;
                open(Screen::Users);
            } else {
                switch (st.shortcut_focus) {
                case 0: open(Screen::About); break;
                case 1: st.file_row = 0; open(Screen::Files); break;
                case 2: st.user_page = std::max(0, host.CurrentUser()); open(Screen::Users); break;
                case 3: st.controllers_row = 0; open(Screen::Controllers); break;
                case 4: st.category = 0; open(Screen::Settings); break;
                case 5: st.modal = Modal::Power; st.modal_row = 0; st.modal_time = 0; host.Play(Sound::Open); break;
                }
            }
        }
        if ((pressed & Triangle) && st.zone == Zone::Games && !all_tile && st.game_focus < int(games.size())) {
            st.info_game = st.game_focus;
            st.modal = Modal::GameInfo;
            st.modal_time = 0;
            host.Play(Sound::Open);
        }
        if (pressed & Options) { st.category = 0; open(Screen::Settings); }
        st.strip_target = std::clamp((st.game_focus - 3) * TileStep, 0.0f, std::max(0, tiles - 4) * TileStep);
        break;
    }
    case Screen::Library: {
        SortLibrary(st.library_order, games, st.sort_by_file);
        const int count = int(games.size());
        const int old = st.library;
        if (pressed & Left) st.library = std::max(0, st.library - 1);
        if (pressed & Right) st.library = std::min(count - 1, st.library + 1);
        if (pressed & Up) st.library = st.library >= 6 ? st.library - 6 : st.library;
        if (pressed & Down) st.library = std::min(count - 1, st.library + 6);
        if (st.library != old) host.Play(Sound::Focus);
        else if (pressed & (Left | Right | Up | Down)) host.Play(Sound::Limit);
        const int row = st.library / 6;
        const float top = 278 + row * 276.0f;
        if (top - st.scroll_target < 278) st.scroll_target = top - 278;
        if (top + 261 - st.scroll_target > 945) st.scroll_target = top + 261 - 945;
        if (pressed & Options) { st.sort_by_file = !st.sort_by_file; host.Play(Sound::Toggle); }
        if (pressed & Circle) open(Screen::Home);
        if (count > 0 && (pressed & Cross)) start(st.library_order[st.library]);
        if (count > 0 && (pressed & Triangle)) {
            st.info_game = st.library_order[st.library];
            st.modal = Modal::GameInfo;
            st.modal_time = 0;
            host.Play(Sound::Open);
        }
        break;
    }
    case Screen::Settings: {
        const Category& category = Categories[st.category];
        // Build this page's rows (same order as Render).
        std::vector<Row> rows;
        if (category.options) {
            for (const int index : OptionsIn(host, category.options)) {
                Row r;
                r.option = index;
                rows.push_back(r);
            }
        } else if (std::string_view{category.name} == "Users") {
            const auto users = host.Users();
            for (int i = 0; i < int(users.size()); ++i)
                rows.push_back({"", "", false, [&, i] { st.user_page = i; st.settings_return = Screen::Settings; open(Screen::Users); }});
            if (users.size() < 8)
                rows.push_back({"", "", false, [&] {
                    st.keyboard_text.clear(); st.keyboard_mode = AddUserName; st.keyboard_row = 1; st.keyboard_col = 0;
                    st.modal = Modal::Keyboard; st.modal_time = 0; host.Play(Sound::Open); }});
        } else {
            rows.push_back({"", "", false, [&] { say(host.ExportLogs()); host.Play(Sound::Select); }});
            rows.push_back({"", "", false, [&] { host.Rescan(); say("Looking for software..."); host.Play(Sound::Select); }});
        }
        if (!st.content) {
            if (pressed & Up) { st.category = (st.category + CategoryCount - 1) % CategoryCount; host.Play(Sound::Focus); }
            if (pressed & Down) { st.category = (st.category + 1) % CategoryCount; host.Play(Sound::Focus); }
            if ((pressed & (Right | Cross)) && !rows.empty()) { st.content = true; st.row = 0; host.Play(Sound::Select); }
            if (pressed & Circle) open(Screen::Home);
        } else {
            if (pressed & Up) { if (st.row > 0) { --st.row; host.Play(Sound::Focus); } else host.Play(Sound::Limit); }
            if (pressed & Down) {
                if (st.row + 1 < int(rows.size())) { ++st.row; host.Play(Sound::Focus); } else host.Play(Sound::Limit);
            }
            if (pressed & (Left | Circle)) { st.content = false; host.Play(Sound::Back); }
            st.row = std::clamp(st.row, 0, std::max(0, int(rows.size()) - 1));
            if ((pressed & Cross) && !rows.empty()) {
                if (rows[st.row].option >= 0) {
                    auto& option = host.Options()[rows[st.row].option];
                    if (option.key == "ui_theme") {
                        host.SetOption(rows[st.row].option, st.row);
                        host.Play(Sound::Toggle);
                    } else {
                        choose_option(rows[st.row].option);
                    }
                } else if (rows[st.row].action) {
                    rows[st.row].action();
                }
            }
        }
        break;
    }
    case Screen::Controllers: {
        const auto types = OptionsIn(host, "controllers");
        const int count = 2 + int(types.size());
        if (pressed & Up) { if (st.controllers_row > 0) { --st.controllers_row; host.Play(Sound::Focus); } else host.Play(Sound::Limit); }
        if (pressed & Down) {
            if (st.controllers_row + 1 < count) { ++st.controllers_row; host.Play(Sound::Focus); } else host.Play(Sound::Limit);
        }
        if (pressed & Circle) open(Screen::Home);
        if (pressed & Cross) {
            if (st.controllers_row == 0) {
                st.modal = Modal::Order; st.order_cursor = 0; st.order_pick = -1; st.modal_time = 0; host.Play(Sound::Open);
            } else if (st.controllers_row == 1) {
                st.modal = Modal::Test; st.circle_hold = 0; st.modal_time = 0; host.Play(Sound::Open);
            } else {
                choose_option(types[st.controllers_row - 2]);
            }
        }
        break;
    }
    case Screen::Users: {
        const auto users = host.Users();
        const int count = std::min(8, int(users.size()) + 1);
        if (pressed & Up) { if (st.user_page > 0) { --st.user_page; host.Play(Sound::Focus); } else host.Play(Sound::Limit); }
        if (pressed & Down) {
            if (st.user_page + 1 < count) { ++st.user_page; host.Play(Sound::Focus); } else host.Play(Sound::Limit);
        }
        st.user_page = std::clamp(st.user_page, 0, count - 1);
        const bool add = st.user_page >= int(users.size());
        if (pressed & Circle) {
            if (st.settings_return == Screen::Settings) { st.settings_return = Screen::Home; open(Screen::Settings); st.content = true; }
            else open(Screen::Home);
        }
        if (pressed & Cross) {
            if (add) {
                st.keyboard_text.clear(); st.keyboard_mode = AddUserName; st.keyboard_row = 1; st.keyboard_col = 0;
                st.modal = Modal::Keyboard; st.modal_time = 0; host.Play(Sound::Open);
            } else if (host.CurrentUser() != st.user_page) {
                host.SetCurrentUser(st.user_page);
                say(users[st.user_page].name + " will play the next game.");
                host.Play(Sound::Select);
            }
        }
        if (!add && (pressed & Triangle)) {
            st.keyboard_text = users[st.user_page].name; st.keyboard_mode = RenameUser; st.keyboard_row = 1; st.keyboard_col = 0;
            st.modal = Modal::Keyboard; st.modal_time = 0; host.Play(Sound::Open);
        }
        if (!add && (pressed & Square)) {
            st.modal = Modal::ConfirmRemove; st.modal_row = 0; st.modal_time = 0; host.Play(Sound::Open);
        }
        if (!add && (pressed & Options)) {
            st.avatar_stage = 0;
            st.avatar_symbol = users[st.user_page].symbol;
            st.avatar_color = users[st.user_page].color;
            st.avatar_selected = st.avatar_symbol;
            open(Screen::Avatar);
        }
        break;
    }
    case Screen::Avatar: {
        const int old = st.avatar_selected;
        if (pressed & Left) st.avatar_selected = std::max(0, st.avatar_selected - 1);
        if (pressed & Right) st.avatar_selected = std::min(AvatarCount - 1, st.avatar_selected + 1);
        if (pressed & Up) st.avatar_selected = st.avatar_selected >= 6 ? st.avatar_selected - 6 : st.avatar_selected;
        if (pressed & Down) st.avatar_selected = std::min(AvatarCount - 1, st.avatar_selected + 6);
        if (old != st.avatar_selected) host.Play(Sound::Focus);
        (st.avatar_stage == 0 ? st.avatar_symbol : st.avatar_color) = st.avatar_selected;
        if (pressed & Circle) {
            if (st.avatar_stage == 1) { st.avatar_stage = 0; st.avatar_selected = st.avatar_symbol; host.Play(Sound::Back); }
            else { st.screen = Screen::Users; st.transition = 0; host.Play(Sound::Back); }
        }
        if (pressed & Cross) {
            if (st.avatar_stage == 0) {
                st.avatar_stage = 1;
                st.avatar_selected = st.avatar_color;
                host.Play(Sound::Select);
            } else {
                host.SetUserLook(st.user_page, st.avatar_color, st.avatar_symbol);
                say("Icon saved.");
                st.screen = Screen::Users;
                st.transition = 0;
                host.Play(Sound::Select);
            }
        }
        break;
    }
    case Screen::About:
        if (pressed & Circle) open(Screen::Home);
        if (pressed & Cross) { say(host.ExportLogs()); host.Play(Sound::Select); }
        break;
    case Screen::Files: {
        // Rows: every source, then "Add Folder".
        const auto sources = host.Sources();
        const int count = int(sources.size()) + 1;
        if (pressed & Up) { if (st.file_row > 0) { --st.file_row; host.Play(Sound::Focus); } else host.Play(Sound::Limit); }
        if (pressed & Down) { if (st.file_row + 1 < count) { ++st.file_row; host.Play(Sound::Focus); } else host.Play(Sound::Limit); }
        st.file_row = std::clamp(st.file_row, 0, count - 1);
        const bool add = st.file_row >= int(sources.size());
        if (pressed & Circle) open(Screen::Home);
        if (pressed & Options) { host.Rescan(); say("Looking for software..."); host.Play(Sound::Select); }
        if (pressed & Triangle) { say(host.ExportLogs()); host.Play(Sound::Select); }
        if (pressed & Cross) {
            if (add) {
                st.keyboard_text = "/mnt/usb0/";
                st.keyboard_mode = AddFolder; st.keyboard_row = 1; st.keyboard_col = 0;
                st.modal = Modal::Keyboard; st.modal_time = 0; host.Play(Sound::Open);
            } else {
                const bool enable = !sources[st.file_row].enabled;
                host.SetSourceEnabled(st.file_row, enable);
                host.Play(Sound::Toggle);
                say(sources[st.file_row].name + (enable ? " is on." : " is off."));
            }
        }
        if (!add && (pressed & Square)) {
            if (sources[st.file_row].removable && host.RemoveSource(st.file_row)) {
                say("Folder removed.");
                host.Play(Sound::Select);
            } else {
                say("Internal storage and USB drives are always listed. Turn them off instead.");
                host.Play(Sound::Error);
            }
        }
        break;
    }
    }
    // Smooth scrolling.
    const float k = std::min(1.0f, dt * 14);
    st.strip += (st.strip_target - st.strip) * k;
    st.scroll += (st.scroll_target - st.scroll) * k;
    return {};
}

// ---- render -------------------------------------------------------------------------------------

void Launcher::Render(Canvas& canvas) {
    State& st = *s;
    const bool white = [&] {
        const int theme = FindOption(host, "ui_theme");
        return theme >= 0 && host.Options()[theme].current == 1;
    }();
    const Palette& p = white ? Light : Dark;
    Ctx x{canvas, p, st.time};
    canvas.Clear(p.base);
    const auto& games = host.Games();
    const auto users = host.Users();
    canvas.PushOpacity(Ease(st.transition / 0.18f));

    const auto cover = [&](const Game& game, float left, float top, float size, bool home) {
        canvas.Shadow(left, top + 1.5f, size, size, 0.4f);
        const auto& icon = home ? game.home_icon : game.library_icon;
        if (icon && !icon->Empty()) {
            if (icon->width == int(size)) canvas.DrawImage(*icon, left, top);
            else canvas.DrawImageScaled(*icon, left, top, size, size);
            return;
        }
        // No icon (homebrew, or not read yet): the title on a plain tile.
        canvas.FillRect(left, top, size, size, p.row);
        canvas.MaskCentered("pad_big", left + size / 2, top + size * 0.36f, p.meta, 0.5f);
        float y = top + size * 0.62f;
        for (const auto& line : canvas.Wrap(game.title, size > 300 ? 28 : 22, size - 40)) {
            canvas.Text(line, left + size / 2, y, size > 300 ? 28 : 22, p.text, Align::Center, size - 30);
            y += size > 300 ? 36 : 28;
            if (y > top + size - 16) break;
        }
    };

    switch (st.screen) {
    case Screen::Home: {
        // Users, top left.
        for (int i = 0; i < int(users.size()); ++i) {
            const float cx = 136.5f + i * 90, cy = 98;
            if (st.zone == Zone::Users && st.user_focus == i) FocusCircle(x, cx, cy, 36);
            Avatar(x, users[i], cx, cy, 36);
            canvas.StrokeCircle(cx, cy, 37, 2, p.title, 0.7f);
            if (i == host.CurrentUser()) canvas.FillRoundRect(cx - 20, 144, 40, 4, 2, p.accent);
            if (st.zone == Zone::Users && st.user_focus == i)
                canvas.Text(users[i].name, cx, 192, 28, p.accent, Align::Center, 300);
        }
        canvas.Text(host.Clock(), 1860, 115.5f, 30, p.text, Align::Right);
        const int shown = std::min<int>(games.size(), HomeSlots);
        const bool all = int(games.size()) > HomeSlots;
        const int tiles = shown + (all ? 1 : 0);
        // Title of the selected game above the row.
        if (st.zone == Zone::Games && tiles > 0) {
            const std::string title = (all && st.game_focus == tiles - 1) ? "All Software" : games[st.game_focus].title;
            const float w = std::clamp(canvas.Measure(title, 32), 450.0f, 1100.0f);
            const float tile_x = StripLeft + st.game_focus * TileStep - st.strip;
            const float left = std::clamp(tile_x + 192 - w / 2, 112.0f, 1808.0f - w);
            canvas.Text(title, left + (canvas.Measure(title, 32) < w ? (w - canvas.Measure(title, 32)) / 2 : 0), 258, 32,
                        p.accent, Align::Left, w);
        } else if (games.empty()) {
            canvas.Text(host.Scanning() ? "Looking for software..." : "Add software in Game Files.", 160.5f, 258, 30, p.copy);
        }
        canvas.PushClip(0, 270, Canvas::Width, 440);
        for (int i = 0; i < std::max(HomeSlots, tiles); ++i) {
            const float left = StripLeft + i * TileStep - st.strip;
            if (left + TileSize < 0 || left > Canvas::Width) continue;
            const bool focused = st.zone == Zone::Games && st.game_focus == i;
            if (i >= tiles) {
                canvas.FillRect(left, TileTop, TileSize, TileSize, p.blank);
            } else if (all && i == tiles - 1) {
                canvas.CircleShadow(left + 192, 483, 120);
                if (focused) FocusCircle(x, left + 192, 483, 120);
                canvas.FillCircle(left + 192, 483, 120, p.shortcut);
                canvas.MaskCentered("grid", left + 192, 483, p.title);
            } else {
                const bool launching = st.launch_time >= 0 && st.launch_game == i;
                if (launching) {
                    const float bump = 1 - 0.06f * std::sin(3.14159f * std::min(1.0f, st.launch_time / 0.3f));
                    const float size = TileSize * bump;
                    const auto& icon = games[i].home_icon;
                    if (icon && !icon->Empty())
                        canvas.DrawImageScaled(*icon, left + (TileSize - size) / 2, TileTop + (TileSize - size) / 2, size, size);
                    else cover(games[i], left, TileTop, TileSize, true);
                    canvas.FillRect(left, TileTop, TileSize, TileSize, {0xffffff},
                                    0.7f * std::max(0.0f, 1 - st.launch_time / 0.35f));
                } else {
                    cover(games[i], left, TileTop, TileSize, true);
                }
                if (focused) FocusRect(x, left, TileTop, TileSize, TileSize);
            }
        }
        canvas.PopClip();
        // Shortcut buttons.
        const Color inks[] = {{0xff4d4d}, {0xffae2e}, {0x4a9bdb}, p.title, p.title, p.title};
        for (int i = 0; i < 6; ++i) {
            const float cx = 555 + i * 162.0f;
            canvas.CircleShadow(cx, 816, 66);
            if (st.zone == Zone::Shortcuts && st.shortcut_focus == i) {
                FocusCircle(x, cx, 816, 66);
                canvas.Text(ShortcutNames[i], cx, 918, 24, p.accent, Align::Center, 330);
            }
            canvas.FillCircle(cx, 816, 66, p.shortcut);
            canvas.MaskCentered(ShortcutIcons[i], cx, 816, inks[i]);
        }
        if (!st.message.empty()) canvas.Text(st.message, 960, 720, 22, p.copy, Align::Center, 1600);
        Hints hints;
        if (st.zone == Zone::Users) hints = {{"cross", "User Page"}};
        else if (st.zone == Zone::Shortcuts) hints = {{"cross", "Open"}};
        else if (all && st.game_focus == tiles - 1) hints = {{"cross", "Open"}};
        else hints = {{"triangle", "Options"}, {"cross", "Start"}};
        Footer(x, host, hints);
        break;
    }
    case Screen::Library: {
        Frame(x, "All Software", "grid_small");
        canvas.MaskCentered("btn_options", 1574, 98, p.text);
        canvas.Text(st.sort_by_file ? "By File Name" : "By Title", 1758, 110, 28, p.text, Align::Right);
        canvas.MaskCentered("chevron_down", 1793, 96, p.text);
        if (st.library_order.size() != games.size()) SortLibrary(st.library_order, games, st.sort_by_file);
        st.library = std::clamp(st.library, 0, std::max(0, int(games.size()) - 1));
        canvas.PushClip(120, 185, 1668, 767);
        const int count = int(st.library_order.size());
        for (int i = 0; i < count; ++i) {
            const float left = 138 + (i % 6) * 276.0f, top = 278 + (i / 6) * 276.0f - st.scroll;
            if (top + 261 < 169 || top > 968) continue;
            cover(games[st.library_order[i]], left, top, 261, false);
            if (i == st.library) {
                if (st.launch_time >= 0)
                    canvas.FillRect(left, top, 261, 261, {0xffffff}, 0.7f * std::max(0.0f, 1 - st.launch_time / 0.35f));
                FocusRect(x, left, top, 261, 261);
            }
        }
        if (count > 0) {
            // Title bubble above the selection.
            const float left = 138 + (st.library % 6) * 276.0f, top = 278 + (st.library / 6) * 276.0f - st.scroll;
            const std::string& title = games[st.library_order[st.library]].title;
            const float w = std::clamp(canvas.Measure(title, 30) + 42, 180.0f, 980.0f);
            const float bx = std::clamp(left + 130.5f - w / 2, 130.5f, 1786.5f - w);
            canvas.Shadow(bx, top - 82.5f, w, 65, 0.36f);
            canvas.FillRoundRect(bx, top - 84, w, 65, 4.5f, p.shortcut);
            for (int k = 0; k < 12; ++k)
                canvas.FillRect(left + 130.5f - (12 - k), top - 19 + k, (12 - k) * 2, 1, p.shortcut);
            canvas.Text(title, bx + 21, top - 41, 30, p.accent, Align::Left, w - 42);
        }
        canvas.PopClip();
        const float total = std::ceil(count / 6.0f) * 276;
        if (total > 767) {
            const float handle = std::max(48.0f, 750 * 767 / total);
            const float maximum = std::max(1.0f, 278 + (std::ceil(count / 6.0f) - 1) * 276 + 261 - 945);
            canvas.FillRoundRect(1814, 190, 6, 750, 3, p.line, 0.22f);
            canvas.FillRoundRect(1814, 190 + (750 - handle) * std::clamp(st.scroll / maximum, 0.0f, 1.0f), 6, handle, 3,
                                 p.text, 0.58f);
        }
        if (count == 0) canvas.Text("No software found.", 960, 480, 36, p.title, Align::Center);
        Footer(x, host, {{"options", "Sort"}, {"triangle", "Options"}, {"circle", "Back"}, {"cross", "Start"}});
        break;
    }
    case Screen::Settings: {
        Frame(x, "System Settings", "gear_small");
        for (int i = 0; i < CategoryCount; ++i) {
            const float y = 209 + i * 68.0f;
            if (st.category == i) {
                canvas.FillRect(156, y, 438, 66, p.row);
                if (!st.content) FocusRect(x, 156, y, 438, 66);
                canvas.FillRect(171, y + 12, 4, 42, p.accent);
            }
            canvas.Text(Categories[i].name, 188, y + 43, 30, st.category == i ? p.accent : p.text);
        }
        const Category& category = Categories[st.category];
        const int selected = st.content ? st.row : -1;
        std::string help;
        if (category.options && std::string_view{category.options} == "theme") {
            const int theme = FindOption(host, "ui_theme");
            for (int i = 0; i < 2; ++i) {
                const float y = 228 + i * 114.0f;
                Plate(x, 714, y, 1086, 112, selected == i);
                canvas.FillRect(750, y + 24, 64, 64, i == 0 ? Color{0x303030} : Color{0xf4f4f4});
                canvas.StrokeRoundRect(750, y + 24, 64, 64, 0, 1, p.line);
                canvas.Text(i == 0 ? "Basic Black" : "Basic White", 846, y + 66.5f, 30, p.text);
                if (theme >= 0 && host.Options()[theme].current == i) canvas.MaskCentered("check", 1748, y + 56, p.accent);
            }
        } else if (category.options) {
            std::vector<Row> rows;
            for (const int index : OptionsIn(host, category.options)) {
                const auto& option = host.Options()[index];
                const std::string& value = option.values[option.current];
                rows.push_back({option.label, value, IsOn(value), {}, index});
                if (int(rows.size()) - 1 == st.row && st.content) help = option.help;
            }
            DrawRows(x, rows, 714, 228, 1086, 80, selected, 8, st.content);
            if (help.empty() && !rows.empty() && st.content) help = host.Options()[rows[0].option].help;
            TextBlock(x, st.content ? help : "Changes are saved at once and apply when software starts.", 750, 228 + 8 * 80 + 40,
                      24, p.copy, 1008, 34, 3);
        } else if (std::string_view{category.name} == "Users") {
            canvas.Text("User Settings", 750, 260, 30, p.text);
            std::vector<Row> rows;
            for (int i = 0; i < int(users.size()); ++i)
                rows.push_back({users[i].name, i == host.CurrentUser() ? "Current user" : "", i == host.CurrentUser()});
            if (users.size() < 8) rows.push_back({"Add User", ""});
            for (int i = 0; i < int(users.size()); ++i) Avatar(x, users[i], 794 + i * 124.0f, 344, 44);
            DrawRows(x, rows, 714, 420, 1086, 80, selected, 6, st.content);
            TextBlock(x, "Each user keeps their own save data. The current user plays the next game.", 750, 920, 24, p.copy, 1008, 34, 1);
        } else {
            canvas.Text("System Information", 750, 258, 30, p.text);
            float y = 310;
            for (const auto& [label, value] : host.SystemInfo()) {
                canvas.Text(label, 750, y, 24, p.copy, Align::Left, 400);
                canvas.Text(value, 1764, y, 24, p.text, Align::Right, 640);
                y += 38;
            }
            std::vector<Row> rows{{"Copy logs to USB", ""}, {"Look for software again", ""}};
            DrawRows(x, rows, 714, std::max(y + 20, 660.0f), 1086, 80, selected, 2, st.content);
        }
        if (!st.message.empty()) canvas.Text(st.message, 750, 948, 24, p.accent, Align::Left, 1040);
        Footer(x, host, {{"circle", "Back"}, {"cross", "OK"}});
        break;
    }
    case Screen::Controllers: {
        Frame(x, "Controllers", "controller_small");
        const auto pads = host.Pads();
        for (int i = 0; i < 4; ++i) {
            const float left = 484 + 252 * i;
            if (pads[i].connected) {
                canvas.Mask("pad_big", left, 236, p.text);
                for (int light = 0; light < 4; ++light)
                    canvas.FillRect(left + 41 + light * 21, 368, 12, 12, light <= i ? p.accent : p.line);
                canvas.Text(pads[i].user, left + 80, 418, 24, p.copy, Align::Center, 230);
            } else {
                canvas.Text("-", left + 80, 316, 32, p.meta, Align::Center);
            }
            canvas.Text("Player " + std::to_string(i + 1), left + 80, 454, 24, pads[i].connected ? p.text : p.meta,
                        Align::Center);
        }
        std::vector<Row> rows{{"Change Order", "", false}, {"Test Controllers", "", false}};
        for (const int index : OptionsIn(host, "controllers")) {
            const auto& option = host.Options()[index];
            rows.push_back({option.label, option.values[option.current], false, {}, index});
        }
        DrawRows(x, rows, 420, 500, 1086, 70, st.controllers_row, 6, true);
        canvas.Text("L3 + R3 + Options on any controller: close the game and return here.", 456, 950, 22, p.copy,
                    Align::Left, 1040);
        if (!st.message.empty()) canvas.Text(st.message, 1500, 950, 22, p.accent, Align::Right, 600);
        Footer(x, host, {{"circle", "Back"}, {"cross", "OK"}});
        break;
    }
    case Screen::Users: {
        const bool add = st.user_page >= int(users.size());
        const User* user = add ? nullptr : &users[st.user_page];
        if (user) Avatar(x, *user, 179, 100, 38);
        canvas.Text(user ? user->name + "'s Page" : "Add User", user ? 238 : 96, 116, 40, p.title, Align::Left, 1500);
        canvas.HLine(96, 1824, 157, 1.5f, p.line);
        const int count = std::min(8, int(users.size()) + 1);
        for (int i = 0; i < count; ++i) {
            const float y = 228 + i * 78.0f;
            if (i == st.user_page) Plate(x, 156, y, 438, 76, true);
            canvas.Text(i < int(users.size()) ? users[i].name : "Add User", 198, y + 48, 28,
                        i == st.user_page ? p.accent : p.text, Align::Left, 372);
        }
        if (user) {
            AvatarSquare(x, *user, 714, 220, 312);
            canvas.FillRect(1062, 220, 738, 112, p.row);
            canvas.Text(user->name, 1098, 290, 40, p.text, Align::Left, 652);
            const bool current = st.user_page == host.CurrentUser();
            canvas.Text(current ? "Current User" : "Press Cross to use this user", 1098, 403, 30, current ? p.accent : p.copy);
            canvas.HLine(1062, 1800, 452, 1.5f, p.line);
            TextBlock(x, "Save data is kept separately for each user, in /data/EdenPS4/nand.", 1098, 495, 24, p.copy, 666, 36, 2);
        } else {
            canvas.Text("Add a user to Eden4.", 714, 350, 30, p.text);
            TextBlock(x, "Each user keeps their own save data. Up to 8 users.", 714, 420, 26, p.copy, 1086, 39, 3);
        }
        if (!st.message.empty()) canvas.Text(st.message, 714, 929, 24, p.accent, Align::Left, 1086);
        Footer(x, host, user ? Hints{{"options", "Edit Icon"}, {"triangle", "Rename"}, {"square", "Remove"},
                                     {"circle", "Back"}, {"cross", "Select"}}
                             : Hints{{"circle", "Back"}, {"cross", "OK"}});
        break;
    }
    case Screen::Avatar: {
        const User preview{users.empty() ? "Player" : users[std::clamp(st.user_page, 0, int(users.size()) - 1)].name,
                           st.avatar_color, st.avatar_symbol, nullptr};
        canvas.Text(st.avatar_stage == 0 ? "Choose an Icon" : "Choose a Background", 96, 112, 40, p.title);
        canvas.HLine(96, 1824, 157, 1.5f, p.line);
        canvas.VLine(541, 195, 914, 1.5f, p.line);
        AvatarSquare(x, preview, 180, 320, 314);
        canvas.Text(preview.name, 337, 700, 30, p.text, Align::Center, 340);
        for (int i = 0; i < AvatarCount; ++i) {
            const float left = 600 + (i % 6) * 190.0f, top = 260 + (i / 6) * 190.0f;
            const User cell{"", st.avatar_stage == 0 ? st.avatar_color : i, st.avatar_stage == 0 ? i : st.avatar_symbol, nullptr};
            AvatarSquare(x, cell, left, top, 150);
            if (i == st.avatar_selected) FocusRect(x, left, top, 150, 150);
        }
        Footer(x, host, {{"circle", "Back"}, {"cross", st.avatar_stage == 0 ? "Next" : "OK"}});
        break;
    }
    case Screen::About: {
        Frame(x, "About", "info_small");
        canvas.Text("Eden4", 420, 264, 40, p.title);
        const auto info = host.SystemInfo();
        if (!info.empty()) canvas.Text(info[0].second, 1500, 264, 30, p.text, Align::Right);
        canvas.HLine(384, 1536, 300, 1.5f, p.line);
        float y = 350;
        for (size_t i = 1; i < info.size(); ++i) {
            canvas.Text(info[i].first, 420, y, 24, p.copy);
            canvas.Text(info[i].second, 1500, y, 24, p.text, Align::Right, 700);
            y += 38;
        }
        y += 20;
        canvas.HLine(384, 1536, y, 1.5f, p.line);
        y += 50;
        const char* const credits[][2] = {
            {"Emulation", "Eden developers and contributors (GPL-3.0)"},
            {"PS4 port", "Eden4, on the DolphinPS4 runtime, Mesa RADV and OpenOrbis"},
            {"HOME menu layout", "Inspired by SwitchLauncher (kasobos67, GPL-3.0)"},
            {"Font", "Nunito (SIL Open Font License)"},
        };
        for (const auto& credit : credits) {
            canvas.Text(credit[0], 420, y, 22, p.meta);
            canvas.Text(credit[1], 1500, y, 24, p.text, Align::Right, 900);
            y += 40;
        }
        TextBlock(x, "Not affiliated with or endorsed by Nintendo or Sony. No console artwork, fonts or sounds are included.",
                  420, y + 30, 22, p.copy, 1080, 32, 2);
        if (!st.message.empty()) canvas.Text(st.message, 420, 940, 24, p.accent, Align::Left, 1100);
        Footer(x, host, {{"circle", "Back"}, {"cross", "Copy Logs to USB"}});
        break;
    }
    case Screen::Files: {
        Frame(x, "Game Files", "folder_small");
        canvas.Text("Sources", 112, 229, 26, p.text);
        canvas.VLine(784, 269, 916, 1, p.line, 0.6f);
        const auto sources = host.Sources();
        const int count = int(sources.size()) + 1;
        const int first = std::clamp(st.file_row - 6, 0, std::max(0, count - 7));
        canvas.PushClip(100, 270, 676, 650);
        for (int i = first; i < std::min(count, first + 7); ++i) {
            const float y = 282 + (i - first) * 88.0f;
            const bool here = i == st.file_row;
            Plate(x, 112, y, 628, 78, here);
            if (i == int(sources.size())) {
                canvas.MaskCentered("plus", 150, y + 39, here ? p.accent : p.text, 0.8f);
                canvas.Text("Add Folder", 190, y + 48, 26, here ? p.accent : p.text);
                continue;
            }
            const Source& source = sources[i];
            canvas.Text(source.name, 136, y + 36, 24, here ? p.accent : p.text, Align::Left, 470);
            const std::string detail = !source.available ? "Not connected"
                                       : !source.enabled  ? source.path
                                       : std::to_string(source.games) + (source.games == 1 ? " game - " : " games - ") + source.path;
            canvas.Text(detail, 136, y + 66, 20, p.meta, Align::Left, 470);
            canvas.Text(source.enabled ? "On" : "Off", 712, y + 48, 26, source.enabled ? p.accent : p.meta, Align::Right);
        }
        canvas.PopClip();
        canvas.Text("Game Files", 844, 326, 30, p.title);
        float y = 384;
        for (const auto& [label, value] : host.FilesInfo()) {
            canvas.Text(label, 860, y + 43, 24, p.text, Align::Left, 320);
            canvas.Text(value, 1778, y + 43, 24, p.accent, Align::Right, 570);
            canvas.HLine(844, 1796, y + 76, 1, p.line, 0.5f);
            y += 80;
        }
        TextBlock(x, "Games are found in each source's folder and one folder down (for example a USB drive's "
                     "switch folder). NSP, XCI, NRO and NSO files. Keys go in /data/EdenPS4/keys.",
                  844, y + 50, 22, p.copy, 952, 34, 4);
        if (!st.message.empty()) canvas.Text(st.message, 844, 900, 22, p.accent, Align::Left, 950);
        const bool add = st.file_row >= int(sources.size());
        Footer(x, host, add ? Hints{{"options", "Refresh"}, {"circle", "Back"}, {"cross", "Add"}}
                            : Hints{{"options", "Refresh"}, {"square", "Remove"}, {"triangle", "Copy Logs"},
                                    {"circle", "Back"}, {"cross", sources[st.file_row].enabled ? "Turn Off" : "Turn On"}});
        break;
    }
    }
    canvas.PopOpacity();

    // ---- modals ----
    if (st.modal != Modal::None) {
        const float in = Ease(st.modal_time / 0.15f);
        Dim(x, in);
        canvas.PushOpacity(in);
        switch (st.modal) {
        case Modal::GameInfo: {
            const Game& game = games[std::clamp(st.info_game, 0, int(games.size()) - 1)];
            Panel(x, 420, 170, 1080, 720);
            cover(game, 468, 248, 261, false);
            TextBlock(x, game.title, 768, 290, 36, p.title, 690, 46, 2);
            char id[32];
            snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(game.program_id));
            const std::pair<std::string, std::string> rows[] = {
                {"File", game.file}, {"Source", game.source.empty() ? "-" : game.source}, {"Size", FormatSize(game.size)},
                {"Title ID", game.program_id ? id : "-"}};
            float y = 560;
            for (const auto& [label, value] : rows) {
                canvas.Text(label, 468, y, 24, p.copy);
                canvas.Text(value, 1452, y, 24, p.text, Align::Right, 760);
                canvas.HLine(468, 1452, y + 22, 1, p.line, 0.6f);
                y += 64;
            }
            canvas.Text("Cross: Start      Circle: Back", 960, 856, 24, p.copy, Align::Center);
            break;
        }
        case Modal::Choice: {
            const auto& option = host.Options()[st.choice_option];
            const float h = 180 + option.values.size() * 84.0f;
            const float top = 540 - h / 2;
            Panel(x, 560, top, 800, h);
            canvas.Text(option.label, 608, top + 72, 32, p.title, Align::Left, 700);
            for (size_t i = 0; i < option.values.size(); ++i) {
                const float y = top + 120 + i * 84.0f;
                Plate(x, 592, y, 736, 76, int(i) == st.modal_row);
                canvas.Text(option.values[i], 632, y + 48, 30, int(i) == st.modal_row ? p.accent : p.text);
                if (int(i) == option.current) canvas.MaskCentered("check", 1290, y + 38, p.accent);
            }
            break;
        }
        case Modal::Power:
        case Modal::ConfirmRemove: {
            const bool power = st.modal == Modal::Power;
            Panel(x, 560, 330, 800, 420);
            const std::string name = users.empty() ? "" : users[std::clamp(st.user_page, 0, int(users.size()) - 1)].name;
            canvas.Text(power ? "Close Eden4?" : "Remove " + name + "?", 960, 430, 36, p.title, Align::Center, 720);
            TextBlock(x, power ? "You return to the PS4 home screen." :
                                 "The user disappears from Eden4. Their save files stay on the console.",
                      620, 500, 24, p.copy, 680, 34, 3);
            canvas.HLine(560, 1360, 650, 1.5f, p.line);
            canvas.VLine(960, 650, 750, 1.5f, p.line);
            const char* labels[] = {"Cancel", power ? "Close" : "Remove"};
            for (int i = 0; i < 2; ++i) {
                if (st.modal_row == i) Plate(x, 560 + i * 400.0f, 652, 400, 98, true);
                canvas.Text(labels[i], 760 + i * 400.0f, 712, 30, st.modal_row == i ? p.accent : p.text, Align::Center);
            }
            break;
        }
        case Modal::Keyboard: {
            const char* prompts[] = {"Enter a name for the new user.", "Enter a new name.",
                                     "Enter a folder, e.g. /mnt/usb0/switch"};
            canvas.Text(prompts[st.keyboard_mode], 960, 150, 32, {0xffffff}, Align::Center);
            canvas.FillRoundRect(360, 200, 1200, 110, 8, p.row);
            // Long paths: show their end.
            std::string shown = st.keyboard_text;
            while (canvas.Measure(shown, 40) > 1110) shown.erase(0, 1);
            canvas.Text(shown, 400, 272, 40, p.text);
            if (std::fmod(st.time, 1.0f) < 0.6f)
                canvas.FillRect(400 + canvas.Measure(shown, 40) + 4, 222, 3, 66, p.accent);
            canvas.Text(std::to_string(st.keyboard_text.size()) + "/" + (st.keyboard_mode == AddFolder ? "120" : "20"),
                        1530, 360, 24, {0xdddddd}, Align::Right);
            canvas.FillRect(0, 440, Canvas::Width, 640, p.base);
            const float key_w = 132, key_h = 92, gap = 8, left0 = (1920 - (10 * key_w + 9 * gap)) / 2;
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 10; ++c) {
                    const float left = left0 + c * (key_w + gap), top = 480 + r * (key_h + gap);
                    canvas.FillRoundRect(left, top, key_w, key_h, 6, p.row);
                    const char ch = KeyChar(r, c, st.keyboard_shift, st.keyboard_mode);
                    canvas.Text(std::string(1, ch), left + key_w / 2, top + 60, 36, p.text, Align::Center);
                    if (st.keyboard_row == r && st.keyboard_col == c) FocusRect(x, left, top, key_w, key_h);
                }
            }
            const struct { int col, span; const char* label; const char* mask; } specials[] = {
                {0, 2, "", "shift"}, {2, 4, "Space", nullptr}, {6, 2, "", "backspace"}, {8, 2, "OK", nullptr}};
            for (const auto& key : specials) {
                const float left = left0 + key.col * (key_w + gap), top = 480 + 4 * (key_h + gap);
                const float w = key.span * key_w + (key.span - 1) * gap;
                const bool ok = key.col == 8;
                canvas.FillRoundRect(left, top, w, key_h, 6, ok ? p.accent : p.row);
                if (key.mask) canvas.MaskCentered(key.mask, left + w / 2, top + key_h / 2,
                                                  key.col == 0 && st.keyboard_shift ? p.accent : p.text);
                else canvas.Text(key.label, left + w / 2, top + 60, 32, ok ? Color{0x1a1a1a} : p.text, Align::Center);
                if (st.keyboard_row == 4 && st.keyboard_col >= key.col && st.keyboard_col < key.col + key.span)
                    FocusRect(x, left, top, w, key_h);
            }
            Footer(x, host, {{"l1", "Shift"}, {"square", "Delete"}, {"options", "OK"}, {"circle", "Cancel"}, {"cross", "Enter"}});
            break;
        }
        case Modal::Order: {
            Panel(x, 300, 220, 1320, 620);
            canvas.Text("Change Order", 360, 300, 36, p.title);
            TextBlock(x, st.order_pick < 0 ? "Choose a player, then the player to swap with." :
                                              "Now choose the player to swap with Player " + std::to_string(st.order_pick + 1) + ".",
                      360, 360, 24, p.copy, 1200, 34, 2);
            const auto pads = host.Pads();
            for (int i = 0; i < 4; ++i) {
                const float left = 380 + i * 300.0f;
                if (st.order_pick == i) canvas.FillRect(left - 20, 440, 260, 300, p.row);
                if (st.order_cursor == i) FocusRect(x, left - 20, 440, 260, 300);
                canvas.Mask("pad_big", left + 20, 480, p.text, pads[i].connected ? 1.0f : 0.2f);
                canvas.Text("Player " + std::to_string(i + 1), left + 110, 650, 28, p.text, Align::Center);
                canvas.Text(pads[i].connected ? pads[i].user : "Not connected", left + 110, 696, 22, p.copy,
                            Align::Center, 240);
            }
            canvas.Text("Cross: Choose      Circle: Done", 960, 800, 24, p.copy, Align::Center);
            break;
        }
        case Modal::Test: {
            Panel(x, 120, 120, 1680, 800);
            canvas.Text("Test Controllers", 180, 200, 36, p.title);
            canvas.Text("Press buttons and move the sticks. Hold Circle to finish.", 180, 250, 24, p.copy);
            if (st.circle_hold > 0) canvas.FillRect(180, 270, 400 * std::min(1.0f, st.circle_hold), 6, p.accent);
            const auto pads = host.Pads();
            const std::pair<uint32_t, const char*> names[] = {
                {Cross, "Cross"}, {Circle, "Circle"}, {Square, "Square"}, {Triangle, "Triangle"},
                {L1, "L1"}, {R1, "R1"}, {L2, "L2"}, {R2, "R2"}, {L3, "L3"}, {R3, "R3"},
                {Up, "Up"}, {Down, "Down"}, {Left, "Left"}, {Right, "Right"}, {Options, "Options"}, {Touchpad, "Touchpad"}};
            for (int i = 0; i < 4; ++i) {
                const float left = 180 + i * 400.0f;
                canvas.Text("Player " + std::to_string(i + 1), left, 330, 30, pads[i].connected ? p.text : p.meta);
                if (!pads[i].connected) { canvas.Text("Not connected", left, 370, 22, p.meta); continue; }
                canvas.Text(pads[i].user, left, 370, 22, p.copy, Align::Left, 360);
                for (int k = 0; k < 16; ++k) {
                    const float bx = left + (k % 4) * 90.0f, by = 400 + (k / 4) * 54.0f;
                    const bool down = pads[i].buttons & names[k].first;
                    canvas.FillRoundRect(bx, by, 84, 44, 6, down ? p.accent : p.row);
                    canvas.Text(names[k].second, bx + 42, by + 30, 20, down ? Color{0x1a1a1a} : p.text, Align::Center, 80);
                }
                const float sticks[2][2] = {{pads[i].lx, pads[i].ly}, {pads[i].rx, pads[i].ry}};
                for (int k = 0; k < 2; ++k) {
                    const float cx = left + 80 + k * 190.0f, cy = 740;
                    canvas.StrokeCircle(cx, cy, 70, 3, p.line);
                    canvas.FillCircle(cx + sticks[k][0] * 60, cy + sticks[k][1] * 60, 16, p.accent);
                }
            }
            break;
        }
        case Modal::None:
            break;
        }
        canvas.PopOpacity();
    }
}

// PS4-style loading screen. Switch games carry no cover art beyond their 256x256 icon, so the
// backdrop is that icon blurred to fill the screen (cropped to 16:9, shrunk to 64x36, scaled up).
void Launcher::RenderLoading(Canvas& canvas, int game) {
    canvas.Clear({0x101010});
    const auto& games = host.Games();
    if (game < 0 || game >= int(games.size())) return;
    const auto& icon = games[game].home_icon;
    const bool has_icon = icon && !icon->Empty();
    if (has_icon) {
        Image crop{icon->width, icon->width * 9 / 16, {}};
        const int top = (icon->height - crop.height) / 2;
        crop.pixels.assign(icon->pixels.begin() + size_t(top) * icon->width,
                           icon->pixels.begin() + size_t(top + crop.height) * icon->width);
        canvas.DrawImage(Resize(Resize(crop, 64, 36), Canvas::Width, Canvas::Height), 0, 0);
        canvas.FillRect(0, 0, Canvas::Width, Canvas::Height, {0x000000}, 0.45f);
    }
    // Darker towards the bottom, where the text is.
    for (int band = 0; band < 24; ++band)
        canvas.FillRect(0, 600 + band * 20.0f, Canvas::Width, 20, {0x000000}, 0.02f * band);
    if (has_icon) {
        canvas.Shadow(768, 236, 384, 384, 0.6f);
        canvas.DrawImage(*icon, 768, 236);
    }
    canvas.Text(games[game].title, 960, has_icon ? 720 : 540, 40, {0xffffff}, Align::Center, 1500);
    // Loading indicator, bottom right: a ring with a bright quarter.
    const float cx = 1810, cy = 1000, r = 22;
    canvas.StrokeCircle(cx, cy, r, 5, {0xffffff}, 0.25f);
    for (int step = 0; step <= 24; ++step) {
        const float angle = -1.5708f + step * (1.5708f / 24);
        canvas.FillCircle(cx + std::cos(angle) * (r - 2.5f), cy + std::sin(angle) * (r - 2.5f), 2.6f, {0xffffff}, 0.9f);
    }
    canvas.Text("Loading", cx - 44, cy + 10, 28, {0xffffff}, Align::Right, 0, 0.9f);
}

} // namespace Ui
