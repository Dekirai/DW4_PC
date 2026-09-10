#pragma once

#include "dw4/igb_document.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dw4 {
class DatabaseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct DatabaseListIdentity {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::uint32_t> memory_reference,memory_owner_reference;
    std::uint32_t count{},capacity{};
    bool operator==(const DatabaseListIdentity&) const=default;
};
struct DatabaseName {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name;
    bool operator==(const DatabaseName&) const=default;
};
template<class T> struct DatabaseGroup {
    DatabaseListIdentity columns_source,data_source;
    std::vector<DatabaseName> columns;
    std::vector<T> values;
};

// Immutable beDataBase graph. Copies share decoded ownership; source occurrence
// and canonical object/list/memory identities remain distinct from raw refs.
class DatabaseDefinition {
public:
    const std::string& source() const noexcept;
    std::size_t source_occurrence() const noexcept;
    std::uint32_t reference() const noexcept;
    std::uint32_t owner_reference() const noexcept;
    const DatabaseGroup<std::int8_t>& char_group() const noexcept;
    const DatabaseGroup<std::int16_t>& short_group() const noexcept;
    const DatabaseGroup<std::int32_t>& int_group() const noexcept;
    const DatabaseGroup<float>& float_group() const noexcept;
    const DatabaseGroup<std::uint8_t>& bool_group() const noexcept;
    const DatabaseGroup<DatabaseName>& string_group() const noexcept;

    // 8031F608: first exact name within char, then short, then int. Only an
    // absent column returns zero; a found column with invalid row is rejected.
    std::int32_t integer(std::int32_t row,std::optional<std::string_view> column) const;
    // 8031F6D0/70C: missing columns/invalid rows are checked host boundaries.
    // Bool values remain raw bytes; null cell names return an empty string.
    std::uint8_t boolean(std::int32_t row,std::optional<std::string_view> column) const;
    std::string_view string(std::int32_t row,std::optional<std::string_view> column) const;
    // 8031F760: first nonempty columns in string,char,short,int,float,bool order.
    std::int32_t row_count() const noexcept;
private:
    struct Data;
    explicit DatabaseDefinition(std::shared_ptr<const Data> data):data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
    friend DatabaseDefinition decode_database(const IgbDocument&,std::uint32_t,std::string,std::size_t);
};

// Decodes all six exact typed pairs. Export/RAM attachment is validated by the
// separate DATABASE info owner. The supported profile requires complete rows
// per nonempty group; their row counts may differ. Empty authored groups remain
// present, and no float-query conversion/default is invented.
DatabaseDefinition decode_database(const IgbDocument& document,std::uint32_t database_reference,
    std::string source,std::size_t source_occurrence);
} // namespace dw4
