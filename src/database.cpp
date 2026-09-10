#include "dw4/database.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

namespace dw4 {
namespace {
constexpr std::uint32_t max_columns=4096,max_values=1048576,max_rows=1048576;
constexpr std::size_t max_elements=2097152,max_storage=16u*1024u*1024u,max_text=4u*1024u*1024u;
[[noreturn]] void fail(const char* reason) {throw DatabaseError(reason);}
void text_profile(std::string_view value) {
    if(value.size()>4096 || value.find('\0')!=std::string_view::npos)fail("Database text exceeds the checked string profile.");
}
struct Reader {
    const IgbDocument& doc;
    std::size_t elements{},storage{},text_bytes{};
    const IgbObject& exact(std::uint32_t reference,std::string_view type) const {
        const auto& result=doc.object(reference);
        const auto& canonical=doc.references.at(result.reference_index);
        if(canonical.kind!=IgbReferenceKind::object || canonical.value_index!=doc.references.at(reference).value_index)
            fail("Database object has an inconsistent canonical reference.");
        if(doc.metadata.object_types.at(result.type_index).name!=type)fail("Unexpected database graph object type.");
        return result;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,std::string_view type,
        std::optional<std::size_t> width=4) const {
        const IgbFieldValue* found{};
        for(const auto& value:object.fields)if(value.slot==slot) {
            if(found)fail("A database object repeats a field slot.");
            found=&value;
        }
        if(!found || doc.metadata.field_types.at(found->type_index).name!=type || (width && found->size!=*width))
            fail("Unexpected database field type or width.");
        doc.field_bytes(*found);return *found;
    }
    std::uint32_t ref(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igObjectRefMetaField");const auto value=doc.field_reference(object,slot);
        if(!value)fail("A required database list reference is null.");return *value;
    }
    DatabaseListIdentity list(std::uint32_t reference,std::string_view type,std::uint32_t width,std::uint32_t limit) {
        const auto& object=exact(reference,type);
        if(object.fields.size()!=3)fail("Database list has unsupported additional fields.");
        field(object,2,"igIntMetaField");field(object,3,"igIntMetaField");field(object,4,"igMemoryRefMetaField");
        const auto count=doc.field_u32(object,2),capacity=doc.field_u32(object,3);
        if(count>capacity || capacity>limit || count>max_elements-elements)fail("Database list allocation budget exceeded.");
        elements+=count;
        const auto memory=doc.field_reference(object,4);std::optional<std::uint32_t> memory_owner;
        if(!memory) {
            if(capacity!=0)fail("An allocated database list has no backing memory.");
        } else {
            const auto bytes=doc.memory(*memory);
            if(bytes.size()!=std::size_t(capacity)*width || bytes.size()>max_storage-storage)
                fail("Database list storage differs from its width/capacity or exceeds the aggregate budget.");
            storage+=bytes.size();
            memory_owner=doc.memories.at(doc.references.at(*memory).value_index).reference_index;
            const auto& canonical=doc.references.at(*memory_owner);
            if(canonical.kind!=IgbReferenceKind::memory || canonical.value_index!=doc.references.at(*memory).value_index)
                fail("Database memory has an inconsistent canonical reference.");
        }
        return {reference,object.reference_index,memory,memory_owner,count,capacity};
    }
    DatabaseName name(std::uint32_t reference) {
        const auto& object=exact(reference,"igNamedObject");
        if(object.fields.size()!=1)fail("Database named object has unsupported additional fields.");
        const auto& value=field(object,2,"igStringMetaField",{});
        if(value.size>4104 || value.size>max_text-text_bytes)fail("Database name allocation budget exceeded.");
        text_bytes+=value.size;
        const auto bytes=doc.field_string(object,2);text_profile(bytes);
        std::optional<std::string> result;
        if(value.size!=4)result=bytes;
        return {reference,object.reference_index,std::move(result)};
    }
    std::vector<DatabaseName> names(const DatabaseListIdentity& source) {
        std::vector<DatabaseName> result;result.reserve(source.count);
        for(const auto item:doc.object_list(source.reference)) {
            if(!item)fail("A database name or string-cell list contains a null object.");
            result.push_back(name(*item));
        }
        return result;
    }
    template<class T> std::vector<T> values(const DatabaseListIdentity& source) const {
        std::vector<T> result;result.reserve(source.count);
        if(!source.count)return result;
        const auto bytes=doc.memory(*source.memory_reference);
        for(std::size_t i=0;i<source.count;++i) {
            if constexpr(std::is_same_v<T,std::int8_t>)result.push_back(std::bit_cast<std::int8_t>(bytes[i]));
            else if constexpr(std::is_same_v<T,std::uint8_t>)result.push_back(bytes[i]);
            else if constexpr(std::is_same_v<T,std::int16_t>) {
                const auto word=std::uint16_t(std::uint16_t(bytes[i*2])|(std::uint16_t(bytes[i*2+1])<<8));
                result.push_back(std::bit_cast<std::int16_t>(word));
            } else {
                std::uint32_t word{};for(std::size_t j=0;j<4;++j)word|=std::uint32_t(bytes[i*4+j])<<(j*8);
                if constexpr(std::is_same_v<T,float>) {
                    const auto value=std::bit_cast<float>(word);
                    if(!std::isfinite(value))fail("A database float cell is nonfinite.");result.push_back(value);
                } else result.push_back(std::bit_cast<std::int32_t>(word));
            }
        }
        return result;
    }
    template<class T> DatabaseGroup<T> group(const IgbObject& database,std::uint16_t slot,std::string_view data_type) {
        auto columns_source=list(ref(database,slot),"igNamedObjectList",4,max_columns);
        constexpr auto width=std::is_same_v<T,DatabaseName>?4u:static_cast<unsigned>(sizeof(T));
        auto data_source=list(ref(database,static_cast<std::uint16_t>(slot+1)),data_type,width,max_values);
        if(columns_source.count==0) {
            if(data_source.count!=0)fail("A database group has cells without any columns.");
        } else if(data_source.count%columns_source.count!=0 || data_source.count/columns_source.count>max_rows)
            fail("A database group does not contain bounded complete rows.");
        auto columns=names(columns_source);
        if constexpr(std::is_same_v<T,DatabaseName>)
            return {columns_source,data_source,std::move(columns),names(data_source)};
        else return {columns_source,data_source,std::move(columns),values<T>(data_source)};
    }
};
template<class T> std::optional<std::size_t> column_index(const DatabaseGroup<T>& group,std::optional<std::string_view> name) {
    if(!name)return {};
    text_profile(*name);
    for(std::size_t i=0;i<group.columns.size();++i)
        if(group.columns[i].name && *group.columns[i].name==*name)return i;
    return {};
}
template<class T> const T& cell(const DatabaseGroup<T>& group,std::int32_t row,std::size_t column) {
    if(row<0 || static_cast<std::size_t>(row)>=group.values.size()/group.columns.size())
        fail("The found database column has no complete row at the requested index.");
    return group.values[static_cast<std::size_t>(row)*group.columns.size()+column];
}
template<class T> std::int32_t rows(const DatabaseGroup<T>& group) noexcept {
    return static_cast<std::int32_t>(group.values.size()/group.columns.size());
}
} // namespace

struct DatabaseDefinition::Data {
    std::string source;
    std::size_t occurrence;
    std::uint32_t reference,owner_reference;
    DatabaseGroup<std::int8_t> chars;
    DatabaseGroup<std::int16_t> shorts;
    DatabaseGroup<std::int32_t> ints;
    DatabaseGroup<float> floats;
    DatabaseGroup<std::uint8_t> bools;
    DatabaseGroup<DatabaseName> strings;
};
const std::string& DatabaseDefinition::source() const noexcept {return data_->source;}
std::size_t DatabaseDefinition::source_occurrence() const noexcept {return data_->occurrence;}
std::uint32_t DatabaseDefinition::reference() const noexcept {return data_->reference;}
std::uint32_t DatabaseDefinition::owner_reference() const noexcept {return data_->owner_reference;}
const DatabaseGroup<std::int8_t>& DatabaseDefinition::char_group() const noexcept {return data_->chars;}
const DatabaseGroup<std::int16_t>& DatabaseDefinition::short_group() const noexcept {return data_->shorts;}
const DatabaseGroup<std::int32_t>& DatabaseDefinition::int_group() const noexcept {return data_->ints;}
const DatabaseGroup<float>& DatabaseDefinition::float_group() const noexcept {return data_->floats;}
const DatabaseGroup<std::uint8_t>& DatabaseDefinition::bool_group() const noexcept {return data_->bools;}
const DatabaseGroup<DatabaseName>& DatabaseDefinition::string_group() const noexcept {return data_->strings;}

std::int32_t DatabaseDefinition::integer(std::int32_t row,std::optional<std::string_view> name) const {
    if(const auto index=column_index(data_->chars,name))return cell(data_->chars,row,*index);
    if(const auto index=column_index(data_->shorts,name))return cell(data_->shorts,row,*index);
    if(const auto index=column_index(data_->ints,name))return cell(data_->ints,row,*index);
    return 0;
}
std::uint8_t DatabaseDefinition::boolean(std::int32_t row,std::optional<std::string_view> name) const {
    const auto index=column_index(data_->bools,name);
    if(!index)fail("The database bool column is absent.");return cell(data_->bools,row,*index);
}
std::string_view DatabaseDefinition::string(std::int32_t row,std::optional<std::string_view> name) const {
    const auto index=column_index(data_->strings,name);
    if(!index)fail("The database string column is absent.");
    const auto& value=cell(data_->strings,row,*index).name;return value?std::string_view(*value):std::string_view{};
}
std::int32_t DatabaseDefinition::row_count() const noexcept {
    if(!data_->strings.columns.empty())return rows(data_->strings);
    if(!data_->chars.columns.empty())return rows(data_->chars);
    if(!data_->shorts.columns.empty())return rows(data_->shorts);
    if(!data_->ints.columns.empty())return rows(data_->ints);
    if(!data_->floats.columns.empty())return rows(data_->floats);
    if(!data_->bools.columns.empty())return rows(data_->bools);
    return 0;
}
DatabaseDefinition decode_database(const IgbDocument& doc,std::uint32_t reference,std::string source,std::size_t occurrence) {
    try {
        text_profile(source);
        if(source.empty() || occurrence==std::numeric_limits<std::size_t>::max())fail("Database requires an explicit source and loaded occurrence.");
        if(doc.metadata.header.byte_order!=IgbByteOrder::little)fail("Big-endian database graphs are unverified.");
        Reader read{doc};const auto& object=read.exact(reference,"beDataBase");
        if(object.fields.size()!=12)fail("Database object has unsupported additional fields.");
        auto chars=read.group<std::int8_t>(object,2,"igCharList");
        auto shorts=read.group<std::int16_t>(object,4,"igShortList");
        auto ints=read.group<std::int32_t>(object,6,"igIntList");
        auto floats=read.group<float>(object,8,"igFloatList");
        auto bools=read.group<std::uint8_t>(object,10,"igBoolList");
        auto strings=read.group<DatabaseName>(object,12,"igNamedObjectList");
        return DatabaseDefinition(std::make_shared<const DatabaseDefinition::Data>(DatabaseDefinition::Data{
            std::move(source),occurrence,reference,object.reference_index,std::move(chars),std::move(shorts),std::move(ints),
            std::move(floats),std::move(bools),std::move(strings)}));
    } catch(const DatabaseError&) {throw;}
      catch(const std::exception& error) {throw DatabaseError(error.what());}
}
} // namespace dw4
