#include "online_script.hpp"

#include "test_path.hpp"

#include "dw4/engine/managers.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/pad.hpp"

#include <algorithm>
#include <cctype>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

namespace dw4::game {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

std::uint32_t parse_buttons(const std::string& text, std::uint32_t& push) {
    namespace b = dw4::pad_button;
    namespace d = dw4::pad_direction;
    std::uint32_t bits = 0;
    push = 0;
    std::stringstream parts(text);
    for (std::string part; std::getline(parts, part, '+');) {
        if (part == "A") bits |= b::a;
        else if (part == "B") bits |= b::b;
        else if (part == "X") bits |= b::x;
        else if (part == "Y") bits |= b::y;
        else if (part == "START") bits |= b::start;
        else if (part == "Z") bits |= b::z;
        else if (part == "L") bits |= b::l;
        else if (part == "R") bits |= b::r;
        else if (part == "UP") bits |= b::up, push |= d::positive_y;
        else if (part == "DOWN") bits |= b::down, push |= d::negative_y;
        else if (part == "LEFT") bits |= b::left, push |= d::negative_x;
        else if (part == "RIGHT") bits |= b::right, push |= d::positive_x;
    }
    return bits;
}

float flat(const engine::Vec3f& a, const engine::Vec3f& b) { return std::hypot(a.x - b.x, a.z - b.z); }

engine::Vec3f position(const engine::ModelCtrlManager::ModelRam& m) {
    return {m.node->matrix(3, 0), m.node->matrix(3, 1), m.node->matrix(3, 2)};
}

const engine::ModelCtrlManager::ModelRam* own_player(engine::Engine& engine, int slot) {
    const auto* models = engine.get<engine::ModelCtrlManager>();
    const auto* p = models ? models->player(slot) : nullptr;
    return p && p->node ? p : nullptr;
}

bool defeated(engine::Engine& engine, int slot) {
    const auto* sys = engine.get<engine::GameSystem>();
    if (!own_player(engine, slot)) return true;
    return sys && sys->players[std::size_t(slot & 3)].hp <= 0;
}

bool playing(engine::Engine& engine, const OnlineScriptFacts& facts) {
    return engine.presenting() && !engine.loading() && (facts.mode == "solo" || facts.mode == "sealed" || facts.mode == "offline");
}

} // namespace

bool OnlineScript::known_action(std::string_view name) {
    static constexpr std::string_view names[] = {"campaign", "errand", "idle", "talk",   "gate",   "goto", "route", "follow", "mat",
                                                 "explore",  "fight",  "wander", "die", "hold", "press", "giveup", "disk", "log", "shot"};
    return std::find(std::begin(names), std::end(names), name) != std::end(names);
}

bool OnlineScript::known_condition(std::string_view key) {
    static constexpr std::string_view names[] = {"stage", "present", "frames",  "dead",   "alive",    "control",     "mode",
                                                 "slot",  "sealed",  "time",    "epoch",  "answered", "hp",          "prompt",
                                                 "asking", "asked",  "partnerdead", "apart", "near", "stagechange", "level", "ghosts"};
    if (key.starts_with("onmat:") || key.starts_with("onme:") || key.starts_with("offme:")) return key.find(':') + 1 < key.size();
    if (key.starts_with("flag:")) return key.size() > 5 && key.find_first_not_of("0123456789", 5) == std::string_view::npos;
    if (key.starts_with("roomflag:")) return key.size() > 9 && key.find_first_not_of("0123456789", 9) == std::string_view::npos;
    return std::find(std::begin(names), std::end(names), key) != std::end(names);
}

