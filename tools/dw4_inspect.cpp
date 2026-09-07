#include "dw4/assets.hpp"
#include "dw4/igb.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace dw4::assets;

std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string json(const std::string& input) {
    std::ostringstream out;
    out << '"';
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto c = static_cast<unsigned char>(input[i]);
        // Preserve valid UTF-8 paths and replace malformed asset-name bytes.
        if (c >= 128) {
            const std::size_t length = c >= 0xc2 && c <= 0xdf ? 2 :
                c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
            bool valid = length && length <= input.size() - i;
            if (valid) {
                for (std::size_t j = 1; j < length; ++j) {
                    const auto next = static_cast<unsigned char>(input[i + j]);
                    if (next < 0x80 || next > 0xbf) valid = false;
                }
                const auto next = static_cast<unsigned char>(input[i + 1]);
                if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next > 0x9f) ||
                    (c == 0xf0 && next < 0x90) || (c == 0xf4 && next > 0x8f)) valid = false;
            }
            if (valid) {
                out.write(input.data() + i, static_cast<std::streamsize>(length));
                i += length - 1;
            } else out << "\\ufffd";
            continue;
        }
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
            else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

std::string hex(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

std::string hex_bytes(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    for (auto byte : bytes) out << std::setw(2) << unsigned(byte);
    return out.str();
}

void igb_json(const dw4::IgbMetadata& metadata, bool list) {
    const auto& header = metadata.header;
    std::cout << "{\"scope\":\"header_and_type_metadata\",\"version\":" << header.version
        << ",\"byte_order\":" << json(header.byte_order == dw4::IgbByteOrder::little ? "little" : "big")
        << ",\"flags\":" << json(hex(header.flags)) << ",\"metadata_bytes\":" << metadata.metadata_bytes
        << ",\"alignment_bytes\":" << metadata.alignment_bytes << ",\"entry_count\":" << header.entry_count
        << ",\"object_count\":" << header.object_count << ",\"memory_reference_count\":" << header.memory_reference_count
        << ",\"field_type_count\":" << metadata.field_types.size() << ",\"object_type_count\":" << metadata.object_types.size();
    if (list) {
        std::cout << ",\"field_types\":[";
        for (std::size_t i = 0; i < metadata.field_types.size(); ++i) {
            if (i) std::cout << ',';
            const auto& type = metadata.field_types[i];
            std::cout << "{\"index\":" << i << ",\"name\":" << json(type.name)
                << ",\"major\":" << type.major << ",\"minor\":" << type.minor << '}';
        }
        std::cout << "],\"object_types\":[";
        for (std::size_t i = 0; i < metadata.object_types.size(); ++i) {
            if (i) std::cout << ',';
            const auto& type = metadata.object_types[i];
            std::cout << "{\"index\":" << i << ",\"name\":" << json(type.name)
                << ",\"major\":" << type.major << ",\"minor\":" << type.minor
                << ",\"parent_index\":" << type.parent_index << ",\"slots\":" << type.slots << ",\"fields\":[";
            for (std::size_t j = 0; j < type.fields.size(); ++j) {
                if (j) std::cout << ',';
                const auto& field = type.fields[j];
                std::cout << "{\"type_index\":" << field.type_index << ",\"slot\":" << field.slot << ",\"size\":" << field.size << '}';
            }
            std::cout << "]}";
        }
        std::cout << ']';
    }
    std::cout << '}';
}

void igb_text(const dw4::IgbMetadata& metadata, bool list) {
    const auto& header = metadata.header;
    std::cout << "IGB v" << header.version << ' ' << (header.byte_order == dw4::IgbByteOrder::little ? "little" : "big")
        << " endian, flags " << hex(header.flags) << ", " << metadata.metadata_bytes << " metadata bytes\n"
        << "  " << metadata.field_types.size() << " field types, " << metadata.object_types.size() << " object types, "
        << header.object_count << " declared objects\n"
        << "  Header/type metadata only; object instances, meshes and textures are not decoded.\n";
    if (!list) return;
    for (std::size_t i = 0; i < metadata.field_types.size(); ++i) {
        const auto& type = metadata.field_types[i];
        std::cout << "  Field type [" << i << "] " << type.name << " version=" << type.major << '.' << type.minor << '\n';
    }
    for (std::size_t i = 0; i < metadata.object_types.size(); ++i) {
        const auto& type = metadata.object_types[i];
        std::cout << "  Object type [" << i << "] " << type.name << " version=" << type.major << '.' << type.minor
            << " parent=" << type.parent_index << " slots=" << type.slots << " fields=" << type.fields.size() << '\n';
        for (const auto& field : type.fields)
            std::cout << "    field type=" << field.type_index << " slot=" << field.slot << " size=" << field.size << '\n';
    }
}

bool igb_cookie(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 48) return false;
    return (bytes[40] == 0xda && bytes[41] == 0xfa && bytes[42] == 0 && bytes[43] == 0) ||
        (bytes[40] == 0 && bytes[41] == 0 && bytes[42] == 0xfa && bytes[43] == 0xda);
}

