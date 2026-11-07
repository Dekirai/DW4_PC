// Round I1b, the arrival prompt in the client session (docs/SEAMLESS.md 19.7).
//
// The gate form (19.7.4): while the engine names the departure the own
// player's next confirming press would start (Engine::pending_departure_
// choice: the lobby gate's destination select, the return mat), that press is
// kept out of the pad stream (the engine sees neutral pads), PLAN goes and
// OPTIONS is awaited. No candidate (or an automatic co-arrival only): the
// press goes out at once, one round trip after it was made. Candidates: the
// overlay opens over the frozen game, without a time limit. A on a row or on
// New instance: CHOICE goes, then the held press in the next pad, and the
// departure's BARRIER names the plan. B: a B press goes to the game (the
// original cancel closes the menu; the player stands at the gate). No
// OPTIONS within World:PlanWaitMs: the press goes out anyway and the barrier
// is decided as a loading-screen arrival. The connection lost while it holds:
// the press is dropped, the menu closed by B, "Connection lost. Try the gate
// again in a moment.".
//
// The loading-screen form (19.7.5): BARRIER_WAIT *choice* with OPTIONS at a
// held barrier; the five-second rule does not fire meanwhile; A answers with
// CHOICE join / new, B with *Back to the lobby* (the relay's DISSOLVE
// redirect); a lost connection closes it (the relay forgot the hold).
//
// The simulation sees an A press later, the same in every replay: the lobby
// is a solo hub instance that is never joined and sends no LOG.
#include "session.hpp"

#include <algorithm>
#include <cinttypes>

