#include "console_nav.hpp"

#include <cmath>

namespace dw4::game::console {

bool value_row(RowKind kind) {
    return kind == RowKind::toggle || kind == RowKind::choice || kind == RowKind::stepper || kind == RowKind::cells || kind == RowKind::binding;
}

bool text_row(RowKind kind) { return kind == RowKind::text || kind == RowKind::password || kind == RowKind::number; }

// ---------------------------------------------------------------- buttons

namespace {
constexpr std::uint32_t kRepeating = kDirections | bit(Button::l2) | bit(Button::r2);
}

void Buttons::update(std::uint32_t down, double now) {
    latched_ &= down; // (a let-go button leaves the latch)
    const std::uint32_t effective = down & ~latched_;
    const std::uint32_t before = down_ & ~latched_;
    fresh_ = effective & ~before;
    fired_ = fresh_;
    for (unsigned i = 0; i < unsigned(Button::count); ++i) {
        const std::uint32_t b = 1u << i;
        if (fresh_ & b) {
            pressed_at_[i] = now;
            next_[i] = now + kRepeatDelay;
        } else if ((effective & b) && (kRepeating & b) && now >= next_[i]) {
            fired_ |= b;
            next_[i] = now + kRepeatEvery;
        }
    }
    down_ = down;
}

double Buttons::held_for(Button b, double now) const { return held(b) ? now - pressed_at_[unsigned(b)] : 0.0; }

// ---------------------------------------------------------------- focus

int next_focusable(const std::vector<NavRow>& rows, int from, int dir, bool wrap) {
    const int n = int(rows.size());
    if (n == 0 || dir == 0) return -1;
    int i = from;
    for (int k = 0; k < n; ++k) {
        i += dir;
        if (i < 0 || i >= n) {
            if (!wrap) return -1;
            i = i < 0 ? n - 1 : 0;
        }
        if (rows[std::size_t(i)].focusable) return i;
    }
    return -1;
}

int first_focusable(const std::vector<NavRow>& rows) { return next_focusable(rows, -1, 1, false); }

int last_focusable(const std::vector<NavRow>& rows) { return next_focusable(rows, int(rows.size()), -1, false); }

int resolve_focus(const std::vector<NavRow>& rows, const std::string& id, int old_index) {
    if (!id.empty())
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == id && rows[i].focusable) return int(i);
    if (old_index < 0) return first_focusable(rows);
    const int start = std::min(old_index, int(rows.size())) - 1;
    if (const int below = next_focusable(rows, start, 1, false); below >= 0) return below;
    return next_focusable(rows, std::min(old_index, int(rows.size())), -1, false);
}

// ---------------------------------------------------------------- pages

void NavCore::set_categories(std::vector<std::string> roots) {
    roots_ = std::move(roots);
    if (category_ >= int(roots_.size())) category_ = 0;
    if (stack_.empty() || std::find(roots_.begin(), roots_.end(), stack_.front()) == roots_.end()) {
        stack_.clear();
        if (!roots_.empty()) stack_.push_back(roots_[std::size_t(category_)]);
        show_page_memo();
    } else {
        // (A category came or went: the shown root keeps its place.)
        category_ = int(std::find(roots_.begin(), roots_.end(), stack_.front()) - roots_.begin());
    }
}

void NavCore::select_category(int index) {
    if (roots_.empty()) return;
    remember();
    category_ = std::clamp(index, 0, int(roots_.size()) - 1);
    stack_ = {roots_[std::size_t(category_)]};
    show_page_memo();
    if (const auto found = home.find(stack_.front()); found != home.end() && found->second != focus_id) {
        focus_id = found->second;
        focus = -1;
    }
}

std::string NavCore::path() const {
    std::string out;
    for (const auto& part : stack_) out += (out.empty() ? "" : "/") + part;
    return out;
}

void NavCore::remember() {
    if (stack_.empty()) return;
    PageMemo& m = memo_[path()];
    m.row = focus_id;
    m.index = focus;
    m.scroll = scroll;
}

void NavCore::show_page_memo() {
    const PageMemo& m = memo_[path()];
    focus_id = m.row;
    focus = m.index;
    scroll = m.scroll;
}

