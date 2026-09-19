#pragma once

#include "dw4/loader_request.hpp"
#include "dw4/igb_runtime_types.hpp"

#include <optional>

namespace dw4 {
struct IgbStreamState;
struct IgbScratchBuffer;
struct IgbDecodeStatusWords;
class IgbDecodeTablesError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Reached runtime field instances and their metadata, distinct from serialized
// IgbFieldType records. Metadata virtual+58 is an explicit selected operation.
struct IgbDecodeFieldList {
    using Storage=std::vector<std::shared_ptr<IgbDecodeRuntimeField>>;
    std::int32_t count{},capacity{};
    std::shared_ptr<Storage> backing=std::make_shared<Storage>();
};
struct IgbDecodeWordList {
    using Storage=std::vector<std::uint32_t>;
    std::int32_t count{},capacity{};
    std::shared_ptr<Storage> backing=std::make_shared<Storage>();
};
struct IgbDecodeName {SystemStageString value;};
struct IgbDecodeNameList {
    using Storage=std::vector<std::shared_ptr<IgbDecodeName>>;
    std::int32_t count{},capacity{};
    std::shared_ptr<Storage> backing=std::make_shared<Storage>();
};
struct IgbDecodeAlignmentRegistry {
    std::shared_ptr<IgbDecodeNameList> names; // Loader singleton +2C.
    std::shared_ptr<IgbDecodeWordList> values; // +30, same physical index.
};
struct IgbDecodeTablesState {
    std::shared_ptr<IgbScratchBuffer> field_buffer,object_type_buffer,alignment_buffer; // +B8/+AC/+EC.
    std::int32_t alignment_bytes{},alignment_count{}; // +F0/+F8.
    std::shared_ptr<IgbDecodeFieldList> field_types; // +BC, original instances or null.
    // 800514AC retains the factory's creator reference in addition to +BC.
    // Cleanup's two live +BC releases must consume these distinct references.
    std::vector<std::shared_ptr<IgbDecodeFieldList>> field_list_creation_refs;
    std::shared_ptr<IgbDecodeWordList> alignments; // +F4, raw alignment words.
    bool faulted() const noexcept {return fault_;}
private:
    friend class IgbDecodeTablesInvocation;
    std::weak_ptr<void> active_;
    bool fault_{};
    std::vector<std::shared_ptr<void>> unreleased_;
};
enum class IgbDecodeTableKind {field_types,alignments,object_type_bytes};
enum class IgbDecodeTableServiceKind {
    field_registry,       // Lazy field registry getter80063B5C.
    field_metadata,       // Selected field instance virtual+58; called again on match.
    create_field_list,    // Actual igMetaFieldList factory80034010(pool118).
    create_alignment_list,// Actual igIntList factory8003657C(pool118).
    resize_field_list,    // Actual list metadata/pool allocator8004155C(element size4).
    resize_alignment_list // Same actual allocator for raw alignment words.
};
struct IgbDecodeTableCall {
    IgbDecodeTableServiceKind kind{};
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<IgbDecodeFieldList> field_list;
    std::shared_ptr<IgbDecodeWordList> alignment_list;
    std::int32_t capacity{};
};
struct IgbDecodeTableOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbDecodeFieldRegistry> registry_result;
    std::shared_ptr<IgbDecodeRuntimeType> metadata_result;
    std::shared_ptr<IgbDecodeFieldList> field_list_result;
    std::shared_ptr<IgbDecodeWordList> alignment_list_result;
};
using IgbDecodeTableFactory=std::function<std::shared_ptr<IgbDecodeTableOperation>(const IgbDecodeTableCall&)>;
struct IgbDecodeTablesServices {
    std::shared_ptr<IgbDecodeFieldRegistry> field_registry; // Null reaches lazy creation.
    // Disengaged means unrepresented loader singleton; engaged null is a reached
    // null singleton. Its names and values may themselves be represented null.
    std::optional<std::shared_ptr<IgbDecodeAlignmentRegistry>> alignment_registry;
    IgbDecodeTableFactory factory;
};

// Original800514AC,80051794 and8004FBE8 only. Shared stream/header state and
// original file pools are explicit owners. Missing runtime constructors and
// callbacks remain pending; successful byte parsing does not build metatypes.
class IgbDecodeTablesInvocation {
public:
    IgbDecodeTablesInvocation(std::shared_ptr<LoaderRequestFile>,std::shared_ptr<IgbStreamState>,
        std::shared_ptr<IgbDecodeTablesState>,IgbDecodeTableKind);
    ~IgbDecodeTablesInvocation();
    IgbDecodeTablesInvocation(const IgbDecodeTablesInvocation&)=delete;
    IgbDecodeTablesInvocation& operator=(const IgbDecodeTablesInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDecodeTablesServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::uint32_t value() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
