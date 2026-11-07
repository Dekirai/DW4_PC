#pragma once

// Round I1b, the arrival prompt on the wire (docs/SEAMLESS.md 19.7, 19.18;
// the layouts agreed in DW4_PC-wt/i1b-contract.md, "prompt 16:30"). All
// little endian, str = u8 length + ASCII.
//
//   PLAN 0x18 (client, the gate form while the confirming press is held):
//     plan u16 | purpose u8 | stage str | point u8 | context u64 | group u8
//   OPTIONS 0x91 (server, the answer to PLAN; at a held barrier; again on a
//   change, at most once a second):
//     plan u16 | inst u32 | visit u32 | stage str | flags u8 | visibility u8 |
//     preselected u8 (row, 0xFF New instance) | rtt u16 | notice u8 |
//     notice name str | count u8 + rows
//     row: wid u32 | host u32 | host name str | relation u8 | friends inside
//     u8 | host stage str | players u8 | here u8 | taken u8 | held u8 |
//     reserved u8 | story label str | same story point u8 | difficulty u8 |
//     settings u8 | state u8 | visibility u8 (0xFF unless staff) | ping u16 |
//     members u8 + (name str | level u16 | species u8)
//   CHOICE 0x19 (client, the prompt's answer):
//     plan u16 | inst u32 | visit u32 | choice u8 | wid u32 | visibility u8 |
//     flags u8 | back point u8
#include "wire.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dw4::game::online::wire {

enum class PlanPurpose : std::uint8_t { gate = 1, return_mat = 2, other = 3 };

struct Plan {
    std::uint16_t plan{};
    PlanPurpose purpose = PlanPurpose::gate;
    std::string stage;
    std::uint8_t point{};
    std::uint64_t context{};
    std::uint8_t group = 1;
};

// A row's state (19.7.2): joinable now (a JOIN at its stage group there, or
// a guest's NEW where it has none: `here` tells), the waiting ones (boss
// fight, loading, arriving now: choosable, the join waits) and the greyed.
enum class RowState : std::uint8_t {
    joinable = 0, boss_fight = 1, loading = 2, host_away = 3, full = 4, arriving = 5, other_quest = 6, story_not_here = 7, too_long = 8
};
bool choosable(RowState state) noexcept;

struct OptionMember {
    std::string name;
    std::uint16_t level{};
    std::uint8_t species{};
    bool operator==(const OptionMember&) const = default;
};

struct OptionRow {
    std::uint32_t wid{}, host{};
    std::string host_name;
    std::uint8_t relation{}; // 1 party (I4), 2 the host a friend, 4 friends inside
    std::uint8_t friends_inside{};
    std::string host_stage;
    std::uint8_t players{}, here{}, taken{}, held{}, reserved{};
    std::string story_label;
    std::uint8_t same_story_point{}, difficulty{}, settings{}; // settings: 1 randomizer, 2 other settings
    RowState state = RowState::joinable;
    std::uint8_t visibility = 0xFF;
    std::uint16_t ping{};
    std::vector<OptionMember> members;
    static constexpr std::uint8_t party = 1, friend_host = 2, friends_in = 4;
    static constexpr std::uint8_t randomizer = 1, other_settings = 2;
    bool operator==(const OptionRow&) const = default;
};

struct Options {
    std::uint16_t plan{};
    std::uint32_t inst{}, visit{};
    std::string stage;
    std::uint8_t flags{};
    Visibility visibility = Visibility::public_;
    std::uint8_t preselected = kNewInstance;
    std::uint16_t rtt{};
    std::uint8_t notice{}; // 1 full now, 2 ended, 3 no longer open
    std::string notice_name;
    std::vector<OptionRow> rows;
    static constexpr std::uint8_t kNewInstance = 0xFF;
    static constexpr std::uint8_t refresh = 1, starting = 2, nothing = 4;
    static constexpr std::size_t kMaxRows = 8;
};

enum class ChoiceKind : std::uint8_t { join = 1, new_instance = 2, back = 3, travel = 4, return_ = 5 };

struct PromptChoice {
    std::uint16_t plan{};
    std::uint32_t inst{}, visit{};
    ChoiceKind choice = ChoiceKind::new_instance;
    std::uint32_t wid{};
    Visibility visibility = Visibility::public_;
    std::uint8_t flags{};
    std::uint8_t back_point = 0xFF; // 0xFF: the relay's default (stagML11 point 3)
};

std::vector<std::uint8_t> encode(const Plan&);
std::vector<std::uint8_t> encode(const Options&);
std::vector<std::uint8_t> encode(const PromptChoice&);
Plan decode_plan(std::span<const std::uint8_t>);
Options decode_options(std::span<const std::uint8_t>);
PromptChoice decode_choice(std::span<const std::uint8_t>);

} // namespace dw4::game::online::wire