bool OnlineScript::load(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file);
    if (!in) {
        error = "cannot read " + file.string();
        return false;
    }
    int number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        // A comment starts with '#' at the line's start or after a blank (a
        // model's "#N" belongs to its name: talk:n_digilfm#1).
        for (std::size_t at = 0; at < line.size(); ++at)
            if (line[at] == '#' && (at == 0 || line[at - 1] == ' ' || line[at - 1] == '\t')) {
                line.resize(at);
                break;
            }
        std::istringstream words(line);
        Step step;
        step.line = number;
        if (!(words >> step.action)) continue;
        if (!known_action(step.action)) {
            error = file.string() + ":" + std::to_string(number) + ": unknown action " + step.action;
            steps_.clear();
            return false;
        }
        if (step.action == "shot") {
            // One name of A-Z a-z 0-9 _ (a file name), no conditions.
            std::string name;
            words >> name;
            if (name.empty() || !std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; })) {
                error = file.string() + ":" + std::to_string(number) + ": shot needs a name of A-Z a-z 0-9 _";
                steps_.clear();
                return false;
            }
            step.args = {name};
            steps_.push_back(std::move(step));
            continue;
        }
        if (step.action == "log") {
            // The rest of the line is the text (no conditions).
            std::string text;
            std::getline(words, text);
            text.erase(0, text.find_first_not_of(" \t"));
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.pop_back();
            step.args = {text};
            steps_.push_back(std::move(step));
            continue;
        }
        bool until = false, fail = false;
        for (std::string word; words >> word;) {
            if (word == "until") {
                until = true;
                fail = false;
                step.until.emplace_back();
                continue;
            }
            if (word == "fail") {
                fail = true;
                continue;
            }
            if (!until && !fail) {
                step.args.push_back(word);
                continue;
            }
            if (word == "or" && !fail) {
                // Another group: the step ends when every condition of one group holds.
                step.until.emplace_back();
                continue;
            }
            std::stringstream conditions(word);
            for (std::string text; std::getline(conditions, text, ',');) {
                if (text.empty()) continue;
                Condition c;
                const auto at = text.find_first_of("=<>");
                c.key = text.substr(0, at);
                if (at != std::string::npos) {
                    std::size_t end = at + 1;
                    if (end < text.size() && text[end] == '=') ++end;
                    c.op = text.substr(at, end - at);
                    c.text = text.substr(end);
                    c.value = std::atof(c.text.c_str());
                }
                if (!known_condition(c.key)) {
                    error = file.string() + ":" + std::to_string(number) + ": unknown condition " + c.key;
                    steps_.clear();
                    return false;
                }
                (fail ? step.fail : step.until.back()).push_back(c);
            }
        }
        std::erase_if(step.until, [](const auto& group) { return group.empty(); });
        steps_.push_back(std::move(step));
    }
    if (steps_.empty()) error = file.string() + " has no steps";
    return !steps_.empty();
}

