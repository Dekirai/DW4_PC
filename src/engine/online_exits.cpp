// Seamless online co-op, free travel in a multi-member instance
// (docs/SEAMLESS.md 5.2, 6.7): the funcStageChange port, the gate disc fork,
// give up, the side rule for defeated members, and the fork primitive the
// exits use. All of it acts only while Engine::online_shared() (online with
// two or more present members); a solo instance and every offline game run
// the original scripts unchanged.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <bit>
#include <cctype>

namespace dw4::engine {

namespace {

constexpr const char* kPromptColour[4] = {"STCHGFONT00/font00/V<0.9,0.47,0.47>", "STCHGFONT00/font00/V<0.47,0.47,0.9>",
                                          "STCHGFONT00/font00/V<0.9,0.9,0.47>", "STCHGFONT00/font00/V<0.47,0.9,0.47>"};
// An asked member answers within 600 frames (10 s); no answer counts as No.
constexpr std::uint64_t kAskFrames = 600;

int lowest_slot(std::uint32_t mask) {
    for (int slot = 0; slot < 4; ++slot)
        if (mask & (1u << slot)) return slot;
    return -1;
}

std::uint8_t living_players(const Engine& engine) {
    std::uint8_t living = 0;
    if (const auto* models = engine.get<ModelCtrlManager>())
        for (int slot = 0; slot < 4; ++slot)
            if (models->player(slot)) living |= std::uint8_t(1u << slot);
    return living;
}

DemoRecord set_record(std::string category, std::optional<std::string> name, std::int32_t index, std::string command,
                      std::optional<std::string> argument = std::nullopt, std::int64_t delay = 0) {
    DemoRecord record;
    record.type = 0;
    record.category = std::move(category);
    record.name = std::move(name);
    record.index = index;
    record.command = std::move(command);
    record.argument = std::move(argument);
    record.delay_frames = delay;
    return record;
}

// DMchkSD: "!=0" / "==1" wait while the answer is true (type 2), the others
// while it is not 1 (type 1).
DemoRecord check_record(std::string category, std::optional<std::string> name, std::int32_t index, std::string command,
                        std::optional<std::string> argument, std::string_view condition) {
    DemoRecord record = set_record(std::move(category), std::move(name), index, std::move(command), std::move(argument));
    record.type = condition == "!=0" || condition == "==1" ? 2 : 1;
    return record;
}

bool answer(Engine& engine, std::string_view category, std::string_view name, int index, std::string_view command) {
    Packet packet;
    engine.messenger().send(engine, category, name, index, command, packet);
    return !packet.bools.empty() && packet.bools[0] != 0;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

} // namespace

// The running exit of a multi-member instance: the owner's prompt is a demo
// work like the original's; after its answer one port record steps this
// state machine every update (the other members' prompts, the departure).
struct Engine::ExitPort {
    enum class State { decide, ask_next, ask_open, ask_window, ask_answer, ask_close, depart, finishing, done };
    State state = State::decide;
    int owner = -1;
    std::string stage, mat, kind;
    int point{};
    std::optional<std::string> text;
    std::uint8_t candidates{}; // the living members on the mat at the owner's yes (the owner left out)
    std::uint8_t asked{};
    int asking = -1;
    std::uint64_t opened{};
    std::uint64_t finish_at{};
};

// The prompt of one member (stag0101_m1__func.lua funcStageChange, 46..147):
// window, selection and the destination text in the member's colour.
static void prompt_records(DemoWork& work, int slot, const std::optional<std::string>& text) {
    if (text) {
        work.records.push_back(set_record("MODELCTRL", "i_zzstchgwb", slot, "GENERATE", "SQ_STOPEN", 10));
        work.records.push_back(set_record("SELECTCTRL", "i_zzstchgwn", slot, "GENERATE", "V<0,-16,0>/V<0,0,0>", 10));
        work.records.push_back(set_record("TEXTCTL", std::nullopt, -1, "ALIGN", "STCHGFONT00/font00/CENTER"));
        work.records.push_back(set_record("TEXTCTL", std::nullopt, -1, "COLOR", kPromptColour[std::clamp(slot, 0, 3)]));
        work.records.push_back(set_record("TEXTCTL", std::nullopt, -1, "TEXTSET", *text, 10));
        work.records.push_back(set_record("TEXTCTL", std::nullopt, -1, "SWITCH", "STCHGFONT00/font00/SET", 30));
    } else {
        work.records.push_back(set_record("MODELCTRL", "i_zzstchgwb", slot, "GENERATE", "SQ_OPEN", 10));
        work.records.push_back(set_record("SELECTCTRL", "i_zzstchgwn", slot, "GENERATE", std::nullopt, 10));
    }
    work.records.push_back(check_record("MODELCTRL", "i_zzstchgwb", slot, "ISRUN", std::nullopt, "==0"));
    work.records.push_back(check_record("SELECTCTRL", "i_zzstchgwn", slot, "ISEND", std::nullopt, "!=0"));
    work.records.push_back(check_record("SELECTCTRL", "i_zzstchgwn", slot, "ISSELECTNOW", std::nullopt, "==1"));
    if (text) work.records.push_back(set_record("TEXTCTL", std::nullopt, -1, "SWITCH", "STCHGFONT00/font00/CLR"));
    work.records.push_back(set_record("MODELCTRL", "i_zzstchgwb", slot, "SQ_CLOSE"));
    work.records.push_back(check_record("SELECTCTRL", "i_zzstchgwn", slot, "ISEND", std::nullopt, "==0"));
}

// The departure of the original (funcStageChange case 0, 212..447) for the
// members this copy still has: the controls off, the half fade back, the
// defeated players' models done, the exit sequences, the full fade and
// NEXTSTAGE.
static std::unique_ptr<DemoWork> departure_work(Engine& engine, const std::string& stage, int point, const std::string& kind) {
    auto work = std::make_unique<DemoWork>();
    auto& r = work->records;
    r.push_back(set_record("STAGECTL", std::nullopt, -1, "PADINPUT", "DISABLE"));
    r.push_back(set_record("MODELCTRL", "i_fadewn01", 0, "SQ_BHLFIN30", std::nullopt, 10));
    r.push_back(check_record("MODELCTRL", "i_fadewn01", 0, "ISRUN", std::nullopt, "==0"));
    r.push_back(check_record("MODELCTRL", "i_fadewn01", 0, "ISSEQNAME", "SQ_BHLFIN30", "==0"));
    r.push_back(check_record("MODELCTRL", "i_fadewn01", 0, "ISSEQNAME", "SQ_BHLFIN30", "!=0"));
    r.push_back(check_record("MODELCTRL", "i_fadewn01", 0, "ISRUN", std::nullopt, "!=0"));
    // This copy's members only: online_split narrows `present` at once, the
    // other side's players go at the end of the step.
    const auto* sys = engine.get<GameSystem>();
    const auto mine = [&](int slot) { return !engine.online().enabled || (engine.online().present & (1u << slot)) != 0; };
    for (int slot = 0; slot < 4 && sys; ++slot) {
        const auto& p = sys->players[std::size_t(slot)];
        if (p.joined && mine(slot) && p.hp == 0) r.push_back(check_record("MODELCTRL", p.digimon, slot, "ISRUN", std::nullopt, "!=0"));
    }
    if (kind == "trans") {
        std::int64_t delay = 0;
        r.push_back(set_record("PLAYER", std::nullopt, -1, "SQ_DEMOWAIT"));
        for (int slot = 0; slot < 4 && sys; ++slot) {
            const auto& p = sys->players[std::size_t(slot)];
            if (!p.joined || !mine(slot)) continue;
            r.push_back(set_record("PLAYER", p.digimon, slot, "SQ_EXIT", std::nullopt, delay));
            delay += 20;
        }
        r.push_back(set_record("MODELCTRL", "i_fadewn01", 0, "GENERATE", "SQ_STCHANGE01", delay + 40));
    } else {
        r.push_back(set_record("MODELCTRL", "i_fadewn01", 0, "GENERATE", "SQ_STCHANGE01"));
    }
    r.push_back(check_record("MODELCTRL", "i_fadewn01", 0, "ISRUN", std::nullopt, "==0"));
    r.push_back(check_record("MODELCTRL", "i_fadewn01", 0, "ISSEQNAME", "SQ_STCHANGELP", "==0"));
    r.push_back(set_record("PLAYERPOINT", std::nullopt, -1, "SETGENERATEPOS", "I<" + std::to_string(point) + ">", 10));
    r.push_back(set_record("STAGECTL", std::nullopt, -1, "NEXTSTAGE", stage, 10));
    return work;
}

struct ExitPortAccess {
    // One update of the exit after the owner's answer; nonzero while it
    // waits (the port record blocks its work).
    static int step(Engine& engine, Engine::ExitPort& exit) {
        using State = Engine::ExitPort::State;
        auto& view = engine.exit_view_;
        auto& messenger = engine.messenger();
        auto send = [&](std::string_view category, std::optional<std::string_view> name, int index, std::string_view command,
                        std::string_view argument = {}) {
            Packet packet;
            if (!argument.empty()) parse_packet_arguments(argument, packet);
            messenger.send(engine, category, name, index, command, packet);
        };
        auto post = [&](const char* category, std::optional<std::string> name, int index, const char* command, std::string_view argument,
                        std::int64_t delay) {
            Packet packet;
            if (!argument.empty()) parse_packet_arguments(argument, packet);
            messenger.post(engine.time() + kFrameNs * delay, category, std::move(name), index, command, std::move(packet));
        };
        auto finish_in = [&](std::uint64_t frames) {
            exit.finish_at = engine.frame() + frames;
            exit.state = State::finishing;
        };
        for (int guard = 0; guard < 8; ++guard) {
            switch (exit.state) {
            case State::decide: {
                // The owner's answer (GETSELECT: 0 yes, 1 no, -1 cancelled).
                Packet packet;
                messenger.send(engine, "SELECTCTRL", "i_zzstchgwn", exit.owner, "GETSELECT", packet);
                const int choice = packet.ints.empty() ? -1 : packet.ints[0];
                if (choice != 0) {
                    // No: the half fade goes, the owner plays on.
                    post("MODELCTRL", "i_fadewn01", 0, "SQ_BHLFIN30", {}, 10);
                    post("PLAYER", std::nullopt, exit.owner, "ONCTRL", {}, 30);
                    finish_in(42);
                    continue;
                }
                view.yes = std::uint8_t(1u << exit.owner);
                view.asking = -1;
                // Every other living member on the mat is asked in turn; who
                // steps on from now on is not part of this departure.
                if (const auto* mats = engine.get<MatCtrlManager>())
                    exit.candidates = std::uint8_t(mats->players_on(exit.mat) & engine.online().present & ~(1u << exit.owner));
                exit.state = State::ask_next;
                continue;
            }
            case State::ask_next: {
                const int next = lowest_slot(exit.candidates & ~exit.asked);
                if (next < 0) {
                    exit.state = State::depart;
                    continue;
                }
                exit.asked |= std::uint8_t(1u << next);
                exit.asking = next;
                view.asking = next;
                view.sees_fade |= std::uint8_t(1u << next);
                exit.opened = engine.frame();
                // The same prompt in its own colour (stag0101_m1__func.lua
                // 46..128 for that member); only its player stops.
                send("PLAYER", std::nullopt, next, "OFCTRL");
                if (exit.text) {
                    post("MODELCTRL", "i_zzstchgwb", next, "GENERATE", "SQ_STOPEN", 10);
                    post("SELECTCTRL", "i_zzstchgwn", next, "GENERATE", "V<0,-16,0>/V<0,0,0>", 10);
                    send("TEXTCTL", std::nullopt, -1, "ALIGN", "STCHGFONT00/font00/CENTER");
                    send("TEXTCTL", std::nullopt, -1, "COLOR", kPromptColour[next]);
                    post("TEXTCTL", std::nullopt, -1, "TEXTSET", *exit.text, 10);
                    post("TEXTCTL", std::nullopt, -1, "SWITCH", "STCHGFONT00/font00/SET", 30);
                } else {
                    post("MODELCTRL", "i_zzstchgwb", next, "GENERATE", "SQ_OPEN", 10);
                    post("SELECTCTRL", "i_zzstchgwn", next, "GENERATE", {}, 10);
                }
                exit.state = State::ask_open;
                return 1;
            }
            case State::ask_open:
                // The window is up (ISRUN), the selection runs (not ISEND).
                if (!answer(engine, "MODELCTRL", "i_zzstchgwb", exit.asking, "ISRUN")) return 1;
                exit.state = State::ask_window;
                continue;
            case State::ask_window:
                if (answer(engine, "SELECTCTRL", "i_zzstchgwn", exit.asking, "ISEND")) return 1;
                exit.state = State::ask_answer;
                continue;
            case State::ask_answer:
                if (answer(engine, "SELECTCTRL", "i_zzstchgwn", exit.asking, "ISSELECTNOW")) {
                    if (engine.frame() - exit.opened < kAskFrames) return 1;
                    // No answer in time: the window closes as a No.
                    send("SELECTCTRL", "i_zzstchgwn", exit.asking, "REMOVE");
                }
                if (exit.text) send("TEXTCTL", std::nullopt, -1, "SWITCH", "STCHGFONT00/font00/CLR");
                send("MODELCTRL", "i_zzstchgwb", exit.asking, "SQ_CLOSE");
                exit.state = State::ask_close;
                return 1;
            case State::ask_close: {
                if (!answer(engine, "SELECTCTRL", "i_zzstchgwn", exit.asking, "ISEND")) return 1;
                Packet packet;
                messenger.send(engine, "SELECTCTRL", "i_zzstchgwn", exit.asking, "GETSELECT", packet);
                if (!packet.ints.empty() && packet.ints[0] == 0) {
                    view.yes |= std::uint8_t(1u << exit.asking);
                } else {
                    // No: that member plays on, its picture without the fade.
                    post("PLAYER", std::nullopt, exit.asking, "ONCTRL", {}, 30);
                    view.sees_fade &= std::uint8_t(~(1u << exit.asking));
                }
                exit.asking = -1;
                view.asking = -1;
                exit.state = State::ask_next;
                continue;
            }
            case State::depart: {
                // The members who said yes and still live go together; the
                // dead stay with the living ones (SEAMLESS.md 6.7): when no
                // living member stays, everyone goes (the original group move).
                const std::uint8_t present = engine.online().present;
                const std::uint8_t leavers = exit_leavers(present, living_players(engine), view.yes);
                if (!leavers) {
                    post("MODELCTRL", "i_fadewn01", 0, "SQ_BHLFIN30", {}, 10);
                    for (int slot = 0; slot < 4; ++slot)
                        if (view.yes & (1u << slot)) post("PLAYER", std::nullopt, slot, "ONCTRL", {}, 30);
                    finish_in(42);
                    continue;
                }
                const bool group = leavers == present;
                const bool local_leaves = group || engine.online_split(leavers, ForkKind::exit);
                if (!local_leaves) {
                    // This copy stays: the leavers are gone, their fade fades
                    // back unseen.
                    view.sees_fade = 0;
                    post("MODELCTRL", "i_fadewn01", 0, "SQ_BHLFIN30", {}, 10);
                    finish_in(42);
                    continue;
                }
                engine.heading_ = ExitHeading{engine.frame(), exit.stage, exit.point, leavers};
                view.sees_fade = engine.online().present;
                engine.demo().start(departure_work(engine, exit.stage, exit.point, exit.kind));
                exit.state = State::done;
                return 0;
            }
            case State::finishing:
                if (engine.frame() < exit.finish_at) return 1;
                view = ExitPromptView{};
                exit.state = State::done;
                return 0;
            case State::done:
                return 0;
            }
        }
        return 1;
    }
};

// funcStageChange(stage, point, text, _) online (registered over the Lua
// global after func.lua while the instance has two or more members;
// SEAMLESS.md 5.2). The prompt owner is the lowest living member that
// stepped on the mat in this update (never MATgetPlayerNo's slot-0
// fallback); only its player stops, the enemies run on, and its prompt is
// drawn on its machine. On Yes every other living member on the mat is asked
// in turn; the members who said yes leave together (a fork, or the original
// group move when every living member goes).
std::vector<Lua4Value> Engine::stage_change_port(std::span<const Lua4Value> args) {
    const auto* mats = get<MatCtrlManager>();
    const auto running = mats ? mats->running_mat() : std::nullopt;
    if (!running) return {};
    const int owner = exit_prompt_owner(running->current, running->previous, running->live, online_.present);
    if (owner < 0 || (exit_port_ && exit_port_->state != ExitPort::State::done)) return {};
    auto exit = std::make_shared<ExitPort>();
    exit->owner = owner;
    exit->mat = running->name;
    exit->stage = !args.empty() && args[0].type() == Lua4Type::String ? std::string(args[0].as_string()) : std::string();
    exit->point = args.size() > 1 && args[1].type() == Lua4Type::Number ? int(args[1].as_number()) : 0;
    if (args.size() > 2 && args[2].type() == Lua4Type::String) exit->text = std::string(args[2].as_string());
    if (lua_)
        if (const auto kind = lua_->get_global("g_StChangeKind"); kind.type() == Lua4Type::String) exit->kind = std::string(kind.as_string());
    exit_port_ = exit;
    exit_view_ = ExitPromptView{};
    exit_view_.active = true;
    exit_view_.owner = owner;
    exit_view_.asking = owner;
    exit_view_.sees_fade = std::uint8_t(1u << owner);
    exit_view_.stays = std::uint8_t(online_.present & ~(1u << owner));
    exit_view_.stage = exit->stage;
    exit_view_.boss_destination = lower(exit->stage).rfind("stagbs", 0) == 0;
    // Only the owner's player stops (the original stops every player and
    // every enemy).
    {
        Packet packet;
        messenger_.send(*this, "PLAYER", std::nullopt, owner, "OFCTRL", packet);
    }
    auto work = std::make_unique<DemoWork>();
    work->records.push_back(set_record("MODELCTRL", "i_fadewn01", 0, "GENERATE", "SQ_BHLFOUT30"));
    prompt_records(*work, owner, exit->text);
    DemoRecord step;
    step.type = 8;
    step.port = [exit](Engine& engine) { return ExitPortAccess::step(engine, *exit); };
    work->records.push_back(std::move(step));
    demo_.start(std::move(work));
    return {};
}

// The gate disc in a multi-member instance (SEAMLESS.md 5): its user forks
// off first (with every defeated member when no living one would stay), then
// gatedisc.lua runs in the user's copy only; the others play on. Returns
// whether this copy runs the script.
bool Engine::gatedisc_fork() {
    auto* sys = get<GameSystem>();
    if (!sys || !sys->gate_stage_name || *sys->gate_stage_name != "Player") return true;
    const int user = std::clamp(int(sys->gate_stage_no), 0, 3);
    std::uint8_t leavers = exit_leavers(online_.present, std::uint8_t(living_players(*this) | (1u << user)), std::uint8_t(1u << user));
    if (!leavers) leavers = online_.present;
    if (leavers == online_.present) {
        heading_ = ExitHeading{frame_, "stagML11", 3, online_.present};
        return true; // nobody living stays: the original, everyone goes
    }
    if (!online_split(leavers, ForkKind::exit)) return false;
    heading_ = ExitHeading{frame_, "stagML11", 3, leavers};
    return true;
}

std::optional<ExitHeading> Engine::take_heading() {
    auto taken = heading_;
    heading_.reset();
    return taken;
}

// Online overrides of the Lua globals (SEAMLESS.md 7.3), after func.lua ran
// and whenever online or the instance's sharing changes (online_rules_changed): funcStageChange is the port
// while two or more members are present, else func.lua's own function (kept
// under a hidden name). NETisOnline and NETsplit exist only online.
void Engine::install_online_lua() {
    if (!lua_ || !online_.enabled) return;
    lua_->set_global("NETisOnline", Lua4Value::host_function("NETisOnline"));
    lua_->set_global("NETsplit", Lua4Value::host_function("NETsplit"));
    const auto current = lua_->get_global("funcStageChange");
    if (current.type() == Lua4Type::Function) lua_->set_global("__dw4_funcStageChange", current);
    const auto original = lua_->get_global("__dw4_funcStageChange");
    if (original.type() != Lua4Type::Function) return;
    lua_->set_global("funcStageChange", online_shared() ? Lua4Value::host_function("funcStageChange") : original);
}

} // namespace dw4::engine
