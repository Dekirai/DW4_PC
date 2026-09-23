#include "dw4/igb_field_member_copy.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
IgbObjectInstance object(std::uint32_t address,unsigned size=0x60,std::uint32_t offset=8,bool typed=true){
    IgbObjectInstance result;result.storage=std::make_shared<IgbObjectStorage>();result.storage->original_address=address;result.offset=offset;
    result.storage->bytes=std::make_shared<IgbScratchBuffer>();result.storage->bytes->bytes.assign(size,0xcc);result.storage->bytes->initialized.assign(size,1);
    if(typed){result.field=std::make_shared<IgbDecodeRuntimeField>();result.field->vtable_word=0x80473f24;
        result.field->physical_storage=IgbFieldStorageBinding{result.storage,offset};result.storage->bindings.push_back({offset,{},{},{},{},result.field});}
    return result;
}
std::uint32_t word(const IgbObjectInstance& object,std::uint32_t offset){return igb_bootstrap_read_word({object.storage,object.offset},offset);}
auto descriptor(std::uint32_t offset,unsigned size=4){auto f=std::make_shared<IgbDecodeRuntimeField>();f->offset=offset;f->size=static_cast<std::uint16_t>(size);return f;}
auto services(){auto s=std::make_shared<IgbFieldMemberCopyServices>();s->values=std::make_shared<IgbFieldValuesServices>();
    s->values->fields=std::make_shared<IgbFieldOperationsServices>();s->values->fields->dependencies=std::make_shared<IgbDependenciesServices>();
    s->objects=std::make_shared<IgbObjectFieldConstructServices>();s->objects->values=s->values;return s;}
