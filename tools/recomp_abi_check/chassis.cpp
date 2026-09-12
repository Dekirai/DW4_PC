#include "common.hpp"

#include "StaticRecompABI.h"

#include <type_traits>

namespace recomp_abi_check {

Layout chassis_layout() {
    static_assert(std::is_standard_layout_v<CPUState>);
    Layout result{GXRUNTIME_CPU_ABI_VERSION, STATICRECOMP_ABI_VERSION,
                  sizeof(CPUState), alignof(CPUState), sizeof(StaticRecompModuleDesc),
                  alignof(StaticRecompModuleDesc), {}, {}, {}};
#define DW4_CPU_FIELD(name) \
    result.cpu_fields.push_back(field<decltype(CPUState::name)>(#name, offsetof(CPUState, name)));
#include "fields.inc"
#undef DW4_CPU_FIELD
#define DW4_DESCRIPTOR_FIELD(name) \
    result.descriptor_fields.push_back(field<decltype(StaticRecompModuleDesc::name)>( \
        #name, offsetof(StaticRecompModuleDesc, name)));
#include "descriptor_fields.inc"
#undef DW4_DESCRIPTOR_FIELD
#define DW4_ABI_RECORD_BEGIN(name) { using Record = StaticRecomp##name; \
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

}  // namespace recomp_abi_check
