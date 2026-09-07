#include "dw4/resources.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
int run(const std::vector<std::filesystem::path>& arguments) {
    try {
        if (arguments.empty() || arguments[0] == "--help") {
            std::cout << "DW4 native logical resource catalog\n"
                "Usage: dw4_catalog EXTRACTED_ROOT [--find TEXT] [--read LOGICAL_PATH]\n"
                "No options shows the first 20 entries. --find lists all matching paths.\n"
                "--read proves logical lookup and reads up to 64 MiB; no files are written.\n";
            return arguments.empty() ? 2 : 0;
        }
        bool filtered = false;
        std::string filter, read_path;
        for (std::size_t i = 1; i < arguments.size(); ++i) {
            const auto option = arguments[i];
            if (i + 1 >= arguments.size()) throw std::runtime_error("Missing option value");
            if (option == "--find") { filter = utf8(arguments[++i]); filtered = true; }
            else if (option == "--read") read_path = utf8(arguments[++i]);
            else throw std::runtime_error("Unknown option");
        }
        const auto mounted = dw4::MountedResources::open(arguments[0]);
        const auto& catalog = mounted.catalog();
        const auto available = std::count_if(catalog.resources.begin(), catalog.resources.end(),
            [](const auto& resource) { return resource.location.has_value(); });
        std::cout << "Catalog: " << catalog.root_label << '\n' << catalog.archives.size()
            << " archives, " << catalog.resources.size() << " logical paths, " << available << " available\n";
        if (!read_path.empty()) {
            auto bytes = mounted.read(read_path);
            const auto& location = *catalog.find(read_path)->location;
            std::cout << "Resolved " << read_path << " -> " << catalog.archives[location.archive_index].filename
                << '[' << location.entry_index << "], " << bytes.size() << " bytes read\n";
            if (bytes.size() >= 48 &&
                ((bytes[40] == 0xda && bytes[41] == 0xfa && bytes[42] == 0 && bytes[43] == 0) ||
                 (bytes[40] == 0 && bytes[41] == 0 && bytes[42] == 0xfa && bytes[43] == 0xda))) {
                const auto document = dw4::read_igb_document(std::move(bytes));
                std::cout << "Decoded IGB: " << document.objects.size() << " object records, "
                    << document.memories.size() << " memory records\n";
            }
            return 0;
        }
        std::cout << "archive\tentry\tlogical_path\n";
        std::size_t shown = 0;
        for (const auto& resource : catalog.resources) {
            if (filtered && resource.logical_path.find(filter) == std::string::npos) continue;
            if (!filtered && shown >= 20) break;
            if (resource.location) {
                const auto& location = *resource.location;
                std::cout << catalog.archives[location.archive_index].filename << '\t' << location.entry_index;
            } else std::cout << "unavailable\t-";
            std::cout << '\t' << resource.logical_path << '\n';
            ++shown;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "dw4_catalog: " << error.what() << '\n';
        return 1;
    }
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::vector<std::filesystem::path> arguments;
    for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
    return run(arguments);
}
