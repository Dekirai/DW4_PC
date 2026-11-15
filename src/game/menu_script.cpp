#include "menu_script.hpp"

#include "console_menu.hpp"
#include "host_view.hpp"
#include "pad_family.hpp"
#include "screen2.hpp"

#include <imgui.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace dw4::game {

namespace {
double seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

SDL_Keycode key_code(const std::string& name) {
    if (name == "up") return SDLK_UP;
    if (name == "down") return SDLK_DOWN;
    if (name == "left") return SDLK_LEFT;
    if (name == "right") return SDLK_RIGHT;
    if (name == "enter") return SDLK_RETURN;
    if (name == "escape") return SDLK_ESCAPE;
    if (name == "backspace") return SDLK_BACKSPACE;
    if (name == "pageup") return SDLK_PAGEUP;
    if (name == "pagedown") return SDLK_PAGEDOWN;
    if (name == "home") return SDLK_HOME;
    if (name == "end") return SDLK_END;
    if (name == "delete") return SDLK_DELETE;
    return SDLK_UNKNOWN;
}

void push_key(SDL_Window* window, SDL_Keycode key, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.windowID = window ? SDL_GetWindowID(window) : 0;
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    e.key.down = down;
    SDL_PushEvent(&e);
}
} // namespace

MenuScript::~MenuScript() {
    if (joystick_) SDL_CloseJoystick(joystick_);
    if (pad_) SDL_DetachVirtualJoystick(pad_);
}

bool MenuScript::load(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot read " + file.string();
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    auto parsed = parse_menu_script(text, error);
    if (!parsed) return false;
    commands_ = std::move(*parsed);
    menu_log_keep(true); // ("expect log" reads the lines since its last expect)
    return true;
}

bool MenuScript::attach_pad(const std::string& family, std::string& error) {
    if (pad_) return true;
    const auto spec = virtual_pad_spec(family);
    if (!spec) {
        error = "DW4_MENU_PAD: unknown family " + family;
        return false;
    }
#ifndef SDL_PLATFORM_ANDROID
    // (A PC test run's window is hidden and never has the focus: SDL would
    // drop the pad's input as a background app's.)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#endif
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = spec->vendor;
    desc.product_id = spec->product;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = std::uint16_t(spec->buttons);
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
    desc.button_mask = (1u << 15) - 1u; // (SOUTH .. DPAD_RIGHT; the Thor's 15 and 16 stay unmapped)
    desc.name = spec->name;
    pad_ = SDL_AttachVirtualJoystick(&desc);
    if (!pad_) {
        error = std::string("the virtual pad: ") + SDL_GetError();
        return false;
    }
    joystick_ = SDL_OpenJoystick(pad_);
    family_ = family;
    return joystick_ != nullptr;
}

void MenuScript::fail(const MenuCommand& c, const std::string& why) {
    ++failures_;
    menu_log("menu script: FAILED line " + std::to_string(c.line) + ": " + why);
}

bool MenuScript::expect(Host& host, const MenuCommand& c) {
    HostView& view = host.view;
    const auto& a = c.args;
    if (c.name == "focus") return host.console.focus_row() == a[0] || (fail(c, "focus is " + host.console.focus_row()), false);
    if (c.name == "page") return host.console.page_path() == a[0] || (fail(c, "page is " + host.console.page_path()), false);
    if (c.name == "dialog") return host.console.dialog_kind() == a[0] || (fail(c, "dialog is '" + host.console.dialog_kind() + "'"), false);
    if (c.name == "open") return view.show_menu || (fail(c, "the menu is closed"), false);
    if (c.name == "closed") return !view.show_menu || (fail(c, "the menu is open"), false);
    if (c.name == "value") {
        const std::string want = a.size() > 1 ? a[1] : std::string();
        const std::string got = host.console.row_value(view, a[0]);
        return got == want || (fail(c, a[0] + " is '" + got + "'"), false);
    }
    if (c.name == "setting") {
        const auto dot = a[0].find('.');
        Settings copy = view.settings;
        copy.store();
        const auto got = copy.ini().get(a[0].substr(0, dot), dot == std::string::npos ? std::string() : a[0].substr(dot + 1));
        return (got && *got == a[1]) || (fail(c, a[0] + " is '" + got.value_or("(none)") + "'"), false);
    }
    if (c.name == "log") {
        for (const auto& line : menu_log_since(log_mark_))
            if (line.find(a[0]) != std::string::npos) return true;
        fail(c, "no log line with '" + a[0] + "'");
        return false;
    }
    return false;
}

