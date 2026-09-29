#include "dw4/engine/managers.hpp"

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

void MatCtrlManager::clear_stage(Engine&) {
    rams_.clear();
    context_ = nullptr;
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
        // 80302F94: set bit (1 << player) for every mat box containing the player.
        for (auto& mat : ram.mats) mat.current = 0;
        for (int player = 0; player < 4 && models; ++player) {
            const auto* model = models->player(player);
            if (!model || !model->node) continue;
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
                engine.run_script(mat.name + ".lua");
                context_ = nullptr;
            }
            mat.previous = mat.current;
        }
    }
}

bool MatCtrlManager::script_call(Engine& engine, const std::string& method, std::vector<Lua4Value>& out) {
    if (method != "MATisAllPlayer" && method != "MATgetPlayerNo" && method != "MATisRideOffPlayer") return false;
    const std::uint32_t current = context_ ? context_->current & 0xFu : 0;
    const std::uint32_t previous = context_ ? context_->previous & 0xFu : 0;
    if (method == "MATisAllPlayer") {
        std::uint32_t joined = 0;
        if (auto* sys = engine.get<GameSystem>())
            for (std::size_t i = 0; i < sys->players.size(); ++i)
                if (sys->players[i].joined) joined |= 1u << i;
        out = {Lua4Value::number(joined && (current & joined) == joined ? 1 : 0)};
        return true;
    }
    if (method == "MATisRideOffPlayer") {
        out = {Lua4Value::number((previous & ~current) ? 1 : 0)};
        return true;
    }
    // The player whose state changed, preferring one that stepped on.
    std::uint32_t changed = current ^ previous;
    std::uint32_t pick = (changed & current) ? (changed & current) : changed ? changed : current;
    int player = 0;
    while (pick && !(pick & 1u)) {
        pick >>= 1;
        ++player;
    }
    out = {Lua4Value::number(player)};
    return true;
}

} // namespace dw4::engine