void dol_json(const DolInfo& dol) {
    std::cout << "{\"file_size\":" << dol.file_size << ",\"entry_point\":" << json(hex(dol.entry_point))
        << ",\"bss_address\":" << json(hex(dol.bss_address)) << ",\"bss_size\":" << dol.bss_size << ",\"sections\":[";
    for (std::size_t i = 0; i < dol.sections.size(); ++i) {
        const auto& section = dol.sections[i];
        if (i) std::cout << ',';
        std::cout << "{\"type\":" << json(section.executable ? "text" : "data") << ",\"index\":" << section.index
            << ",\"file_offset\":" << section.file_offset << ",\"address\":" << json(hex(section.address))
            << ",\"size\":" << section.size << '}';
    }
    std::cout << "]}";
}

void dol_text(const DolInfo& dol) {
    std::cout << "DOL: " << dol.file_size << " bytes, " << dol.sections.size()
        << " loaded sections, entry " << hex(dol.entry_point) << "\n";
    for (const auto& section : dol.sections)
        std::cout << "  " << (section.executable ? "text" : "data") << '[' << section.index << "] file "
            << hex(section.file_offset) << " -> " << hex(section.address) << " (" << section.size << " bytes)\n";
    std::cout << "  BSS: " << hex(dol.bss_address) << " (" << dol.bss_size << " bytes)\n";
}

std::size_t number(const std::filesystem::path& value) {
    const auto string = value.string();
    std::size_t result{};
    const auto [end, error] = std::from_chars(string.data(), string.data() + string.size(), result);
    if (error != std::errc{} || end != string.data() + string.size())
        throw AssetError("Expected an unsigned decimal integer");
    return result;
}

void usage() {
    std::cout << "DW4 PC native asset inspector (does not execute the game)\n"
        "Usage: dw4_inspect <extracted-directory|archive.afs|main.dol|file.igb> [--json]\n"
        "       dw4_inspect archive.afs [--list] [--entry INDEX] [--preview-bytes N] [--json]\n"
        "       dw4_inspect file.igb [--list] [--json]\n"
        "Archive reads are bounded; preview defaults to 64 bytes (maximum 1 MiB).\n"
        "Selected IGB entries also expose type metadata using a prefix capped at 16 MiB.\n";
}

