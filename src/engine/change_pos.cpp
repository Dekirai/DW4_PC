#include "dw4/engine/managers.hpp"

namespace dw4::engine {

// beChangePosTransform (update 802F4494): the transform ([11] of each RAM)
// stands at the players' centre. After the first frame it follows the centre
// only while every player is within 40 units of it (X/Z); otherwise it holds,
// so the HitLand limit circle under it keeps the players together.
void ChangePosManager::add_info(Engine& engine, const ObjectView& info) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<ChangeRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        if (record) ram->transform = engine.scenes().node(record.ref(11));
        rams_.push_back(std::move(ram));
    }
}

void ChangePosManager::update(Engine& engine) {
    auto* models = engine.get<ModelCtrlManager>();
    if (!models) return;
    // PLAYER GETMATRIX: the generated players' positions.
    std::vector<Vec3f> players;
    for (const auto& ram : models->rams())
        if (ram->is_player && ram->generated && ram->node) {
            const auto& m = ram->node->matrix;
            players.push_back({m(3, 0), m(3, 1), m(3, 2)});
        }
    if (players.empty()) return;
    Vec3f centre{};
    for (const auto& p : players) centre = centre + p;
    // 3 divide, 2 and 4 scale, one or five and more stay the sum.
    const std::size_t count = players.size();
    if (count == 3) centre = centre * (1.0f / 3.0f);
    else if (count == 2) centre = centre * 0.5f;
    else if (count == 4) centre = centre * 0.25f;
    for (auto& ram : rams_) {
        if (!ram->transform) continue;
        bool follow = true;
        if (ram->state != 0)
            for (const auto& p : players) {
                const float dx = p.x - centre.x, dz = p.z - centre.z;
                if (dx * dx + dz * dz >= 1600.0f) {
                    follow = false;
                    break;
                }
            }
        if (follow) ram->last = centre;
        ram->transform->matrix(3, 0) = ram->last.x;
        ram->transform->matrix(3, 1) = ram->last.y;
        ram->transform->matrix(3, 2) = ram->last.z;
        ram->state = 1;
    }
}

} // namespace dw4::engine
