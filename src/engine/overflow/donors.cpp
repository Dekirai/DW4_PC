// Data Overflow (H2, SURVIVAL.md 13.4): the run engine's donor loads.
//
// The randomizer's donor loader (Engine::load_enemy_donors, route_donor)
// brings up to six families into a shuffled stage and never into a boss
// arena (randomizer::fixed_stage). A run engine takes the same route with an
// explicit list instead: every family the director names (the families'
// donor stages come from the randomizer's table), Guardians from a named
// donor (a stage's main member or a separate package), on any arena, with no
// family limit; route_donor never takes a ded_*.lua, so no story flag, scene
// or door of a Guardian runs. Stream entries (BGM, jingles) of other stages
// come by name from their sound lists. All of it after boot.lua and before
// the stage's infos register (loader state 3), like the randomizer's.
#include "dw4/engine/overflow.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/randomizer.hpp"
#include "dw4/packages.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace dw4::engine {

void Engine::load_overflow_donors() {
    if (!overflow_) return;
    const auto& request = overflow_->donors;
    if (request.empty()) return;
    // The families the stage brings itself (its own infos and boot.lua's
    // separate packages) stay its own.
    std::set<std::string, std::less<>> present;
    for (const auto& info : deferred_infos_)
        if (info.type() == "beModelCtrlInfo") present.insert(std::string(info.str(2)));
    for (const auto& item : loader_queue_) {
        const auto slash = item.name.find_last_of('/');
        const std::string base = item.name.substr(slash == std::string::npos ? 0 : slash + 1);
        if (base.size() > 4 && base.ends_with(".igb")) present.insert(base.substr(0, base.size() - 4));
    }
    std::map<std::string, std::vector<std::string>> by_donor;
    auto add = [&](const std::string& family, const std::string& donor) {
        if (present.contains(family)) return;
        auto& list = by_donor[donor];
        if (std::find(list.begin(), list.end(), family) == list.end()) list.push_back(family);
        present.insert(family);
    };
    for (const auto& name : request.families) {
        const auto* family = randomizer::family(name);
        if (!family || !family->donor) {
            log("overflow: no donor stage for " + name);
            continue;
        }
        add(name, family->donor);
    }
    for (const auto& [name, donor] : request.guardians) add(name, donor);
    std::string text = "overflow: donors for " + stage_name_ + ":";
    for (const auto& [donor, families] : by_donor) {
        try {
            std::shared_ptr<const IgbDocument> document, sounds;
            if (donor.ends_with(".igb")) {
                // A separate package (stag0202/separate/e_skulgrey.igb).
                document = std::make_shared<const IgbDocument>(read_igb_document(resources_.read(donor)));
                files_.push_back({donor, document, false});
            } else {
                const auto descriptor = load_package_descriptor(resources_, donor + ".igb");
                const PackageMember* main = nullptr;
                const PackageMember* preliminary = nullptr;
                for (const auto& member : descriptor.members) {
                    if (member.suffix != "/s" && !main) main = &member;
                    if (member.suffix == "/s" && !preliminary) preliminary = &member;
                }
                if (!main) throw std::runtime_error("no main member");
                document = std::make_shared<const IgbDocument>(read_package_member(resources_, *main));
                files_.push_back({donor + main->specifier, document, false});
                if (preliminary) {
                    sounds = std::make_shared<const IgbDocument>(read_package_member(resources_, *preliminary));
                    files_.push_back({donor + preliminary->specifier, sounds, false});
                }
            }
            const auto targets = route_donor(document, families);
            for (const auto& name : families) text += " " + name + " (" + donor + ")";
            // Their SE sets and banks (a separate package carries its own).
            if (auto* sound = get<SoundManager>()) {
                for (const auto& bank : sound->add_donor_sounds(*this, sounds ? *sounds : *document, targets)) text += " +" + bank;
            }
        } catch (const std::exception& error) {
            log("overflow: donor load " + donor + " failed: " + error.what());
        }
    }
    for (const auto& [stage, names] : request.streams) {
        try {
            const auto descriptor = load_package_descriptor(resources_, stage + ".igb");
            const PackageMember* preliminary = nullptr;
            for (const auto& member : descriptor.members)
                if (member.suffix == "/s") {
                    preliminary = &member;
                    break;
                }
            if (!preliminary) throw std::runtime_error("no sound member");
            auto sounds = std::make_shared<const IgbDocument>(read_package_member(resources_, *preliminary));
            files_.push_back({stage + preliminary->specifier, sounds, false});
            if (auto* sound = get<SoundManager>()) {
                const std::set<std::string, std::less<>> wanted(names.begin(), names.end());
                for (const auto& name : sound->add_named_sounds(*this, *sounds, wanted)) text += " ~" + name + " (" + stage + ")";
            }
        } catch (const std::exception& error) {
            log("overflow: stream load " + stage + " failed: " + error.what());
        }
    }
    log(text);
}

// The named sounds of a stage's sound list with their files and sfp banks,
// as add_donor_sounds takes the sounds of a donor's SE sets.
std::vector<std::string> SoundManager::add_named_sounds(Engine& engine, const IgbDocument& preliminary,
                                                        const std::set<std::string, std::less<>>& names) {
    std::vector<std::string> added;
    if (!preliminary.info_list_reference) return added;
    std::vector<ObjectView> infos;
    for (const auto& element : preliminary.object_list(*preliminary.info_list_reference))
        if (element) infos.emplace_back(&preliminary, *element);
    std::set<std::string, std::less<>> banks;
    for (const auto& info : infos) {
        if (info.type() != "beSoundInfo") continue;
        std::vector<std::string> files;
        for (const auto& file : info.list(7)) files.emplace_back(file ? std::string(file.str(2)) : std::string());
        for (const auto& data : info.list(5)) {
            if (!data || !names.contains(data.str(2)) || sounds_.contains(data.str(2))) continue;
            SoundData sound{data.i16(4), data.i16(5), data.i16(8), std::uint32_t(data.i32(6)), data.i16(9)};
            if (sound.file >= 0 && std::size_t(sound.file) < files.size()) {
                const auto& file = files[std::size_t(sound.file)];
                const auto found = std::find(files_.begin(), files_.end(), file);
                sound.file = int(found - files_.begin());
                if (found == files_.end()) files_.push_back(file);
                std::string bank = file.substr(file.find_last_of('/') + 1);
                for (auto& c : bank) c = char(std::tolower(static_cast<unsigned char>(c)));
                banks.insert(bank);
            }
            sounds_[std::string(data.str(2))] = sound;
            added.emplace_back(data.str(2));
        }
    }
    for (const auto& info : infos) {
        if (info.type() != "beStaticInfo") continue;
        for (const auto& data : info.list(5)) {
            if (!data || data.type() != "beCriSfpData") continue;
            std::string name(data.str(2));
            for (auto& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (banks.contains(name)) add_bank(engine, data, false);
        }
    }
    return added;
}

} // namespace dw4::engine
