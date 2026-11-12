// Round I1b: go along, the client's part (docs/SEAMLESS.md 19.30.5-19.30.7).
// The relay makes the offers (OFFER), holds a member's request until the
// target's floor is ready and stamps GO; this file keeps the offers and their
// notices for the UI, sends INTENT 4 (go along / withdraw), applies the GO
// events of a step together (Engine::online_go with their mask) and lets a GO
// that waited 300 ticks for a membership-safe step expire, on every engine
// alike.
#include "session.hpp"

#include "dw4/engine/online.hpp"

#include <algorithm>
#include <utility>

namespace dw4::game::online {

namespace w = wire;

namespace {
// A closing line stays this long, greyed (19.30.6).
constexpr double kClosingSeconds = 3.0;
// The notices on screen at once: a viewer has at most three other members.
constexpr std::size_t kMaxNotices = 3;

std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) out += (i ? (i + 1 == names.size() ? " and " : ", ") : "") + names[i];
    return out;
}
} // namespace

GoOffer* Session::find_go_offer(std::uint16_t id) {
    for (auto& o : go_offers_)
        if (o.id == id) return &o;
    return nullptr;
}

std::string Session::go_first_name(const GoOffer* offer) const {
    if (offer && !offer->entrants.empty()) return member_name(offer->entrants.front());
    if (go_target_) return member_name(go_target_);
    return "the others";
}

std::string Session::go_row_text(const GoOffer& o) const {
    if (!o.open && !o.closing.empty()) return o.closing;
    const std::string floor = config_.floor_name ? config_.floor_name(o.stage) : stage_title(o.stage);
    const bool deeper = !o.first_stage.empty() && o.stage != o.first_stage;
    return go_first_name(&o) + " is in " + floor + (deeper ? " now." : ".");
}

std::string Session::go_notice_text(const GoOffer& o) const {
    const std::string floor = config_.floor_name ? config_.floor_name(o.stage) : stage_title(o.stage);
    if (o.late) return go_first_name(&o) + " is in " + floor + ". Go along?";
    std::vector<std::string> names;
    for (const auto e : o.entrants) names.push_back(member_name(e));
    return join_names(names) + " went into " + floor + ". Go along?";
}

std::string Session::go_grey(const GoOffer& o) const {
    if (!o.open) return "(closed)";
    if (go_state_ != GoState::none) return go_offer_ == o.id ? std::string() : "(waiting)";
    if (mode_ != Mode::solo && mode_ != Mode::sealed) return "(loading)";
    if (engine_.pending_stage()) return "(loading)";
    if (!(engine_.alive_mask() & (1u << (slot_ & 3)))) return "(defeated)";
    const auto stage = engine_.stage();
    if (is_hub_stage(stage) || (config_.dungeon_stage && config_.dungeon_stage(stage))) return "(in a dungeon)";
    return {};
}

std::string Session::go_line() const {
    const auto* offer = const_cast<Session*>(this)->find_go_offer(go_offer_);
    const std::string name = go_first_name(offer);
    switch (go_state_) {
    case GoState::asked:
    case GoState::waiting: return "Going along when " + name + "'s floor is ready...";
    case GoState::boss: return name + " is in a boss fight. Going along when it ends.";
    case GoState::stamped:
    case GoState::going: return "Going along...";
    case GoState::none: break;
    }
    return {};
}

std::vector<const GoOffer*> Session::go_notices(double now) const {
    std::vector<const GoOffer*> out;
    for (const auto& o : go_offers_) {
        if (!o.open || o.closed_by_b || o.notice_at < 0 || now >= o.notice_at + o.notice_seconds) continue;
        out.push_back(&o);
        if (out.size() == kMaxNotices) break;
    }
    return out;
}

bool Session::go_along(std::uint16_t offer) {
    auto* o = find_go_offer(offer);
    if (!o || !go_grey(*o).empty() || go_state_ != GoState::none || wid_ == 0 || !connected()) return false;
    send(w::Type::intent, w::encode(w::Intent{inst_, visit_, w::IntentKind::go_along, std::int32_t(offer)}));
    go_state_ = GoState::asked;
    go_offer_ = offer;
    go_target_ = o->entrants.empty() ? 0 : o->entrants.front();
    go_target_stage_ = o->stage;
    go_offer_stage_ = o->stage;
    trace("go along asked inst=%u visit=%u offer=%u", inst_, visit_, unsigned(offer));
    log("go along: asked for offer %u (%s)", unsigned(offer), o->stage.c_str());
    return true;
}

