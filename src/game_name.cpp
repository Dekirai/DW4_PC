#include "dw4/game_name.hpp"
#include "dw4/assets.hpp"

#include <algorithm>
#include <array>
#include <fstream>

namespace dw4 {
namespace {
constexpr std::size_t max_mappings=424,max_input=65535;
constexpr std::uint32_t table_address=0x80425770;
[[noreturn]] void fail(const char* message) {throw GameDataError(message);}
std::uint16_t be16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>((std::uint16_t(bytes[0])<<8)|bytes[1]);
}
std::shared_ptr<GameDataUnicode> unicode(std::size_t units) {
    auto result=std::make_shared<GameDataUnicode>();
    result->buffer=std::make_shared<std::vector<std::uint16_t>>(units,std::uint16_t{});
    return result;
}
}
GameNameTable::GameNameTable(std::vector<GameNameMapping> entries) {
    if(entries.size()>max_mappings)fail("Game-name conversion table exceeds 424 mapping entries.");
    for(const auto& entry:entries)if(entry.unicode==0xffff)
        fail("Game-name mapping entries must precede the original FFFF sentinel.");
    entries_=std::make_shared<const std::vector<GameNameMapping>>(std::move(entries));
}
const std::vector<GameNameMapping>& GameNameTable::entries() const noexcept {return *entries_;}
GameNameTable read_game_name_table(const std::filesystem::path& main_dol) {
    try {
        const auto info=assets::inspect_dol(main_dol);
        constexpr auto byte_count=(max_mappings+1)*4;
        const assets::DolSection* section{};
        for(const auto& candidate:info.sections)
            if(table_address>=candidate.address &&
               std::uint64_t(table_address)+byte_count<=std::uint64_t(candidate.address)+candidate.size)
                section=&candidate;
        if(!section || section->executable)fail("Original game-name table is absent from its expected DOL data section.");
        std::ifstream input(main_dol,std::ios::binary|std::ios::ate);
        if(!input || input.tellg()<0 || static_cast<std::uint64_t>(input.tellg())!=info.file_size)
            fail("Game-name DOL changed or could not be reopened.");
        const auto at=std::uint64_t(section->file_offset)+(table_address-section->address);
        input.seekg(static_cast<std::streamoff>(at));
        std::array<std::uint8_t,byte_count> bytes{};
        input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(!input || static_cast<std::size_t>(input.gcount())!=bytes.size())
            fail("Original game-name table is truncated.");
        if(be16(bytes.data()+max_mappings*4)!=0xffff || be16(bytes.data()+max_mappings*4+2)!=0xffff)
            fail("Original game-name table has an unexpected final sentinel.");
        std::vector<GameNameMapping> entries;entries.reserve(max_mappings);
        for(std::size_t i=0;i<max_mappings;++i)
            entries.push_back({be16(bytes.data()+i*4),be16(bytes.data()+i*4+2)});
        return GameNameTable(std::move(entries));
    } catch(const GameDataError&) {throw;}
      catch(const std::exception& error) {throw GameDataError(error.what());}
}
GameNameConverter::GameNameConverter(GameNameTable table,std::shared_ptr<std::vector<std::uint8_t>> convert_data)
    :table_(std::move(table)),convert_data_(std::move(convert_data)) {
    if(!convert_data_)fail("Game-name conversion requires its actual prepared beFont convertData owner.");
    if(convert_data_->size()>max_input)fail("Prepared beFont scratch exceeds its bounded byte profile.");
}
const std::shared_ptr<std::vector<std::uint8_t>>& GameNameConverter::convert_data() const noexcept {return convert_data_;}
std::shared_ptr<GameDataUnicode> GameNameConverter::convert(std::optional<std::string_view> input) {
    if(executing_)fail("A beFont conversion operation cannot recursively enter itself.");
    if(!input)fail("Original beFont conversion requires a non-null input string.");
    if(input->size()>max_input || input->find('\0')!=std::string_view::npos)
        fail("Game-name input exceeds its bounded NUL-terminated byte profile.");
    if(convert_data_->size()>max_input)fail("Prepared beFont scratch exceeds its bounded byte profile.");
    struct Guard {bool& busy;explicit Guard(bool& value):busy(value){busy=true;}~Guard(){busy=false;}} guard(executing_);
    const auto byte=[&](std::size_t at)->std::uint8_t {
        // A final high-bit byte consumes the original C string's zero terminator.
        return at<input->size()?static_cast<std::uint8_t>((*input)[at]):std::uint8_t{};
    };
    const auto find=[&](std::uint16_t encoded)->const GameNameMapping* {
        for(const auto& entry:table_.entries())if(entry.encoded==encoded)return &entry;
        return nullptr;
    };
    std::size_t count{};
    // 802FB5F8..724: count output and replace the same scratch string once per
    // non-CR/LF input unit, even when that unit has no mapping.
    for(std::size_t cursor=0;cursor<input->size();) {
        const auto first=byte(cursor);
        if(first==10) {if(count>0)++count;++cursor;continue;}
        if(first==13) {++cursor;continue;}
        const auto width=(first&0x80u)?std::size_t{2}:std::size_t{1};
        const auto encoded=width==2 ? static_cast<std::uint16_t>((std::uint16_t(first)<<8)|byte(cursor+1)) : std::uint16_t(first);
        if(find(encoded))++count;
        convert_data_->clear();convert_data_->push_back(first);
        if(width==2)convert_data_->push_back(byte(cursor+1));
        cursor+=width;
    }
    auto result=unicode(count+1);
    std::size_t written{};
    // 802FB778..870: the LF threshold is >1 here, whereas pass one uses >0.
    // Single-byte matches output that input byte, not the mapping's first word.
    for(std::size_t cursor=0;cursor<input->size();) {
        const auto first=byte(cursor);
        if(first==10) {if(written>1)(*result->buffer)[written++]=10;++cursor;continue;}
        if(first==13) {++cursor;continue;}
        const auto width=(first&0x80u)?std::size_t{2}:std::size_t{1};
        const auto encoded=width==2 ? static_cast<std::uint16_t>((std::uint16_t(first)<<8)|byte(cursor+1)) : std::uint16_t(first);
        if(const auto* found=find(encoded))(*result->buffer)[written++]=width==2?found->unicode:first;
        cursor+=width;
    }
    (*result->buffer)[written]=0;
    return result;
}
std::shared_ptr<GameDataUnicode> make_player_name(const GameDataUnicode& converted) {
    if(!converted.buffer)fail("PLAYERNAME requires the completed converter's owned Unicode buffer.");
    auto result=unicode(9);
    for(std::size_t i=0;i<8;++i) {
        if(i>=converted.buffer->size())fail("PLAYERNAME source ended before its original zero terminator or eight units.");
        const auto unit=(*converted.buffer)[i];
        if(!unit)break;
        (*result->buffer)[i]=unit;
    }
    return result;
}
} // namespace dw4
