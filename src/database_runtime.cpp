#include "dw4/database_runtime.hpp"
#include "dw4/action_event_runtime.hpp"

#include <limits>
#include <map>
#include <utility>

namespace dw4 {
namespace {
constexpr std::uint32_t max_infos=4096,max_rams=4096,max_exports=65536;
[[noreturn]] void fail(const char* reason) {throw DatabaseError(reason);}
void checked_text(std::string_view value) {
    if(value.size()>4096 || value.find('\0')!=std::string_view::npos)
        fail("DATABASE source or name exceeds its bounded byte profile.");
}
struct Reader {
    const IgbDocument& doc;
    std::size_t links{},text_bytes{};
    const IgbObject& object(std::uint32_t ref,std::string_view type) const {
        const auto& value=doc.object(ref);
        if(doc.metadata.object_types.at(value.type_index).name!=type)
            fail("Unexpected DATABASE object type.");
        return value;
    }
    const IgbFieldValue& field(const IgbObject& value,std::uint16_t slot,std::string_view type,
        std::optional<std::size_t> width=4) const {
        const IgbFieldValue* found{};
        for(const auto& part:value.fields) if(part.slot==slot) {
            if(found) fail("DATABASE object repeats a field slot.");
            found=&part;
        }
        if(!found || doc.metadata.field_types.at(found->type_index).name!=type || (width && found->size!=*width))
            fail("Unexpected DATABASE field type or width.");
        doc.field_bytes(*found);return *found;
    }
    std::optional<std::uint32_t> ref(const IgbObject& value,std::uint16_t slot) const {
        field(value,slot,"igObjectRefMetaField");
        const auto result=doc.field_reference(value,slot);
        if(result) doc.object(*result);
        return result;
    }
    std::uint8_t byte(const IgbObject& value,std::uint16_t slot) const {
        return doc.field_bytes(field(value,slot,"igBoolMetaField",1))[0];
    }
    std::optional<std::string> name(const IgbObject& value) {
        const auto& part=field(value,2,"igStringMetaField",{});
        if(part.size>4104 || part.size>8u*1024u*1024u-text_bytes)
            fail("DATABASE aggregate name budget exceeded.");
        text_bytes+=part.size;
        const auto result=doc.field_string(value,2);
        checked_text(result);
        return part.size==4 ? std::optional<std::string>{} : std::string(result);
    }
    std::pair<DatabaseListIdentity,std::vector<std::uint32_t>> list(std::uint32_t ref,
        std::string_view type,std::uint32_t limit) {
        const auto& value=object(ref,type);
        field(value,2,"igIntMetaField");field(value,3,"igIntMetaField");field(value,4,"igMemoryRefMetaField");
        const auto count=doc.field_u32(value,2),capacity=doc.field_u32(value,3);
        if(count>capacity || capacity>limit || count>131072u-links)
            fail("DATABASE object list exceeds its bounded capacity.");
        links+=count;
        const auto memory=doc.field_reference(value,4);
        if((!memory && capacity) || (memory && doc.memory(*memory).size()!=std::size_t(capacity)*4))
            fail("DATABASE object list storage differs from declared capacity.");
        std::optional<std::uint32_t> memory_owner;
        if(memory) memory_owner=doc.memories.at(doc.references.at(*memory).value_index).reference_index;
        DatabaseListIdentity identity{ref,value.reference_index,memory,memory_owner,count,capacity};
        std::vector<std::uint32_t> result;result.reserve(count);
        for(const auto item:doc.object_list(ref)) {
            if(!item) fail("DATABASE object list contains a null row.");
            result.push_back(*item);
        }
        return {identity,std::move(result)};
    }
};
}
struct DatabaseManagerProgram::Data {
    std::string source;
    std::uint32_t occurrence{};
    std::vector<DatabaseInfoDefinition> infos;
};
const std::string& DatabaseManagerProgram::source() const noexcept {return data_->source;}
std::uint32_t DatabaseManagerProgram::source_occurrence() const noexcept {return data_->occurrence;}
const std::vector<DatabaseInfoDefinition>& DatabaseManagerProgram::infos() const noexcept {return data_->infos;}

DatabaseManagerProgram decode_database_managers(const IgbDocument& doc,std::string source,std::uint32_t occurrence) {
    try {
        checked_text(source);
        if(source.empty() || occurrence==std::numeric_limits<std::uint32_t>::max())
            fail("DATABASE requires an explicit source and valid loaded occurrence.");
        if(doc.metadata.header.byte_order!=IgbByteOrder::little)
            fail("Big-endian DATABASE manager serialization is unverified.");
        if(!doc.info_list_reference) fail("DATABASE requires an explicit exported info list.");
        Reader read{doc};
        auto data=std::make_shared<DatabaseManagerProgram::Data>();
        data->source=std::move(source);data->occurrence=occurrence;
        std::map<std::uint32_t,std::shared_ptr<const DatabaseDefinition>> databases;
        std::size_t ram_count{},database_bytes{};
        for(const auto reference:read.list(*doc.info_list_reference,"igInfoList",max_exports).second) {
            const auto& info=doc.object(reference);
            if(!doc.is_a(info,"igInfo")) fail("DATABASE export list contains a non-info object.");
            // 802F255C compares exact virtual type pointers, not inheritance.
            if(doc.metadata.object_types.at(info.type_index).name!="beDBManagerInfo") continue;
            if(data->infos.size()>=max_infos) fail("DATABASE info count exceeds its budget.");
            DatabaseInfoDefinition decoded;
            decoded.reference=reference;decoded.owner_reference=info.reference_index;
            decoded.name=read.name(info);decoded.active=read.byte(info,4);
            decoded.data_reference=read.ref(info,5);
            if(decoded.data_reference) read.object(*decoded.data_reference,"beBaseInfoDataList");
            const auto rams=read.ref(info,6);
            if(!rams) fail("DATABASE info requires its actual RAM list.");
            auto [identity,occurrences]=read.list(*rams,"beBaseInfoRamList",max_rams);
            if(occurrences.size()>max_rams-ram_count) fail("DATABASE aggregate RAM occurrence budget exceeded.");
            ram_count+=occurrences.size();decoded.ram_list=identity;
            for(const auto ram_ref:occurrences) {
                const auto& ram=read.object(ram_ref,"beBaseInfoRam");
                DatabaseRamDefinition entry;
                entry.reference=ram_ref;entry.owner_reference=ram.reference_index;
                read.field(ram,2,"igIntMetaField");entry.authored_index=doc.field_i32(ram,2);
                entry.active=read.byte(ram,5);
                for(std::uint16_t slot=6;slot<=9;++slot) {
                    read.field(ram,slot,"igIntMetaField");entry.authored_words[slot-6]=doc.field_i32(ram,slot);
                }
                entry.data_reference=read.ref(ram,10);
                if(entry.data_reference) read.object(*entry.data_reference,"beBaseInfoDataList");
                decoded.rams.push_back(std::move(entry));
            }
            decoded.database_reference=read.ref(info,7);
            if(decoded.database_reference) {
                const auto owner=read.object(*decoded.database_reference,"beDataBase").reference_index;
                auto found=databases.find(owner);
                if(found==databases.end()) {
                    auto database=std::make_shared<const DatabaseDefinition>(
                        decode_database(doc,*decoded.database_reference,data->source,occurrence));
                    const auto charge=[&](std::size_t bytes) {
                        if(bytes>128u*1024u*1024u-database_bytes)
                            fail("DATABASE aggregate decoded ownership exceeds its storage budget.");
                        database_bytes+=bytes;
                    };
                    const auto group=[&](const auto& values) {
                        charge(sizeof(values.values.front())*values.values.size());
                        charge(sizeof(DatabaseName)*values.columns.size());
                        for(const auto& column:values.columns) if(column.name) charge(column.name->size());
                    };
                    group(database->char_group());group(database->short_group());group(database->int_group());
                    group(database->float_group());group(database->bool_group());group(database->string_group());
                    for(const auto& cell:database->string_group().values) if(cell.name) charge(cell.name->size());
                    found=databases.emplace(owner,std::move(database)).first;
                }
                decoded.database=found->second;
            }
            data->infos.push_back(std::move(decoded));
        }
        return DatabaseManagerProgram(std::move(data));
    } catch(const DatabaseError&) {throw;}
      catch(const std::exception& error) {throw DatabaseError(error.what());}
}

struct DatabaseRuntime::Impl {
    ControllerObjectId manager=ControllerObjectId::allocate_native();
    std::vector<DatabaseManagerProgram> sources;
    std::map<ControllerObjectId,std::shared_ptr<DatabaseRamState>> rams;
    std::map<ControllerObjectId,ControllerObjectId> aliases;
    std::shared_ptr<DatabaseRamState> find(ControllerObjectId id) const {
        const auto alias=aliases.find(id);
        if(alias==aliases.end()) fail("DATABASE RAM does not belong to this manager.");
        return rams.at(alias->second);
    }
};
DatabaseRuntime::DatabaseRuntime(ActionEventRuntime& runtime):impl_(std::make_shared<Impl>()),runtime_(runtime) {}
DatabaseRuntime::~DatabaseRuntime()=default;
ControllerObjectId DatabaseRuntime::manager_id() const noexcept {return impl_->manager;}
const std::vector<DatabaseManagerProgram>& DatabaseRuntime::sources() const noexcept {return impl_->sources;}
const DatabaseRamState& DatabaseRuntime::ram(ControllerObjectId id) const {return *impl_->find(id);}
void DatabaseRuntime::set_active(ControllerObjectId id,std::uint8_t active) {impl_->find(id)->active=active;}

void DatabaseRuntime::register_source(DatabaseManagerProgram program) {
    for(const auto& existing:impl_->sources)
        if(existing.source_occurrence()==program.source_occurrence())
            fail("DATABASE source occurrence is already registered with this manager.");
    if(impl_->sources.size()>=4096) fail("DATABASE loaded source count exceeds its budget.");
    // All allocations and final shared-RAM writes are private until the atomic
    // provider/row registration succeeds. No executing provider is replaced.
    auto sources=impl_->sources;sources.push_back(program);
    std::map<ControllerObjectId,std::shared_ptr<DatabaseRamState>> rams;
    std::map<ControllerObjectId,ControllerObjectId> aliases;
    std::vector<MessengerRegistration> rows;
    for(const auto& info:program.infos()) {
        for(std::size_t ordinal=0;ordinal<info.rams.size();++ordinal) {
            const auto& authored=info.rams[ordinal];
            const ControllerObjectId id{program.source_occurrence(),authored.owner_reference};
            auto found=rams.find(id);
            if(found==rams.end()) found=rams.emplace(id,std::make_shared<DatabaseRamState>()).first;
            auto& ram=*found->second;
            ram={id,impl_->manager,{program.source_occurrence(),info.owner_reference},
                static_cast<std::int32_t>(ordinal),authored.active,{},info.database};
            aliases.emplace(ControllerObjectId{program.source_occurrence(),authored.reference},id);
            aliases.emplace(id,id);
            // 80305308 substitutes the empty literal for a null info name.
            rows.push_back({id,"DATABASE",info.name.value_or(""),static_cast<std::int32_t>(ordinal)});
        }
    }
    std::vector<PreparedControllerRegistration> providers;providers.reserve(rams.size());
    for(const auto& [id,ram]:rams) {
        providers.push_back({id,{
            [ram]()->std::optional<std::uint8_t> {return ram->active;},
            [ram] {
                return MessengerManager{
                    // Actual virtual+90 is 802F2238, an unconditional zero.
                    [](NativeMessenger&,const MessengerDelivery&) {return MessengerCallResult{true,0,{}};},
                    [ram](NativeMessenger& messenger,const MessengerDelivery& delivery,std::string_view command) {
                        if(delivery.recipient!=ram->id) fail("DATABASE delivery belongs to another RAM.");
                        // Actual virtual+8C 8031F890 resolves the captured text.
                        if(command!="GETDATABASE") return MessengerCallResult{true,0,{}};
                        if(ram->database) messenger.buffer().add_database(ram->database);
                        else messenger.buffer().add_object({});
                        return MessengerCallResult{true,1,{}};
                    }};
            }},program.source()});
    }
    runtime_.register_controllers(providers,rows);
    impl_->rams.merge(rams);impl_->aliases.merge(aliases);impl_->sources.swap(sources);
}
} // namespace dw4
