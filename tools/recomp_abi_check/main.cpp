#include "common.hpp"

#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace {

std::string quoted(const std::string& value) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value) {
        if (character == '"' || character == '\\') {
            output << '\\' << static_cast<char>(character);
        } else if (character < 32) {
            constexpr char digits[] = "0123456789abcdef";
            output << "\\u00" << digits[character >> 4] << digits[character & 15];
        } else {
            output << static_cast<char>(character);
        }
    }
    output << '"';
    return output.str();
}

std::string descriptor_type(std::string value) {
    // The facade and chassis intentionally give their equivalent range and
    // REL descriptor structures different names. CPUState has the same name.
    constexpr char source[] = "ModernGekko";
    constexpr char target[] = "StaticRecomp";
    for (auto at = value.find(source); at != std::string::npos; at = value.find(source, at)) {
        value.replace(at, sizeof(source) - 1, target);
        at += sizeof(target) - 1;
    }
    return value;
}

void compare_fields(const std::vector<recomp_abi_check::Field>& host,
                    const std::vector<recomp_abi_check::Field>& chassis,
                    bool descriptor, std::vector<std::string>& errors) {
    if (host.size() != chassis.size()) {
        errors.push_back("Field count differs between host and chassis.");
        return;
    }
    for (std::size_t index = 0; index < host.size(); ++index) {
        auto left = host[index];
        auto right = chassis[index];
        if (descriptor) {
            left.type = descriptor_type(left.type);
            right.type = descriptor_type(right.type);
        }
        if (left != right) {
            errors.push_back(std::string(descriptor ? "Descriptor" : "CPU") +
                             " field differs: " + left.name);
        }
    }
}

void write_fields(std::ostream& output, const std::vector<recomp_abi_check::Field>& fields) {
    output << '[';
    bool first = true;
    for (const auto& field : fields) {
        if (!first) output << ',';
        first = false;
        output << "{\"name\":" << quoted(field.name) << ",\"type\":" << quoted(field.type)
               << ",\"offset\":" << field.offset << ",\"size\":" << field.size
               << ",\"alignment\":" << field.alignment << '}';
    }
    output << ']';
}

void write_layout(std::ostream& output, const recomp_abi_check::Layout& value) {
    output << "{\"cpu_abi\":" << value.cpu_abi << ",\"module_abi\":" << value.module_abi
           << ",\"cpu_size\":" << value.cpu_size << ",\"cpu_alignment\":" << value.cpu_alignment
           << ",\"descriptor_size\":" << value.descriptor_size
           << ",\"descriptor_alignment\":" << value.descriptor_alignment << ",\"cpu_fields\":";
    write_fields(output, value.cpu_fields);
    output << ",\"descriptor_fields\":";
    write_fields(output, value.descriptor_fields);
    output << ",\"nested_records\":[";
    for (std::size_t index = 0; index < value.nested_records.size(); ++index) {
        if (index) output << ',';
        const auto& record = value.nested_records[index];
        output << "{\"name\":" << quoted(record.name) << ",\"size\":" << record.size
               << ",\"alignment\":" << record.alignment << ",\"fields\":";
        write_fields(output, record.fields);
        output << '}';
    }
    output << ']';
    output << '}';
}

