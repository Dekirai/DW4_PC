#include "common.hpp"

#include "moderngekko/module_abi.h"

#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace recomp_abi_check {

Layout host_layout() {
    static_assert(std::is_standard_layout_v<CPUState>);
    Layout result{MODERNGEKKO_CPU_ABI_VERSION, MODERNGEKKO_MODULE_ABI_VERSION,
                  sizeof(CPUState), alignof(CPUState), sizeof(ModernGekkoModuleDesc),
                  alignof(ModernGekkoModuleDesc), {}, {}, {}};
#define DW4_CPU_FIELD(name) \
    result.cpu_fields.push_back(field<decltype(CPUState::name)>(#name, offsetof(CPUState, name)));
#include "fields.inc"
#undef DW4_CPU_FIELD
#define DW4_DESCRIPTOR_FIELD(name) \
    result.descriptor_fields.push_back(field<decltype(ModernGekkoModuleDesc::name)>( \
        #name, offsetof(ModernGekkoModuleDesc, name)));
#include "descriptor_fields.inc"
#undef DW4_DESCRIPTOR_FIELD
#define DW4_ABI_RECORD_BEGIN(name) { using Record = ModernGekko##name; \
    RecordLayout record{#name, sizeof(Record), alignof(Record), {}};
#define DW4_ABI_RECORD_FIELD(name) \
    record.fields.push_back(field<decltype(Record::name)>(#name, offsetof(Record, name)));
#define DW4_ABI_RECORD_END() result.nested_records.push_back(std::move(record)); }
#include "records.inc"
#undef DW4_ABI_RECORD_BEGIN
#undef DW4_ABI_RECORD_FIELD
#undef DW4_ABI_RECORD_END
    return result;
}

Descriptor inspect_descriptor(const void* pointer) {
    if (!pointer) {
        throw std::runtime_error("The module descriptor getter returned null.");
    }
    // An older descriptor may have no tail fields. Check its first word before
    // reading any member beyond that common prefix.
    std::uint32_t module_abi{};
    std::memcpy(&module_abi, pointer, sizeof(module_abi));
    if (module_abi != MODERNGEKKO_MODULE_ABI_VERSION) {
        throw std::runtime_error("DLL module ABI differs from the expected descriptor version.");
    }
    const auto& value = *static_cast<const ModernGekkoModuleDesc*>(pointer);
    const auto* end = static_cast<const char*>(std::memchr(value.game_id, 0, sizeof(value.game_id)));
    return {value.abi_version, value.cpu_abi_version, value.cpu_state_size,
            value.entry_point, std::string(value.game_id, end ? end : value.game_id + sizeof(value.game_id)),
            end != nullptr, value.dispatch != nullptr,
            value.num_code_ranges, value.num_smc_ranges, value.num_chunk_ranges};
}

}  // namespace recomp_abi_check
