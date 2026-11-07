#pragma once

// Test driver (not part of the game): --online-script FILE plays this
// machine's pad in a scripted online room run (tools/online_test, the
// scenarios of docs/SEAMLESS.md 16). The file is a list of steps; each step
// is an action that runs until all of its conditions hold, then the next one
// starts:
//
//   ACTION [ARGS] [until COND[,COND...] [or COND[,COND...]]...] [fail COND[,COND...]]
//
// (the step ends when every condition of one group holds; a fail condition
// that holds while the step runs prints "online-script: FAIL ...", which
// tools/online_test counts as a failed check; '#' at a line's start or after
// a blank begins a comment, inside a word it is part of it: n_digilfm#1)
//
//   campaign            the story bot (src/game/campaign.cpp; it plays slot 0,
//                       so only while this member is slot 0)
//   errand ACTION [STAGE] one step of the story bot instead of its
//                       objectives (CampaignBot::set_errand; slot 0 too):
//                       "talk:n_digilfm#1" walks up to that model's talk box
//                       the way the campaign does, presses A, moves the
//                       messages on and answers choices with their first
//                       entry; in STAGE (default: the step's stage)
//   idle                no input
//   gate MAT N          onto a lobby gate mat, N times down in its menu, A
//   talk                A now and then while this member's player has no
//                       control (talk windows, messages)
//   goto X Z [R]        walk to within R (4) of (X, Z) over the stage collision
//   route X,Z X,Z ...   walk the waypoints one after the other, striking the
//                       enemies that come within 15 units
//   follow [D]          stay within D (40) of the nearest other present member
//   mat NAME [yes|no [N]] walk onto the mat and stay there; an exit prompt
//                       that asks this member is answered (default yes; with
//                       N the prompt's owner waits until N present members
//                       stand on the mat), other windows are confirmed
//   explore MAT [yes|no] play through the stage to the mat (ExploreBot,
//                       striking the enemies that come near), answering an
//                       exit prompt that asks this member as "mat"
//   fight [X Z R]       attack the nearest living enemy (within R of (X, Z))
//   wander X Z R        walk between points within R of (X, Z), fighting
//                       enemies that come near
//   die                 walk into the nearest enemy without guarding or
//                       attacking
//   hold BUTTONS [N]    hold BUTTONS (A+B+X+START, '+'-joined) for N frames
//   press BUTTONS       one press (6 frames)
//   giveup              hold START while defeated (give up, SEAMLESS.md 5)
//   disk RECORD         use a disk of the L panel (8: the Gate Disk)
//   menu [close]        open the status window with START (this member's
//                       own, docs/SEAMLESS.md 5.3); "close": B until it is shut
//   browse              inside the status window: DOWN, A, B by turns every
//                       90 frames from the 60th on (its pages one after the other)
//   talkto MODEL[#N]    walk up to the NPC or map recorder (striking the
//                       enemies that come within 15 units on the way), strike
//                       it (A) and page through the talk this starts with A
//   log TEXT            one trace line
//   shot NAME           a screenshot of this frame's picture, NAME.bmp beside
//                       --screenshot's file (the ghost figures' look,
//                       docs/SEAMLESS.md 12; a run without --screenshot
//                       takes none)
//
//   COND: stage=NAME  present=N  present>=N  present<=N  frames>=N  dead
//         alive  control  apart>D  near<D  asked  mode=NAME  partnerdead
//         slot=N  sealed>=N (ticks of this visit executed as sealed)
//         time>=S (seconds since the start)  prompt=0|1  asking=N (-1: none)
//         onmat:NAME>=N (present members on the mat)  onme:NAME  offme:NAME
//         stagechange (the stage differs from the one the step began in)
//         flag:N=V (the engine's flag N, an absent flag counts as 0)
//         epoch>=S (the system clock, seconds since 1970: tools/online_test
//         writes @N as its scenario start + N)  answered>=N (exit prompt
//         answers given in this step)  hp<=N (this member's HP)
//         level>=N (this member's own character's level)
//         ghosts>=N (ghost figures this machine shows: other room members
//         in this hub stage, docs/SEAMLESS.md 12)
//         roomflag:N=V (flag N of the room story this machine holds as
//         confirmed, docs/SEAMLESS.md 8.3: the server's newest it was sent,
//         ahead of the engine until the next barrier; 0 without one)
//         key:N=V (the engine's key item N, the common flag folder; round
//         G-core)  difficulty=N (the engine's difficulty, 0-2)
//         menu=0|1 (this member's own status window is open, 5.3)
//         menus=N (present members with their own window open)
//         talking=0|1 (this member's own talk or map recorder runs)
//         scenes=N (present members in their own talk)
//         lost>=N (HP this member lost since the step began)
//         enemy<N (a living enemy within N units of this member)
//
// Without "until", hold / press / log / shot end by themselves, goto and route when
// the goal is reached, and the others never end. Every step start, exit prompt change and stage change is traced
// ("online-script: ..."). The driver only reads the engine (the instance's
// state, the same on every member's machine) and the session's facts, and
// writes only this machine's pad, which the session sends like any input.
#include "campaign.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/online_story.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game {