void NavCore::open_page(const std::string& id) {
    remember();
    stack_.push_back(id);
    show_page_memo();
}

bool NavCore::pop_page() {
    if (stack_.size() <= 1) return false;
    remember();
    stack_.pop_back();
    show_page_memo();
    return true;
}

void NavCore::page_invalid() {
    if (stack_.size() <= 1) return;
    stack_.pop_back();
    show_page_memo();
}

bool NavCore::request_focus(const std::string& page_path, const std::string& row_id, bool show_cursor) {
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (at <= page_path.size()) {
        const auto slash = page_path.find('/', at);
        const auto part = page_path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
        if (!part.empty()) parts.push_back(part);
        if (slash == std::string::npos) break;
        at = slash + 1;
    }
    if (parts.empty()) return false;
    const auto root = std::find(roots_.begin(), roots_.end(), parts.front());
    if (root == roots_.end()) return false;
    select_category(int(root - roots_.begin()));
    for (std::size_t i = 1; i < parts.size(); ++i) open_page(parts[i]);
    if (!row_id.empty()) {
        focus_id = row_id;
        focus = -1;
    }
    if (show_cursor) {
        area = Area::list;
        mode = InputMode::pad;
    }
    return true;
}

void NavCore::set_focus(const std::vector<NavRow>& rows, int index) {
    focus = index;
    focus_id = index >= 0 && index < int(rows.size()) ? rows[std::size_t(index)].id : std::string();
    cell = 0;
    if (index >= 0 && index < int(rows.size())) {
        const auto found = cells.find(focus_id);
        if (found != cells.end()) cell = std::clamp(found->second, 0, std::max(rows[std::size_t(index)].cells - 1, 0));
    }
}

void NavCore::resolve(const std::vector<NavRow>& rows) { set_focus(rows, resolve_focus(rows, focus_id, focus)); }

void NavCore::open_modal(ModalKind kind, const std::string& row, int count, int focus_at, double now, Buttons& buttons) {
    modal = {};
    modal.kind = kind;
    modal.row = row;
    modal.count = count;
    modal.focus = std::clamp(focus_at, 0, std::max(count - 1, 0));
    modal.opened = now;
    buttons.latch();
}

void NavCore::close_modal(Buttons& buttons) {
    modal = {};
    buttons.latch();
}

// ---------------------------------------------------------------- a pass's buttons

