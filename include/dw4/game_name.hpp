#pragma once

#include "dw4/game_data.hpp"

#include <filesystem>

namespace dw4 {
struct GameDataUnicode;
struct GameNameMapping {
    std::uint16_t unicode{},encoded{};
    bool operator==(const GameNameMapping&) const=default;
};

// Ordered pairs preceding the original FFFF sentinel. No original mapping
// bytes are compiled into the port. Synthetic tables use the same first-match
// behavior, including the single-byte branch's independent output value.
class GameNameTable {
public:
    explicit GameNameTable(std::vector<GameNameMapping> entries);
    const std::vector<GameNameMapping>& entries() const noexcept;
private:
    std::shared_ptr<const std::vector<GameNameMapping>> entries_;
};
GameNameTable read_game_name_table(const std::filesystem::path& main_dol);

// Only the original beFont conversion operation. The caller must resolve the
// actual same-context font and supply its retained +38 convertData owner.
// Having a mapping table does not establish that this font service exists.
class GameNameConverter {
public:
    GameNameConverter(GameNameTable table,std::shared_ptr<std::vector<std::uint8_t>> convert_data);
    GameNameConverter(const GameNameConverter&)=delete;
    GameNameConverter& operator=(const GameNameConverter&)=delete;
    GameNameConverter(GameNameConverter&&)=delete;
    GameNameConverter& operator=(GameNameConverter&&)=delete;
    std::shared_ptr<GameDataUnicode> convert(std::optional<std::string_view> input);
    const std::shared_ptr<std::vector<std::uint8_t>>& convert_data() const noexcept;
private:
    GameNameTable table_;
    std::shared_ptr<std::vector<std::uint8_t>> convert_data_;
    bool executing_{};
};

// 8036ED14..EE48: a distinct zeroed nine-unit name buffer, with at most eight
// source units copied up to the first zero. No Unicode scalar interpretation.
std::shared_ptr<GameDataUnicode> make_player_name(const GameDataUnicode& converted);
} // namespace dw4
