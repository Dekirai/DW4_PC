// Seamless online co-op, own windows and scenes (docs/SEAMLESS.md 5.3, round
// 63): online, outside the hub and UI stages, a member's status window and its
// local scenes (NPC talks and map recorders, the hit_n_* scripts) belong to
// that member alone. They run in the simulation like everything else - every
// engine of the instance runs them with the member's sealed pads - but they
// pause nothing and show only on the member's own screen:
//
// - Inside a member's scope (Engine::SceneScope: its window's run, the script
//   run of its scene, the demo works and posts these start) the messenger
//   narrows the instance-wide commands to the member (PLAYER -1 OFCTRL /
//   ONCTRL: its player stands without its input but keeps its control, so it
//   can be hit), keeps the protective ones from the instance (ENEMY -1 OFCTRL
//   / ONCTRL, every category freeze, GENERATER, the quest timer's stop),
//   turns STAGECTL PADINPUT into the member's own pad switch and LAYERCTL
//   into its own picture's layer filter.
// - The window models and cursors it addresses are the member's own copies
//   ("i_messwn@1"): the model's subtree cloned under the same parent (a new
//   child of its action switch) with its own MODELCTRL, ACTION and SELECTCTRL
//   RAMs, made on first use, the same on every machine. Text lines are copied
//   the same way (TextCtlManager). So two or more members can have windows at
//   once, and an instance-wide scene keeps the originals.
// - Every machine's picture leaves out the other members' copies and applies
//   only its own member's layer filter; only its own copies' sound effects
//   play (SoundManager's listener).
//
// Offline, in hub stages and with online off nothing here acts: no scope is
// ever entered, so the messenger, the managers and the picture run the
// original code (det.sh, the story runs and the save state suite prove it).
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>