struct OnlineScriptFacts {
    int frame = 0;               // the host's frame
    int own_slot = 0;            // this member's instance slot
    std::uint8_t present = 1;    // present slots of the instance
    std::string mode = "offline"; // the session mode (session.hpp mode_name)
    std::uint32_t tick = 0;      // the next tick of the visit
    const engine::RoomStory* room_story = nullptr; // the session's confirmed room story (none yet: nullptr)
    int ghosts = 0;              // ghost figures shown (GhostFigures::shown, presentation)
};

class OnlineScript {
public:
    struct Output {
        std::uint32_t buttons{};
        std::uint32_t push{};  // pad_direction bits that push the main stick fully
        bool walk{};
        float x{}, z{};
        float reach{4.0f};
        std::string shot; // a "shot" step ended in this update: its name
    };
    struct Condition {
        std::string key, op, text;
        double value{};
    };
    struct Step {
        std::string action;
        std::vector<std::string> args;
        std::vector<std::vector<Condition>> until; // groups joined by "or"
        std::vector<Condition> fail;                // "fail COND,...": none of them may hold while the step runs
        int line{};
    };
    bool load(const std::filesystem::path& file, std::string& error);
    bool active() const { return !steps_.empty(); }
    bool finished() const { return cursor_ >= steps_.size(); }
    const std::vector<Step>& steps() const { return steps_; }
    // The names load() accepts (an unknown action or condition fails the load).
    static bool known_action(std::string_view name);
    static bool known_condition(std::string_view key);
    Output update(engine::Engine& engine, const OnlineScriptFacts& facts);

private:
    bool holds(engine::Engine& engine, const OnlineScriptFacts& facts, const Condition& c) const;
    Output act(engine::Engine& engine, const OnlineScriptFacts& facts, const Step& step);
    // `direct`: straight at (x, z) without a planned route (the route action's
    // waypoints are known walkable).
    bool walk_to(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, float x, float z, float close, int frame,
                 Output& out, bool direct = false);
    void trace_prompt(engine::Engine& engine, const OnlineScriptFacts& facts);

    std::vector<Step> steps_;
    std::size_t cursor_ = 0;
    int step_frames_ = 0;       // live steps since the step began
    bool step_started_ = false;
    std::uint32_t sealed_ticks_ = 0;
    std::string last_stage_;
    std::string traced_prompt_;
    std::unique_ptr<CampaignBot> campaign_;
    std::unique_ptr<ExploreBot> explorer_;
    // The route of goto / mat / wander.
    std::vector<engine::Vec3f> route_;
    std::size_t route_next_ = 0;
    engine::Vec3f route_goal_{1e9f, 0, 1e9f};
    int route_frame_ = -100000;
    int wander_index_ = 0;
    std::size_t waypoint_ = 0;  // the route action's next waypoint
    bool departed_ = false;     // the depart action posted its departure
    bool step_reached_ = false; // goto / route: the goal reached (the step ends without "until")
    std::string step_stage_;    // the stage the step began in (stagechange)
    bool failed_ = false;       // a fail condition of this step held (traced once)
    int menu_at_ = -1;          // the gate action: the frame the menu came up
    int step_hp_ = 0;           // this member's HP when the step began (lost)
    // talkto: where this member stood when it first came within 12 units of
    // the model (the free side to back off to).
    engine::Vec3f talk_from_{};
    bool talk_from_set_ = false;
    // The route action: where this member stood 40 frames ago, held up.
    engine::Vec3f stuck_at_{};
    int stuck_frame_ = -1000;
    bool stuck_ = false;
    // Prompt answers: the frame the current press sequence began.
    int answer_at_ = -1000;
    int answers_ = 0;           // prompt answers given in this step
    // The disk action: the disk's row, the rows above it, its start.
    int disk_line_ = -1, disk_ups_ = 0, disk_at_ = 0;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
};

} // namespace dw4::game