std::vector<Command> NavCore::step(Buttons& b, double now, const std::vector<NavRow>& rows, int visible, float right_y) {
    std::vector<Command> out;
    if (focus_id.empty() || focus < 0 || focus >= int(rows.size()) || rows[std::size_t(focus)].id != focus_id) resolve(rows);
    const std::uint32_t fresh = b.any_fresh();
    // Touch mode: the first direction, A or X only shows the cursor; B,
    // Start, L1 / R1, L2 / R2 act at once (and the cursor shows).
    if (mode == InputMode::touch && modal.kind == ModalKind::none && fresh) {
        mode = InputMode::pad;
        const std::uint32_t show_only = kDirections | bit(Button::a) | bit(Button::x);
        if ((fresh & show_only) && !(fresh & ~show_only)) {
            if (area == Area::list && focus < 0) set_focus(rows, first_focusable(rows));
            out.push_back({Cmd::cursor});
            b.latch();
            return out;
        }
    }
    if (fresh) mode = InputMode::pad;
    const auto clamp_focus = [&](int target) {
        if (target >= 0) set_focus(rows, target);
    };
    // ---- A modal takes every button.
    if (modal.kind != ModalKind::none) {
        switch (modal.kind) {
        case ModalKind::picker: {
            int moved = modal.focus;
            if (b.fired(Button::up)) --moved;
            if (b.fired(Button::down)) ++moved;
            if (b.fired(Button::l2)) moved -= std::max(visible, 1);
            if (b.fired(Button::r2)) moved += std::max(visible, 1);
            moved = std::clamp(moved, 0, std::max(modal.count - 1, 0));
            if (moved != modal.focus) {
                modal.focus = moved;
                out.push_back({Cmd::modal_move});
            }
            if (b.fresh(Button::a) || b.fresh(Button::start)) out.push_back({Cmd::modal_ok});
            else if (b.fresh(Button::b)) out.push_back({Cmd::modal_cancel});
            break;
        }
        case ModalKind::confirm: {
            int moved = modal.focus;
            if (b.fired(Button::left)) --moved;
            if (b.fired(Button::right)) ++moved;
            moved = std::clamp(moved, 0, std::max(modal.count - 1, 0));
            if (moved != modal.focus) {
                modal.focus = moved;
                out.push_back({Cmd::modal_move});
            }
            if (b.fresh(Button::a) && now - modal.opened >= Modal::kConfirmDelay) out.push_back({Cmd::modal_ok});
            else if (b.fresh(Button::b)) out.push_back({Cmd::modal_cancel});
            break;
        }
        case ModalKind::text:
            if (b.fresh(Button::a) || b.fresh(Button::start)) out.push_back({Cmd::modal_ok});
            else if (b.fresh(Button::b)) out.push_back({Cmd::modal_cancel});
            break;
        case ModalKind::capture:
        case ModalKind::none: break;
        }
        return out;
    }
    const int categories = int(roots_.size());
    const auto category_step = [&](int dir) {
        if (categories == 0) return;
        select_category((category_ + dir + categories) % categories);
        out.push_back({Cmd::category, dir});
    };
    // ---- The sidebar
    if (area == Area::sidebar) {
        if (b.fresh(Button::b)) {
            out.push_back({Cmd::close, 0, "the menu's B"});
            return out;
        }
        if (b.fresh(Button::start)) {
            out.push_back({Cmd::close, 0, "Start"});
            return out;
        }
        if (b.fresh(Button::l1)) category_step(-1);
        else if (b.fresh(Button::r1)) category_step(1);
        for (const auto [button, dir] : {std::pair{Button::up, -1}, std::pair{Button::down, 1}}) {
            if (!b.fired(button) || categories == 0) continue;
            int target = category_ + dir;
            if (target < 0 || target >= categories) {
                if (!b.fresh(button)) continue; // (held: it stays at the end)
                target = (target + categories) % categories;
            }
            select_category(target);
            out.push_back({Cmd::category, dir});
        }
        if (b.fresh(Button::a) || b.fresh(Button::right)) {
            area = Area::list;
            resolve(rows);
            out.push_back({Cmd::reveal});
        }
        return out;
    }
    // ---- The list
    if (b.fresh(Button::start)) {
        out.push_back({Cmd::close, 0, "Start"});
        return out;
    }
    const NavRow* row = focus >= 0 && focus < int(rows.size()) ? &rows[std::size_t(focus)] : nullptr;
    if (b.fresh(Button::b)) {
        if (row && row->own_back) out.push_back({Cmd::row_back});
        else if (pop_page()) {
            out.push_back({Cmd::page_back});
            b.latch();
        } else area = Area::sidebar;
        return out;
    }
    if (b.fresh(Button::l1) || b.fresh(Button::r1)) {
        category_step(b.fresh(Button::l1) ? -1 : 1);
        area = Area::list;
        b.latch();
        return out;
    }
    bool moved = false;
    for (const auto [button, dir] : {std::pair{Button::up, -1}, std::pair{Button::down, 1}}) {
        if (!b.fired(button)) continue;
        const int target = next_focusable(rows, focus, dir, b.fresh(button));
        clamp_focus(target);
        moved = true;
    }
    for (const auto [button, dir] : {std::pair{Button::l2, -1}, std::pair{Button::r2, 1}}) {
        if (!b.fired(button)) continue;
        int target = focus;
        for (int k = 0; k < std::max(visible, 1); ++k) {
            const int next = next_focusable(rows, target, dir, false);
            if (next < 0) break;
            target = next;
        }
        clamp_focus(target);
        moved = true;
    }
    if (b.fresh(Button::home)) {
        clamp_focus(first_focusable(rows));
        moved = true;
    }
    if (b.fresh(Button::end)) {
        clamp_focus(last_focusable(rows));
        moved = true;
    }
    if (moved) {
        out.push_back({Cmd::reveal});
        return out;
    }
    row = focus >= 0 && focus < int(rows.size()) ? &rows[std::size_t(focus)] : nullptr;
    for (const auto [button, dir] : {std::pair{Button::left, -1}, std::pair{Button::right, 1}}) {
        if (!b.fired(button)) continue;
        if (!row) {
            if (dir < 0 && b.fresh(button)) area = Area::sidebar;
            continue;
        }
        if (row->kind == RowKind::cells || row->kind == RowKind::binding) {
            const int target = std::clamp(cell + dir, 0, std::max(row->cells - 1, 0));
            if (target != cell) {
                cell = target;
                cells[focus_id] = cell;
                out.push_back({Cmd::reveal});
            }
        } else if (value_row(row->kind)) {
            // (A stepper held 1.5 s steps five at a time.)
            const int steps = row->kind == RowKind::stepper && b.held_for(button, now) >= 1.5 ? 5 : 1;
            out.push_back({Cmd::change, dir * steps});
        } else if (dir < 0) {
            if (b.fresh(button)) area = Area::sidebar;
        } else if (row->kind == RowKind::link && b.fresh(button)) {
            out.push_back({Cmd::open_link});
        }
    }
    if (row && b.fresh(Button::a)) out.push_back({Cmd::activate});
    if (row && row->second && b.fresh(Button::x)) out.push_back({Cmd::second});
    if (std::abs(right_y) > 0.5f) out.push_back({Cmd::scroll, right_y > 0 ? -1 : 1});
    return out;
}

