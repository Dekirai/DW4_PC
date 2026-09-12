#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <typeinfo>
#include <vector>

namespace recomp_abi_check {

struct Field {
    std::string name;
    std::string type;
    std::size_t offset;
    std::size_t size;
    std::size_t alignment;

    bool operator==(const Field&) const = default;
};

template <typename T>
Field field(const char* name, std::size_t offset) {
    return {name, typeid(T).name(), offset, sizeof(T), alignof(T)};
}

struct RecordLayout {
    std::string name;
    std::size_t size;
    std::size_t alignment;
    std::vector<Field> fields;
};

struct Layout {
    std::uint32_t cpu_abi;
    std::uint32_t module_abi;
    std::size_t cpu_size;
    std::size_t cpu_alignment;
    std::size_t descriptor_size;
    std::size_t descriptor_alignment;
    std::vector<Field> cpu_fields;
    std::vector<Field> descriptor_fields;
    std::vector<RecordLayout> nested_records;
};

struct Descriptor {
    std::uint32_t module_abi;
    std::uint32_t cpu_abi;
    std::uint32_t cpu_size;
    std::uint32_t entry;
    std::string game_id;
    bool game_id_terminated;
    bool has_dispatch;
    std::uint32_t code_ranges;
    std::uint32_t smc_ranges;
    std::uint32_t chunks;
};

Layout host_layout();
Layout chassis_layout();
Descriptor inspect_descriptor(const void* pointer);

}  // namespace recomp_abi_check
