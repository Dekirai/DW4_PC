#pragma once

#include "dw4/igb_runtime_types.hpp"

namespace dw4 {
class IgbFieldReferenceError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
// Captures an optional physical descriptor identity without reading its bytes.
// Reached full32 accesses use that allocation; only originally canonical-only
// descriptors use the host member. No guest retain/release occurs on lifetime.
class IgbFieldReferenceAccess {
public:
    explicit IgbFieldReferenceAccess(std::shared_ptr<IgbDecodeRuntimeField>);
    ~IgbFieldReferenceAccess();
    IgbFieldReferenceAccess(const IgbFieldReferenceAccess&)=delete;
    IgbFieldReferenceAccess& operator=(const IgbFieldReferenceAccess&)=delete;
    // Reads never refresh/write the canonical cache. Successful writes update
    // physical storage first and synchronize the canonical packed word.
    std::uint32_t read();
    void write(std::uint32_t);
    bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
