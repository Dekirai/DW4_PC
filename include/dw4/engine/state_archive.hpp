#pragma once

// A small binary archive for save state files: one io() call per field
// serves both directions, so a struct's writer and reader cannot drift
// apart. Little endian, sizes as 32-bit counts; reading past the end or an
// implausible count throws.
#include "dw4/engine/math.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace dw4::engine {

class StateArchive {
public:
    // Writing into `out`.
    explicit StateArchive(std::vector<std::uint8_t>& out) : out_(&out) {}
    // Reading from `in`.
    explicit StateArchive(std::span<const std::uint8_t> in) : in_(in) {}
    bool reading() const { return out_ == nullptr; }
    bool at_end() const { return reading() && position_ == in_.size(); }

    template <class T> void io(T& value) {
        if constexpr (std::is_same_v<T, bool>) {
            std::uint8_t b = value ? 1 : 0;
            bytes(&b, 1);
            value = b != 0;
        } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
            bytes(&value, sizeof value);
        } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::u16string>) {
            std::uint32_t count = std::uint32_t(value.size());
            io(count);
            check(count, sizeof(typename T::value_type));
            if (reading()) value.resize(count);
            if (count) bytes(value.data(), count * sizeof(typename T::value_type));
        } else if constexpr (std::is_same_v<T, Vec3f>) {
            io(value.x);
            io(value.y);
            io(value.z);
        } else {
            serialize(*this, value); // found by argument-dependent lookup
        }
    }
    template <class T> void io(std::vector<T>& values) {
        std::uint32_t count = std::uint32_t(values.size());
        io(count);
        check(count, 1);
        if (reading()) values.resize(count);
        for (auto& value : values) io(value);
    }
    template <class T, std::size_t N> void io(std::array<T, N>& values) {
        for (auto& value : values) io(value);
    }
    template <class K, class V, class C> void io(std::map<K, V, C>& values) {
        std::uint32_t count = std::uint32_t(values.size());
        io(count);
        check(count, 1);
        if (reading()) {
            values.clear();
            for (std::uint32_t i = 0; i < count; ++i) {
                K key{};
                V value{};
                io(key);
                io(value);
                values.emplace(std::move(key), std::move(value));
            }
        } else {
            for (auto& [key, value] : values) {
                K k = key;
                io(k);
                io(value);
            }
        }
    }

private:
    void bytes(void* data, std::size_t size) {
        if (out_) {
            const auto* p = static_cast<const std::uint8_t*>(data);
            out_->insert(out_->end(), p, p + size);
        } else {
            if (position_ + size > in_.size()) throw std::runtime_error("save state is truncated");
            std::memcpy(data, in_.data() + position_, size);
            position_ += size;
        }
    }
    void check(std::uint32_t count, std::size_t element) const {
        if (reading() && std::uint64_t(count) * std::max<std::size_t>(element, 1) > in_.size() - position_)
            throw std::runtime_error("save state holds an implausible count");
    }
    std::vector<std::uint8_t>* out_{};
    std::span<const std::uint8_t> in_;
    std::size_t position_{};
};

} // namespace dw4::engine
