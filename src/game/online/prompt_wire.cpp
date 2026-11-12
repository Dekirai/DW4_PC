#include "prompt_wire.hpp"

namespace dw4::game::online::wire {

bool choosable(RowState state) noexcept {
    return state == RowState::joinable || state == RowState::boss_fight || state == RowState::loading || state == RowState::arriving;
}

std::vector<std::uint8_t> encode(const Plan& m) {
    Writer w;
    w.u16(m.plan);
    w.u8(std::uint8_t(m.purpose));
    w.str8(m.stage);
    w.u8(m.point);
    w.u64(m.context);
    w.u8(m.group);
    return std::move(w.out);
}

Plan decode_plan(std::span<const std::uint8_t> b) {
    Reader r{b};
    Plan m;
    m.plan = r.u16();
    const auto purpose = r.u8();
    if (purpose < 1 || purpose > 3) throw WireError("unknown plan purpose");
    m.purpose = PlanPurpose(purpose);
    m.stage = r.str8(kMaxStage);
    m.point = r.u8();
    m.context = r.u64();
    m.group = r.u8();
    r.end();
    if (m.plan == 0 || m.plan == 0xFFFF || m.stage.empty() || m.group < 1 || m.group > 4) throw WireError("PLAN out of range");
    return m;
}

std::vector<std::uint8_t> encode(const Options& m) {
    if (m.rows.size() > Options::kMaxRows) throw WireError("too many OPTIONS rows");
    Writer w;
    w.u16(m.plan);
    w.u32(m.inst);
    w.u32(m.visit);
    w.str8(m.stage);
    w.u8(m.flags);
    w.u8(std::uint8_t(m.visibility));
    w.u8(m.preselected);
    w.u16(m.rtt);
    w.u8(m.notice);
    w.str8(m.notice_name);
    w.u8(std::uint8_t(m.rows.size()));
    for (const auto& row : m.rows) {
        w.u32(row.wid);
        w.u32(row.host);
        w.str8(row.host_name);
        w.u8(row.relation);
        w.u8(row.friends_inside);
        w.str8(row.host_stage);
        w.u8(row.players);
        w.u8(row.here);
        w.u8(row.taken);
        w.u8(row.held);
        w.u8(row.reserved);
        w.str8(row.story_label);
        w.u8(row.same_story_point);
        w.u8(row.difficulty);
        w.u8(row.settings);
        w.u8(std::uint8_t(row.state));
        w.u8(row.visibility);
        w.u16(row.ping);
        if (row.members.size() > 4) throw WireError("too many OPTIONS members");
        w.u8(std::uint8_t(row.members.size()));
        for (const auto& x : row.members) {
            w.str8(x.name);
            w.u16(x.level);
            w.u8(x.species);
        }
    }
    return std::move(w.out);
}

Options decode_options(std::span<const std::uint8_t> b) {
    Reader r{b};
    Options m;
    m.plan = r.u16();
    m.inst = r.u32();
    m.visit = r.u32();
    m.stage = r.str8(kMaxStage);
    m.flags = r.u8();
    const auto visibility = r.u8();
    if (visibility > 2) throw WireError("unknown visibility");
    m.visibility = Visibility(visibility);
    m.preselected = r.u8();
    m.rtt = r.u16();
    m.notice = r.u8();
    m.notice_name = r.str8(64);
    const std::size_t n = r.u8();
    if (n > Options::kMaxRows) throw WireError("too many OPTIONS rows");
    for (std::size_t i = 0; i < n; ++i) {
        OptionRow row;
        row.wid = r.u32();
        row.host = r.u32();
        row.host_name = r.str8(64);
        row.relation = r.u8();
        row.friends_inside = r.u8();
        row.host_stage = r.str8(kMaxStage);
        row.players = r.u8();
        row.here = r.u8();
        row.taken = r.u8();
        row.held = r.u8();
        row.reserved = r.u8();
        row.story_label = r.str8(64);
        row.same_story_point = r.u8();
        row.difficulty = r.u8();
        row.settings = r.u8();
        const auto state = r.u8();
        if (state > std::uint8_t(RowState::too_long)) throw WireError("unknown row state");
        row.state = RowState(state);
        row.visibility = r.u8();
        row.ping = r.u16();
        const std::size_t k = r.u8();
        if (k > 4) throw WireError("too many OPTIONS members");
        for (std::size_t j = 0; j < k; ++j) {
            OptionMember x;
            x.name = r.str8(64);
            x.level = r.u16();
            x.species = r.u8();
            row.members.push_back(std::move(x));
        }
        if (row.wid == 0 || row.players > 4 || row.here > 4) throw WireError("OPTIONS row out of range");
        m.rows.push_back(std::move(row));
    }
    r.end();
    if (m.preselected != Options::kNewInstance && m.preselected >= m.rows.size()) throw WireError("OPTIONS preselection out of range");
    return m;
}

std::vector<std::uint8_t> encode(const PromptChoice& m) {
    Writer w;
    w.u16(m.plan);
    w.u32(m.inst);
    w.u32(m.visit);
    w.u8(std::uint8_t(m.choice));
    w.u32(m.wid);
    w.u8(std::uint8_t(m.visibility));
    w.u8(m.flags);
    w.u8(m.back_point);
    return std::move(w.out);
}

PromptChoice decode_choice(std::span<const std::uint8_t> b) {
    Reader r{b};
    PromptChoice m;
    m.plan = r.u16();
    m.inst = r.u32();
    m.visit = r.u32();
    const auto choice = r.u8();
    if (choice < 1 || choice > 5) throw WireError("unknown choice");
    m.choice = ChoiceKind(choice);
    m.wid = r.u32();
    const auto visibility = r.u8();
    if (visibility > 2) throw WireError("unknown visibility");
    m.visibility = Visibility(visibility);
    m.flags = r.u8();
    m.back_point = r.u8();
    r.end();
    return m;
}

} // namespace dw4::game::online::wire
