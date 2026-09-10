#include "dw4/game_data.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation) {
    ++checks;try {operation();}catch(const GameDataError&){return;}
    throw std::runtime_error("Invalid native game-data state was accepted.");
}
template<class T> std::shared_ptr<T> named(std::optional<std::string> name) {
    auto result=std::make_shared<T>();result->name=std::move(name);return result;
}
void defaults() {
    static_assert(!std::is_copy_constructible_v<GameDataState>);
    static_assert(!std::is_move_constructible_v<GameDataState>);
    const GameDataInt integer;const GameDataBool boolean;const GameDataString string;
    check(!integer.name && integer.data==0 && !boolean.name && boolean.data==0 && !string.name && !string.data,
        "Fresh named wrappers lost nullable names or exact zero/null defaults.");
    GameDataState state;
    check(game_common_schema().size()==13 && game_player_schema().size()==64 && state.common()->entries().size()==13,
        "Native common/player schemas are incomplete.");
    std::set<const void*> wrappers,arrays,disks;
    auto inspect=[&](const GameDataList& row,std::span<const GameDataSchemaField> schema) {
        check(row.entries().size()==schema.size(),"Generated row has a different field count.");
        for(std::size_t i=0;i<schema.size();++i) {
            const auto& field=schema[i];const auto& object=row.entries()[i];
            check(object.index()==static_cast<std::size_t>(field.kind)+1 && row.find(field.name)==&object,
                "Generated field order, exact name or wrapper type changed.");
            std::visit([&](const auto& pointer) {
                using T=std::decay_t<decltype(pointer)>;
                if constexpr(!std::is_same_v<T,std::monostate> && !std::is_same_v<T,std::shared_ptr<GameDataDisk>>) {
                    check(pointer && pointer->name==field.name && wrappers.insert(pointer.get()).second,
                        "Generated fields share an unintended wrapper identity.");
                    if constexpr(std::is_same_v<T,std::shared_ptr<GameDataInt>> || std::is_same_v<T,std::shared_ptr<GameDataBool>>)
                        check(pointer->data==0,"Generated scalar is not its actual zero default.");
                    else if constexpr(std::is_same_v<T,std::shared_ptr<GameDataString>>)
                        check(!pointer->data,"Generated string payload is empty instead of null.");
                    else {
                        check(pointer->data && arrays.insert(pointer->data.get()).second,"Generated referenced lists share an unintended owner.");
                        const auto count=field.name=="DISKFOLDER"?12:field.count;
                        check(pointer->data->size()==count,"Generated referenced list has a wrong initial count.");
                        for(std::size_t n=0;n<pointer->data->size();++n) {
                            const auto& value=pointer->data->at(n);
                            if constexpr(std::is_same_v<T,std::shared_ptr<GameDataIntList>>)
                                check(value==((field.name=="BASE_LEVEL" || field.name=="BASE_SKILL")?1:0),
                                    "Generated integer-list values differ from CE1C initialization.");
                            else if constexpr(std::is_same_v<T,std::shared_ptr<GameDataBoolList>>)
                                check(value==((field.name=="BASE_FLAG" && n==960)?1:0),"Generated flag-list values differ from CE1C initialization.");
                            else if constexpr(std::is_same_v<T,std::shared_ptr<GameDataStringList>>)
                                check(!value,"Generated string-list slots must be null.");
                            else if constexpr(std::is_same_v<T,std::shared_ptr<GameDataFloatList>>)
                                check(std::bit_cast<std::uint32_t>(value)==0,"Generated float-list slots must be positive zero.");
                            else if constexpr(std::is_same_v<T,std::shared_ptr<GameDataObjectList>>) {
                                if(field.name=="DISKFOLDER") {
                                    const auto* disk=std::get_if<std::shared_ptr<GameDataDisk>>(&value);
                                    check(disk && *disk && (*disk)->kind_no==4 && (*disk)->record_no==static_cast<std::int32_t>(n) &&
                                        (*disk)->is_equipped==0 && (*disk)->count==0 && disks.insert(disk->get()).second,
                                        "Disk folder lost a distinct initialized beNDMWItemDisk object.");
                                } else check(std::holds_alternative<std::monostate>(value),"Generated object-list slot must be null.");
                            }
                        }
                    }
                } else throw std::runtime_error("Generated row contains an unnamed object.");
            },object);
        }
    };
    inspect(*state.common(),game_common_schema());
    for(std::size_t i=0;i<4;++i)inspect(*state.player(i),game_player_schema());
    check(wrappers.size()==269 && disks.size()==48,"Generated four-player graph is incomplete.");
    check(state.player(0)->require<GameDataObjectList>("PLAYERNAME")->data->empty(),"PLAYERNAME was converted into a string.");
    rejects([&]{state.player(0)->require<GameDataString>("PLAYERNAME");});
    state.player(0)->require<GameDataInt>("HP")->data=125;
    check(state.player(0)->require<GameDataInt>("MHP")->data==0 && state.player(1)->require<GameDataInt>("HP")->data==0,
        "Live HP mutation leaked into another independent wrapper.");
}
void first_named_lookup() {
    auto null_name=named<GameDataInt>(std::nullopt);null_name->data=4;
    auto empty=named<GameDataInt>(std::string{});empty->data=7;
    auto first=named<GameDataInt>("HP");first->data=-9;
    auto second=named<GameDataInt>("HP");second->data=100;
    auto wrong=named<GameDataBool>("HP");wrong->data=255;
    GameDataList row({null_name,empty,first,second,first});
    check(row.require<GameDataInt>("")==empty && row.require<GameDataInt>("HP")==first &&
        row.entries().size()==5 && std::get<std::shared_ptr<GameDataInt>>(row.entries().back())==first,
        "Lookup lost nullable names, empty names, first matches or duplicate aliases.");
    check(!row.find("hp") && !row.find<GameDataInt>("missing"),"Named lookup is not exact byte matching.");
    rejects([&]{row.require<GameDataInt>("missing");});
    GameDataList mismatched({wrong,first});
    check(mismatched.require<GameDataBool>("HP")->data==255,"Raw noncanonical Bool byte was normalized.");
    rejects([&]{mismatched.find<GameDataInt>("HP");});
    rejects([&]{mismatched.require<GameDataInt>("HP");});
    auto array=named<GameDataIntList>("COUNTER");array->data->push_back(-4);
    auto alias=named<GameDataIntList>("OTHER");alias->data=array->data;
    GameDataList lists({array,alias});
    lists.require<GameDataIntList>("OTHER")->data->at(0)=19;
    check(array->data->at(0)==19 && lists.require<GameDataIntList>("COUNTER")==array,
        "Named array lookup copied its wrapper or inner list.");
    auto text=named<GameDataString>("TEXT");GameDataList texts({text});
    check(!texts.require<GameDataString>("TEXT")->data,"Null string was normalized before use.");
    text->data=std::string{};
    check(texts.require<GameDataString>("TEXT")->data==std::optional<std::string>{std::string{}},"Live empty string lost its non-null identity.");
}
void resets_and_retained_owners() {
    GameDataState state;const auto common=state.common();
    const auto old_common=common->require<GameDataInt>("NINZU");old_common->data=4;
    const auto old_flags=common->require<GameDataBoolList>("FLAG")->data;old_flags->at(9)=255;
    const auto zero=state.player(0);const auto one=state.player(1);const auto two=state.player(2);const auto three=state.player(3);
    const auto old_hp=one->require<GameDataInt>("HP");old_hp->data=250;
    const auto old_disk=std::get<std::shared_ptr<GameDataDisk>>(one->require<GameDataObjectList>("DISKFOLDER")->data->at(11));old_disk->count=3;
    state.reset_player(1);
    check(state.common()==common && common->require<GameDataInt>("NINZU")==old_common && old_common->data==4 && old_flags->at(9)==255,
        "Resetting a nonzero player changed common data or its identities.");
    check(state.player(1)!=one && state.player(0)==zero && state.player(2)==two && state.player(3)==three,
        "Player reset replaced the wrong row identities.");
    check(old_hp->data==250 && old_disk->count==3 && one->require<GameDataInt>("HP")==old_hp &&
        state.player(1)->require<GameDataInt>("HP")!=old_hp && state.player(1)->require<GameDataInt>("HP")->data==0,
        "Reset rewrote retained old HP/disk owners or reused a replacement wrapper.");
    const auto replacement=state.player(1);state.reset_player(0);
    check(state.common()==common && common->require<GameDataInt>("NINZU")!=old_common &&
        common->require<GameDataInt>("NINZU")->data==0 && old_common->data==4 && old_flags->at(9)==255,
        "Player-zero reset did not repopulate the same common-list owner with fresh wrappers.");
    check(state.player(0)!=zero && state.player(1)==replacement && state.player(2)==two && state.player(3)==three,
        "Player-zero reset disturbed other rows.");
    for(const auto index:{std::size_t{4},std::numeric_limits<std::size_t>::max()}) {
        rejects([&]{state.player(index);});rejects([&]{state.reset_player(index);});
    }
    check(state.common()==common && state.player(1)==replacement && old_hp->data==250,"Rejected index published a partial reset.");
    std::weak_ptr<GameDataInt> released;
    {GameDataState temporary;released=temporary.player(2)->require<GameDataInt>("HP");temporary.reset_player(2);
        check(released.expired(),"Reset leaked an unretained obsolete wrapper.");}
}
void malformed_rows() {
    rejects([]{GameDataList row({std::monostate{}});});
    rejects([]{GameDataList row({std::shared_ptr<GameDataInt>{}});});
    rejects([]{GameDataList row({std::make_shared<GameDataDisk>()});});
    auto missing=named<GameDataIntList>("SKILL");missing->data.reset();rejects([&]{GameDataList row({missing});});
    auto wrong_name=named<GameDataInt>(std::string("HP\0alien",8));rejects([&]{GameDataList row({wrong_name});});
    wrong_name->name=std::string(4097,'x');rejects([&]{GameDataList row({wrong_name});});
    auto text=named<GameDataString>("TEXT");text->data=std::string("x\0y",3);rejects([&]{GameDataList row({text});});
    auto strings=named<GameDataStringList>("STRINGS");strings->data->push_back(std::string("x\0y",3));rejects([&]{GameDataList row({strings});});
    auto floats=named<GameDataFloatList>("POS");floats->data->push_back(std::numeric_limits<float>::infinity());rejects([&]{GameDataList row({floats});});
    auto huge=named<GameDataBoolList>("FLAGS");huge->data->resize(65537);rejects([&]{GameDataList row({huge});});
    huge->data->resize(40000);auto other=named<GameDataBoolList>("OTHER");other->data->resize(40000);
    rejects([&]{GameDataList row({huge,other});});
    auto scalar=named<GameDataInt>("HP");rejects([&]{GameDataList row(std::vector<GameDataObject>(4097,scalar));});
    scalar->name=std::string(4096,'x');rejects([&]{GameDataList row(std::vector<GameDataObject>(257,scalar));});
    auto objects=named<GameDataObjectList>("OBJECTS");objects->data->push_back(std::shared_ptr<GameDataDisk>{});
    rejects([&]{GameDataList row({objects});});
    objects->data->clear();objects->data->push_back(objects);rejects([&]{GameDataList row({objects});});objects->data->clear();
    scalar->name="HP";GameDataList row({scalar});
    rejects([&]{row.find(std::string("HP\0",3));});
    scalar->name=std::string("HP\0",3);rejects([&]{row.find("HP");});
}

