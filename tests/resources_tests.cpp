#include "dw4/resources.hpp"
#include "dw4/igb_document.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void rejects(const std::function<void()>& operation) {
    try { operation(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid catalog was accepted");
}
dw4::ResourceCatalog sample() {
    return {"engine-root", {{"area.afs", 10}, {"ndmw.afs", 20}}, {
        {"scene/map.igb", 3, dw4::ResourceLocation{0, 2}},
        {"image/title.igb", 4, dw4::ResourceLocation{1, 7}},
        {"optional/missing.igb", 5, std::nullopt}
    }};
}
}

int main(int argc, char** argv) {
    try {
        const auto valid = sample();
        dw4::validate_resource_catalog(valid);
        require(valid.find("scene/map.igb")->location == dw4::ResourceLocation{0, 2}, "Logical lookup mismatch");
        require(valid.find("Scene/map.igb") == nullptr, "Lookup must preserve case");
        require(!valid.find("optional/missing.igb")->location, "Missing resource sentinel lost");
        for (const auto* path : {"../escape", "C:/drive", "/absolute", "a//b", "a/./b", "a\\b", "a/", "a\nb"}) {
            auto malformed = valid;
            malformed.resources[0].logical_path = path;
            rejects([&] { dw4::validate_resource_catalog(malformed); });
        }
        for (const auto* name : {"../data.afs", "C:data.afs", "folder/data.afs", "game.iso", ""}) {
            auto malformed = valid;
            malformed.archives[0].filename = name;
            rejects([&] { dw4::validate_resource_catalog(malformed); });
        }
        auto oversized_path = valid;
        oversized_path.resources[0].logical_path = std::string(4096, 'a') + '/' +
            std::string(4096, 'b') + '/' + std::string(4096, 'c') + '/' + std::string(4096, 'd');
        rejects([&] { dw4::validate_resource_catalog(oversized_path); });
        auto duplicate = valid;
        duplicate.resources.push_back(valid.resources[0]);
        dw4::validate_resource_catalog(duplicate); // Identical aliases are harmless.
        duplicate.resources.back().location = dw4::ResourceLocation{0, 3};
        rejects([&] { dw4::validate_resource_catalog(duplicate); });
        auto bad = valid;
        bad.resources[0].location = dw4::ResourceLocation{2, 0};
        rejects([&] { dw4::validate_resource_catalog(bad); });
        bad.resources[0].location = dw4::ResourceLocation{0, 10};
        rejects([&] { dw4::validate_resource_catalog(bad); });
        bad = valid;
        bad.archives[1].filename = bad.archives[0].filename;
        rejects([&] { dw4::validate_resource_catalog(bad); });
        rejects([] { dw4::read_resource_catalog(dw4::IgbDocument{}); });

        if (argc == 2) {
            const auto mounted = dw4::MountedResources::open(std::filesystem::path(argv[1]));
            const auto& catalog = mounted.catalog();
            std::size_t missing = 0, compared = 0;
            for (const auto& resource : catalog.resources) {
                if (!resource.location) { ++missing; continue; }
                const auto& location = *resource.location;
                const auto& archive = mounted.archives().at(location.archive_index);
                if (archive.entries().at(location.entry_index).size > 2u * 1024u * 1024u) continue;
                if (compared < 20) {
                    require(mounted.read(resource.logical_path) == archive.read(location.entry_index),
                        "Logical catalog read differs from raw archive entry");
                    ++compared;
                }
            }
            require(compared > 0, "No catalog payloads compared");
            std::cout << "Catalog integration: " << catalog.archives.size() << " archives, "
                << catalog.resources.size() << " paths, " << missing << " unavailable, "
                << compared << " direct-read equivalence checks\n";
        }
        std::cout << "Resource catalog tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Resource catalog test failure: " << error.what() << '\n';
        return 1;
    }
}