bool Session::cancel_go_along() {
    if (go_state_ != GoState::asked && go_state_ != GoState::waiting && go_state_ != GoState::boss) return false;
    if (connected()) send(w::Type::intent, w::encode(w::Intent{inst_, visit_, w::IntentKind::go_along, 0}));
    trace("go along cancelled inst=%u visit=%u offer=%u", inst_, visit_, unsigned(go_offer_));
    log("go along: cancelled (offer %u)", unsigned(go_offer_));
    // (Until the relay answers: a Cancel that crossed the stamp is too late,
    // the GO runs, and the relay's OFFER with flag 16 says so, on_offer.)
    go_cancelled_offer_ = go_offer_;
    go_state_ = GoState::none;
    go_offer_ = 0;
    return true;
}

bool Session::go_back_press(bool a, bool b) {
    if (mode_ != Mode::parked || go_parked_at_ < 0) {
        go_back_asked_ = false;
        return false;
    }
    if (!go_back_asked_) {
        if (!b) return false;
        // 19.30.7 step 4: the host's leave ends the instance for everyone, so
        // it is asked first, as the F1 menu's *Leave instance*.
        if (is_host()) {
            go_back_asked_ = true;
            return false;
        }
        return go_back_to_lobby();
    }
    if (a) {
        go_back_asked_ = false;
        return go_back_to_lobby();
    }
    if (b) go_back_asked_ = false;
    return false;
}

void Session::close_go_notices(std::uint16_t offer) {
    if (auto* o = find_go_offer(offer)) {
        o->closed_by_b = true;
        o->notice_due = false;
    }
}

bool Session::go_back_to_lobby() {
    if (mode_ != Mode::parked || go_parked_at_ < 0 || wid_ == 0 || !connected()) return false;
    // (From the barrier: the relay answers the held GO barrier with the way
    // home; the host's leave ends the instance, 19.6.5.)
    send(w::Type::intent, w::encode(w::Intent{inst_, visit_, w::IntentKind::leave, 0}));
    trace("go back to the lobby inst=%u visit=%u", inst_, visit_);
    log("go along: back to the lobby from the held barrier");
    go_parked_at_ = -1;
    return true;
}

bool Session::go_notice_blocked() const {
    // 19.30.6: the notice waits during the member's own scene (a talk, a map
    // recorder), a demo of its stage group (a camera tour, an exit prompt),
    // while it is defeated, at a barrier or loading; the F1 menu and the own
    // status window do not hold it.
    if (mode_ != Mode::solo && mode_ != Mode::sealed) return true;
    if (engine_.pending_stage()) return true;
    if (!engine_.membership_safe()) return true;
    if (engine_.own_scene(slot_)) return true;
    if (!(engine_.alive_mask() & (1u << (slot_ & 3)))) return true;
    return false;
}