namespace dw4::game::online {
namespace w = wire;

namespace {

PadSample neutral(const PadSample& in) {
    PadSample n;
    n.connected = in.connected;
    return n;
}

const char* state_text(w::RowState s) {
    switch (s) {
    case w::RowState::joinable: return "joinable";
    case w::RowState::boss_fight: return "boss";
    case w::RowState::loading: return "loading";
    case w::RowState::host_away: return "host_away";
    case w::RowState::full: return "full";
    case w::RowState::arriving: return "arriving";
    case w::RowState::other_quest: return "other_quest";
    case w::RowState::story_not_here: return "story_not_here";
    case w::RowState::too_long: return "too_long";
    }
    return "?";
}

const char* visibility_word(w::Visibility v) {
    return v == w::Visibility::friends ? "friends" : v == w::Visibility::private_ ? "private" : "public";
}

w::PlanPurpose purpose_of(engine::DepartureChoice::Purpose p) {
    switch (p) {
    case engine::DepartureChoice::Purpose::gate: return w::PlanPurpose::gate;
    case engine::DepartureChoice::Purpose::return_mat: return w::PlanPurpose::return_mat;
    default: return w::PlanPurpose::other;
    }
}

} // namespace

std::array<PadSample, 4> Session::before_step(const PadSample& local, double now) {
    return step_pads(gate_filter(local, now), now);
}

PadSample Session::gate_filter(const PadSample& local, double now) {
    PadSample out = local;
    const bool a_held = (local.buttons & pad_button::a) != 0;
    switch (gate_phase_) {
    case GatePhase::idle:
        if (a_held && !engine_a_ && config_.gate_form && hub_ && hello_done_ && transport_ && member_id_ != 0) {
            if (const auto target = engine_.pending_departure_choice(); target && target->reader == slot_) {
                // (The confirming press of a departure from home: held, PLAN, OPTIONS awaited.)
                gate_target_ = *target;
                gate_phase_ = GatePhase::waiting;
                gate_since_ = now;
                gate_plan_ = 0;
                departure_plan_ = 0;
                chosen_join_ = false;
                chosen_host_.clear();
                trace("prompt hold stage=%s point=%d menu=%s cursor=%d", target->stage.c_str(), target->point, target->menu.c_str(), target->cursor);
                send_plan(now);
                out = neutral(local);
            }
        }
        break;
    case GatePhase::waiting:
        out = neutral(local);
        if (gate_plan_ == 0) send_plan(now); // (PLAN's throttle: a second confirm within the second waits for it)
        if (now - gate_since_ > config_.plan_wait) {
            // It never sticks: the press goes out, the barrier is decided as a loading-screen arrival.
            trace("prompt plan-timeout plan=%u", unsigned(gate_plan_));
            log("no answer to the gate's plan within %.1f s: the departure goes on", config_.plan_wait);
            departure_plan_ = 0;
            gate_phase_ = GatePhase::release;
        }
        break;
    case GatePhase::open: out = neutral(local); break;
    case GatePhase::release:
        out = neutral(local);
        out.buttons = pad_button::a; // (the held press, a rising edge after the neutral pads)
        gate_phase_ = GatePhase::idle;
        break;
    case GatePhase::cancel:
        out = neutral(local);
        out.buttons = pad_button::b; // (the gate's own cancel closes the destination menu)
        gate_phase_ = GatePhase::idle;
        break;
    }
    if ((gate_phase_ == GatePhase::waiting || gate_phase_ == GatePhase::open) && !engine_.pending_departure_choice()) {
        // (The menu went otherwise: nothing to depart from any more.)
        trace("prompt hold dropped (the menu closed)");
        if (prompt_ && prompt_->form == PromptForm::gate) close_prompt("menu closed");
        gate_phase_ = GatePhase::idle;
    }
    engine_a_ = (out.buttons & pad_button::a) != 0;
    return out;
}

void Session::send_plan(double now) {
    if (now - plan_sent_at_ < 1.0 || !transport_) return;
    plan_sent_at_ = now;
    gate_plan_ = next_plan_;
    next_plan_ = std::uint16_t(next_plan_ >= 0xFFFE ? 1 : next_plan_ + 1);
    w::Plan plan;
    plan.plan = gate_plan_;
    plan.purpose = purpose_of(gate_target_.purpose);
    plan.stage = gate_target_.stage;
    plan.point = std::uint8_t(std::clamp(gate_target_.point, 0, 255));
    plan.context = engine_.session_context_hash();
    plan.group = 1;
    send(w::Type::plan, w::encode(plan));
    trace("prompt plan=%u stage=%s point=%u", unsigned(plan.plan), plan.stage.c_str(), unsigned(plan.point));
}

void Session::on_options(const w::Options& o, double now) {
    if (o.plan != 0) {
        if (gate_phase_ == GatePhase::open && o.plan == prompt_plan_ && prompt_ && prompt_->form == PromptForm::gate) {
            open_prompt(PromptForm::gate, o, now); // (a refresh: the cursor stays on its instance)
            return;
        }
        if (gate_phase_ != GatePhase::waiting || o.plan != gate_plan_) return;
        if ((o.flags & (w::Options::starting | w::Options::nothing)) != 0 || o.rows.empty()) {
            // Nothing to choose, or an automatic co-arrival: the press goes out now.
            trace("prompt none plan=%u flags=%u", unsigned(o.plan), unsigned(o.flags));
            departure_plan_ = o.plan;
            gate_phase_ = GatePhase::release;
            return;
        }
        prompt_plan_ = o.plan;
        gate_phase_ = GatePhase::open;
        open_prompt(PromptForm::gate, o, now);
        return;
    }
    // A held barrier's (the loading-screen form).
    if (mode_ != Mode::parked || o.inst != inst_ || o.visit != visit_) return;
    prompt_hold_ = true;
    if ((o.flags & w::Options::nothing) != 0 || o.rows.empty()) {
        // (An undecodable OPTIONS: nothing to choose, so a new instance with the remembered visibility.)
        w::PromptChoice c;
        c.inst = inst_;
        c.visit = visit_;
        c.choice = w::ChoiceKind::new_instance;
        c.visibility = o.visibility;
        send(w::Type::choice, w::encode(c));
        return;
    }
    open_prompt(PromptForm::loading, o, now);
}

void Session::open_prompt(PromptForm form, const w::Options& o, double now) {
    const bool refresh = prompt_ && prompt_->form == form;
    std::optional<std::uint32_t> kept; // (a refresh keeps the cursor on the same instance, or on New instance)
    bool kept_new = false;
    w::Visibility visibility = o.visibility;
    bool details = false;
    double opened_at = now;
    if (refresh) {
        if (prompt_->new_instance()) kept_new = true;
        else kept = prompt_->rows[prompt_->cursor].wid;
        visibility = prompt_->visibility;
        details = prompt_->details;
        opened_at = prompt_->opened_at;
    }
    PromptView v;
    v.form = form;
    v.stage = o.stage;
    v.rows = o.rows;
    v.visibility = visibility;
    v.details = details;
    v.opened_at = opened_at;
    v.rtt_ms = o.rtt ? int(o.rtt) : -1;
    if (o.notice != 0 && !o.notice_name.empty())
        v.notice = o.notice == 1 ? o.notice_name + "'s instance is full now."
                   : o.notice == 2 ? "Couldn't join " + o.notice_name + ": the instance has ended."
                                   : "Couldn't join " + o.notice_name + ": it is not open to you any more.";
    else if (refresh)
        v.notice = prompt_->notice;
    v.cursor = o.preselected < v.rows.size() ? o.preselected : v.rows.size();
    if (kept_new) v.cursor = v.rows.size();
    else if (kept)
        for (std::size_t i = 0; i < v.rows.size(); ++i)
            if (v.rows[i].wid == *kept) v.cursor = i;
    prompt_ = std::move(v);
    trace("prompt %s form=%s stage=%s rows=%zu cursor=%s preselected=%s rows=[%s]", refresh ? "refresh" : "open",
          form == PromptForm::gate ? "gate" : "loading", prompt_->stage.c_str(), prompt_->rows.size(),
          prompt_->new_instance() ? "new" : std::to_string(prompt_->cursor).c_str(),
          o.preselected < o.rows.size() ? std::to_string(o.rows[o.preselected].wid).c_str() : "new", prompt_rows_text().c_str());
    if (!refresh) {
        log("arrival prompt (%s form) for %s: %zu instance(s)", form == PromptForm::gate ? "gate" : "loading-screen", prompt_->stage.c_str(),
            prompt_->rows.size());
        auto_now_ = config_.auto_choice;
        if (!config_.auto_sequence.empty()) {
            auto_now_ = config_.auto_sequence.front();
            if (config_.auto_sequence.size() > 1) config_.auto_sequence.erase(config_.auto_sequence.begin());
        }
        if (auto_now_ != AutoChoice::none) auto_answer_at_ = now + config_.auto_choice_delay;
    }
}

std::string Session::prompt_rows_text() const {
    std::string text;
    if (!prompt_) return text;
    for (const auto& r : prompt_->rows) {
        if (!text.empty()) text += ",";
        text += std::to_string(r.wid) + ":" + r.host_name + ":" + state_text(r.state) + ":" + std::to_string(r.here) + ":" + r.host_stage;
    }
    return text;
}

void Session::prompt_input(std::uint32_t buttons, double now) {
    const std::uint32_t pressed = buttons & ~prompt_buttons_;
    prompt_buttons_ = buttons;
    if (!prompt_ || pressed == 0) return;
    auto& v = *prompt_;
    if (pressed & pad_button::up) {
        if (v.cursor > 0) --v.cursor;
    } else if (pressed & pad_button::down) {
        if (v.cursor < v.rows.size()) ++v.cursor;
    } else if ((pressed & (pad_button::left | pad_button::right)) && v.new_instance()) {
        const int step = (pressed & pad_button::right) ? 1 : 2;
        v.visibility = w::Visibility((int(v.visibility) + step) % 3);
    } else if (pressed & pad_button::x) {
        v.details = !v.details;
    } else if (pressed & pad_button::a) {
        if (!v.new_instance() && !w::choosable(v.rows[v.cursor].state)) return; // (a greyed row: not choosable before round I3)
        answer_prompt(false, now);
    } else if (pressed & pad_button::b) {
        answer_prompt(true, now);
    }
}

void Session::answer_prompt(bool back, double now) {
    (void)now;
    if (!prompt_) return;
    const auto v = *prompt_;
    w::PromptChoice c;
    if (v.form == PromptForm::gate) c.plan = prompt_plan_;
    else {
        c.inst = inst_;
        c.visit = visit_;
    }
    if (back) {
        c.choice = w::ChoiceKind::back;
    } else if (v.new_instance()) {
        c.choice = w::ChoiceKind::new_instance;
        c.visibility = v.visibility;
    } else {
        c.choice = w::ChoiceKind::join;
        c.wid = v.rows[v.cursor].wid;
    }
    send(w::Type::choice, w::encode(c));
    trace("prompt answer choice=%s wid=%u visibility=%s form=%s", back ? "back" : c.choice == w::ChoiceKind::join ? "join" : "new", c.wid,
          visibility_word(c.visibility), v.form == PromptForm::gate ? "gate" : "loading");
    chosen_join_ = !back && c.choice == w::ChoiceKind::join;
    chosen_host_ = chosen_join_ ? v.rows[v.cursor].host_name : std::string();
    if (v.form == PromptForm::gate) {
        // A: CHOICE first, then the held press in the next pad (the BARRIER names the plan); B: a B press to the game.
        departure_plan_ = back ? 0 : prompt_plan_;
        gate_phase_ = back ? GatePhase::cancel : GatePhase::release;
        close_prompt(back ? "back" : "answered");
        return;
    }
    // The loading-screen form: the relay answers with BARRIER_OK, a join's hold or a refreshed prompt.
    if (chosen_join_) loading_ = "Joining " + chosen_host_ + "...";
    close_prompt(back ? "back" : "answered");
}

void Session::close_prompt(const char* why) {
    if (!prompt_) return;
    trace("prompt closed (%s)", why);
    prompt_.reset();
    auto_answer_at_ = -1;
}

void Session::auto_answer(double now) {
    auto_answer_at_ = -1;
    if (!prompt_) return;
    auto& v = *prompt_;
    switch (auto_now_) {
    case AutoChoice::none: return;
    case AutoChoice::back: answer_prompt(true, now); return;
    case AutoChoice::new_instance: v.cursor = v.rows.size(); break;
    case AutoChoice::first: {
        // The first choosable row by 19.7.3's order, wherever the cursor is (New instance when none).
        v.cursor = v.rows.size();
        for (std::size_t i = 0; i < v.rows.size(); ++i)
            if (w::choosable(v.rows[i].state)) {
                v.cursor = i;
                break;
            }
        break;
    }
    case AutoChoice::cursor: break; // (the preselection: A at once)
    }
    answer_prompt(false, now);
}

void Session::prompt_lost(double now) {
    (void)now;
    if (gate_phase_ == GatePhase::waiting || gate_phase_ == GatePhase::open) {
        // The held press is dropped and the menu closed (the player stands at the gate as after B).
        close_prompt("connection lost");
        gate_phase_ = GatePhase::cancel;
        departure_plan_ = 0;
        notice("Connection lost. Try the gate again in a moment.");
    } else if (prompt_) {
        close_prompt("connection lost");
    }
    prompt_hold_ = false;
}

} // namespace dw4::game::online
