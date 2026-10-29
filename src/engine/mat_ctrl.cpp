#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <bit>

namespace dw4::engine {

void MatCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    std::vector<Mat> mats;
    for (const auto& data : info.list(8)) {
        if (!data) continue;
        Mat mat;
        mat.name = std::string(data.str(2));
        if (const auto box = data.ref(3)) {
            mat.min = box.vec3(2).value_or(Vec3f{});
            mat.max = box.vec3(3).value_or(Vec3f{});
        }
        mats.push_back(std::move(mat));
    }
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<MatRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        ram->running = ram->active;
        ram->visuals = engine.scenes().node(info.ref(7));
        ram->mats = mats;
        engine.messenger().add_row("MATCTRL", ram->info_name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
}

std::optional<Vec3f> MatCtrlManager::mat_centre(std::string_view name) const {
    for (const auto& ram : rams_)
        for (const auto& mat : ram->mats)
            if (mat.name == name || "mat" + mat.name == name)
                return Vec3f{(mat.min.x + mat.max.x) * 0.5f, (mat.min.y + mat.max.y) * 0.5f, (mat.min.z + mat.max.z) * 0.5f};
    return std::nullopt;
}

std::vector<MatCtrlManager::MatBox> MatCtrlManager::mat_boxes() const {
    std::vector<MatBox> out;
    for (const auto& ram : rams_)
        for (const auto& mat : ram->mats) out.push_back({mat.name, mat.min, mat.max});
    return out;
}

void MatCtrlManager::clear_stage(Engine&) {
    rams_.clear();
    context_ = nullptr;
    live_ = 0;
}

bool MatCtrlManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    (void)engine;
    (void)packet;
    auto& ram = static_cast<MatRam&>(base);
    if (command == "SHOWMAT" || command == "HIDEMAT") {
        if (ram.visuals && ram.visuals->kind == NodeKind::switch_node) ram.visuals->set_all_children(command == "SHOWMAT");
        return true;
    }
    if (command == "SHOW_LUACALL" || command == "HIDE_LUACALL") return true; // debug overlay text
    if (command == "PAUSE") { ram.running = false; return true; }
    if (command == "RESTART") { ram.running = true; return true; }
    return false;
}

void MatCtrlManager::update(Engine& engine) {
    auto* models = engine.get<ModelCtrlManager>();
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.running) continue;
        // 803030A8: the first update of a running RAM (+0x18 != 1) only
        // builds its per-mat arrays (current/previous player bits 0, every
        // mat enabled, the manager's player bytes cleared) and sets +0x18;
        // the mat test 80302F94 runs from the next update on. PAUSE/RESTART
        // only switch +0x14 (80303B14), so this happens once per RAM.
        if (!ram.initialized) {
            for (auto& mat : ram.mats) {
                mat.current = mat.previous = 0;
                mat.enabled = true;
            }
            live_ = 0;
            ram.initialized = true;
            continue;
        }
        // 0x80303668..0x8030372C: PLAYER GETMATRIX; a player whose index is
        // in the reply (a generated one) is marked live (manager +0x20 +
        // player), then 80302F94 sets bit (1 << player) for every mat box
        // containing it.
        for (auto& mat : ram.mats) mat.current = 0;
        live_ = 0;
        for (int player = 0; player < 4 && models; ++player) {
            const auto* model = models->player(player);
            if (!model || !model->node) continue;
            live_ |= 1u << player;
            const auto& m = model->node->matrix;
            const Vec3f p{m(3, 0), m(3, 1), m(3, 2)};
            for (auto& mat : ram.mats)
                if (p.x >= mat.min.x && p.x <= mat.max.x && p.y >= mat.min.y && p.y <= mat.max.y && p.z >= mat.min.z &&
                    p.z <= mat.max.z)
                    mat.current |= 1u << player;
        }
        for (auto& mat : ram.mats) {
            if (mat.enabled && (mat.current & 0xFu) != (mat.previous & 0xFu)) {
                context_ = &mat;
                pending_run_.reset();
                // The run's trigger slot (online pad redirect, 5.1): the
                // lowest live player that stepped on in this update.
                const std::uint32_t stepped_on = mat.current & ~mat.previous & live_ & 0xFu;
                const Engine::TriggerScope trigger(engine, stepped_on ? std::countr_zero(stepped_on) : -1);
                engine.run_script(mat.name + ".lua");
                if (pending_run_) mat.enabled = *pending_run_;
                context_ = nullptr;
            }
            mat.previous = mat.current;
        }
    }
}

bool MatCtrlManager::script_call(Engine& engine, const std::string& method, std::span<const Lua4Value> args,
                                 std::vector<Lua4Value>& out) {
    (void)engine;
    if (method == "MATsetRun") {
        pending_run_ = !args.empty() && args[0].type() == Lua4Type::Number && args[0].as_number() != 0;
        return true;
    }
    if (method != "MATisAllPlayer" && method != "MATgetPlayerNo" && method != "MATisRideOffPlayer" &&
        method != "MATgetPlayerMatStat" && method != "MATisPlayer" && method != "MATisRidePlayer")
        return false;
    const std::uint32_t current = context_ ? context_->current & 0xFu : 0;
    const std::uint32_t previous = context_ ? context_->previous & 0xFu : 0;
    if (method == "MATisPlayer") {
        // 80302B14: any player on the running mat.
        out = {Lua4Value::number(current ? 1 : 0)};
        return true;
    }
    if (method == "MATgetPlayerMatStat") {
        // 80302DB8: 3 staying on, 2 stepped on, 1 stepped off, 0 away.
        const int slot = !args.empty() && args[0].type() == Lua4Type::Number ? int(args[0].as_number()) : 0;
        const std::uint32_t bit = slot >= 0 && slot < 4 ? 1u << slot : 0u;
        const bool now = (current & bit) != 0, before = (previous & bit) != 0;
        out = {Lua4Value::number(now && before ? 3 : now ? 2 : before ? 1 : 0)};
        return true;
    }
    if (method == "MATisAllPlayer") {
        // 80302B40: every live player (manager +0x20..+0x23) on the mat - not
        // every joined one: with a defeated player's bit capsule out, the
        // others can still take an exit (dng411b1 two-player run: player 2
        // gets the gate prompt alone). With nobody alive it is true.
        out = {Lua4Value::number((current & live_) == live_ ? 1 : 0)};
        return true;
    }
    // 80302D08 / MATisRideOffPlayer: the player stepped on / off this frame
    // (without an argument: any player).
    if (method == "MATisRidePlayer" || method == "MATisRideOffPlayer") {
        std::uint32_t mask = 0xFu;
        if (!args.empty() && args[0].type() == Lua4Type::Number) mask = 1u << std::clamp(int(args[0].as_number()), 0, 3);
        const std::uint32_t changed = method == "MATisRidePlayer" ? current & ~previous : previous & ~current;
        out = {Lua4Value::number((changed & mask) ? 1 : 0)};
        return true;
    }
    // MATgetPlayerNo (80302C3C): the first live player that stepped on the
    // mat this update, else 0.
    const std::uint32_t stepped_on = current & ~previous & live_;
    int player = 0;
    while (player < 4 && !(stepped_on & (1u << player))) ++player;
    out = {Lua4Value::number(player < 4 ? player : 0)};
    return true;
}

} // namespace dw4::engine