// Optional direct comparison with original DOL data. This checks all schema
// records and initialization constants/instructions, not an emulated factory.
class OriginalDol {
public:
    explicit OriginalDol(const std::filesystem::path& root) {
        std::ifstream stream(root/"sys"/"main.dol",std::ios::binary|std::ios::ate);
        if(!stream)throw std::runtime_error("Cannot open original sys/main.dol.");
        const auto size=stream.tellg();
        if(size<0x100 || size>32*1024*1024)throw std::runtime_error("Original DOL has an invalid size.");
        bytes.resize(static_cast<std::size_t>(size));stream.seekg(0);stream.read(reinterpret_cast<char*>(bytes.data()),size);
        if(!stream)throw std::runtime_error("Original DOL read was incomplete.");
    }
    std::uint32_t word(std::uint32_t address) const {return at(offset(address,4));}
    std::uint8_t byte(std::uint32_t address) const {return bytes.at(offset(address,1));}
    std::string string(std::uint32_t address) const {
        std::string value;for(std::uint32_t i=0;i<4096;++i) {const auto c=byte(address+i);if(c==0)return value;value.push_back(static_cast<char>(c));}
        throw std::runtime_error("Original schema name has no bounded terminator.");
    }
private:
    std::vector<std::uint8_t> bytes;
    std::uint32_t at(std::size_t n) const {
        return (std::uint32_t(bytes.at(n))<<24)|(std::uint32_t(bytes.at(n+1))<<16)|(std::uint32_t(bytes.at(n+2))<<8)|bytes.at(n+3);
    }
    std::size_t offset(std::uint32_t address,std::uint32_t length) const {
        for(std::size_t i=0;i<18;++i) {
            const auto base=at(0x48+i*4),size=at(0x90+i*4),file=at(i*4);
            if(address>=base && address-base<=size && length<=size-(address-base) &&
                file<=bytes.size() && address-base<=bytes.size()-file && length<=bytes.size()-file-(address-base))
                return file+(address-base);
        }
        throw std::runtime_error("Original DOL address is outside stored sections.");
    }
};
void original(const std::filesystem::path& root) {
    const OriginalDol dol(root);
    auto schema=[&](std::uint32_t start,std::span<const GameDataSchemaField> fields) {
        for(std::size_t i=0;i<fields.size();++i) {
            const auto address=start+static_cast<std::uint32_t>(i*12);const auto& field=fields[i];
            check(dol.word(address)==static_cast<std::uint32_t>(field.kind) && dol.word(address+4)==field.count &&
                dol.string(dol.word(address+8))==field.name,"Native schema differs from an actual original DOL record.");
        }
        const auto end=start+static_cast<std::uint32_t>(fields.size()*12);
        check(dol.word(end)==0xffffffffu && dol.word(end+4)==0 && dol.string(dol.word(end+8)).empty(),"Original schema sentinel changed.");
    };
    schema(0x804ea930,game_common_schema());schema(0x804ea9d8,game_player_schema());
    check(dol.word(0x804eace4)==0 && dol.word(0x804eace8)==0 && dol.word(0x804ead10)==1 && dol.word(0x804ead14)==1 &&
        dol.byte(0x80561410)==0 && dol.byte(0x80561411)==1,"Original generated-list initialization constants changed.");
    const std::array<std::uint32_t,7> disk_setter{0x38a00004,0x38000000,0x90a30008,0x9083000c,0x98030010,0x90030014,0x4e800020};
    for(std::size_t i=0;i<disk_setter.size();++i)
        check(dol.word(0x803b10a0+static_cast<std::uint32_t>(i*4))==disk_setter[i],"Original disk initialization instructions changed.");
    check(dol.string(0x80455150)=="beNDMWItemDisk" && dol.string(0x80455218)=="beNDMWItemBase", "Original disk/base type names changed.");
    std::cout<<"Original generated game data: schema_records=77 schema_kinds=8 players=4 initialized_disks=48\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        defaults();first_named_lookup();resets_and_retained_owners();malformed_rows();
        if(args.size()==2)original(args[1]);else if(args.size()!=1)throw std::runtime_error("Usage: dw4_game_data_tests [EXTRACTED_ROOT]");
        std::cout<<"game_data: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"game_data after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
} // namespace
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);
}