namespace dw4::engine {

namespace {

// The models a member's own window or scene draws as its own copies: the
// interface models (i_*, j_*) and the NPC portrait of a talk window (n_*ic),
// but not the HUD and the other overlays every member shares (the status
// panels and their icons, damage and item numbers, the get-item panels, the
// boss HP, the quest timer and name, the exit prompt, the evolution overlays).
bool own_copy_model(std::string_view name) {
    if (name.find('@') != std::string_view::npos) return false; // a copy already
    if (name.starts_with("n_") && name.ends_with("ic")) return true;
    if (!name.starts_with("i_") && !name.starts_with("j_")) return false;
    static constexpr std::string_view shared[] = {"i_stpnl",   "i_stdownic", "i_wzsl",     "i_dmgnum",  "i_pcdmgnum", "i_bitnum",
                                                  "i_expnum",  "i_getitempnl", "i_rareicon", "i_bcnumic", "i_pcnumic",  "i_bosshp",
                                                  "i_timenm",  "i_questnm",  "i_zzstchg",  "i_digilf",  "i_evo",      "i_pointcur",
                                                  "i_breakcur"};
    for (const auto prefix : shared)
        if (name.starts_with(prefix)) return false;
    return true;
}

std::string copy_name(std::string_view name, int slot) { return std::string(name) + "@" + char('0' + slot); }

constexpr std::uint8_t bit(int slot) { return std::uint8_t(1u << slot); }

Node* parent_of(Node* from, const Node* child, int depth = 0) {
    if (!from || depth > 96) return nullptr;
    for (auto* c : from->children) {
        if (c == child) return from;
        if (auto* found = parent_of(c, child, depth + 1)) return found;
    }
    return nullptr;
}

} // namespace

bool Engine::scenes_scoped() const { return online_.enabled && !hub_stage(stage_name_); }

Engine::SceneScope::SceneScope(Engine& engine, int slot)
    : engine_(engine), saved_scope_(engine.scene_scope_), saved_trigger_(engine.trigger_slot_) {
    const int scope = slot >= 0 && slot < 4 ? slot : -1;
    engine.scene_scope_ = scope;
    engine.messenger_.set_scope(scope);
    engine.demo_.set_scope(scope);
    // (Its pad redirect: an index-0 window of the scope reads the member's pad, 7.4.)
    if (scope >= 0) engine.set_trigger(scope);
}

Engine::SceneScope::~SceneScope() {
    engine_.scene_scope_ = saved_scope_;
    engine_.messenger_.set_scope(saved_scope_);
    engine_.demo_.set_scope(saved_scope_);
    if (engine_.trigger_slot_ != saved_trigger_) engine_.set_trigger(saved_trigger_);
}

void Engine::set_view_slot(int slot) {
    view_slot_ = slot < 0 || slot > 3 ? 0 : slot;
    if (auto* sound = get<SoundManager>()) sound->set_listener_slot(online_.enabled ? view_slot_ : -1);
}

bool Engine::scope_message(std::string_view category, std::optional<std::string_view>& name, std::int32_t& index,
                           std::string_view command, Packet& packet, std::string& storage) {
    const int slot = scene_scope_;
    if (slot < 0) return true;
    // The original's protection of a window or talk: the instance's freezes
    // (every category's ONFREEZE / OFFREEZE to all its RAMs, the generators),
    // the enemies' control and the quest timer's stop. None of it reaches the
    // instance: it plays on, and the member can be hit.
    if ((command == "ONFREEZE" || command == "OFFREEZE") && (index == -1 || !name || category == "GENERATER")) return false;
    if (category == "ENEMY" && index == -1 && (command == "OFCTRL" || command == "ONCTRL")) return false;
    if (name && name->starts_with("i_timenm") && (command == "SQ_STOP" || command == "SQ_RESTART")) return false;
    // The instance's music stays (the status window and the talks do not
    // change it; SoundManager keeps a scope's BGM starts out as well).
    if (category == "SOUND" && (command == "BGMSTOP" || command == "ENVSTOP" || command == "SESTOP")) return false;
    // Every player's control: the member's own player stands without its
    // input instead (it keeps its control: it can be hit, hold_own_input);
    // another member's player is not the scene's.
    if (category == "PLAYER" && (command == "OFCTRL" || command == "ONCTRL")) {
        if (index == -1 || index == slot) hold_own_input(slot, false, command == "OFCTRL");
        return false;
    }
    // The pad input: the member's own pad.
    if (category == "STAGECTL" && command == "PADINPUT") {
        const bool off = !packet.strings.empty() && packet.string(0) == "DISABLE";
        if (off) scoped_pads_off_ |= bit(slot);
        else scoped_pads_off_ &= std::uint8_t(~bit(slot));
        return false;
    }
    // The layers: the member's own picture.
    if (category == "LAYERCTL" && command == "SWITCH") {
        if (packet.strings.size() < 2) return false;
        const std::string group(packet.string(0));
        const auto mode = packet.string(1);
        auto& off = scoped_layers_off_[std::size_t(slot)];
        const auto found = std::find(off.begin(), off.end(), group);
        const bool hidden = found != off.end();
        const bool hide = mode == "CLR" || mode == "DISPOFF" ? true : mode == "SET" || mode == "DISPON" ? false : mode == "FLIP" ? !hidden : hidden;
        if (group == "ALL") {
            off.clear();
            if (hide) off.push_back("ALL");
        } else if (hide && !hidden) {
            off.push_back(group);
        } else if (!hide && hidden) {
            off.erase(found);
        }
        return false;
    }
    // The member's own copies of the window models and cursors.
    if (name && own_copy_model(*name) && (category == "MODELCTRL" || category == "ACTION" || category == "SELECTCTRL")) {
        if (index == -1) {
            if (const auto* models = get<ModelCtrlManager>())
                for (const auto& ram : models->rams())
                    if (ram->info_name == *name) make_window_copy(*name, ram->index, slot);
        } else {
            make_window_copy(*name, index, slot);
        }
        storage = copy_name(*name, slot);
        name = storage;
    }
    return true;
}

Node* Engine::clone_subtree(const Node* root, std::unordered_map<const Node*, Node*>& map) {
    if (!root) return nullptr;
    if (const auto found = map.find(root); found != map.end()) return found->second;
    auto copy = std::make_unique<Node>(*root);
    Node* node = copy.get();
    copy_nodes_.push_back(std::move(copy));
    map[root] = node;
    for (auto& child : node->children) child = clone_subtree(child, map);
    for (auto& appearance : node->appearance_roots)
        if (const auto found = map.find(appearance); found != map.end()) appearance = found->second;
    if (node->copy_source)
        if (const auto found = map.find(node->copy_source); found != map.end()) node->copy_source = found->second;
    if (node->copy_actor)
        if (const auto found = map.find(node->copy_actor); found != map.end()) node->copy_actor = found->second;
    return node;
}

// The member's own copy of a window model's RAM `index`: its subtree (the
// action switch's child that holds it, a new child of that switch, hidden
// until its action shows it) and its MODELCTRL, ACTION and SELECTCTRL RAMs
// under the name "name@slot". false: the model has no such RAM.
bool Engine::make_window_copy(std::string_view name, int index, int slot) {
    const std::string copy = copy_name(name, slot);
    const std::string key = copy + "#" + std::to_string(index);
    if (copies_made_.contains(key)) return true;
    auto* models = get<ModelCtrlManager>();
    auto* actions = get<ActionManager>();
    auto* selects = get<SelectCtrlManager>();
    auto* model = models ? models->find(name, index) : nullptr;
    const auto* action = actions ? actions->find(name, index) : nullptr;
    const bool select = selects && selects->has(name, index);
    if (!model && !action && !select) return false;
    copies_made_.insert(key);
    std::unordered_map<const Node*, Node*> map;
    Node* parent = nullptr;
    Node* root = nullptr;
    std::size_t child = 0;
    if (action && action->switch_node && action->switch_child < action->switch_node->children.size()) {
        parent = action->switch_node;
        root = clone_subtree(parent->children[action->switch_child], map);
    } else if (model && model->node) {
        parent = parent_of(&global_root_, model->node);
        if (parent) root = clone_subtree(model->node, map);
    }
    if (parent && root) {
        parent->children.push_back(root);
        child = parent->children.size() - 1;
        if (parent->kind == NodeKind::switch_node) parent->set_child_enabled(child, false);
        copies_.push_back({parent, root, slot});
    }
    const auto remap = [&](const Node* node) -> Node* {
        if (!node) return nullptr;
        const auto found = map.find(node);
        return found != map.end() ? found->second : nullptr;
    };
    if (model) models->add_copy(*this, *model, copy, remap);
    if (action) actions->add_copy(*this, *action, copy, remap, child);
    if (select) selects->add_copy(*this, name, index, copy);
    if (static const bool trace = std::getenv("DW4_TRACE_SCENES") != nullptr; trace)
        log("own copy " + key + (root ? " (" + std::to_string(map.size()) + " nodes)" : " (no subtree)"));
    return true;
}

Node* Engine::scene_copy_line(const Node* child, const Node* line, Node* parent, std::size_t& index) {
    if (!child || !line || !parent || scene_scope_ < 0) return nullptr;
    std::unordered_map<const Node*, Node*> map;
    Node* root = clone_subtree(child, map);
    const auto found = map.find(line);
    if (!root || found == map.end()) return nullptr;
    Node* copy = found->second;
    // A line's colour is its own (TEXTCTL COLOR edits the line node's colour
    // attributes; the copy's are copies).
    for (auto& attribute : copy->attributes) {
        if (!attribute || attribute->kind != AttrKind::color) continue;
        copy_attributes_.push_back(std::make_unique<Attribute>(*attribute));
        attribute = copy_attributes_.back().get();
    }
    parent->children.push_back(root);
    index = parent->children.size() - 1;
    if (parent->kind == NodeKind::switch_node) parent->set_child_enabled(index, false);
    copies_.push_back({parent, root, scene_scope_});
    return copy;
}

void Engine::clear_scene_copies() {
    for (const auto& copy : copies_)
        if (copy.parent) std::erase(copy.parent->children, copy.root);
    copies_.clear();
    copies_made_.clear();
    copy_nodes_.clear();
    copy_attributes_.clear();
    scoped_pads_off_ = 0;
    own_window_hold_ = own_scene_hold_ = 0;
    for (auto& layers : scoped_layers_off_) layers.clear();
}

void Engine::hold_own_input(int slot, bool window, bool on) {
    if (slot < 0 || slot > 3) return;
    auto& mask = window ? own_window_hold_ : own_scene_hold_;
    if (on) mask |= bit(slot);
    else mask &= std::uint8_t(~bit(slot));
}

void Engine::release_scene_holds() {
    if (!own_scene_hold_) return;
    for (int slot = 0; slot < 4; ++slot)
        if ((own_scene_hold_ & bit(slot)) && !own_scene_running(slot)) own_scene_hold_ &= std::uint8_t(~bit(slot));
}

bool Engine::own_window_open(int slot) const {
    const auto* windows = get<SubWindowManager>();
    return windows && windows->own_window_open(slot);
}

bool Engine::own_scene_running(int slot) const {
    return slot >= 0 && slot < 4 && demo_.running_for(slot) != demo_.running_unscoped();
}

std::uint8_t Engine::busy_slots() const {
    std::uint8_t busy = 0;
    for (int slot = 0; slot < 4; ++slot)
        if (own_window_open(slot) || own_scene_running(slot)) busy |= bit(slot);
    return busy;
}

void Engine::end_own_scenes(int slot) {
    if (slot < 0 || slot > 3) return;
    if (auto* windows = get<SubWindowManager>()) windows->end_own_window(*this, slot);
    demo_.drop_scope(slot);
    // (Posted messages of the scope still arrive: they reach the member's own
    // copies, which nobody sees, and its slot, which no player holds.)
    scoped_pads_off_ &= std::uint8_t(~bit(slot));
    own_window_hold_ &= std::uint8_t(~bit(slot));
    own_scene_hold_ &= std::uint8_t(~bit(slot));
    scoped_layers_off_[std::size_t(slot)].clear();
    // Its copies hide: a member who joins this slot later starts with its own
    // copies hidden (their actions show them again on use).
    for (const auto& copy : copies_)
        if (copy.slot == slot && copy.parent && copy.parent->kind == NodeKind::switch_node)
            for (std::size_t i = 0; i < copy.parent->children.size(); ++i)
                if (copy.parent->children[i] == copy.root) copy.parent->set_child_enabled(i, false);
}

bool Engine::scoped_picture_filter() const {
    for (const auto& copy : copies_)
        if (copy.slot != view_slot_) return true;
    return !scoped_layers_off_[std::size_t(view_slot_)].empty();
}

void Engine::scoped_picture_hidden(std::vector<const Node*>& out) {
    for (const auto& copy : copies_)
        if (copy.slot != view_slot_ && copy.root) out.push_back(copy.root);
    const auto& off = scoped_layers_off_[std::size_t(view_slot_)];
    if (off.empty()) return;
    if (const auto* layers = get<LayerManager>()) {
        if (std::find(off.begin(), off.end(), "ALL") != off.end()) {
            for (const auto* group : layers->groups()) out.push_back(group);
            return;
        }
        for (const auto& name : off)
            if (const Node* group = layers->group(name)) out.push_back(group);
    }
}

} // namespace dw4::engine