int run(const std::vector<std::filesystem::path>& args) {
    const bool want_json = std::find(args.begin(), args.end(), "--json") != args.end();
    try {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") { usage(); return args.empty() ? 2 : 0; }
        bool list = false;
        bool have_entry = false;
        std::size_t entry_index = 0;
        std::size_t preview = 64;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--json") continue;
            if (args[i] == "--list") { list = true; continue; }
            if (args[i] == "--entry" && i + 1 < args.size()) { entry_index = number(args[++i]); have_entry = true; continue; }
            if (args[i] == "--preview-bytes" && i + 1 < args.size()) { preview = number(args[++i]); continue; }
            throw AssetError("Unknown argument or missing option value");
        }
        if (preview > 1024u * 1024u) throw AssetError("Preview limit must be at most 1 MiB");
        const auto& path = args[0];
        if (std::filesystem::is_directory(path)) {
            if (list || have_entry) throw AssetError("Use --list and --entry with a single AFS archive");
            const auto disc = inspect_extracted(path);
            if (want_json) {
                std::cout << "{\"kind\":\"extracted_disc\",\"root\":" << json(utf8(disc.root))
                    << ",\"game_id\":" << json(disc.header.game_id) << ",\"title\":" << json(disc.header.title)
                    << ",\"disc_number\":" << unsigned(disc.header.disc_number) << ",\"revision\":" << unsigned(disc.header.revision)
                    << ",\"supported_revision\":" << (disc.header.is_dw4_usa() ? "true" : "false") << ",\"dol\":";
                dol_json(disc.dol);
                std::cout << ",\"banner_path\":" << json(utf8(disc.banner_path)) << ",\"archives\":[";
                for (std::size_t i = 0; i < disc.archives.size(); ++i) {
                    if (i) std::cout << ',';
                    const auto& archive = disc.archives[i];
                    std::cout << "{\"path\":" << json(utf8(archive.path)) << ",\"file_size\":" << archive.file_size
                        << ",\"entry_count\":" << archive.entry_count << ",\"has_names\":" << (archive.has_names ? "true" : "false") << '}';
                }
                std::cout << "],\"warnings\":[";
                for (std::size_t i = 0; i < disc.warnings.size(); ++i) {
                    if (i) std::cout << ',';
                    std::cout << json(disc.warnings[i]);
                }
                std::cout << "]}\n";
            } else {
                std::cout << disc.header.title << " [" << disc.header.game_id << "] disc " << unsigned(disc.header.disc_number)
                    << " revision " << unsigned(disc.header.revision) << '\n';
                dol_text(disc.dol);
                for (const auto& archive : disc.archives)
                    std::cout << utf8(archive.path.filename()) << ": " << archive.entry_count << " entries, "
                        << archive.file_size << " bytes, names " << (archive.has_names ? "present" : "absent") << '\n';
                std::cout << "Banner: " << (disc.banner_path.empty() ? "unavailable" : "96x32 RGB5A3 decoded successfully") << '\n';
                for (const auto& warning : disc.warnings) std::cout << "Warning: " << warning << '\n';
            }
            return 0;
        }
        auto extension = path.extension().string();
        for (auto& c : extension) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (extension == ".igb") {
            if (have_entry) throw AssetError("Use --entry with an AFS archive");
            const auto metadata = dw4::read_igb_metadata(path);
            if (want_json) {
                std::cout << "{\"kind\":\"igb_metadata\",\"path\":" << json(utf8(path)) << ",\"igb\":";
                igb_json(metadata, list);
                std::cout << "}\n";
            } else igb_text(metadata, list);
            return 0;
        }
        if (extension == ".dol") {
            if (list || have_entry) throw AssetError("Use --list and --entry with an AFS archive");
            const auto dol = inspect_dol(path);
            if (want_json) { dol_json(dol); std::cout << '\n'; }
            else dol_text(dol);
            return 0;
        }
        const auto archive = AfsArchive::open(path);
        // Complete any selected read before emitting JSON, so errors never produce partial JSON.
        const auto bytes = have_entry ? archive.read_prefix(entry_index, preview) : std::vector<std::uint8_t>{};
        std::optional<dw4::IgbMetadata> entry_metadata;
        std::string igb_error;
        if (have_entry) {
            const auto signature = archive.read_prefix(entry_index, 48);
            if (igb_cookie(signature)) {
                try {
                    const auto metadata_prefix = archive.read_prefix(entry_index, 16u * 1024u * 1024u);
                    entry_metadata = dw4::read_igb_metadata(std::span<const std::uint8_t>(metadata_prefix));
                } catch (const dw4::IgbError& error) {
                    igb_error = error.what();
                }
            }
        }
        if (want_json) {
            std::cout << "{\"kind\":\"afs\",\"path\":" << json(utf8(path)) << ",\"file_size\":" << archive.file_size()
                << ",\"entry_count\":" << archive.entries().size() << ",\"has_names\":" << (archive.has_names() ? "true" : "false");
            if (list || have_entry) {
                std::cout << ",\"entries\":[";
                bool first = true;
                for (std::size_t i = 0; i < archive.entries().size(); ++i) {
                    if (!list && i != entry_index) continue;
                    if (!first) std::cout << ',';
                    first = false;
                    const auto& entry = archive.entries()[i];
                    std::cout << "{\"index\":" << i << ",\"offset\":" << entry.offset << ",\"size\":" << entry.size << ",\"name\":" << json(entry.name) << '}';
                }
                std::cout << ']';
            }
            if (have_entry) std::cout << ",\"preview_index\":" << entry_index << ",\"preview_hex\":" << json(hex_bytes(bytes));
            if (entry_metadata) {
                std::cout << ",\"igb\":";
                igb_json(*entry_metadata, list);
            }
            if (!igb_error.empty()) std::cout << ",\"igb_error\":" << json(igb_error);
            std::cout << "}\n";
        } else {
            std::cout << utf8(path.filename()) << ": " << archive.entries().size() << " entries, " << archive.file_size()
                << " bytes, names " << (archive.has_names() ? "present" : "absent") << '\n';
            if (list || have_entry)
                for (std::size_t i = 0; i < archive.entries().size(); ++i) {
                    if (!list && i != entry_index) continue;
                    const auto& entry = archive.entries()[i];
                    std::cout << '[' << i << "] " << entry.name << " offset=" << hex(entry.offset) << " size=" << entry.size << '\n';
                }
            if (have_entry) std::cout << "Prefix [" << entry_index << "]: " << hex_bytes(bytes) << '\n';
            if (entry_metadata) igb_text(*entry_metadata, list);
            if (!igb_error.empty()) std::cout << "IGB diagnostic: " << igb_error << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        if (want_json) std::cout << "{\"error\":" << json(error.what()) << "}\n";
        else std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::vector<std::filesystem::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return run(args);
}
