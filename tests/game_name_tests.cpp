#include "dw4/game_name.hpp"
#include "dw4/assets.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
using namespace dw4;
using Bytes=std::vector<std::uint8_t>;
unsigned checks{};
void check(bool condition,const char* reason) {++checks;if(!condition)throw std::runtime_error(reason);}
template<class F> void rejects(F operation) {
    ++checks;try{operation();}catch(const GameDataError&){return;}
    throw std::runtime_error("Invalid game-name operation was accepted.");
}
std::string raw(std::initializer_list<std::uint8_t> bytes) {return std::string(bytes.begin(),bytes.end());}
GameNameTable table() {return GameNameTable({{0x7777,0x41},{0x42,0x42},{0x43,0x43},{0x3000,0x8140},
    {0x3042,0x82a0},{0x3043,0x82a0},{0,0x8100},{0xd800,0x8340},{0xdc00,0x8341}});}

void conversion() {
    auto scratch=std::make_shared<Bytes>(Bytes{7,8,9});GameNameConverter convert(table(),scratch);
    const auto result=convert.convert("ABC");
    check(result && !result->name && *result->buffer==std::vector<std::uint16_t>{65,66,67,0},
        "Single-byte conversion used the table's output word instead of the original input byte.");
    check(convert.convert_data()==scratch && *scratch==Bytes{67},"Converter replaced its scratch owner or retained earlier units.");
    const auto pair=convert.convert(raw({0x82,0xa0,0x81,0x40}));
    check(*pair->buffer==std::vector<std::uint16_t>{0x3042,0x3000,0} && *scratch==Bytes{0x81,0x40},
        "Two-byte conversion lost exact first-match order or big-endian source assembly.");
    check(*result->buffer==std::vector<std::uint16_t>{65,66,67,0},"A later conversion changed an earlier Unicode owner.");
    const auto unknown=convert.convert(raw({0x82,0xff,0x5a}));
    check(*unknown->buffer==std::vector<std::uint16_t>{0} && *scratch==Bytes{0x5a},
        "Unmapped bytes were emitted or failed to update scratch.");
    const auto retained=*scratch;const auto empty=convert.convert("");
    check(*empty->buffer==std::vector<std::uint16_t>{0} && *scratch==retained,"Empty conversion cleared the original retained scratch.");
    const auto controls=convert.convert("\r\n\r\n");
    check(*controls->buffer==std::vector<std::uint16_t>{0} && *scratch==retained,"Skipped leading control bytes changed scratch or output.");
    const auto lf=convert.convert("A\nB\nC");
    check(*lf->buffer==std::vector<std::uint16_t>{65,66,10,67,0,0} && *scratch==Bytes{67},
        "Distinct first-pass >0 and second-pass >1 LF thresholds or zeroed spare capacity changed.");
    const auto many_lf=convert.convert("A\n\n\n");
    check(*many_lf->buffer==std::vector<std::uint16_t>{65,0,0,0,0} && *scratch==Bytes{65},
        "Suppressed LF output did not preserve first-pass allocation size.");
    const auto trailing=convert.convert(raw({0x81}));
    check(*trailing->buffer==std::vector<std::uint16_t>{0,0} && *scratch==Bytes{0x81,0},
        "Final high-bit byte did not consume the implicit C-string terminator.");
    const auto surrogates=convert.convert(raw({0x83,0x40,0x83,0x41}));
    check(*surrogates->buffer==std::vector<std::uint16_t>{0xd800,0xdc00,0},"Conversion interpreted Unicode scalar values instead of exact16-bit units.");
    auto no_table=GameNameTable({});GameNameConverter absent(no_table,scratch);
    check(*absent.convert("ABC")->buffer==std::vector<std::uint16_t>{0} && *scratch==Bytes{67},
        "Synthetic empty mapping table invented ASCII fallback.");
    const auto table_copy=no_table;check(&table_copy.entries()==&no_table.entries(),"Table copy lost immutable shared ownership.");
}