void MenuScript::finger(Host& host, int action, float x, float y, bool lower) {
    HostView& view = host.view;
#ifdef SDL_PLATFORM_ANDROID
    if (lower) {
        // (The lower screen's queue, as Android's LowerScreen fills it; its scale.)
        const Screen2Display d = screen2_display();
        const float s = ui_scale_factor(view.settings.ui_scale, float(d.dpi > 0 ? d.dpi : 160) / 160.0f, true);
        screen2_touch(action, x * s, y * s);
        return;
    }
#else
    (void)lower;
#endif
    const float s = ui_scale();
    int w = 1, h = 1;
    if (view.window) SDL_GetWindowSize(view.window, &w, &h);
    const float px = x * s, py = y * s;
    const SDL_WindowID window = view.window ? SDL_GetWindowID(view.window) : 0;
    SDL_Event f{};
    f.type = action == 0 ? SDL_EVENT_FINGER_DOWN : action == 2 ? SDL_EVENT_FINGER_UP : SDL_EVENT_FINGER_MOTION;
    f.tfinger.timestamp = SDL_GetTicksNS();
    f.tfinger.touchID = 1;
    f.tfinger.fingerID = 1;
    f.tfinger.x = px / float(std::max(w, 1));
    f.tfinger.y = py / float(std::max(h, 1));
    f.tfinger.pressure = action == 2 ? 0.0f : 1.0f;
    f.tfinger.windowID = window;
    SDL_PushEvent(&f);
    // (Pushed fingers make no mouse events: ImGui's touch comes as SDL's
    // touch mouse.)
    SDL_Event m{};
    m.type = SDL_EVENT_MOUSE_MOTION;
    m.motion.timestamp = SDL_GetTicksNS();
    m.motion.windowID = window;
    m.motion.which = SDL_TOUCH_MOUSEID;
    m.motion.x = px;
    m.motion.y = py;
    SDL_PushEvent(&m);
    if (action == 0 || action == 2) {
        SDL_Event b{};
        b.type = action == 0 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        b.button.timestamp = SDL_GetTicksNS();
        b.button.windowID = window;
        b.button.which = SDL_TOUCH_MOUSEID;
        b.button.button = SDL_BUTTON_LEFT;
        b.button.down = action == 0;
        b.button.clicks = 1;
        b.button.x = px;
        b.button.y = py;
        SDL_PushEvent(&b);
    }
}

