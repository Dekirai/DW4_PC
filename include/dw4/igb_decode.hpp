#pragma once

#include "dw4/game_clock.hpp"
#include "dw4/igb_decode_tables.hpp"
#include "dw4/igb_stream.hpp"

namespace dw4 {
class IgbDecodeError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Native ownership for the concrete igIGBFile fields. Its canonical file owns
// this state; stream->file is weak, so retaining fields does not create a cycle.
// Zero construction is not the original reflected factory or its defaults.
struct IgbDecodeState {
    std::shared_ptr<IgbStreamState> stream=std::make_shared<IgbStreamState>();
    std::shared_ptr<IgbDecodeTablesState> tables=std::make_shared<IgbDecodeTablesState>();
    std::int32_t object_count{}; // Directory +08, distinct from header object count.
    std::uint8_t objects_initialized{}; // Directory +38.
};

enum class IgbDecodeServiceKind {
    open_stream,          // 8004F870.
    read_header,          // 8004F974.
    field_types,          // 800514AC.
    alignments,           // 80051794.
    object_type_bytes,    // 8004FBE8.
    construct_types,      // 8004FC94: runtime metatypes and slot remapping.
    prepare_types,        // 800505E0.
    table_521ec,          // 800521EC, version-dependent position in the stream.
    memory_pool_names,    // 80052554.
    shared_509d4,         // 800509D4; three separate original shared-entry stages.
    shared_50c68,         // 80050C68.
    shared_50e80,         // 80050E80.
    unshared_50f58,       // 80050F58.
    table_520e8,          // 800520E8, version-dependent position in the stream.
    populate_objects,    // 80051AE0.
    file_payload,        // Reached file virtual+84 (concrete80053218).
    final_table,         // 80051CB0.
    export_infos,        // 80051450.
    activate_objects,    // 800527D8, including getter and object virtual+90.
    postprocess,         // Reached file virtual+78 (concrete80052E78).
    close_stream,        // 80051E04, including conditional8004D8D8.
    cleanup,             // 80051E30, including both LIVE +BC releases.
    legacy_warning,      // 8006EEE4; same captured stream filename twice.
    timing_log           // 8006EDA4; captured input filename, binary64 elapsed.
};
struct IgbDecodeCall {
    IgbDecodeServiceKind kind{};
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbDecodeState> state;
    SystemStageString filename;
    double elapsed_seconds{};
};
struct IgbDecodeOperation {std::function<MessengerCallResult()> resume;};
using IgbDecodeFactory=std::function<std::shared_ptr<IgbDecodeOperation>(const IgbDecodeCall&)>;
struct IgbDecodeServices {
    std::shared_ptr<IgbStreamServices> stream;
    std::shared_ptr<IgbDecodeTablesServices> tables;
    // Disengaged is unrepresented global setup; engaged null is an actual null
    // global timer and faults when dereferenced. Both samples reload the owner.
    std::optional<std::shared_ptr<NativeGameClock>> clock; // Global805622FC.
    std::optional<std::uint8_t> warning_suppressed,timing_suppressed; // 8056213E/F.
    std::string diagnostic; // Native storage for readFile(%s), global804F2188.
    IgbDecodeFactory factory;
};

// Original800528DC orchestration, including80052794 failure tail. A helper's
// completed flag means it returned; its word is compared with the LIVE failure
// global in stream->statuses. Successful parsing alone never completes loading.
class IgbDecodeInvocation {
public:
    IgbDecodeInvocation(std::shared_ptr<LoaderRequestFile>,SystemStageString filename);
    ~IgbDecodeInvocation();
    IgbDecodeInvocation(const IgbDecodeInvocation&)=delete;
    IgbDecodeInvocation& operator=(const IgbDecodeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDecodeServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::int32_t value() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

// Concrete open/header/three early table bodies. Remaining reached operations
// require their real implementations; no parser-based success fallback exists.
IgbDecodeFactory make_native_igb_stage_factory(std::weak_ptr<IgbDecodeServices>,
    IgbDecodeFactory remaining={});
// Connect the actual decoder to the directory request/path-search caller.
// Weak service capture avoids an ownership cycle through installed factories.
LoaderRequestFactory make_native_igb_loader_factory(std::weak_ptr<IgbDecodeServices>,
    LoaderRequestFactory remaining={});
} // namespace dw4