void Session::on_offer(const w::Offer& m, double now) {
    auto* o = find_go_offer(m.id);
    if (m.op == w::OfferOp::closed) {
        if (!o) return;
        if (m.close == w::OfferClose::gone || m.close == w::OfferClose::none) {
            // Over for this member (it went along, entered a dungeon, left):
            // no closing line.
            std::erase_if(go_offers_, [&](const GoOffer& x) { return x.id == m.id; });
        } else {
            const std::string name = go_first_name(o);
            const std::string stage = m.stage.empty() ? o->stage : m.stage;
            const std::string dungeon = config_.dungeon_name ? config_.dungeon_name(stage) : stage_title(stage);
            const std::string floor = config_.floor_name ? config_.floor_name(stage) : stage_title(stage);
            o->open = false;
            o->notice_due = false;
            o->closed_at = now;
            o->closing = m.close == w::OfferClose::defeated ? name + " was defeated in " + floor + "."
                         : m.close == w::OfferClose::ended  ? std::string("The instance has ended.")
                                                            : name + " left " + dungeon + ".";
        }
        trace("offer closed id=%u reason=%u", unsigned(m.id), unsigned(m.close));
        if (go_cancelled_offer_ == m.id) go_cancelled_offer_ = 0;
        // (The entrants left: a stamped GO lands in a new stage group there,
        // "... left ... before you arrived", not "went on to".)
        if (go_offer_ == m.id && m.close != w::OfferClose::gone && m.close != w::OfferClose::none) go_offer_stage_.clear();
        if (go_offer_ == m.id && (go_state_ == GoState::asked || go_state_ == GoState::waiting || go_state_ == GoState::boss)) {
            // (The relay's go refused names the reason; without it the request
            // ends here as well.)
            go_state_ = GoState::none;
            go_offer_ = 0;
        }
        return;
    }
    if (!o) {
        GoOffer fresh;
        fresh.id = m.id;
        fresh.opened_at = now;
        fresh.first_stage = m.stage;
        go_offers_.insert(go_offers_.begin(), std::move(fresh));
        o = &go_offers_.front();
    } else if (!o->open) {
        // A silent reopen (a re-entry within the relay's reopen time).
        o->open = true;
        o->closing.clear();
        o->closed_at = -1;
    }
    for (const auto e : m.entrants) (void)member_name(e);
    o->entrants = m.entrants;
    o->stage = m.stage;
    o->flags = m.flags;
    if (m.notice_seconds) o->notice_seconds = m.notice_seconds;
    if ((m.flags & w::Offer::notice) && !o->closed_by_b && !(m.flags & w::Offer::closed_by_b)) {
        o->notice_due = true;
        o->late = (m.flags & w::Offer::late) != 0;
        o->notice_at = -1;
        o->notice_stage = m.stage;
    }
    constexpr std::uint8_t kRequestFlags = w::Offer::stamped | w::Offer::boss | w::Offer::waiting;
    if (go_cancelled_offer_ == m.id) {
        if ((m.flags & w::Offer::stamped) && go_state_ == GoState::none) {
            // The own Cancel crossed the stamp (19.30.7): the GO is sealed and
            // runs, the relay keeps the request: "Going along...".
            go_offer_ = m.id;
            go_state_ = GoState::stamped;
            go_target_ = o->entrants.empty() ? 0 : o->entrants.front();
            go_target_stage_ = m.stage;
            log("go along: the cancel came after the stamp; going along (offer %u)", unsigned(m.id));
        }
        if (!(m.flags & kRequestFlags) || go_offer_ == m.id) go_cancelled_offer_ = 0; // (answered)
    }
    // The own request's state as the relay holds it.
    if (go_offer_ == m.id) {
        go_offer_stage_ = m.stage;
        if (m.flags & w::Offer::stamped) go_state_ = go_state_ == GoState::going ? GoState::going : GoState::stamped;
        else if (m.flags & w::Offer::boss) go_state_ = GoState::boss;
        else if (m.flags & w::Offer::waiting) go_state_ = GoState::waiting;
        else if (go_state_ == GoState::stamped) {
            // The relay holds no request any more (it ended without a
            // departure: expired, done, or the instance moved on).
            go_state_ = GoState::none;
            go_offer_ = 0;
        }
    }
    trace("offer %s id=%u flags=%x entrants=%zu stage=%s", m.op == w::OfferOp::open ? "open" : "changed", unsigned(m.id), unsigned(m.flags),
          m.entrants.size(), m.stage.c_str());
    tick_go(now);
}

void Session::tick_go(double now) {
    // A waiting request of a defeated member ends (19.30.7: "Couldn't go
    // along: you were defeated."); the relay hears the withdraw.
    if ((go_state_ == GoState::asked || go_state_ == GoState::waiting || go_state_ == GoState::boss) && (mode_ == Mode::solo || mode_ == Mode::sealed) &&
        !engine_.pending_stage() && !(engine_.alive_mask() & (1u << (slot_ & 3)))) {
        cancel_go_along();
        notice("Couldn't go along: you were defeated.");
    }
    // Closing lines go after their 3 s.
    std::erase_if(go_offers_, [&](const GoOffer& o) { return !o.open && o.closed_at >= 0 && now - o.closed_at > kClosingSeconds; });
    if (wid_ == 0) {
        // At home: no offers (the relay closes them; a DISSOLVE may come first).
        go_offers_.clear();
        if (go_state_ != GoState::going) {
            go_state_ = GoState::none;
            go_offer_ = 0;
        }
        return;
    }
    bool blocked = false, checked = false;
    for (auto& o : go_offers_) {
        if (!o.open || !o.notice_due || o.closed_by_b) continue;
        if (!checked) {
            blocked = go_notice_blocked();
            checked = true;
        }
        if (blocked) break;
        o.notice_due = false;
        o.notice_at = now;
        // (A notice that waited while the entrants moved on names where they
        // are now: "Kim is in Ancient Ruins B2. Go along?", 19.30.5.)
        if (!o.notice_stage.empty() && o.stage != o.notice_stage) o.late = true;
        trace("offer notice id=%u late=%d", unsigned(o.id), o.late ? 1 : 0);
        log("go along: notice for offer %u: %s", unsigned(o.id), go_notice_text(o).c_str());
    }
}