void MenuScript::update(Host& host) {
    HostView& view = host.view;
    // The virtual pad first: it reads as player 1's menu pad once the game opened it.
    if (pad_ && !pad_logged_) {
        for (SDL_Gamepad* pad : view.gamepads)
            if (SDL_GetGamepadID(pad) == pad_) {
                view.menu_pad_override = pad;
                pad_logged_ = true;
                menu_log(std::string("menu script: + pad ") + SDL_GetGamepadName(pad) + " " + family_id(view.family_of(pad)));
            }
        if (!pad_logged_ && ++passes_ < 240) return;
        passes_ = 0;
    }
    if (at_ >= commands_.size()) {
        if (!commands_.empty() && !host.finished) {
            host.finished = true;
            menu_log("menu script: done, " + std::to_string(failures_) + " failed");
        }
        return;
    }
    const MenuCommand& c = commands_[at_];
    const double now = seconds();
    bool done = false;
    const auto set_button = [&](const std::string& name, bool down) {
        if (!joystick_) return;
        static const std::pair<const char*, int> map[] = {
            {"a", SDL_GAMEPAD_BUTTON_SOUTH},        {"b", SDL_GAMEPAD_BUTTON_EAST},          {"x", SDL_GAMEPAD_BUTTON_WEST},
            {"y", SDL_GAMEPAD_BUTTON_NORTH},        {"select", SDL_GAMEPAD_BUTTON_BACK},     {"start", SDL_GAMEPAD_BUTTON_START},
            {"l1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"r1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"up", SDL_GAMEPAD_BUTTON_DPAD_UP},
            {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN}, {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},   {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
            {"thor-l2", kThorL2Button},             {"thor-r2", kThorR2Button}};
        if (name == "l2" || name == "r2") {
            SDL_SetJoystickVirtualAxis(joystick_, name == "l2" ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, down ? 32767 : 0);
            return;
        }
        for (const auto& [n, button] : map)
            if (name == n) SDL_SetJoystickVirtualButton(joystick_, button, down);
    };
    switch (c.kind) {
    case MenuCommand::Kind::wait:
        done = ++passes_ > c.count;
        break;
    case MenuCommand::Kind::waitms:
        if (phase_ == 0) {
            started_ = now;
            phase_ = 1;
        }
        done = now - started_ >= c.count / 1000.0;
        break;
    case MenuCommand::Kind::pad:
    case MenuCommand::Kind::stick:
        if (!joystick_) {
            fail(c, "no virtual pad");
            done = true;
            break;
        }
        if (c.kind == MenuCommand::Kind::pad && c.count < 0) {
            // ("pad a down" / "pad a up": the button stays as set.)
            set_button(c.name, c.count == -1);
            done = true;
            break;
        }
        if (phase_ == 0) {
            if (c.kind == MenuCommand::Kind::pad) set_button(c.name, true);
            else {
                SDL_SetJoystickVirtualAxis(joystick_, SDL_GAMEPAD_AXIS_LEFTX, std::int16_t(std::clamp(c.x0, -1.0f, 1.0f) * 32767.0f));
                SDL_SetJoystickVirtualAxis(joystick_, SDL_GAMEPAD_AXIS_LEFTY, std::int16_t(std::clamp(-c.y0, -1.0f, 1.0f) * 32767.0f));
            }
            phase_ = 1;
            passes_ = 0;
        } else if (phase_ == 1 && ++passes_ >= c.count) {
            if (c.kind == MenuCommand::Kind::pad) set_button(c.name, false);
            else {
                SDL_SetJoystickVirtualAxis(joystick_, SDL_GAMEPAD_AXIS_LEFTX, 0);
                SDL_SetJoystickVirtualAxis(joystick_, SDL_GAMEPAD_AXIS_LEFTY, 0);
            }
            phase_ = 2;
        } else if (phase_ == 2) {
            done = true;
        }
        break;
    case MenuCommand::Kind::key:
    case MenuCommand::Kind::back:
    case MenuCommand::Kind::f1: {
        const SDL_Keycode key = c.kind == MenuCommand::Kind::back ? SDLK_AC_BACK : c.kind == MenuCommand::Kind::f1 ? SDLK_F1 : key_code(c.name);
        push_key(view.window, key, phase_ == 0);
        done = phase_ == 1;
        phase_ = 1;
        break;
    }
    case MenuCommand::Kind::tap:
        // (Down, up, then a few passes: ImGui takes one button change per
        // frame, the lower screen draws every other pass.)
        if (phase_ == 0) finger(host, 0, c.x0, c.y0, c.lower);
        else if (phase_ == 1) finger(host, 2, c.x0, c.y0, c.lower);
        done = ++phase_ > 6;
        break;
    case MenuCommand::Kind::longpress:
        if (phase_ == 0) {
            finger(host, 0, c.x0, c.y0, c.lower);
            started_ = now;
            phase_ = 1;
        } else if (phase_ >= 2) {
            done = ++phase_ > 7;
        } else if (now - started_ >= 0.8) {
            finger(host, 2, c.x0, c.y0, c.lower);
            phase_ = 2;
        } else {
            finger(host, 1, c.x0, c.y0, c.lower);
        }
        break;
    case MenuCommand::Kind::drag:
        if (phase_ == 0) {
            finger(host, 0, c.x0, c.y0, c.lower);
            started_ = now;
            phase_ = 1;
        } else if (phase_ >= 2) {
            done = ++phase_ > 7;
        } else {
            const double t = c.count > 0 ? std::clamp((now - started_) / (c.count / 1000.0), 0.0, 1.0) : 1.0;
            const float x = c.x0 + (c.x1 - c.x0) * float(t), y = c.y0 + (c.y1 - c.y0) * float(t);
            finger(host, t >= 1.0 ? 2 : 1, x, y, c.lower);
            if (t >= 1.0) phase_ = 2;
        }
        break;
    case MenuCommand::Kind::type:
        if (ImGui::GetCurrentContext()) ImGui::GetIO().AddInputCharactersUTF8(c.name.c_str());
        done = true;
        break;
    case MenuCommand::Kind::shot:
        host.shot = c.name;
        done = true;
        break;
    case MenuCommand::Kind::screen2:
        view.menu_on_screen2 = view.show_menu;
        menu_log("menu script: the menu on the second screen");
        done = true;
        break;
    case MenuCommand::Kind::expect:
        expect(host, c);
        log_mark_ = menu_log_count();
        done = true;
        break;
    }
    if (done) {
        ++at_;
        phase_ = 0;
        passes_ = 0;
    }
}

} // namespace dw4::game