bool OnlineScript::holds(engine::Engine& engine, const OnlineScriptFacts& facts, const Condition& c) const {
    const auto* models = engine.get<engine::ModelCtrlManager>();
    const auto* me = own_player(engine, facts.own_slot);
    auto compare = [&](double v) {
        if (c.op == "=" || c.op == "==") return v == c.value;
        if (c.op == ">=") return v >= c.value;
        if (c.op == "<=") return v <= c.value;
        if (c.op == ">") return v > c.value;
        if (c.op == "<") return v < c.value;
        return false;
    };
    auto others = [&](auto&& each) {
        for (int s = 0; s < 4; ++s)
            if (s != facts.own_slot && (facts.present & (1u << s))) each(s, models ? models->player(s) : nullptr);
    };
    if (c.key == "stage") return playing(engine, facts) && lower(engine.stage_name()) == lower(c.text);
    if (c.key == "stagechange") return lower(engine.stage_name()) != step_stage_;
    if (c.key.rfind("flag:", 0) == 0) {
        const auto found = engine.flags().find(std::atoi(c.key.c_str() + 5));
        return compare(found != engine.flags().end() ? found->second : 0);
    }
    if (c.key.rfind("roomflag:", 0) == 0) {
        const auto flag = std::size_t(std::atoi(c.key.c_str() + 9));
        return compare(facts.room_story && flag < facts.room_story->flags.size() ? facts.room_story->flags[flag] : 0);
    }
    if (c.key == "present") return compare(std::popcount(unsigned(facts.present)));
    if (c.key == "ghosts") return compare(facts.ghosts);
    if (c.key == "frames") return compare(step_frames_);
    if (c.key == "dead") return playing(engine, facts) && defeated(engine, facts.own_slot);
    if (c.key == "alive") return playing(engine, facts) && !defeated(engine, facts.own_slot);
    if (c.key == "control")
        return playing(engine, facts) && me && me->control_enabled && me->seq_name.rfind("SQ_DEMO", 0) != 0 && engine.demo().running() == 0;
    if (c.key == "mode") return facts.mode == c.text;
    if (c.key == "slot") return compare(facts.own_slot);
    if (c.key == "sealed") return compare(sealed_ticks_);
    if (c.key == "time") return compare(std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count());
    if (c.key == "epoch")
        return compare(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count());
    if (c.key == "answered") return compare(answers_);
    if (c.key == "hp") {
        const auto* sys = engine.get<engine::GameSystem>();
        return playing(engine, facts) && compare(me && sys ? sys->players[std::size_t(facts.own_slot & 3)].hp : 0);
    }
    if (c.key == "level") {
        // This machine's own character's level (a DISSOLVE that kept the partner's showed the partner's).
        const auto* sys = engine.get<engine::GameSystem>();
        return playing(engine, facts) && sys && compare(sys->players[std::size_t(facts.own_slot & 3)].stats.level);
    }
    if (c.key == "prompt") return compare(engine.exit_prompt().active ? 1 : 0);
    if (c.key == "asking") return compare(engine.exit_prompt().active ? engine.exit_prompt().asking : -1);
    if (c.key.rfind("onmat:", 0) == 0) {
        const auto* mats = engine.get<engine::MatCtrlManager>();
        const std::uint32_t on = mats ? mats->players_on(c.key.substr(6)) : 0;
        return playing(engine, facts) && compare(std::popcount(on & facts.present));
    }
    if (c.key.rfind("onme:", 0) == 0 || c.key.rfind("offme:", 0) == 0) {
        const bool want_on = c.key[1] == 'n';
        const auto* mats = engine.get<engine::MatCtrlManager>();
        const std::uint32_t on = mats ? mats->players_on(c.key.substr(c.key.find(':') + 1)) : 0;
        return playing(engine, facts) && ((on & (1u << facts.own_slot)) != 0) == want_on;
    }
    if (c.key == "asked") {
        const auto& prompt = engine.exit_prompt();
        return prompt.active && prompt.asking == facts.own_slot;
    }
    if (c.key == "partnerdead") {
        bool any = false, all = true;
        others([&](int, const engine::ModelCtrlManager::ModelRam* p) {
            any = true;
            all = all && (!p || !p->node);
        });
        return playing(engine, facts) && any && all;
    }
    if (c.key == "apart" || c.key == "near") {
        if (!me) return false;
        bool all_far = true, any_near = false, any = false;
        others([&](int, const engine::ModelCtrlManager::ModelRam* p) {
            if (!p || !p->node) return;
            any = true;
            const float d = flat(position(*me), position(*p));
            all_far = all_far && d > c.value;
            any_near = any_near || d < c.value;
        });
        return c.key == "apart" ? any && all_far : any_near;
    }
    std::printf("online-script: unknown condition %s\n", c.key.c_str());
    return false;
}

bool OnlineScript::walk_to(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, float x, float z, float close, int frame,
                           Output& out, bool direct) {
    const engine::Vec3f me = position(player);
    const engine::Vec3f goal{x, me.y, z};
    if (flat(me, goal) <= close) return true;
    if (direct) {
        out.walk = true;
        out.x = x;
        out.z = z;
        out.reach = 4.0f;
        return false;
    }
    // A route over the stage collision (as SEEK mat: in game_main), replanned
    // when the goal moves, every three seconds or when it is used up.
    if (auto* hitland = engine.get<engine::HitLandManager>();
        hitland && flat(goal, me) > 20.0f && (flat(goal, route_goal_) > 15.0f || frame - route_frame_ > 180 || route_next_ >= route_.size())) {
        const WithoutLimitCircle without_limit(*hitland);
        route_ = find_walk_path(hitland->world(), me, goal, hitland->delivery_flags(player.info_name, player.index));
        route_next_ = 0;
        route_goal_ = goal;
        route_frame_ = frame;
    }
    while (route_next_ < route_.size() && flat(route_[route_next_], me) < 5.0f) ++route_next_;
    out.walk = true;
    out.x = x;
    out.z = z;
    out.reach = 4.0f;
    if (flat(goal, me) > 20.0f && route_next_ < route_.size()) {
        out.x = route_[route_next_].x;
        out.z = route_[route_next_].z;
    }
    return false;
}