struct Library {
    HMODULE handle{};
    ~Library() { if (handle) FreeLibrary(handle); }
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::cerr << "Usage: recomp-abi-check <module.dll> <report.json>\n";
        return 2;
    }
    try {
        const auto host = recomp_abi_check::host_layout();
        const auto chassis = recomp_abi_check::chassis_layout();
        std::vector<std::string> errors;
        if (host.cpu_abi != chassis.cpu_abi) errors.push_back("Host/chassis CPU ABI versions differ.");
        if (host.cpu_size != chassis.cpu_size || host.cpu_alignment != chassis.cpu_alignment)
            errors.push_back("Host/chassis CPU size or alignment differs.");
        if (host.module_abi != chassis.module_abi || host.descriptor_size != chassis.descriptor_size ||
            host.descriptor_alignment != chassis.descriptor_alignment)
            errors.push_back("Host/chassis module descriptor layout differs.");
        compare_fields(host.cpu_fields, chassis.cpu_fields, false, errors);
        compare_fields(host.descriptor_fields, chassis.descriptor_fields, true, errors);
        if (host.nested_records.size() != chassis.nested_records.size()) {
            errors.push_back("Host/chassis nested descriptor record counts differ.");
        } else {
            for (std::size_t index = 0; index < host.nested_records.size(); ++index) {
                const auto& left = host.nested_records[index];
                const auto& right = chassis.nested_records[index];
                if (left.name != right.name || left.size != right.size || left.alignment != right.alignment)
                    errors.push_back("Host/chassis nested descriptor layout differs: " + left.name);
                compare_fields(left.fields, right.fields, true, errors);
            }
        }

        std::optional<recomp_abi_check::Descriptor> descriptor;
        // Do not interpret a DLL descriptor using inconsistent declarations.
        // LoadLibrary runs only ordinary DLL initialization; the only exported
        // function called here is the descriptor getter, never game dispatch.
        Library library;
        if (errors.empty()) {
            library.handle = LoadLibraryExW(argv[1], nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!library.handle) {
                errors.push_back("LoadLibraryExW failed: " + std::to_string(GetLastError()));
            } else {
                const auto address = GetProcAddress(library.handle, "staticrecomp_get_module");
                if (!address) {
                    errors.push_back("The module has no staticrecomp_get_module export.");
                } else {
                    using Getter = const void* (__cdecl*)();
                    Getter getter{};
                    static_assert(sizeof(getter) == sizeof(address));
                    std::memcpy(&getter, &address, sizeof(getter));
                    try {
                        descriptor = recomp_abi_check::inspect_descriptor(getter());
                    } catch (const std::exception& error) {
                        errors.push_back(error.what());
                    }
                    if (descriptor) {
                    if (descriptor->module_abi != host.module_abi)
                        errors.push_back("DLL module ABI differs from host/chassis.");
                    if (descriptor->cpu_abi != host.cpu_abi || descriptor->cpu_size != host.cpu_size)
                        errors.push_back("DLL CPU ABI or size differs from host/chassis.");
                    if (!descriptor->game_id_terminated || descriptor->game_id != "GDJEB2")
                        errors.push_back("DLL game ID is not GDJEB2.");
                    if (descriptor->entry != 0x80005240u)
                        errors.push_back("DLL entry point is not the verified USA DOL entry.");
                    if (!descriptor->has_dispatch || descriptor->code_ranges == 0 || descriptor->chunks == 0)
                        errors.push_back("DLL has no dispatch or code/chunk coverage.");
                    }
                }
            }
        }

        std::ofstream report{std::filesystem::path(argv[2]), std::ios::binary};
        if (!report) throw std::runtime_error("Cannot create the ABI report.");
        report << "{\"schema_version\":1,\"passed\":" << (errors.empty() ? "true" : "false")
               << ",\"scope\":\"CPU/module declarations and descriptor getter only; no game execution\",\"host\":";
        write_layout(report, host);
        report << ",\"chassis\":";
        write_layout(report, chassis);
        report << ",\"descriptor\":";
        if (descriptor) {
            report << "{\"module_abi\":" << descriptor->module_abi
                   << ",\"cpu_abi\":" << descriptor->cpu_abi << ",\"cpu_size\":" << descriptor->cpu_size
                   << ",\"game_id\":" << quoted(descriptor->game_id) << ",\"entry_point\":" << descriptor->entry
                   << ",\"code_ranges\":" << descriptor->code_ranges << ",\"smc_ranges\":" << descriptor->smc_ranges
                   << ",\"chunks\":" << descriptor->chunks << '}';
        } else report << "null";
        report << ",\"errors\":[";
        for (std::size_t index = 0; index < errors.size(); ++index) {
            if (index) report << ',';
            report << quoted(errors[index]);
            std::cerr << errors[index] << '\n';
        }
        report << "]}\n";
        report.close();
        if (!report) throw std::runtime_error("Could not finish the ABI report.");
        if (!errors.empty()) return 1;
        std::cout << "ABI check passed: " << host.cpu_fields.size() << " CPU fields, "
                  << host.descriptor_fields.size() << " descriptor fields; CPU ABI "
                  << host.cpu_abi << ", size " << host.cpu_size << ". No game code executed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ABI check failed: " << error.what() << '\n';
        return 1;
    }
}
