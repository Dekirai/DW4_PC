#include "dw4/igb_buffer_copy.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using K=IgbBufferServiceKind;using F=IgbBufferFunction;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class T>void rejects(T action,const char* why){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
std::shared_ptr<IgbScratchBuffer> bytes(std::size_t size,std::uint8_t value=0xcc){
    auto result=std::make_shared<IgbScratchBuffer>();result->bytes.assign(size,value);result->initialized.assign(size,1);return result;
}
// Explicit590A0 and pool virtual leaves; all helper control flow and bytes are
// the real native caller. Pools may differ between the three owner lookups.
struct Fixture {
    std::shared_ptr<IgbBufferServices> services=std::make_shared<IgbBufferServices>();
    IgbRuntimeFieldDefaultSource source{bytes(20),4},destination{bytes(24),6};
    std::uint32_t source_address=0x100004,destination_address=0x200006,first_size=3,second_size=2;
    std::vector<std::shared_ptr<ModelSceneObject>> pools{std::make_shared<ModelSceneData>(),std::make_shared<ModelSceneData>(),std::make_shared<ModelSceneData>()};
    std::vector<IgbBufferCall> calls;unsigned owners{},sizes{};
    std::function<MessengerCallResult(const IgbBufferCall&,IgbBufferOperation&,MessengerCallResult)> action;
    std::function<void(const IgbBufferCall&,std::shared_ptr<IgbBufferOperation>&)> select;
    Fixture(){
        services->values=std::make_shared<IgbFieldValuesServices>();
        for(std::size_t n=0;n<source.buffer->bytes.size();++n)source.buffer->bytes[n]=static_cast<std::uint8_t>(0x40+n);
        services->factory=[this](const IgbBufferCall& c){calls.push_back(c);auto op=std::make_shared<IgbBufferOperation>();
            const auto owner_index=c.kind==K::allocation_pool?owners++:0;
            const auto size_index=c.kind==K::pool_virtual && c.virtual_slot==0x150?sizes++:0;
            op->resume=[this,c,owner_index,size_index,out=op.get()]{MessengerCallResult result{true,0,{}};
                if(c.kind==K::allocation_pool)out->pool_result=pools.at(owner_index%pools.size());
                else if(c.virtual_slot==0x150)result.value=size_index==0?first_size:second_size;
                else if(c.virtual_slot==0xd8){out->buffer_result=destination;out->buffer_address=destination_address;}
                else if(c.virtual_slot!=0x108)throw std::runtime_error("Unexpected prepared pool virtual");
                if(action)return action(c,*out,result);return result;
            };if(select)select(c,op);return op;
        };
    }
};
void exact_copy_order_and_live_bytes(){
    Fixture f;const auto captured=f.source;const auto expected_pool=f.pools[0];
    f.action=[&](const auto& c,auto&,auto status){
        if(c.kind==K::pool_virtual && c.virtual_slot==0xd8){
            check(c.pool==expected_pool && c.bytes==3 && !c.buffer.buffer && c.address==0,"Clone allocation lost its first owner/size or passed an invented source argument.");
            captured.buffer->bytes[4]=0x91;captured.buffer->bytes[5]=0x92;
            f.source={bytes(20,0xee),0};
        }
        return status;
    };
    IgbBufferCopyInvocation copy(F::copy,captured,f.source_address);
    rejects([&]{copy.buffer_result();},"Incomplete buffer clone exposed a result.");
    check(copy.resume(f.services).completed && copy.completed() && !copy.faulted(),"Exact buffer helper chain did not finish.");
    check(f.calls.size()==6 && f.owners==3 && f.sizes==2,"Buffer clone skipped an independent owner or size query.");
    check(f.calls[0].kind==K::allocation_pool && f.calls[1].kind==K::allocation_pool &&
        f.calls[2].virtual_slot==0x150 && f.calls[2].pool==f.pools[1] && f.calls[3].virtual_slot==0xd8 &&
        f.calls[3].pool==expected_pool && f.calls[4].kind==K::allocation_pool &&
        f.calls[5].virtual_slot==0x150 && f.calls[5].pool==f.pools[2],"5647C original callback order changed.");
    for(const auto& c:f.calls)if(c.kind==K::allocation_pool || c.virtual_slot==0x150 || c.virtual_slot==0x108)
        check(c.buffer.buffer==captured.buffer && c.buffer.offset==4 && c.address==f.source_address,"Helper recaptured the caller's source buffer.");
    const auto out=copy.buffer_result();check(out.buffer==f.destination.buffer && out.offset==6 && copy.buffer_address()==f.destination_address,
        "Copied buffer lost its independent physical offset or arithmetic address.");
    check(out.buffer->bytes[6]==0x91 && out.buffer->bytes[7]==0x92 && out.buffer->bytes[8]==0xcc && out.buffer->bytes[5]==0xcc,
        "Copy used the first size, snapshotted before allocation, or overwrote padding.");
    check(copy.resume().completed && f.calls.size()==6,"Completed helper replayed its allocator or size queries.");
}
void size_and_free_callers(){
    for(const auto function:{F::size,F::free}){
        Fixture f;f.first_size=0xabcdef01;bool freed{};
        f.action=[&](const auto& c,auto&,auto status){if(c.kind==K::pool_virtual && c.virtual_slot==0x108){freed=true;c.buffer.buffer->released=true;status.value=0x13579;}return status;};
        IgbBufferCopyInvocation call(function,f.source,f.source_address);const auto status=call.resume(f.services);
        check(status.completed && f.calls.size()==2 && f.calls[0].kind==K::allocation_pool &&
            f.calls[1].pool==f.pools[0] && f.calls[1].virtual_slot==(function==F::size?0x150u:0x108u),
            "Size/free helper changed owner lookup or selected virtual.");
        check(function==F::size?status.value==0xabcdef01:freed && status.value==0x13579,"Size result was truncated or free virtual return was lost.");
        check(f.source.buffer->released==(function==F::free),"Helper invented a release outside the free endpoint.");
        check(call.resume().completed && f.calls.size()==2,"Completed size/free helper replayed its pool callback.");
    }
    // These callers only pass the source pointer to explicit owner/virtual
    // endpoints. Its represented content is not dereferenced by the wrapper.
    for(const auto function:{F::size,F::free}){
        Fixture f;IgbBufferCopyInvocation call(function,{},0);check(call.resume(f.services).completed && f.calls.size()==2 &&
            f.calls[0].address==0 && f.calls[1].address==0,"Size/free helper invented a buffer-null guard before its explicit endpoints.");
    }
}
void null_pools_and_results(){
    for(const auto function:{F::size,F::free}){
        Fixture f;f.pools[0].reset();IgbBufferCopyInvocation call(function,f.source,f.source_address);
        rejects([&]{call.resume(f.services);},"Null owner was accepted by a reached pool virtual.");
        check(call.faulted() && f.calls.size()==1,"Null pool failure selected a virtual endpoint or lost failure state.");
    }
    for(unsigned owner=0;owner<3;++owner){
        Fixture f;f.pools[owner].reset();IgbBufferCopyInvocation call(F::copy,f.source,f.source_address);
        rejects([&]{call.resume(f.services);},"Copy ignored a null independently returned pool.");
        const auto expected=owner==0?3u:owner==1?2u:5u;
        check(call.faulted() && f.calls.size()==expected,"Null copy pool fault occurred at a different original dereference.");
    }
    for(const auto size:{0u,1u}){
        Fixture f;f.destination={};f.destination_address=0;f.second_size=size;
        IgbBufferCopyInvocation call(F::copy,f.source,f.source_address);
        if(size==0)check(call.resume(f.services).completed && !call.buffer_result().buffer && call.buffer_address()==0,
            "Null allocation with zero second size was dereferenced.");
        else{rejects([&]{call.resume(f.services);},"Null allocation with positive second size skipped byte copy.");check(call.faulted(),"Null destination failure was not retained.");}
        check(f.calls.size()==6,"Null allocation skipped the original second size query.");
    }
    Fixture empty;empty.source={};empty.source_address=0;empty.second_size=0;empty.first_size=0;
    IgbBufferCopyInvocation no_access(F::copy,{},0);check(no_access.resume(empty.services).completed && empty.calls.size()==6,
        "Zero-byte helper performed unnecessary source access or skipped endpoint calls.");
}
void retained_pending_and_teardown(){
    Fixture f;bool ready{};unsigned polls{},allocations{};const auto old_source=f.source;
    f.select=[&](const auto& c,auto& op){if(c.kind==K::pool_virtual && c.virtual_slot==0xd8){++allocations;
        op->buffer_result=f.destination;op->buffer_address=f.destination_address;
        op->resume=[&]{++polls;return MessengerCallResult{ready,0,"Prepared allocation"};};}};
    IgbBufferCopyInvocation copy(F::copy,f.source,f.source_address);
    check(!copy.resume(f.services).completed && polls==1 && allocations==1 && f.calls.size()==4,"Copy did not retain its pending allocation.");
    f.first_size=19;f.source={bytes(20,0x11),0};old_source.buffer->bytes[4]=0x87;old_source.buffer->bytes[5]=0x88;ready=true;
    const auto factory=f.services->factory;f.services->factory={};
    check(!copy.resume(f.services).completed && polls==2 && allocations==1,"Pending allocation was replaced or missing second owner was completed.");
    f.services->factory=factory;check(copy.resume(f.services).completed && polls==2 && allocations==1 && f.calls[3].bytes==3 &&
        copy.buffer_result().buffer->bytes[6]==0x87 && copy.buffer_result().buffer->bytes[7]==0x88,
        "Resuming copy reselected allocation or lost captured source and live bytes.");
    Fixture teardown;bool destroyed{};teardown.select=[&](const auto& c,auto& op){
        if(c.kind==K::pool_virtual && c.virtual_slot==0x150 && teardown.sizes==2){
            auto token=std::shared_ptr<int>(new int(0),[&](int* value){delete value;destroyed=true;teardown.source.buffer->bytes[4]=0xa7;});
            op->resume=[token]{return MessengerCallResult{true,1,{}};};
        }};
    IgbBufferCopyInvocation live(F::copy,teardown.source,teardown.source_address);
    check(live.resume(teardown.services).completed && destroyed && live.buffer_result().buffer->bytes[6]==0xa7,
        "Byte copy read source storage before the second size callback teardown.");
    Fixture free;bool free_ready{};unsigned free_polls{};const auto old=free.source;
    free.select=[&](const auto& c,auto& op){if(c.kind==K::pool_virtual && c.virtual_slot==0x108)
        op->resume=[&,c]{++free_polls;if(free_ready)c.buffer.buffer->released=true;return MessengerCallResult{free_ready,0,"Prepared free"};};};
    IgbBufferCopyInvocation releasing(F::free,old,free.source_address);check(!releasing.resume(free.services).completed,"Free did not suspend.");
    free.source={bytes(20,0x33),0};free.services->factory={};free_ready=true;
    check(releasing.resume(free.services).completed && old.buffer->released && !free.source.buffer->released && free_polls==2 && free.calls.size()==2,
        "Pending free changed its captured allocation or reselected a callback.");
}
void late_bindings_and_live_results(){
    Fixture f;const auto source=f.source,destination=f.destination;f.source={};f.destination={};
    IgbBufferCopyInvocation copy(F::copy,{},f.source_address);
    check(!copy.resume(f.services).completed && f.calls.size()==6 && !copy.faulted(),"Unknown host buffer mappings did not suspend at the reached byte copy.");
    f.services->values->buffers.push_back({source.buffer,f.source_address-source.offset});
    f.services->values->buffers.push_back({destination.buffer,f.destination_address-destination.offset});
    source.buffer->bytes[source.offset]=0x55;source.buffer->bytes[source.offset+1]=0x66;
    check(copy.resume(f.services).completed && copy.buffer_result().buffer==destination.buffer && copy.buffer_result().offset==destination.offset &&
        destination.buffer->bytes[destination.offset]==0x55 && destination.buffer->bytes[destination.offset+1]==0x66 && f.calls.size()==6,
        "Late buffer mappings restarted callbacks or snapshotted source bytes before access.");
    Fixture destination_only;const auto output=destination_only.destination;destination_only.destination={};
    IgbBufferCopyInvocation retained_byte(F::copy,destination_only.source,destination_only.source_address);
    check(!retained_byte.resume(destination_only.services).completed && destination_only.calls.size()==6,
        "Absent destination mapping did not suspend at the first original byte store.");
    destination_only.source.buffer->bytes[4]=0x99;destination_only.source.buffer->bytes[5]=0x88;
    destination_only.services->values->buffers.push_back({output.buffer,destination_only.destination_address-output.offset});
    check(retained_byte.resume(destination_only.services).completed && output.buffer->bytes[7]==0x45 && output.buffer->bytes[6]==0x99,
        "A pending destination mapping lost the already read source byte or snapshotted later bytes.");
    Fixture partial;partial.services->factory={};IgbBufferCopyInvocation owner(F::size,partial.source,partial.source_address);
    check(!owner.resume(partial.services).completed && !owner.faulted(),"Missing owner service invented a size.");
    Fixture adjacent;adjacent.source.offset=0;const auto input=adjacent.source,output2=adjacent.destination;
    adjacent.source={};adjacent.destination={};
    adjacent.services->values->buffers.push_back({bytes(4),adjacent.source_address-4});
    adjacent.services->values->buffers.push_back({input.buffer,adjacent.source_address-input.offset});
    adjacent.services->values->buffers.push_back({output2.buffer,adjacent.destination_address-output2.offset});
    IgbBufferCopyInvocation exact_start(F::copy,{},adjacent.source_address);
    check(exact_start.resume(adjacent.services).completed && output2.buffer->bytes[output2.offset]==input.buffer->bytes[input.offset],
        "An adjacent allocation's one-past pointer concealed the actual source allocation.");
    Fixture conflict;conflict.services->values->buffers.push_back({bytes(8),conflict.source_address});
    conflict.services->values->buffers.push_back({bytes(8),conflict.source_address});
    IgbBufferCopyInvocation ambiguous(F::copy,{},conflict.source_address);
    rejects([&]{ambiguous.resume(conflict.services);},"Conflicting arithmetic source mappings silently selected a host buffer.");
    check(conflict.calls.size()==6 && ambiguous.faulted(),"Mapping conflict changed the original completed helper prefix.");
}
void unequal_sizes_and_fault_prefixes(){
    for(bool backward:{false,true}){
        Fixture f;f.source={bytes(4),0};f.destination={bytes(4),0};f.first_size=4;f.second_size=4;
        f.source_address=backward?0x10000u:0x20000u;f.destination_address=backward?0x20000u:0x10000u;
        f.source.buffer->bytes={1,2,3,4};f.source.buffer->initialized[backward?1:2]=0;
        IgbBufferCopyInvocation copy(F::copy,f.source,f.source_address);rejects([&]{copy.resume(f.services);},"Byte copy read an uninitialized source byte.");
        const std::vector<std::uint8_t> expected=backward?std::vector<std::uint8_t>{0xcc,0xcc,3,4}:std::vector<std::uint8_t>{1,2,0xcc,0xcc};
        check(f.destination.buffer->bytes==expected && copy.faulted() && f.calls.size()==6,"Source failure changed the original directional write prefix.");
        rejects([&]{copy.resume(f.services);},"Faulted byte copy was resumed.");rejects([&]{copy.buffer_result();},"Faulted byte copy exposed a completed result.");
    }
    Fixture small;small.source={bytes(5,0x71),0};small.destination={bytes(2),0};small.source_address=0x20000;small.destination_address=0x10000;
    small.first_size=2;small.second_size=5;IgbBufferCopyInvocation too_small(F::copy,small.source,small.source_address);
    rejects([&]{too_small.resume(small.services);},"Larger second size was truncated to allocation size.");
    check(small.calls[3].bytes==2 && small.destination.buffer->bytes==std::vector<std::uint8_t>(2,0x71),
        "Unequal sizes lost the original allocator argument or completed store prefix.");
    Fixture huge;huge.source={bytes(1,0x6b),0};huge.destination={bytes(1),0};huge.source_address=0x20000;huge.destination_address=0x10000;
    huge.first_size=1;huge.second_size=0xffffffff;IgbBufferCopyInvocation bounded(F::copy,huge.source,huge.source_address);
    rejects([&]{bounded.resume(huge.services);},"Huge original size escaped physical access bounds.");
    check(huge.destination.buffer->bytes[0]==0x6b && huge.calls.size()==6,"A blanket size check erased the first valid original byte store.");
    Fixture released;released.action=[&](const auto& c,auto&,auto status){if(c.kind==K::pool_virtual && c.virtual_slot==0xd8)released.source.buffer->released=true;return status;};
    IgbBufferCopyInvocation invalid(F::copy,released.source,released.source_address);
    rejects([&]{invalid.resume(released.services);},"Released source storage was copied from an early snapshot.");
    check(released.calls.size()==6 && released.destination.buffer->bytes==std::vector<std::uint8_t>(24,0xcc),
        "Released source fault bypassed the second size query or wrote destination bytes.");
}
void overlap_offsets_and_wrapping(){
    for(bool backward:{false,true}){
        Fixture f;auto shared=bytes(10);shared->bytes={0,1,2,3,4,5,6,7,8,9};
        f.source={shared,backward?1u:3u};f.destination={shared,backward?3u:1u};
        f.source_address=0x10000+f.source.offset;f.destination_address=0x10000+f.destination.offset;f.first_size=6;f.second_size=6;
        IgbBufferCopyInvocation copy(F::copy,f.source,f.source_address);check(copy.resume(f.services).completed,"Overlapping helper copy failed.");
        const std::vector<std::uint8_t> expected=backward?std::vector<std::uint8_t>{0,1,2,1,2,3,4,5,6,9}:std::vector<std::uint8_t>{0,3,4,5,6,7,8,7,8,9};
        check(shared->bytes==expected,"Helper copy direction or nonzero physical offsets changed overlap behavior.");
    }
    Fixture self;self.destination=self.source;self.destination_address=self.source_address;self.first_size=4;self.second_size=4;
    const auto before=self.source.buffer->bytes;IgbBufferCopyInvocation same(F::copy,self.source,self.source_address);
    check(same.resume(self.services).completed && self.source.buffer->bytes==before && self.calls.size()==6,
        "Self-copy bypassed original allocator/size callbacks or changed source bytes.");
    Fixture wrap;wrap.source={bytes(16,0x2a),0};wrap.destination={bytes(16),0};wrap.source_address=0xfffffff8;wrap.destination_address=0xfffffff9;
    wrap.first_size=16;wrap.second_size=16;IgbBufferCopyInvocation null_byte(F::copy,wrap.source,wrap.source_address);
    rejects([&]{null_byte.resume(wrap.services);},"Wrapped effective null source byte was treated as host storage.");
    check(std::all_of(wrap.destination.buffer->bytes.begin(),wrap.destination.buffer->bytes.begin()+9,[](auto b){return b==0xcc;}) &&
        std::all_of(wrap.destination.buffer->bytes.begin()+9,wrap.destination.buffer->bytes.end(),[](auto b){return b==0x2a;}),
        "Wrapped directional arithmetic changed the prefix before the zero-address read.");
}
void failures_and_ownership(){
    for(unsigned stop=0;stop<6;++stop){Fixture f;unsigned index{};
        f.action=[&](const auto&,auto&,auto status){if(index++==stop)throw std::runtime_error("Prepared endpoint failure");return status;};
        IgbBufferCopyInvocation copy(F::copy,f.source,f.source_address);rejects([&]{copy.resume(f.services);},"Helper swallowed an endpoint failure.");
        check(copy.faulted() && f.calls.size()==stop+1,"Endpoint failure changed its captured callback prefix.");
        rejects([&]{copy.resume(f.services);},"Faulted helper replayed its endpoint.");
    }
    Fixture recursive;IgbBufferCopyInvocation call(F::copy,recursive.source,recursive.source_address);
    recursive.action=[&](const auto&,auto&,auto status){rejects([&]{call.resume(recursive.services);},"Recursive buffer helper entry succeeded.");return status;};
    rejects([&]{call.resume(recursive.services);},"Caught recursive fault did not invalidate outer helper.");check(call.faulted(),"Recursive failure was not retained.");
    Fixture teardown;bool teardown_rejected{};const auto source_owner=teardown.source.buffer;
    auto finishing=std::make_unique<IgbBufferCopyInvocation>(F::size,teardown.source,teardown.source_address);
    teardown.select=[&](const auto& c,auto& op){if(c.kind==K::pool_virtual){
        auto token=std::shared_ptr<int>(new int(0),[&](int* value){delete value;
            try{finishing->resume(teardown.services);}catch(const std::runtime_error&){teardown_rejected=true;}});
        op->resume=[token]{return MessengerCallResult{true,17,{}};};
    }};
    rejects([&]{finishing->resume(teardown.services);},"Recursive final callback teardown was treated as successful completion.");
    check(teardown_rejected && finishing->faulted(),"Final teardown did not preserve its recursive invalidation.");finishing.reset();
    check(std::find(teardown.services->unfinished.begin(),teardown.services->unfinished.end(),source_owner)!=teardown.services->unfinished.end(),
        "Fault during final callback teardown discarded captured buffer lifetime.");
    Fixture abandoned;const auto destination=abandoned.destination.buffer;
    abandoned.select=[&](const auto& c,auto& op){if(c.kind==K::allocation_pool && abandoned.owners==3)
        op->resume=[] {return MessengerCallResult{false,0,"Prepared second size owner"};};};
    {IgbBufferCopyInvocation pending(F::copy,abandoned.source,abandoned.source_address);
        check(!pending.resume(abandoned.services).completed && abandoned.calls.size()==5,"Abandonment did not reach an allocated pending copy.");}
    check(std::find(abandoned.services->unfinished.begin(),abandoned.services->unfinished.end(),destination)!=abandoned.services->unfinished.end(),
        "Abandoned copy discarded its already allocated buffer owner.");
}
}
int main(){try{exact_copy_order_and_live_bytes();size_and_free_callers();null_pools_and_results();retained_pending_and_teardown();
    late_bindings_and_live_results();unequal_sizes_and_fault_prefixes();overlap_offsets_and_wrapping();failures_and_ownership();
    std::cout<<"IGB buffer copy tests passed: "<<checks<<" checks\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
