#pragma once

// Test runs' display language changes (docs/L10N.md): DW4_L10N_SWITCH
// "F:code,F:code" (a change before each listed frame's step) and
// DW4_L10N_SWITCH_EVERY=N (every N frames between English and another
// language). Languages (language.hpp) applies them; header only, so the
// i18n tests check the schedule without the renderer.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dw4::game {

class LanguageSwitches {
public:
    // "F:code,F:code"; false (the items before the bad one stay) and
    // `error` for an item that is not FRAME:CODE.
    bool parse(std::string_view text, std::string* error = nullptr) {
        while (!text.empty()) {
            const auto comma = text.find(',');
            const auto item = text.substr(0, comma);
            text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
            const auto colon = item.find(':');
            if (colon == std::string_view::npos || colon == 0 || colon + 1 >= item.size()) {
                if (error) *error = "expected FRAME:CODE, got '" + std::string(item) + "'";
                return false;
            }
            int frame = 0;
            for (const char c : item.substr(0, colon)) {
                if (c < '0' || c > '9' || frame > 100000000) {
                    if (error) *error = "a frame is a number: '" + std::string(item) + "'";
                    return false;
                }
                frame = frame * 10 + (c - '0');
            }
            schedule_.emplace_back(frame, std::string(item.substr(colon + 1)));
        }
        return true;
    }

    // Every `frames` frames (frame 0 aside) between English and `other`.
    void every(int frames, std::string other) {
        every_ = frames;
        every_other_ = std::move(other);
    }
    const std::string& other() const { return every_other_; }

    struct Due {
        std::vector<std::string> codes; // the listed changes at the frame, in their order
        bool alternate = false;         // then the alternation's change (English <-> other)
    };
    // What to apply before `frame`'s step, once per frame: the host asks
    // every pass, and a pass without a step (online: waiting for sealed
    // ticks) asks again at the same frame. Before, each such pass switched
    // again (an even number of them undid the frame's change, and every
    // switch read the pack's files again).
    Due due(int frame) {
        Due d;
        if (frame == asked_) return d;
        asked_ = frame;
        for (const auto& [at, code] : schedule_)
            if (at == frame) d.codes.push_back(code);
        d.alternate = every_ > 0 && frame > 0 && frame % every_ == 0;
        return d;
    }

private:
    std::vector<std::pair<int, std::string>> schedule_;
    int every_ = 0;
    std::string every_other_;
    int asked_ = -1; // the frame asked last
};

} // namespace dw4::game