auto string(std::uint32_t references,const char* value){auto s=std::make_shared<IgbInternedString>();s->references=references;s->value=std::make_shared<const std::string>(value);return s;}
void name(const IgbObjectInstance& object,const std::shared_ptr<IgbInternedString>& value,std::uint32_t address,bool physical){
    object.field->name_string=value;object.field->name_string_address=address;object.field->name=value?value->value:SystemStageString{};
    if(physical)igb_bootstrap_write_word({object.storage,object.offset},0xc,address);
}
void raw_canonical_and_padding(){
    for(const auto function:{0x800642e4u,0x80064318u,0x8006434cu}){
        auto source=object(0x100000),target=object(0x200000);source.field->offset=0x12345678;
        IgbFieldMemberCopyInvocation copy(descriptor(8),target,source,function);
        check(copy.resume().completed && word(target,8)==0x12345678 && target.field->offset==0x12345678,"Raw copy read stale physical field offset or lost canonical publication.");
        check(word(source,8)==0xcccccccc && target.storage->bytes->bytes[7]==0xcc && target.storage->bytes->bytes[20]==0xcc,"Raw copy rewrote source or padding.");
        source.field->size=0x1234;IgbFieldMemberCopyInvocation half(descriptor(0x14,2),target,source,function);
        check(half.resume().completed && target.field->size==0x1234 && word(target,0x14)==0x1234cccc,"Unsigned-short member copied constructor padding.");
        source.field->flag_27=std::uint8_t{2};IgbFieldMemberCopyInvocation byte(descriptor(0x27,1),target,source,function);
        check(byte.resume().completed && target.field->flag_27==2 && target.storage->bytes->bytes[target.offset+0x28]==0xcc,"Raw policy byte changed its neighboring member.");
    }
    auto source=object(0x100000),target=object(0x200000);auto cell=std::make_shared<IgbRuntimeFieldSlot>();
    source.field->global_slot=std::weak_ptr(cell);source.field->global_slot_address=0x805617ec;
    IgbFieldMemberCopyInvocation global(descriptor(0x1c),target,source,0x8006434c);
    check(global.resume().completed && word(target,0x1c)==0x805617ec && target.field->global_slot && target.field->global_slot->lock()==cell,"Raw global pointer lost its explicit cell binding.");
    IgbFieldMemberCopyInvocation self(descriptor(0x1c),source,source,0x8006434c);
    check(self.resume().completed && source.field->global_slot && source.field->global_slot->lock()==cell,"Self-copy invalidated its captured nonowning cell.");
    auto generic=object(0x300000,0x40,0,false);generic.storage->bytes->bytes[0x16]=0x7a;
    IgbFieldMemberCopyInvocation padding(descriptor(0x16,1),target,generic,0x800642e4);
    check(padding.resume().completed && target.storage->bytes->bytes[target.offset+0x16]==0x7a,"Generic source bytes were treated as canonical field storage.");
}
void raw_overlap_and_fault_prefix(){
    for(const bool backwards:{false,true}){
        auto source=object(0x100000,16,backwards?0u:2u,false),target=source;target.offset=backwards?2u:0u;
        for(unsigned i=0;i<16;++i)source.storage->bytes->bytes[i]=static_cast<std::uint8_t>(i);
        const auto before=source.storage->bytes->bytes;IgbFieldMemberCopyInvocation copy(descriptor(0,8),target,source,0x800642e4);
        check(copy.resume().completed,"Overlapping raw member copy did not complete.");
        for(unsigned i=0;i<8;++i)check(target.storage->bytes->bytes[target.offset+i]==before[source.offset+i],"Raw copy changed original address-directed overlap behavior.");
    }
    auto source=object(0x200000,8,0,false),target=object(0x100000,8,0,false);
    source.storage->bytes->bytes={1,2,3,4,5,6,7,8};source.storage->bytes->initialized[2]=0;
    IgbFieldMemberCopyInvocation fault(descriptor(0,4),target,source,0x80064318);
    rejects([&]{fault.resume();},"Unavailable source byte did not fault.");
    check(fault.faulted() && target.storage->bytes->bytes==std::vector<std::uint8_t>({1,2,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}),"Raw copy validated the whole source before its original write prefix.");
    IgbFieldMemberCopyInvocation zero(descriptor(0xffffffff,0),{}, {},0x8006434c);
    check(zero.resume().completed,"Zero-byte copy dereferenced a null source or destination.");
    auto wrapped_source=object(0xfffffffc,12,0,false),wrapped_target=object(0x100000,12,0,false);
    wrapped_source.storage->bytes->bytes[8]=0x5a;
    IgbFieldMemberCopyInvocation wrapped(descriptor(8,1),wrapped_target,wrapped_source,0x800642e4);
    check(wrapped.resume().completed && wrapped_target.storage->bytes->bytes[8]==0x5a,"Wrapped guest arithmetic replaced bounded host offsets.");
    auto null_member=object(0xfffffff0,0x30,0,false);
    IgbFieldMemberCopyInvocation null_read(descriptor(0x10,1),target,null_member,0x800642e4);
    rejects([&]{null_read.resume();},"A member read whose arithmetic pointer wrapped to null was accepted.");
    auto invalid=object(0x300000);invalid.storage->bindings.clear();
    IgbFieldMemberCopyInvocation no_access(descriptor(0,0),invalid,invalid,0x800642e4);
    check(no_access.resume().completed,"Zero-byte copy validated an unreachable malformed typed binding.");
}
void raw_partial_canonical_and_aliases(){
    auto source=object(0x200000),target=object(0x100000);source.field->offset=0xaabbccdd;target.field->offset=0x11223344;
    IgbFieldMemberCopyInvocation byte(descriptor(8,1),target,source,0x800642e4);
    check(byte.resume().completed && target.field->offset==0xaa223344 && word(target,8)==0xaacccccc,"Partial raw store replaced logical neighboring bytes with stale physical bytes.");
    IgbFieldMemberCopyInvocation waiting(descriptor(8,8),target,source,0x800642e4);
    check(!waiting.resume().completed && target.field->offset==0xaabbccdd && word(target,8)==0xaabbccdd,
        "Pending raw source pointer hid its already completed physical/canonical prefix.");
    source.field->name_string=std::shared_ptr<IgbInternedString>{};source.field->name_string_address=0;
    check(waiting.resume().completed && word(target,0xc)==0 && target.field->name_string && !*target.field->name_string,"Pending raw member did not resume its original byte position.");
    auto alias_source=object(0x300000),alias_target=alias_source;alias_target.offset=10;alias_target.field=std::make_shared<IgbDecodeRuntimeField>();
    alias_target.field->physical_storage=IgbFieldStorageBinding{alias_target.storage,10};alias_target.storage->bindings.push_back({10,{},{},{},{},alias_target.field});
    alias_source.field->offset=0x11223344;alias_target.field->offset=0x33445566;
    IgbFieldMemberCopyInvocation overlap(descriptor(8,4),alias_target,alias_source,0x8006434c);
    check(overlap.resume().completed && alias_target.field->offset==0x11223344 && alias_source.field->offset==0x11221122,
        "Overlapping typed views did not observe each original byte store.");
    check(word(alias_target,8)==0x11223344,"Typed alias synchronization changed physical byte order.");
    auto wrapped_source=object(0x100000),wrapped_target=object(0x200000);wrapped_target.field->vtable_word=0xaabbccdd;
    IgbFieldMemberCopyInvocation wrapped(descriptor(0xfffffffe,4),wrapped_target,wrapped_source,0x800642e4);
    rejects([&]{wrapped.resume();},"Wrapped raw copy skipped the later unavailable source byte.");
    check(wrapped.faulted() && word(wrapped_target,0)==0x8047cccc && wrapped_target.field->vtable_word==0x8047ccdd,
        "Wrapped partial copy failed to synchronize its two completed header bytes without changing logical neighbors.");
    check(wrapped_target.storage->bytes->bytes[wrapped_target.offset+4]==0xcc && wrapped_source.field->vtable_word==0x80473f24,
        "Wrapped fault prefix changed the next header word or source identity.");
}
void string_capture_and_pending(){
    for(const auto function:{0x80071b58u,0x80071bc4u}){
        auto s=services();auto source=object(0x100000),target=object(0x200000);auto incoming=string(7,"source"),old=string(1,"old"),replacement=string(11,"new");
        name(source,incoming,0x300008,false);name(target,old,0x400008,true);
        igb_bind_field_string(s->values,0x300008,incoming);igb_bind_field_string(s->values,0x400008,old);igb_bind_field_string(s->values,0x500008,replacement);
        bool ready{};unsigned selections{},polls{};
        s->values->fields->dependencies->factory=[&](const IgbDependenciesCall& c){++selections;check(c.kind==IgbDependenciesServiceKind::release_string && c.string==old,"String wrapper interned data or released the wrong header.");
            auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"prepared release"};};return op;};
        auto field=descriptor(0xc);IgbFieldMemberCopyInvocation copy(field,target,source,function);
        check(!copy.resume(s).completed && old->references==0 && incoming->references==9 && word(target,0xc)==0x400008,"Pending string assignment lost its outer retain or published too early.");
        name(source,replacement,0x500008,false);field->offset=0x40;ready=true;s->values->fields->dependencies->factory={};
        check(copy.resume(s).completed && selections==1 && polls==2 && incoming->references==8 && replacement->references==11,"String wrapper replayed captures or selected a replacement callback.");
        check(word(target,0xc)==0x300008 && target.field->name_string==incoming && word(target,0x40)==0xcccccccc,"String wrapper changed the captured source or destination offset.");
        check(copy.resume(s).completed && polls==2,"Completed string member copied again.");
    }
    auto s=services();auto source=object(0x100000),target=object(0x200000);auto incoming=string(3,"late"),other=string(9,"changed");
    name(source,incoming,0,false);source.field->name_string_address.reset();name(target,{},0,true);
    IgbFieldMemberCopyInvocation late(descriptor(0xc),target,source,0x80071b58);
    check(!late.resume(s).completed && incoming->references==3,"Unknown string arithmetic identity invented a pointer or retain.");
    source.field->name_string=other;igb_bind_field_string(s->values,0x300008,incoming);
    check(late.resume(s).completed && word(target,0xc)==0x300008 && incoming->references==4 && other->references==9,"Late address binding recaptured the source header.");
    auto alias=object(0x300000);name(alias,incoming,0x300008,true);
    IgbFieldMemberCopyInvocation self(descriptor(0xc),alias,alias,0x80071b58);
    check(self.resume(s).completed && incoming->references==4,"String self-copy changed its total reference count.");
}
void object_assignment_and_binding_guards(){
    auto s=services();auto source=object(0x100000),target=object(0x200000),metadata=object(0x300000);
    metadata.field.reset();metadata.metadata=std::make_shared<IgbRuntimeMetaType>();metadata.storage->bindings={{metadata.offset,{},metadata.metadata,{},{},{}}};
    source.field->declaring_type=std::weak_ptr(metadata.metadata);igb_bootstrap_write_word({target.storage,target.offset},0x18,0);
    auto field=descriptor(0x18);field->flag_3c=std::uint8_t{0};field->flag_35=std::uint8_t{0};
    IgbFieldMemberCopyInvocation copy(field,target,source,0x8006a81c);
    check(!copy.resume(s).completed && word(target,0x18)==0,"Missing metadata arithmetic binding fabricated an incoming object.");
    igb_bind_object_field_instance(s->objects,metadata);
    check(copy.resume(s).completed && word(target,0x18)==0x300008 && target.field->declaring_type && target.field->declaring_type->lock()==metadata.metadata,"Object parent copy bypassed actual typed assignment.");
    auto waiting=object(0x500000);name(waiting,string(2,"missing"),0x600008,false);name(target,{},0,true);
    IgbFieldMemberCopyInvocation guard(descriptor(0xc),target,waiting,0x80071b58);
    check(!guard.resume(s).completed,"Missing string header did not suspend.");
    waiting.field->physical_storage.reset();
    rejects([&]{guard.resume(s);},"Pending member copy accepted a removed physical field binding.");
    check(guard.faulted() && word(target,0xc)==0,"Binding fault executed later member stores.");
}
void explicit_boundaries_and_retention(){
    auto s=services();auto source=object(0x100000),target=object(0x200000);unsigned selections{},polls{};bool ready{};
    IgbFieldMemberCopyInvocation call(descriptor(0x20),target,source,0x80062824);
    check(!call.resume(s).completed,"Unrecovered memory copy was treated as a successful raw pointer copy.");
    s->factory=[&](const IgbFieldMemberCopyCall& c){++selections;check(c.function==0x80062824 && c.source.field==source.field && c.destination.field==target.field,"Explicit member boundary lost captured typed arguments.");
        auto op=std::make_shared<IgbFieldMemberCopyOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"prepared member"};};return op;};
    check(!call.resume(s).completed && selections==1,"Late member implementation was not selected.");
    s->factory={};ready=true;check(call.resume(s).completed && polls==2 && selections==1,"Pending member boundary did not retain its selected continuation.");
    {IgbFieldMemberCopyInvocation abandoned(descriptor(0x20),target,source,0x80062824);check(!abandoned.resume(s).completed,"Abandoned memory boundary completed.");}
    check(std::find(s->unfinished.begin(),s->unfinished.end(),source.storage)!=s->unfinished.end() &&
        std::find(s->unfinished.begin(),s->unfinished.end(),target.storage)!=s->unfinished.end(),"Abandoning a partial copy discarded its captured allocations.");
}
}
int main(){try{raw_canonical_and_padding();raw_overlap_and_fault_prefix();raw_partial_canonical_and_aliases();string_capture_and_pending();object_assignment_and_binding_guards();explicit_boundaries_and_retention();
    std::cout<<"igb_field_member_copy_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Field member copy failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
