// Save state files: the stage snapshot (StageSnapshot) as bytes.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/state_archive.hpp"

namespace dw4 {

void serialize(engine::StateArchive& a, PadStick& v) {
    a.io(v.x);
    a.io(v.y);
}
void serialize(engine::StateArchive& a, PadMaskState& v) {
    a.io(v.held);
    a.io(v.previous);
    a.io(v.trigger);
    a.io(v.repeat);
    a.io(v.repeat_countdown);
}
void serialize(engine::StateArchive& a, PadState& v) {
    a.io(v.connected);
    a.io(v.left);
    a.io(v.right);
    a.io(v.left_directions);
    a.io(v.right_directions);
    a.io(v.buttons);
}
void serialize(engine::StateArchive& a, PadSample& v) {
    a.io(v.connected);
    a.io(v.left);
    a.io(v.right);
    a.io(v.buttons);
}

} // namespace dw4

namespace dw4::engine {

void serialize(StateArchive& a, ItemDevice& v) {
    a.io(v.type);
    a.io(v.record);
    a.io(v.flag);
    a.io(v.rank);
    a.io(v.value);
}
void serialize(StateArchive& a, GameSystem::Stats& v) {
    a.io(v.ready);
    a.io(v.level);
    a.io(v.species);
    a.io(v.tsuyosa);
    a.io(v.mamori);
    a.io(v.kashikosa);
    a.io(v.seishin);
    a.io(v.subayasa);
    a.io(v.meichuritsu);
    a.io(v.chimeiritsu);
    a.io(v.mahiritsu);
    a.io(v.attack_delay);
    a.io(v.attack_zokusei);
    a.io(v.resist);
    a.io(v.kaihiritsu);
    a.io(v.bougyoritsu);
    a.io(v.mahiteikou);
    a.io(v.dokuteikou);
    a.io(v.exp);
    a.io(v.next_exp);
    a.io(v.exp_rate);
    a.io(v.bit_rate);
    a.io(v.luck_rate);
    a.io(v.hp_drain);
    a.io(v.mp_drain);
    a.io(v.weapon_record);
    a.io(v.weapon_skill_no);
    a.io(v.skill);
    a.io(v.weapon_model);
}
void serialize(StateArchive& a, GameSystem::PlayerSlot::Disk& v) {
    a.io(v.record);
    a.io(v.count);
}
void serialize(StateArchive& a, GameSystem::PlayerSlot& v) {
    a.io(v.joined);
    a.io(v.unique);
    a.io(v.name);
    a.io(v.digimon);
    a.io(v.weapons);
    a.io(v.hp);
    a.io(v.max_hp);
    a.io(v.mp);
    a.io(v.max_mp);
    a.io(v.stats);
    a.io(v.devices);
    a.io(v.bank_devices);
    a.io(v.disks);
    a.io(v.bits);
    a.io(v.bank_bits);
    a.io(v.xdata);
    a.io(v.upcnt);
    a.io(v.updata);
    a.io(v.downdata);
    a.io(v.power_socket);
    a.io(v.power_pocket);
    a.io(v.main_slot);
    a.io(v.main_pocket);
    a.io(v.sub_slot);
    a.io(v.wazalist);
    a.io(v.base_isuse);
    a.io(v.base_level);
    a.io(v.base_exp);
    a.io(v.base_skill);
    a.io(v.base_upcnt);
    a.io(v.cards);
}
void serialize(StateArchive& a, GameSystem::BitChip& v) {
    a.io(v.stage);
    a.io(v.position);
    a.io(v.bits);
    a.io(v.exp);
    a.io(v.digimon);
}
void serialize(StateArchive& a, Cheats& v) {
    a.io(v.god);
    a.io(v.infinite_mp);
    a.io(v.one_hit);
    a.io(v.exp_rate);
    a.io(v.damage_dealt);
    a.io(v.damage_taken);
    a.io(v.drop_seed);
}

namespace {
constexpr std::uint32_t kSnapshotVersion = 5; // 5: HUD attack-delay ring; 4: first-info update order (replays differ); 3: one system generator

void serialize_system(StateArchive& a, GameSystem& v) {
    a.io(v.flag_folder);
    a.io(v.bit_chips);
    a.io(v.players);
    a.io(v.difficulty);
    a.io(v.difficulty_max);
    a.io(v.camera_mode);
    a.io(v.light_mode);
    a.io(v.ninzu);
    a.io(v.game_started);
    a.io(v.gate_stage_name);
    a.io(v.gate_stage_no);
    a.io(v.player_pos);
}

void serialize_snapshot(StateArchive& a, StageSnapshot& s) {
    std::uint32_t version = kSnapshotVersion;
    a.io(version);
    if (version != kSnapshotVersion) throw std::runtime_error("save state from another version");
    a.io(s.stage);
    a.io(s.time);
    a.io(s.frame);
    a.io(s.flags);
    a.io(s.counters);
    a.io(s.pad_input_enabled);
    a.io(s.cheats);
    OriginalPads::States pads = s.pads.states();
    a.io(pads);
    if (a.reading()) s.pads.restore(pads);
    a.io(s.random);
    // The GameSystem copy.
    bool has_system = s.system != nullptr;
    a.io(has_system);
    if (has_system) {
        auto system = a.reading() ? std::make_shared<GameSystem>() : std::make_shared<GameSystem>(*static_cast<const GameSystem*>(s.system.get()));
        serialize_system(a, *system);
        if (a.reading()) s.system = system;
    }
    SoundManager::serialize_persistent(a, s.sound);
}
} // namespace

std::vector<std::uint8_t> encode_snapshot(const StageSnapshot& snapshot) {
    std::vector<std::uint8_t> out;
    StateArchive archive(out);
    auto copy = snapshot;
    serialize_snapshot(archive, copy);
    return out;
}

StageSnapshot decode_snapshot(std::span<const std::uint8_t> bytes) {
    StateArchive archive(bytes);
    StageSnapshot snapshot;
    serialize_snapshot(archive, snapshot);
    return snapshot;
}

} // namespace dw4::engine