void player_copy_and_errors() {
    auto scratch=std::make_shared<Bytes>();GameNameConverter converter(table(),scratch);
    auto source=converter.convert("ABCABCABC");const auto copied=make_player_name(*source);
    check(copied!=source && copied->buffer!=source->buffer && !copied->name &&
        *copied->buffer==std::vector<std::uint16_t>{65,66,67,65,66,67,65,66,0},"PLAYERNAME did not create a separate exact nine-unit buffer.");
    (*source->buffer)[0]=90;check((*copied->buffer)[0]==65,"PLAYERNAME borrowed the converter's mutable allocation.");
    GameDataUnicode direct;direct.name="incoming named Unicode";
    direct.buffer=std::make_shared<std::vector<std::uint16_t>>(std::initializer_list<std::uint16_t>{1,0,9,9});
    const auto stopped=make_player_name(direct);
    check(!stopped->name && *stopped->buffer==std::vector<std::uint16_t>{1,0,0,0,0,0,0,0,0},
        "PLAYERNAME copied the source object's name or units after the first zero.");
    direct.buffer=std::make_shared<std::vector<std::uint16_t>>(8,std::uint16_t{0xffff});
    check(make_player_name(direct)->buffer->size()==9 && (*make_player_name(direct)->buffer)[7]==0xffff,
        "An eight-unit source required a ninth source unit or rejected rawFFFF.");
    GameDataUnicode missing;rejects([&]{make_player_name(missing);});
    direct.buffer=std::make_shared<std::vector<std::uint16_t>>();rejects([&]{make_player_name(direct);});
    *direct.buffer={1,2,3};rejects([&]{make_player_name(direct);});
    *scratch={7,8};rejects([&]{converter.convert(std::nullopt);});
    rejects([&]{converter.convert(std::string_view("A\0B",3));});
    rejects([&]{converter.convert(std::string(65536,'A'));});
    check(*scratch==Bytes{7,8},"Input validation mutated the original scratch before failure.");
    rejects([&]{GameNameConverter missing_owner(table(),{});});
    rejects([&]{GameNameConverter large(table(),std::make_shared<Bytes>(65536));});
    rejects([&]{GameNameTable too_many(std::vector<GameNameMapping>(425));});
    rejects([&]{GameNameTable sentinel({{0xffff,7}});});
    const auto bounded=converter.convert(std::string(65535,'A'));
    check(bounded->buffer->size()==65536 && bounded->buffer->back()==0 && (*bounded->buffer)[65534]==65,
        "Maximum bounded input did not retain its actual output and terminator.");
}

void be32(Bytes& bytes,std::size_t at,std::uint32_t value) {
    for(std::size_t i=0;i<4;++i)bytes.at(at+i)=static_cast<std::uint8_t>(value>>((3-i)*8));
}
struct Files {
    std::filesystem::path directory;std::vector<std::filesystem::path> files;
    Files() {
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned attempt=0;attempt<100;++attempt) {
            directory=std::filesystem::temp_directory_path()/("dw4_game_name_"+std::to_string(stamp)+"_"+std::to_string(attempt));
            if(std::filesystem::create_directory(directory))return;
        }
        throw std::runtime_error("Cannot create game-name fixture directory.");
    }
    ~Files() {std::error_code error;for(const auto& file:files)std::filesystem::remove(file,error);std::filesystem::remove(directory,error);}
    std::filesystem::path write(const Bytes& bytes) {
        const auto path=directory/(std::to_string(files.size())+".dol");files.push_back(path);
        std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(!output)throw std::runtime_error("Cannot write game-name fixture.");return path;
    }
};
Bytes dol() {
    Bytes bytes(0x120+425*4);be32(bytes,0,0x100);be32(bytes,0x48,0x80003100);be32(bytes,0x90,0x20);
    be32(bytes,7*4,0x120);be32(bytes,0x48+7*4,0x80425770);be32(bytes,0x90+7*4,425*4);be32(bytes,0xe0,0x80003100);
    for(std::size_t i=0;i<424;++i)be32(bytes,0x120+i*4,(static_cast<std::uint32_t>(i+1)<<16)|static_cast<std::uint32_t>(i));
    be32(bytes,0x120+424*4,0xffffffff);return bytes;
}
void table_files() {
    Files files;const auto bytes=dol();const auto decoded=read_game_name_table(files.write(bytes));
    check(decoded.entries().size()==424 && decoded.entries()[0]==GameNameMapping{1,0} &&
        decoded.entries()[423]==GameNameMapping{424,423},"DOL mapping extraction changed paired16-bit endian order.");
    auto bad=bytes;be32(bad,0x120+424*4,0xffff0000);rejects([&]{read_game_name_table(files.write(bad));});
    bad=bytes;be32(bad,0x120+4,0xffff0010);rejects([&]{read_game_name_table(files.write(bad));});
    bad=bytes;be32(bad,0x48+7*4,0x80425774);rejects([&]{read_game_name_table(files.write(bad));});
    bad=bytes;bad.pop_back();rejects([&]{read_game_name_table(files.write(bad));});
    rejects([&]{read_game_name_table(files.directory/"absent.dol");});
}