void Session::on_go_refused(const w::WorldNotice& m) {
    const std::string name = m.member ? member_name(m.member) : go_first_name(find_go_offer(go_offer_));
    const std::string stage = m.text.empty() ? go_target_stage_ : m.text;
    const std::string dungeon = config_.dungeon_name ? config_.dungeon_name(stage) : stage_title(stage);
    trace("go refused reason=%u offer=%u", unsigned(m.argument), unsigned(go_offer_));
    log("go along: refused (reason %u)", unsigned(m.argument));
    if (go_state_ != GoState::going) {
        go_state_ = GoState::none;
        go_offer_ = 0;
    }
    switch (w::GoRefused(m.argument)) {
    case w::GoRefused::closed: notice("Couldn't go along: " + name + " left " + dungeon + "."); break;
    case w::GoRefused::defeated: notice("Couldn't go along: you were defeated."); break;
    case w::GoRefused::busy: notice("Couldn't go along: " + name + "'s floor stayed busy."); break;
    case w::GoRefused::moved:
    case w::GoRefused::ended: break; // (no text: the member moved on; the END's own notice)
    case w::GoRefused::not_offered:
    default: notice("Couldn't go along."); break;
    }
}

// The GO events of one step (19.30.7): one Engine::online_go per target with
// the mask of its slots (applied one by one, the first GO's fork would leave
// the second slot on the other side).
void Session::apply_gos(std::vector<w::Event>& gos, bool replaying) {
    while (!gos.empty()) {
        const std::string stage = gos.front().stage;
        const int point = gos.front().point;
        std::uint8_t mask = 0;
        std::erase_if(gos, [&](const w::Event& e) {
            if (e.stage != stage || e.point != point) return false;
            mask = std::uint8_t(mask | (1u << (e.slot & 3)));
            return true;
        });
        const bool own = (mask & (1u << (slot_ & 3))) != 0;
        const bool departs = engine_.go(mask, stage, point);
        trace("go applied inst=%u visit=%u tick=%u mask=%x stage=%s point=%d departs=%d%s", inst_, visit_, tick_, unsigned(mask), stage.c_str(), point,
              departs ? 1 : 0, replaying ? " (replay)" : "");
        if (replaying) continue;
        if (departs) {
            go_state_ = GoState::going;
            go_target_stage_ = stage;
            log("go along: departing to %s point %d at tick %u", stage.c_str(), point, tick_);
        } else if (own) {
            // The own slot dropped out (defeated, leaving): the GO does nothing.
            log("go along: the GO at tick %u did nothing for this member", tick_);
            notice("Couldn't go along: you were defeated.");
            go_state_ = GoState::none;
            go_offer_ = 0;
            if (connected()) send(w::Type::intent, w::encode(w::Intent{inst_, visit_, w::IntentKind::go_along, w::kGoEndedArgument}));
        }
    }
}

void Session::go_expired(bool replaying) {
    trace("go expired inst=%u visit=%u tick=%u%s", inst_, visit_, tick_, replaying ? " (replay)" : "");
    if (replaying) return;
    log("go along: the GO expired at tick %u", tick_);
    notice("Couldn't go along: a scene here took too long.");
    go_state_ = GoState::none;
    go_offer_ = 0;
    if (connected()) send(w::Type::intent, w::encode(w::Intent{inst_, visit_, w::IntentKind::go_along, w::kGoEndedArgument}));
}

bool Session::test_hook_request(w::TestHook hook, int value) {
#ifdef DW4_ONLINE_DEV
    if (mode_ != Mode::sealed || !connected()) return false;
    const std::int32_t argument = std::int32_t(std::uint8_t(hook)) | (std::max(0, std::min(value, 0x7FFFFF)) << 8);
    send(w::Type::intent, w::encode(w::Intent{inst_, visit_, w::IntentKind::test, argument}));
    trace("test hook asked inst=%u visit=%u hook=%u value=%d", inst_, visit_, unsigned(hook), value);
    return true;
#else
    (void)hook, (void)value;
    return false;
#endif
}

bool Session::go_along_raw(std::uint16_t offer) {
#ifdef DW4_ONLINE_DEV
    if (offer == 0 || go_state_ != GoState::none || wid_ == 0 || !connected()) return false;
    send(w::Type::intent, w::encode(w::Intent{inst_, visit_, w::IntentKind::go_along, std::int32_t(offer)}));
    go_state_ = GoState::asked;
    go_offer_ = offer;
    if (const auto* o = find_go_offer(offer)) {
        go_target_ = o->entrants.empty() ? 0 : o->entrants.front();
        go_target_stage_ = o->stage;
        go_offer_stage_ = o->stage;
    }
    trace("go along asked inst=%u visit=%u offer=%u (raw)", inst_, visit_, unsigned(offer));
    return true;
#else
    (void)offer;
    return false;
#endif
}

} // namespace dw4::game::online