// ---------------------------------------------------------------- touch

void Gesture::down(float x, float y, double now) {
    stopped_fling_ = velocity_ != 0;
    velocity_ = 0;
    active_ = true;
    dragging_ = false;
    long_taken_ = false;
    x0_ = x;
    y0_ = last_y_ = y;
    t0_ = now;
    pending_ = 0;
    samples_.clear();
    samples_.push_back({now, y});
}

void Gesture::move(float x, float y, double now) {
    if (!active_) return;
    if (!dragging_ && (std::abs(y - y0_) > kSlop || std::abs(x - x0_) > kSlop)) {
        dragging_ = true;
        last_y_ = y0_;
    }
    if (dragging_) {
        pending_ += last_y_ - y;
        last_y_ = y;
    }
    samples_.push_back({now, y});
    while (samples_.size() > 2 && now - samples_.front().t > kVelocityWindow) samples_.erase(samples_.begin());
}

bool Gesture::up(float x, float y, double now) {
    if (!active_) return false;
    move(x, y, now);
    active_ = false;
    const bool tap = !dragging_ && !stopped_fling_ && !long_taken_;
    if (dragging_ && samples_.size() >= 2) {
        const Sample& a = samples_.front();
        const Sample& z = samples_.back();
        const double dt = z.t - a.t;
        if (dt > 1e-4) {
            const float v = float((a.y - z.y) / dt);
            if (std::abs(v) >= kFlingStart) velocity_ = std::clamp(v, -kFlingMax, kFlingMax);
        }
    }
    return tap;
}

void Gesture::cancel() {
    active_ = false;
    dragging_ = false;
    pending_ = 0;
    samples_.clear();
}

float Gesture::fling(double dt) {
    if (velocity_ == 0 || dt <= 0) return 0;
    const float d = float(velocity_ * dt);
    velocity_ *= float(std::exp(-dt / kFlingDecay));
    if (std::abs(velocity_) < kFlingStop) velocity_ = 0;
    return d;
}

bool Gesture::long_press(double now, double seconds) {
    if (!active_ || dragging_ || long_taken_ || now - t0_ < seconds) return false;
    long_taken_ = true;
    return true;
}

bool Gesture::pressed_shown(double now) const { return active_ && !dragging_ && now - t0_ >= kPressShow; }

float clamp_scroll(float scroll, float content, float view) { return std::max(0.0f, std::min(scroll, content - view)); }

float reveal_scroll(float scroll, float view, float content, float top, float bottom, float margin, bool first, bool last) {
    float target = scroll;
    if (first) target = 0;
    else if (last) target = content - view;
    else if (top - margin < scroll) target = top - margin;
    else if (bottom + margin > scroll + view) target = bottom + margin - view;
    return clamp_scroll(target, content, view);
}

} // namespace dw4::game::console