void original(const std::filesystem::path& root) {
    const auto table=read_game_name_table(root/"sys"/"main.dol");
    check(table.entries().size()==424,"Original beFont mapping table count changed.");
    auto scratch=std::make_shared<Bytes>();GameNameConverter converter(table,scratch);
    const auto value=converter.convert(raw({0x81,0x40,0x82,0xa0}));
    check(*value->buffer==std::vector<std::uint16_t>{0x3000,0x3042,0},"Original beFont source mapping changed.");
    for(const auto& mapping:table.entries()) {
        std::string input;
        if(mapping.encoded>=0x100)input.push_back(static_cast<char>(mapping.encoded>>8));
        input.push_back(static_cast<char>(mapping.encoded&255));
        if(input.find('\0')!=std::string::npos)continue;
        const auto result=converter.convert(input);
        check(result && result->buffer && !result->buffer->empty() && result->buffer->back()==0,
            "Original mapping input failed bounded Unicode ownership.");
    }
}

Bytes hex(std::string_view text) {
    if(text=="-")return {};
    if(text.size()%2)throw std::runtime_error("Invalid oracle byte text.");
    Bytes result;for(std::size_t i=0;i<text.size();i+=2)result.push_back(static_cast<std::uint8_t>(std::stoul(std::string(text.substr(i,2)),nullptr,16)));
    return result;
}
void oracle() {
    std::string line;while(std::getline(std::cin,line)) {
        try {
            std::istringstream input(line);std::string bytes,scratch_bytes;std::size_t count{};
            if(!(input>>bytes>>scratch_bytes>>count) || count>424)throw std::runtime_error("Invalid oracle packet.");
            std::vector<GameNameMapping> entries;
            for(std::size_t i=0;i<count;++i) {unsigned unicode{},encoded{};if(!(input>>unicode>>encoded) || unicode>65535 || encoded>65535)throw std::runtime_error("Invalid oracle pair.");entries.push_back({static_cast<std::uint16_t>(unicode),static_cast<std::uint16_t>(encoded)});}
            auto scratch=std::make_shared<Bytes>(hex(scratch_bytes));GameNameConverter converter(GameNameTable(std::move(entries)),scratch);
            const auto raw_bytes=bytes=="~"?Bytes{}:hex(bytes);const std::string text(raw_bytes.begin(),raw_bytes.end());
            const auto result=converter.convert(bytes=="~"?std::optional<std::string_view>{}:std::string_view(text));
            const auto player=make_player_name(*result);
            std::cout<<result->buffer->size();for(const auto value:*result->buffer)std::cout<<' '<<value;
            std::cout<<' '<<scratch->size();for(const auto value:*scratch)std::cout<<' '<<unsigned(value);
            for(const auto value:*player->buffer)std::cout<<' '<<value;std::cout<<'\n';
        } catch(const std::exception&) {std::cout<<"ERROR\n";}
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--oracle") {oracle();return 0;}
        conversion();player_copy_and_errors();table_files();
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: game_name_tests [ORIGINAL_ROOT | --oracle]");
        std::cout<<checks<<" game-name checks passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"Game-name test failure: "<<error.what()<<'\n';return 1;}
}