void OnlineScript::trace_prompt(engine::Engine& engine, const OnlineScriptFacts& facts) {
    const auto& prompt = engine.exit_prompt();
    char line[200];
    std::snprintf(line, sizeof line, "active=%d owner=%d asking=%d yes=%x stays=%x to=%s", prompt.active ? 1 : 0, prompt.owner, prompt.asking,
                  unsigned(prompt.yes), unsigned(prompt.stays), prompt.stage.c_str());
    if (line != traced_prompt_) {
        traced_prompt_ = line;
        std::printf("online-script: prompt %s (frame %d, slot %d, tick %u)\n", line, facts.frame, facts.own_slot, facts.tick);
        std::fflush(stdout);
    }
}

OnlineScript::Output OnlineScript::act(engine::Engine& engine, const OnlineScriptFacts& facts, const Step& step) {
    Output out;
    auto* models = engine.get<engine::ModelCtrlManager>();
    const auto* me = own_player(engine, facts.own_slot);
    const int frame = facts.frame;
    auto arg = [&](std::size_t i, float fallback = 0) { return i < step.args.size() ? float(std::atof(step.args[i].c_str())) : fallback; };
    const bool control = me && me->control_enabled && me->seq_name.rfind("SQ_DEMO", 0) != 0;
    // The nearest living enemy (within r of (cx, cz) when r > 0).
    auto nearest_enemy = [&](float cx, float cz, float r) -> const engine::ModelCtrlManager::ModelRam* {
        if (!models || !me) return nullptr;
        const engine::ModelCtrlManager::ModelRam* best = nullptr;
        float best_d = 1e30f;
        for (const auto& e : models->rams()) {
            if (!e->generated || !e->node || e->category != "ENEMY" || e->hp <= 0) continue;
            const auto at = position(*e);
            if (r > 0 && std::hypot(at.x - cx, at.z - cz) > r) continue;
            const float d = flat(at, position(*me));
            if (d < best_d) best_d = d, best = e.get();
        }
        return best;
    };
    if (step.action == "campaign" || step.action == "errand") {
        // (errand: the campaign bot's step ACTION in STAGE, by default the
        // stage the step began in; campaign: its own objectives.)
        if (!campaign_) campaign_ = std::make_unique<CampaignBot>();
        if (step.action == "errand") campaign_->set_errand(step.args.size() > 1 ? step.args[1] : step_stage_, step.args.empty() ? "" : step.args[0]);
        else campaign_->set_errand("", "");
        const auto c = campaign_->update(engine, models ? models->player(0) : nullptr, frame);
        out.buttons = c.buttons;
        out.walk = c.walk;
        out.x = c.x;
        out.z = c.z;
        out.reach = c.reach;
        return out;
    }
    if (step.action == "idle" || !playing(engine, facts)) return out;
    if (step.action == "talk") {
        // Talk windows and messages of this member: A now and then while its
        // player has no control.
        if (!me || !me->control_enabled || me->seq_name.rfind("SQ_DEMO", 0) == 0)
            if (frame % 40 < 3) out.buttons = dw4::pad_button::a;
        return out;
    }
    if (step.action == "gate") {
        // A lobby gate (the original's area menu, not the exit port): onto the
        // mat, then N times down in the menu and A.
        if (!me) return out;
        const bool menu = !me->control_enabled || me->seq_name.rfind("SQ_DEMO", 0) == 0;
        if (!menu) {
            menu_at_ = -1;
            const auto* mats = engine.get<engine::MatCtrlManager>();
            if (const auto centre = mats && !step.args.empty() ? mats->mat_centre(step.args[0]) : std::nullopt)
                walk_to(engine, *me, centre->x, centre->z, 2.0f, frame, out, flat(*centre, position(*me)) < 120.0f);
            return out;
        }
        if (menu_at_ < 0) menu_at_ = frame;
        const int t = frame - menu_at_, downs = int(arg(1));
        if (t >= 40 && t < 40 + 20 * downs && (t - 40) % 20 < 3) out.buttons = dw4::pad_button::down;
        else if (t >= 50 + 20 * downs && t < 53 + 20 * downs) out.buttons = dw4::pad_button::a;
        return out;
    }
    if (step.action == "hold" || step.action == "press") {
        out.buttons = parse_buttons(step.args.empty() ? std::string() : step.args[0], out.push);
        return out;
    }
    if (step.action == "giveup") {
        if (defeated(engine, facts.own_slot)) out.buttons = dw4::pad_button::start;
        return out;
    }
    if (!me) return out;
    if (step.action == "goto") {
        if (control && walk_to(engine, *me, arg(0), arg(1), arg(2, 4.0f), frame, out)) step_reached_ = true;
        return out;
    }
    // Strikes an enemy within `r` of this member (true: busy with it).
    auto strike_near = [&](float r) {
        const auto* enemy = nearest_enemy(position(*me).x, position(*me).z, r);
        if (!enemy || !control) return false;
        const auto at = position(*enemy);
        if (flat(at, position(*me)) > 10.0f) walk_to(engine, *me, at.x, at.z, 10.0f, frame, out);
        if (frame % 20 < 3) out.buttons |= dw4::pad_button::a;
        return true;
    };
    if (step.action == "route") {
        // Waypoints "X,Z" one after the other, straight from one to the next
        // (within 6 units counts as there), striking the enemies within 15
        // units on the way; done at the last one. Held up for 40 frames (a
        // bush or a crate in the way), it strikes ahead as it walks.
        if (!control || strike_near(15.0f)) return out;
        if (frame - stuck_frame_ >= 40) {
            stuck_ = flat(position(*me), stuck_at_) < 3.0f;
            stuck_at_ = position(*me);
            stuck_frame_ = frame;
        }
        while (waypoint_ < step.args.size()) {
            const auto& w = step.args[waypoint_];
            const auto comma = w.find(',');
            const float x = float(std::atof(w.c_str())), z = comma == std::string::npos ? 0.0f : float(std::atof(w.c_str() + comma + 1));
            if (!walk_to(engine, *me, x, z, 6.0f, frame, out, true)) {
                if (stuck_ && frame % 15 < 3) out.buttons |= dw4::pad_button::a;
                return out;
            }
            ++waypoint_;
            route_.clear();
            route_next_ = 0;
            route_goal_ = {1e9f, 0, 1e9f};
        }
        step_reached_ = true;
        return out;
    }
    if (step.action == "follow") {
        // Within D (default 40) of the nearest other present member, striking
        // the enemies that come within 15 units.
        if (!control || strike_near(15.0f)) return out;
        const engine::ModelCtrlManager::ModelRam* lead = nullptr;
        float best = 1e30f;
        for (int s = 0; s < 4; ++s)
            if (s != facts.own_slot && (facts.present & (1u << s)))
                if (const auto* p = own_player(engine, s); p && flat(position(*p), position(*me)) < best)
                    best = flat(position(*p), position(*me)), lead = p;
        if (lead) walk_to(engine, *me, position(*lead).x, position(*lead).z, arg(0, 40.0f), frame, out);
        return out;
    }
    // An exit prompt asking this member: yes (A on the first choice) or no
    // (the cursor down, then A). "yes N": the owner of the prompt waits with
    // its yes until N present members stand on the mat (the others are asked
    // only when they are on it at that moment, SEAMLESS.md 5.2).
    auto answer = [&](bool yes, int wait_for, const std::string& mat) {
        const auto& prompt = engine.exit_prompt();
        if (yes && wait_for > 1 && prompt.owner == facts.own_slot) {
            const auto* mats = engine.get<engine::MatCtrlManager>();
            const std::uint32_t on = mats ? mats->players_on(mat) & facts.present : 0;
            if (std::popcount(on) < wait_for) return;
        }
        int phase = frame - answer_at_;
        if (phase > 40) answer_at_ = frame, phase = 0;
        if (yes) {
            if (phase < 3) out.buttons = dw4::pad_button::a;
            if (phase == 0) ++answers_;
        } else if (phase < 4) {
            out.buttons = dw4::pad_button::down;
        } else if (phase >= 16 && phase < 19) {
            out.buttons = dw4::pad_button::a;
            if (phase == 16) ++answers_;
        }
    };
    const auto& prompt_view = engine.exit_prompt();
    const bool asked_now = prompt_view.active && prompt_view.asking == facts.own_slot;
    if (step.action == "mat") {
        const bool yes = step.args.size() < 2 || step.args[1] != "no";
        const int wait_for = step.args.size() > 2 ? std::atoi(step.args[2].c_str()) : 0;
        if (asked_now) {
            answer(yes, wait_for, step.args.empty() ? std::string() : step.args[0]);
            return out;
        }
        // A window of this member (talks, menus, a solo instance's original
        // exit prompt): A now and then.
        if (!control) {
            if (frame % 40 < 3) out.buttons = dw4::pad_button::a;
            return out;
        }
        const auto* mats = engine.get<engine::MatCtrlManager>();
        const auto centre = mats && !step.args.empty() ? mats->mat_centre(step.args[0]) : std::nullopt;
        // (Straight at a mat within 120 units: the planned route of the stage
        // collision ran into walls next to stag0101's matz02.)
        if (centre) walk_to(engine, *me, centre->x, centre->z, 2.0f, frame, out, flat(*centre, position(*me)) < 120.0f);
        return out;
    }
    if (step.action == "fight" || step.action == "wander") {
        const bool wander = step.action == "wander";
        const float cx = arg(0), cz = arg(1), r = arg(2);
        const auto* enemy = nearest_enemy(wander ? position(*me).x : cx, wander ? position(*me).z : cz, wander ? 25.0f : r);
        if (enemy && control) {
            const auto at = position(*enemy);
            if (flat(at, position(*me)) > 12.0f) walk_to(engine, *me, at.x, at.z, 12.0f, frame, out);
            if (flat(at, position(*me)) < 20.0f && frame % 20 < 3) out.buttons |= dw4::pad_button::a;
            return out;
        }
        if (wander && control) {
            // Eight points round the centre, one after the other.
            const float angle = float(wander_index_ % 8) * 0.785398f;
            const float tx = cx + r * std::sin(angle), tz = cz + r * std::cos(angle);
            if (walk_to(engine, *me, tx, tz, 6.0f, frame, out) || step_frames_ % 900 == 899) ++wander_index_;
        } else if (control && r > 0) {
            walk_to(engine, *me, cx, cz, 6.0f, frame, out);
        }
        return out;
    }
    if (step.action == "explore") {
        // The ExploreBot (test_path.hpp) plays through the stage to the mat,
        // striking the enemies that come near; an exit prompt that asks this
        // member is answered as in "mat".
        const bool yes = step.args.size() < 2 || step.args[1] != "no";
        if (asked_now) {
            answer(yes, step.args.size() > 2 ? std::atoi(step.args[2].c_str()) : 0, step.args.empty() ? std::string() : step.args[0]);
            return out;
        }
        if (!explorer_) {
            explorer_ = std::make_unique<ExploreBot>();
            explorer_->set_transit(true);
        }
        const auto d = explorer_->update(engine, *me, step.args.empty() ? std::string() : step.args[0], frame);
        out.walk = control;
        out.x = d.x;
        out.z = d.z;
        out.reach = d.reach;
        if (d.attack) {
            if (d.jump_attack) {
                if (frame % 40 < 3) out.buttons |= dw4::pad_button::x;
                else if (frame % 40 >= 10 && frame % 40 < 13) out.buttons |= dw4::pad_button::a;
            } else if (frame % (d.fast_attack ? 10 : 20) < 3) {
                out.buttons |= dw4::pad_button::a;
            }
        }
        if ((d.confirm && frame % 30 < 3) || (d.pick_up && frame % 20 < 3) || d.press_a) out.buttons |= dw4::pad_button::a;
        if (d.press_b) out.buttons |= dw4::pad_button::b;
        if (d.jump) out.buttons |= dw4::pad_button::x;
        return out;
    }
    if (step.action == "disk") {
        // A disk of the L panel (the campaign driver's start_disk): L, the
        // cursor up to the first row, down to the disk's row, A, L.
        const auto* sys = engine.get<engine::GameSystem>();
        if (!sys) return out;
        const auto& slot = sys->players[std::size_t(facts.own_slot & 3)];
        const int record = int(arg(0, 8));
        if (disk_line_ < 0) {
            int rows = 0, line = -1;
            for (std::size_t i = 0; i + 2 < slot.disks.size(); ++i) {
                if (slot.disks[i].count <= 0) continue;
                if (slot.disks[i].record == record && line < 0) line = rows;
                ++rows;
            }
            std::printf("online-script: disk %d in row %d of %d (frame %d)\n", record, line, rows, frame);
            if (line < 0) return out;
            disk_line_ = line;
            disk_ups_ = std::max(rows - 1, 0);
            disk_at_ = frame;
        }
        const int t = frame - disk_at_;
        const int downs_at = 4 + 2 * disk_ups_, a_at = downs_at + 2 * disk_line_ + 2;
        if (t < 1) out.buttons = dw4::pad_button::l;
        else if (t >= 4 && t < downs_at && t % 2 == 0) out.buttons = dw4::pad_button::up;
        else if (t >= downs_at && t < downs_at + 2 * disk_line_ && t % 2 == 0) out.buttons = dw4::pad_button::down;
        else if (t == a_at) out.buttons = dw4::pad_button::a;
        else if (t == a_at + 24 && me->panel_open) out.buttons = dw4::pad_button::l;
        else if (t > a_at + 90) disk_line_ = -1; // (again, if the step goes on)
        return out;
    }
    if (step.action == "die") {
        if (const auto* enemy = nearest_enemy(0, 0, 0); enemy && control) {
            const auto at = position(*enemy);
            walk_to(engine, *me, at.x, at.z, 3.0f, frame, out);
        }
        return out;
    }
    std::printf("online-script: unknown action %s (line %d)\n", step.action.c_str(), step.line);
    return out;
}

