// SPDX-License-Identifier: GPL-3.0-or-later
// The HOME menu: a Switch-style launcher drawn on a Ui::Canvas. It only uses the Host interface
// below, so it also builds on a PC for previews (ps4/ui-preview).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "yuzu_ps4/ui_canvas.h"

namespace Ui {

struct Game {
    std::string file;     // file name in the games folder
    std::string title;    // from the game's metadata (file name until it is read)
    std::string path;
    std::string source;   // name of the source it was found in
    uint64_t program_id{};
    uint64_t size{};
    std::shared_ptr<const Image> home_icon;    // 384 x 384
    std::shared_ptr<const Image> library_icon; // 261 x 261
};

struct User {
    std::string name;
    int color{};  // index into the avatar palette
    int symbol{}; // index into the avatar symbols
    std::shared_ptr<const Image> photo; // the user's own avatar picture, if any
};

struct Pad {
    bool connected{};
    std::string user;  // PS4 user name
    int type{};        // index into Host::ControllerTypes()
    uint32_t buttons{}; // live state (Button bits) for the controller test
    float lx{}, ly{}, rx{}, ry{};
};

/// A place games are scanned from: internal storage, a USB drive, or a folder the user added.
struct Source {
    std::string name, path;
    bool enabled{}, available{}, removable{};
    int games{};
};

/// One switch shown in System Settings (ps4.ini). `category` picks the settings page.
struct Option {
    std::string key, label, help, category;
    std::vector<std::string> values;
    int current{};
};

enum Button : uint32_t {
    Up = 1u << 0, Down = 1u << 1, Left = 1u << 2, Right = 1u << 3,
    Cross = 1u << 4, Circle = 1u << 5, Square = 1u << 6, Triangle = 1u << 7,
    Options = 1u << 8, L1 = 1u << 9, R1 = 1u << 10, L2 = 1u << 11, R2 = 1u << 12,
    L3 = 1u << 13, R3 = 1u << 14, Touchpad = 1u << 15,
};

enum class Sound { Focus, Select, Back, Launch, Error, Toggle, Open, Limit };

class Host {
public:
    virtual ~Host() = default;
    virtual const std::vector<Game>& Games() = 0; // HOME order: recently played first
    virtual bool Scanning() = 0;                  // metadata still being read
    virtual void Rescan() = 0;

    virtual std::vector<User> Users() = 0;
    virtual int CurrentUser() = 0;
    virtual void SetCurrentUser(int index) = 0;
    virtual bool AddUser(const std::string& name) = 0;
    virtual bool RenameUser(int index, const std::string& name) = 0;
    virtual bool RemoveUser(int index) = 0;
    virtual void SetUserLook(int index, int color, int symbol) = 0;

    virtual std::array<Pad, 4> Pads() = 0;
    virtual void SwapPads(int a, int b) = 0;

    virtual std::vector<Option>& Options() = 0;
    virtual void SetOption(int index, int value) = 0;

    virtual std::string Clock() = 0;
    virtual std::vector<std::pair<std::string, std::string>> SystemInfo() = 0;
    virtual std::vector<std::pair<std::string, std::string>> FilesInfo() = 0;
    virtual std::vector<Source> Sources() = 0;
    virtual void SetSourceEnabled(int index, bool enabled) = 0;
    virtual bool AddSource(const std::string& path) = 0;  // false if the folder cannot be opened
    virtual bool RemoveSource(int index) = 0;
    virtual std::string ExportLogs() = 0;
    virtual void Play(Sound sound) = 0;
};

/// What the frontend should do after a frame.
struct Request {
    enum Kind { None, Launch, Exit } kind{None};
    int game{-1};
};

class Launcher {
public:
    explicit Launcher(Host& host);
    ~Launcher();
    /// `pressed` has the buttons newly pressed this frame (with auto-repeat for directions),
    /// `held` the buttons down now. dt in seconds.
    Request Update(float dt, uint32_t pressed, uint32_t held);
    void Render(Canvas& canvas);
    /// A plain frame shown while a game starts.
    void RenderLoading(Canvas& canvas, int game);
    /// After a game: back to HOME with the played game first.
    void Returned(const std::string& message);

private:
    struct State;
    std::unique_ptr<State> s;
    Host& host;
};

} // namespace Ui
