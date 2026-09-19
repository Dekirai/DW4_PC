#pragma once

#include "dw4/loader_request.hpp"

#include <span>

namespace dw4 {
class IgbStreamError : public std::runtime_error {public: using std::runtime_error::runtime_error;};
struct IgbDecodeStatusWords {std::optional<std::uint32_t> success,failure;};

// Explicit native byte-stream implementation. It is not an unimplemented
// original file-manager callback disguised as a successfully opened file.
struct IgbByteStream {
    SystemStageString filename; // Original igFile +0C.
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::uint32_t position{},mode{},alignment{4}; // Cursor, +14, +24.
    std::int32_t size{}; // +1C.
};
struct IgbScratchBuffer {
    std::vector<std::uint8_t> bytes;
    // Native allocations begin unknown. Short reads only initialize bytes
    // actually copied; consumers reject unknown bytes at the reached read.
    std::vector<std::uint8_t> initialized;
    bool released{}; // Preserve invalidity of aliases after original pool free.
};
struct IgbEndianField {std::uint16_t width{};};
// Explicit native replacement for the selected pool's scratch free/allocate
// virtuals. Identity is weak so a service binding is not a guest reference.
struct IgbNativeScratchPool {std::weak_ptr<ModelSceneObject> owner;};

struct IgbStreamState {
    std::weak_ptr<LoaderRequestFile> file; // Canonical directory/name/+A4, owned by caller.
    std::shared_ptr<IgbDecodeStatusWords> statuses=std::make_shared<IgbDecodeStatusWords>();
    std::shared_ptr<IgbByteStream> stream; // +68.
    std::shared_ptr<IgbScratchBuffer> buffer; // +108.
    std::int32_t buffer_capacity{}; // +104, independently represented.
    std::uint32_t expected_cookie{},version{},owns_stream{}; // +60/+64/+C4.
    std::uint8_t swapped{},flag_d4{},flag_d5{},flag_d6{},flag_d7{},flag_124{};
    std::uint32_t legacy_mode{}; // +D8.
    std::uint32_t entry_bytes{},entry_count{},object_type_bytes{},object_type_count{};
    std::uint32_t object_bytes{},object_count{},memory_reference_bytes{},memory_reference_count{};
    std::uint32_t field_type_bytes{},field_type_count{};
    std::shared_ptr<IgbEndianField> int_field,short_field; // +CC/+D0.
    std::vector<IgbNativeScratchPool> native_scratch_pools;
private:
    friend class IgbOpenInvocation;
    friend class IgbHeaderInvocation;
    friend class IgbBufferInvocation;
    std::weak_ptr<void> active_open_,active_header_,active_buffer_;
    bool open_fault_{},header_fault_{},buffer_fault_{};
    std::vector<std::shared_ptr<IgbEndianField>> unpublished_fields_;
};

void igb_bind_native_scratch_pool(const std::shared_ptr<IgbStreamState>&,
    const std::shared_ptr<ModelSceneObject>&);

std::shared_ptr<IgbByteStream> igb_stream_from_bytes(SystemStageString filename,
    std::vector<std::uint8_t> bytes,std::uint32_t alignment=4);
std::uint32_t igb_read_elements(const std::shared_ptr<IgbByteStream>&,
    const std::shared_ptr<IgbScratchBuffer>&,std::size_t offset,std::uint32_t size,std::uint32_t count);
std::span<const std::uint8_t> igb_buffer_bytes(const std::shared_ptr<IgbScratchBuffer>&,
    std::size_t offset,std::size_t length);
std::uint32_t igb_buffer_word(const std::shared_ptr<IgbScratchBuffer>&,std::size_t offset);
void igb_swap_words(const std::shared_ptr<IgbScratchBuffer>&,std::size_t offset,
    std::uint16_t width,std::int32_t count); // Actual80064B70 finite bounded bytes.
class IgbBufferInvocation {
public:
    IgbBufferInvocation(std::shared_ptr<IgbStreamState>,std::int32_t minimum);
    ~IgbBufferInvocation();
    IgbBufferInvocation(const IgbBufferInvocation&)=delete;
    IgbBufferInvocation& operator=(const IgbBufferInvocation&)=delete;
    MessengerCallResult resume();
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

struct IgbOpenOperation {std::function<MessengerCallResult()> resume;};
struct IgbStreamNameOperation {
    std::function<MessengerCallResult()> resume;
    SystemStageString result;
};
struct IgbStreamServices {
    // Concrete8004D6DC opening operation. It owns publication of state.stream
    // and returns the original result word, before8004F870 examines it.
    std::function<std::shared_ptr<IgbOpenOperation>(const std::shared_ptr<IgbStreamState>&,
        const SystemStageString&,std::uint32_t mode)> open;
    // Reached only when the directory name is null/empty. Includes actual
    // string-pool acquisition and interning; no empty successful default.
    std::function<std::shared_ptr<IgbStreamNameOperation>(const SystemStageString&)> intern_filename;
    // Represented native igIntMetaField/igShortMetaField width behavior. Their
    // complete reflected registration is outside the header-only profile.
    std::shared_ptr<IgbEndianField> int_field=std::make_shared<IgbEndianField>(IgbEndianField{4});
    std::shared_ptr<IgbEndianField> short_field=std::make_shared<IgbEndianField>(IgbEndianField{2});
};
// Bind one exact filename to actual supplied bytes. Other names remain pending;
// this does not guess original file-manager path dispatch or catalog setup.
std::shared_ptr<IgbStreamServices> make_igb_byte_stream_services(SystemStageString filename,
    std::vector<std::uint8_t> bytes);

class IgbOpenInvocation {
public:
    IgbOpenInvocation(std::shared_ptr<IgbStreamState>,SystemStageString filename);
    ~IgbOpenInvocation();
    IgbOpenInvocation(const IgbOpenInvocation&)=delete;
    IgbOpenInvocation& operator=(const IgbOpenInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbStreamServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
class IgbHeaderInvocation {
public:
    explicit IgbHeaderInvocation(std::shared_ptr<IgbStreamState>);
    ~IgbHeaderInvocation();
    IgbHeaderInvocation(const IgbHeaderInvocation&)=delete;
    IgbHeaderInvocation& operator=(const IgbHeaderInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbStreamServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