OnlineScript::Output OnlineScript::update(engine::Engine& engine, const OnlineScriptFacts& facts) {
    const std::string stage = lower(engine.stage_name());
    if (stage != last_stage_) {
        last_stage_ = stage;
        sealed_ticks_ = 0;
        std::printf("online-script: stage %s (frame %d)\n", stage.c_str(), facts.frame);
        std::fflush(stdout);
    }
    if (facts.mode == "sealed") ++sealed_ticks_;
    trace_prompt(engine, facts);
    std::string shot; // (a "shot" step that ended in this update)
    for (int guard = 0; guard < 16 && cursor_ < steps_.size(); ++guard) {
        const auto& step = steps_[cursor_];
        if (!step_started_) {
            step_started_ = true;
            step_frames_ = 0;
            route_.clear();
            route_next_ = 0;
            explorer_.reset();
            disk_line_ = -1;
            answers_ = 0;
            waypoint_ = 0;
            step_reached_ = false;
            step_stage_ = stage;
            failed_ = false;
            route_goal_ = {1e9f, 0, 1e9f};
            const auto* me = own_player(engine, facts.own_slot);
            std::printf("online-script: step %zu (line %d) %s%s (frame %d, stage %s, slot %d, present %x, mode %s, tick %u, at %.0f,%.0f)\n",
                        cursor_ + 1, step.line, step.action.c_str(),
                        (step.action == "log" || step.action == "shot") && !step.args.empty() ? (": " + step.args[0]).c_str() : "", facts.frame, stage.c_str(),
                        facts.own_slot, unsigned(facts.present), facts.mode.c_str(), facts.tick, me ? me->node->matrix(3, 0) : 0.0f,
                        me ? me->node->matrix(3, 2) : 0.0f);
            std::fflush(stdout);
        }
        bool done = false;
        if (!step.until.empty()) {
            done = std::any_of(step.until.begin(), step.until.end(), [&](const std::vector<Condition>& group) {
                return std::all_of(group.begin(), group.end(), [&](const Condition& c) { return holds(engine, facts, c); });
            });
        } else if (step.action == "log") {
            done = true;
        } else if (step.action == "shot") {
            done = true;
            shot = step.args.empty() ? std::string() : step.args[0];
        } else if (step.action == "press") {
            done = step_frames_ >= 6;
        } else if (step.action == "hold") {
            done = step_frames_ >= int(step.args.size() > 1 ? std::atoi(step.args[1].c_str()) : 6);
        } else if (step.action == "goto" || step.action == "route") {
            done = step_reached_;
        }
        if (!done && !failed_)
            for (const auto& c : step.fail)
                if (holds(engine, facts, c)) {
                    // A check of the scenario: tools/online_test counts every FAIL line.
                    failed_ = true;
                    std::printf("online-script: FAIL (line %d) %s%s%s (frame %d, stage %s, slot %d, present %x, mode %s, tick %u)\n", step.line,
                                c.key.c_str(), c.op.c_str(), c.text.c_str(), facts.frame, stage.c_str(), facts.own_slot, unsigned(facts.present),
                                facts.mode.c_str(), facts.tick);
                    std::fflush(stdout);
                    break;
                }
        if (!done) {
            ++step_frames_;
            // Where this member is, every five seconds (test traces;
            // DW4_ONLINE_SCRIPT_EVERY=N: every N frames).
            static const int every = std::getenv("DW4_ONLINE_SCRIPT_EVERY") ? std::max(1, std::atoi(std::getenv("DW4_ONLINE_SCRIPT_EVERY"))) : 300;
            if (step_frames_ % every == 1)
                if (const auto* me = own_player(engine, facts.own_slot)) {
                    const auto* sys = engine.get<engine::GameSystem>();
                    std::string extra;
                    if (step.action == "mat" && !step.args.empty())
                        if (const auto* mats = engine.get<engine::MatCtrlManager>())
                            if (const auto c = mats->mat_centre(step.args[0]))
                                extra = " mat " + step.args[0] + " at " + std::to_string(int(c->x)) + "," + std::to_string(int(c->z));
                    if (every != 300)
                        if (const auto* models = engine.get<engine::ModelCtrlManager>())
                            for (const auto& m : models->rams())
                                if (m->node && m.get() != me && flat(position(*m), position(*me)) < 30.0f)
                                    extra += " [" + m->info_name + " " + m->category + " " + std::to_string(int(flat(position(*m), position(*me)))) + "]";
                    std::printf("online-script: at %.0f,%.0f,%.0f hp %d control %d present %x mode %s tick %u (frame %d)%s\n", me->node->matrix(3, 0),
                                me->node->matrix(3, 1), me->node->matrix(3, 2), sys ? sys->players[std::size_t(facts.own_slot & 3)].hp : -1,
                                me->control_enabled ? 1 : 0, unsigned(facts.present), facts.mode.c_str(), facts.tick, facts.frame, extra.c_str());
                    std::fflush(stdout);
                }
            auto out = act(engine, facts, step);
            out.shot = shot;
            return out;
        }
        ++cursor_;
        step_started_ = false;
        if (cursor_ >= steps_.size()) {
            std::printf("online-script: done (frame %d, stage %s)\n", facts.frame, stage.c_str());
            std::fflush(stdout);
        }
    }
    Output out;
    out.shot = shot;
    return out;
}

} // namespace dw4::game
