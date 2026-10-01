#include "dw4/resources.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/packages.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
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
                "Usage: dw4_catalog EXTRACTED_ROOT [--find TEXT] [--read LOGICAL_PATH [--out FILE]]\n"
                "       dw4_catalog EXTRACTED_ROOT --package LOGICAL_PATH [--member INDEX]\n"
                "No options shows the first 20 entries. --find lists all matching paths.\n"
                "--read proves logical lookup and reads up to 64 MiB; --out FILE writes the bytes\n"
                "(mod templates: mods/<id>/files/<logical path> replaces that file in the game).\n"
                "EXTRACTED_ROOT may also be the disc image.\n"
                "--package lists descriptor members; --member explicitly reads one and lists its exports.\n";
            return arguments.empty() ? 2 : 0;
        }
        bool filtered = false;
        std::string filter, read_path, package_path;
        std::filesystem::path out_path;
        std::optional<std::uint32_t> member_index;
        std::set<std::string> seen;
        for (std::size_t i = 1; i < arguments.size(); ++i) {
            const auto option = arguments[i];
            if (!seen.insert(utf8(option)).second) throw std::runtime_error("Duplicate option");
            if (i + 1 >= arguments.size()) throw std::runtime_error("Missing option value");
            if (option == "--find") { filter = utf8(arguments[++i]); filtered = true; }
            else if (option == "--read") read_path = utf8(arguments[++i]);
            else if (option == "--package") package_path = utf8(arguments[++i]);
            else if (option == "--out") out_path = arguments[++i];
            else if (option == "--member") {
                const auto value = utf8(arguments[++i]); std::uint32_t parsed{};
                const auto converted = std::from_chars(value.data(),value.data()+value.size(),parsed);
                if (value.empty() || converted.ec != std::errc{} || converted.ptr != value.data()+value.size())
                    throw std::runtime_error("Member index must be an unsigned decimal 32-bit integer");
                member_index = parsed;
            }
            else throw std::runtime_error("Unknown option");
        }
        if (seen.contains("--package") && package_path.empty()) throw std::runtime_error("Package path is empty");
        if (member_index && package_path.empty()) throw std::runtime_error("--member requires --package");
        if (!out_path.empty() && read_path.empty()) throw std::runtime_error("--out requires --read");
        if (!package_path.empty() && (filtered || seen.contains("--read")))
            throw std::runtime_error("--package is exclusive with --find and --read");
        const auto mounted = dw4::MountedResources::open(arguments[0]);
        const auto& catalog = mounted.catalog();
        const auto available = std::count_if(catalog.resources.begin(), catalog.resources.end(),
            [](const auto& resource) { return resource.location.has_value(); });
        std::cout << "Catalog: " << catalog.root_label << '\n' << catalog.archives.size()
            << " archives, " << catalog.resources.size() << " logical paths, " << available << " available\n";
        if (!package_path.empty()) {
            const auto package = dw4::load_package_descriptor(mounted,package_path);
            std::cout << "Package: " << package.logical_path << ", descriptor ref " << package.reference_index
                << ", section " << package.section << '\n' << package.provenance << '\n'
                << "member\tfile_ref\tspecifier\treadable\tdiagnostic\n";
            for (const auto& member : package.members)
                std::cout << member.index << '\t' << member.file_reference << '\t' << member.specifier
                    << '\t' << member.supported << '\t' << member.diagnostic << '\n';
            if (member_index) {
                if (*member_index >= package.members.size()) throw std::runtime_error("Member index is outside the original filename list");
                const auto& member = package.members[*member_index];
                const auto document = dw4::read_package_member(mounted,member);
                const auto exports = dw4::list_package_exports(document);
                std::cout << "Read member " << *member_index << ": " << member.provenance << '\n'
                    << "Decoded IGB: " << document.objects.size() << " objects, " << exports.size() << " explicit exports\n"
                    << "export\treference\ttype\tname\tscene_index\n";
                std::size_t ordinal = 0, scene_index = 0;
                for (const auto& item : exports) {
                    std::cout << ordinal++ << '\t' << item.reference_index << '\t' << item.type << '\t' << item.name << '\t';
                    if (document.is_a(document.object(item.reference_index),"igSceneInfo")) std::cout << scene_index++;
                    else std::cout << '-';
                    std::cout << '\n';
                }
            }
            return 0;
        }
        if (!read_path.empty()) {
            auto bytes = mounted.read(read_path);
            const auto& location = *catalog.find(read_path)->location;
            std::cout << "Resolved " << read_path << " -> " << catalog.archives[location.archive_index].filename
                << '[' << location.entry_index << "], " << bytes.size() << " bytes read\n";
            if (!out_path.empty()) {
                std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                if (!out) throw std::runtime_error("Cannot write the output file");
                std::cout << "Wrote " << utf8(out_path) << '\n';
            }
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
