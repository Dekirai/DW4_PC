#include "dw4/igb_stream.hpp"
#include "dw4/igb.hpp"
#include "dw4/resources.hpp"

#include <array>
#include <bit>
#include <fstream>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const IgbStreamError&) {return;}throw std::runtime_error(reason);
}
SystemStageString text(const char* value) {return std::make_shared<const std::string>(value);}
std::vector<std::uint8_t> header(std::uint32_t flags=6,bool little=false,std::uint32_t cookie=0xfada) {
    std::array<std::uint32_t,12> words{64,1,128,2,256,3,512,4,1024,5,cookie,flags};
    std::vector<std::uint8_t> result;
    for(auto word:words)for(unsigned byte=0;byte<4;++byte)
        result.push_back(static_cast<std::uint8_t>(word>>(8*(little ? byte:3-byte))));
    return result;
}
struct Fixture {
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<IgbStreamState> state=std::make_shared<IgbStreamState>();
    std::shared_ptr<IgbStreamServices> services=std::make_shared<IgbStreamServices>();
    Fixture(std::vector<std::uint8_t> bytes=header()) {
        file->filename=text("stage.igb");state->file=file;state->expected_cookie=0xfada;
        file->pool_118=std::make_shared<ModelSceneData>();igb_bind_native_scratch_pool(state,file->pool_118);
        state->statuses->success=0;state->statuses->failure=1;
        state->stream=igb_stream_from_bytes(file->filename,std::move(bytes));
    }
};
void headers() {
    for(bool little:{false,true})for(std::uint32_t flags:{0u,1u,6u,7u,8u,0xffffu,0x80000006u,0x40000006u,
        0x20000006u,0x10000006u,0xf0000006u,0x80000001u,0x10000u,0x10001u}) {
        Fixture f(header(flags,little));f.state->swapped=77;f.state->flag_d4=9;f.state->entry_count=99;
        IgbHeaderInvocation call(f.state);const auto result=call.resume(f.services);
        const auto valid=(flags&0xffffu)<=7;
        check(result.completed && result.value==(valid ? 0u:1u),"Header acceptance/status differed.");
        check(f.state->version==(flags&0xffffu) && f.state->swapped==static_cast<std::uint8_t>(little),"Version/endian publication changed.");
        check(f.state->stream->position==48 && f.state->int_field==f.services->int_field && f.state->short_field==f.services->short_field,
            "Header read or field-owner publication changed.");
        if(valid) {
            check(f.state->flag_d4==((flags>>31)&1) && f.state->flag_d5==((flags>>30)&1) &&
                f.state->flag_d6==((flags>>29)&1) && f.state->flag_124==((flags>>28)&1),"Header high flags changed.");
            check(f.state->flag_d7==(flags==1) && f.state->legacy_mode==(flags==1),"Legacy flag tested low16 instead of wholeword.");
            check(f.state->entry_bytes==64 && f.state->entry_count==1 && f.state->object_type_bytes==128 &&
                f.state->object_type_count==2 && f.state->object_bytes==256 && f.state->object_count==3 &&
                f.state->memory_reference_bytes==512 && f.state->memory_reference_count==4 &&
                f.state->field_type_bytes==1024 && f.state->field_type_count==5,"Header count/size mapping changed.");
        }else check(f.state->flag_d4==9 && f.state->entry_count==99,"Invalid version published later fields.");
        check(call.resume().completed && f.state->stream->position==48,"Completed header replayed stream read.");
    }
    Fixture bad(header(6,false,0x12345678));bad.state->swapped=81;bad.state->version=57;
    IgbHeaderInvocation invalid(bad.state);check(invalid.resume(bad.services).value==1,"Invalid cookie succeeded.");
    check(bad.state->swapped==81 && bad.state->version==57 && igb_buffer_word(bad.state->buffer,40)==0x78563412,
        "Failed cookie swap changed endian flag/version or omitted in-place mutation.");
    Fixture alternate(header(6,false,0x12345678));alternate.state->expected_cookie=0x12345678;
    IgbHeaderInvocation expected(alternate.state);check(expected.resume(alternate.services).value==0,"Expected cookie was hardcoded.");
}
void scratch_and_short_reads() {
    Fixture f;f.file->option_a4=193;
    IgbBufferInvocation ensure(f.state,192);check(ensure.resume().completed && f.state->buffer_capacity==196,"Buffer size rounding changed.");
    const auto first=f.state->buffer;check(first->bytes.size()==196 && first->initialized[0]==0,"Fresh scratch was fabricated as initialized.");
    IgbBufferInvocation reuse(f.state,100);check(reuse.resume().completed && f.state->buffer==first,"Adequate scratch was reallocated.");
    IgbBufferInvocation grow(f.state,201);check(grow.resume().completed && first->released && f.state->buffer_capacity==204,
        "Scratch growth did not invalidate old aliases or round upward.");
    rejects([&]{igb_buffer_bytes(first,0,0);},"Released alias remained readable.");
    const auto dangling=f.state->buffer;f.state->stream.reset();f.file->option_a4=0;
    IgbBufferInvocation unavailable_alignment(f.state,300);
    rejects([&]{unavailable_alignment.resume();},"Missing alignment stream was accepted.");
    check(f.state->buffer==dangling && dangling->released && f.state->buffer_capacity==300,
        "Failed growth erased the released pointer before replacement publication.");
    Fixture sized; sized.file->option_a4=0xffffffffu;
    IgbBufferInvocation size_hint(sized.state,12);check(size_hint.resume().completed && sized.state->buffer_capacity==48,"Negative hint did not use stream size.");
    Fixture pending_buffer;pending_buffer.state->statuses->success.reset();
    IgbBufferInvocation deferred(pending_buffer.state,192);
    check(!deferred.resume().completed && pending_buffer.state->buffer_capacity==192,"Missing status preempted allocation prefix.");
    const auto allocated=pending_buffer.state->buffer;pending_buffer.file->option_a4=999;
    pending_buffer.state->statuses->success=17;
    check(deferred.resume().value==17 && pending_buffer.state->buffer==allocated && pending_buffer.state->buffer_capacity==192,
        "Pending allocation status replayed or changed allocation.");
    Fixture short_file(std::vector<std::uint8_t>{1,2,3,4,5});
    IgbHeaderInvocation truncated(short_file.state);
    rejects([&]{truncated.resume(short_file.services);},"Short fresh header used unknown allocator bytes.");
    check(short_file.state->stream->position==5 && short_file.state->int_field==short_file.services->int_field &&
        short_file.state->buffer->initialized[4] && !short_file.state->buffer->initialized[5],"Short-read publication prefix changed.");
    Fixture stale;IgbHeaderInvocation initial(stale.state);check(initial.resume(stale.services).value==0,"Initial header failed.");
    stale.state->stream=igb_stream_from_bytes(text("short"),{0,0,0,32});
    IgbHeaderInvocation reread(stale.state);check(reread.resume(stale.services).value==0 && stale.state->entry_bytes==32 && stale.state->entry_count==1,
        "Ignored short-read result did not preserve known old scratch bytes.");
}
void pending_header() {
    Fixture f;f.state->statuses->failure.reset();
    IgbHeaderInvocation call(f.state);
    check(!call.resume(f.services).completed && f.state->buffer && f.state->stream->position==0,
        "Absent comparison word read input too soon or blocked buffer allocation.");
    f.state->statuses->failure=1;
    check(!call.resume().completed && f.state->stream->position==48,"Absent field binding did not retain completed read.");
    f.state->statuses->success.reset();
    check(!call.resume(f.services).completed && f.state->version==6 && f.state->entry_count==1,
        "Missing final success word prevented header field publication.");
    f.state->statuses->success=29;check(call.resume().value==29 && f.state->stream->position==48,"Final status replayed header.");
    Fixture same; same.state->statuses->failure=0;
    IgbHeaderInvocation equal_codes(same.state);
    check(equal_codes.resume(same.services).value==0 && same.state->stream->position==0,
        "Equal success/failure words bypassed exact original comparison.");
    Fixture null_field;null_field.services->int_field.reset();null_field.services->short_field.reset();
    IgbHeaderInvocation direct(null_field.state);check(direct.resume(null_field.services).value==0,"Unused null endian field was rejected.");
    Fixture needs_swap(header(6,true));needs_swap.services->int_field.reset();
    IgbHeaderInvocation missing(needs_swap.state);rejects([&]{missing.resume(needs_swap.services);},"Reached null endian field was accepted.");
}
void scratch_pool_boundaries() {
    Fixture missing;missing.file->pool_118.reset();missing.file->option_a4=193;
    IgbBufferInvocation allocate_null(missing.state,192);
    rejects([&]{allocate_null.resume();},"Null allocation pool was silently replaced.");
    check(missing.state->buffer_capacity==196 && !missing.state->buffer,
        "Null allocation pool lost the sizing prefix.");
    Fixture release;IgbBufferInvocation initial(release.state,192);check(initial.resume().completed,"Initial allocation failed.");
    const auto original=release.state->buffer;release.file->pool_118.reset();
    IgbBufferInvocation fast(release.state,100);check(fast.resume().completed,"Adequate buffer incorrectly required a pool.");
    IgbBufferInvocation release_null(release.state,200);
    rejects([&]{release_null.resume();},"Null free pool was silently replaced.");
    check(release.state->buffer==original && !original->released && release.state->buffer_capacity==192,
        "Null free pool mutated fields before its dereference.");
    Fixture deferred;deferred.state->native_scratch_pools.clear();const auto captured=deferred.file->pool_118;
    IgbBufferInvocation pending_allocate(deferred.state,192);
    check(!pending_allocate.resume().completed && deferred.state->buffer_capacity==192 && !deferred.state->buffer,
        "Unbound nonnull pool silently allocated.");
    auto replacement=std::make_shared<ModelSceneData>();deferred.file->pool_118=replacement;
    igb_bind_native_scratch_pool(deferred.state,replacement);deferred.file->option_a4=1000;
    check(!pending_allocate.resume().completed,"Pending allocation changed its captured pool identity.");
    igb_bind_native_scratch_pool(deferred.state,captured);
    check(pending_allocate.resume().completed && deferred.state->buffer->bytes.size()==192,
        "Pending allocation replayed sizing or lost its captured arguments.");
    const auto old=deferred.state->buffer;deferred.state->native_scratch_pools.clear();
    IgbBufferInvocation pending_free(deferred.state,300);
    check(!pending_free.resume().completed && !old->released,"Unbound free mutated its buffer.");
    deferred.file->pool_118=captured;deferred.file->option_a4=301;
    igb_bind_native_scratch_pool(deferred.state,replacement);
    check(!pending_free.resume().completed && old->released && deferred.state->buffer==old && deferred.state->buffer_capacity==304,
        "Free did not retain its pool or allocation did not reload the new pool.");
    igb_bind_native_scratch_pool(deferred.state,captured);
    check(pending_free.resume().completed && deferred.state->buffer!=old && deferred.state->buffer->bytes.size()==304,
        "Resumed allocation failed to publish its replacement after a captured free.");
}
void field_owner_prefixes() {
    Fixture first;first.state->int_field=std::make_shared<IgbEndianField>(IgbEndianField{4});
    std::weak_ptr<IgbEndianField> old_int=first.state->int_field,incoming_int=first.services->int_field;
    {
        IgbHeaderInvocation call(first.state);
        rejects([&]{call.resume(first.services);},"Header silently destroyed its final old int field.");
        check(first.state->stream->position==48 && first.state->int_field==old_int.lock() && !first.state->short_field && !first.state->version,
            "Int-field destructor boundary lost the exact read/publication prefix.");
    }
    first.services.reset();check(!incoming_int.expired(),"Failed int-field publication lost its acquired incoming owner.");
    Fixture second;second.state->short_field=std::make_shared<IgbEndianField>(IgbEndianField{2});
    std::weak_ptr<IgbEndianField> old_short=second.state->short_field,incoming_short=second.services->short_field;
    {
        IgbHeaderInvocation call(second.state);
        rejects([&]{call.resume(second.services);},"Header silently destroyed its final old short field.");
        check(second.state->int_field==second.services->int_field && second.state->short_field==old_short.lock() && !second.state->version,
            "Short-field destructor boundary lost the previously published int owner.");
    }
    second.services.reset();check(!incoming_short.expired(),"Failed short-field publication lost its acquired incoming owner.");
    Fixture same;same.state->int_field=same.services->int_field;same.state->short_field=same.services->short_field;
    IgbHeaderInvocation identity(same.state);check(identity.resume(same.services).value==0,"Same-field publication failed its acquire-before-release path.");
}
void opening() {
    Fixture f;f.state->stream.reset();f.services=make_igb_byte_stream_services(text("stage.igb"),header());
    IgbOpenInvocation call(f.state,text("stage.igb"));
    check(call.resume(f.services).value==0 && f.state->stream && f.state->owns_stream==1 && f.state->stream->mode==4,
        "Exact supplied byte stream did not open/publish mode.");
    const auto stream=f.state->stream;check(call.resume().completed && f.state->stream==stream,"Completed open recreated stream.");
    Fixture existing;existing.state->owns_stream=73;IgbOpenInvocation keep(existing.state,text("another.igb"));
    check(keep.resume().value==0 && existing.state->owns_stream==73,"Existing stream was reopened or marked auto-opened.");
    Fixture none;none.state->stream.reset();none.state->statuses->failure.reset();IgbOpenInvocation empty(none.state,{});
    check(!empty.resume().completed,"Unbound reached failure word was fabricated.");none.state->statuses->failure=9;
    check(empty.resume().value==9 && !none.state->stream,"No filename/stream path opened a resource.");
    Fixture unknown;unknown.state->stream.reset();auto provider=make_igb_byte_stream_services(text("stage.igb"),header());
    IgbOpenInvocation other(unknown.state,text("Stage.igb"));check(!other.resume(provider).completed && !unknown.state->stream,
        "Byte adapter guessed unbound filename/path dispatch.");
    Fixture failed;failed.state->stream.reset();auto created=igb_stream_from_bytes(text("stage.igb"),header());
    failed.services->open=[&](const auto& state,const auto&,std::uint32_t mode) {
        check(mode==4,"Original open mode changed.");auto operation=std::make_shared<IgbOpenOperation>();
        operation->resume=[state,created] {state->stream=created;return MessengerCallResult{true,1,{}};};return operation;
    };
    IgbOpenInvocation failure(failed.state,text("stage.igb"));
    check(failure.resume(failed.services).value==1 && !failed.state->stream && !failed.state->owns_stream,
        "Failed opening did not clear reloaded stream before return.");
    Fixture final_release;final_release.state->stream.reset();
    final_release.services->open=[](const auto& state,const auto&,std::uint32_t) {
        auto operation=std::make_shared<IgbOpenOperation>();operation->resume=[state] {
            state->stream=igb_stream_from_bytes(text("stage.igb"),header());return MessengerCallResult{true,1,{}};
        };return operation;
    };
    IgbOpenInvocation final(final_release.state,text("stage.igb"));
    rejects([&]{final.resume(final_release.services);},"Failed open silently destroyed its final igFile owner.");
    check(bool(final_release.state->stream) && !final_release.state->owns_stream,
        "Final igFile destructor boundary cleared the reached stream pointer.");
    Fixture no_result;no_result.state->stream.reset();no_result.services->open=[](const auto&,const auto&,std::uint32_t) {
        auto op=std::make_shared<IgbOpenOperation>();op->resume=[] {return MessengerCallResult{true,99,{}};};return op;
    };
    IgbOpenInvocation null_stream(no_result.state,text("stage.igb"));
    check(null_stream.resume(no_result.services).value==1 && no_result.state->owns_stream==1,
        "Nonfailure result with null stream lost the auto-opened prefix.");
}
void filenames_and_ownership() {
    for(const auto& pair:std::array<std::pair<const char*,const char*>,5>{{{"a/b.igb","b.igb"},{"a\\b.igb","b.igb"},
        {"a/b/","b/"},{"x","x"},{"/abc","abc"}}}) {
        Fixture f;f.file->filename.reset();f.state->stream->filename=text(pair.first);SystemStageString selected;
        IgbOpenInvocation call(f.state,{});
        check(!call.resume(f.services).completed && !f.file->filename,"Filename interning was fabricated.");
        f.services->intern_filename=[&](const auto& candidate) {
            selected=candidate;auto op=std::make_shared<IgbStreamNameOperation>();op->result=candidate;
            op->resume=[] {return MessengerCallResult{true,0,{}};};return op;
        };
        check(call.resume(f.services).value==0 && selected && *selected==pair.second && *f.file->filename==pair.second,
            "Original basename scan or retained interning result changed.");
    }
    Fixture empty;empty.file->filename.reset();empty.state->stream->filename=text("");IgbOpenInvocation invalid(empty.state,{});
    rejects([&]{invalid.resume(empty.services);},"Empty stream name fabricated a byte-before-start result.");
    Fixture overlap;overlap.state->stream.reset();
    IgbOpenInvocation first(overlap.state,text("stage.igb"));check(!first.resume().completed,"Missing opener did not pause.");
    IgbOpenInvocation second(overlap.state,text("stage.igb"));rejects([&]{second.resume();},"Overlapping open accepted.");
    check(first.resume(make_igb_byte_stream_services(text("stage.igb"),header())).completed,"Rejected overlap poisoned original owner.");
    Fixture abandoned;abandoned.state->statuses->success.reset();
    {IgbHeaderInvocation old(abandoned.state);check(!old.resume(abandoned.services).completed,"Missing header status did not pause.");}
    IgbHeaderInvocation restart(abandoned.state);rejects([&]{restart.resume(abandoned.services);},"Abandoned header restarted.");
}
void compare_original(const char* label,std::vector<std::uint8_t> bytes) {
    const auto metadata=read_igb_metadata(bytes);Fixture f(bytes);f.state->stream.reset();
    auto service=make_igb_byte_stream_services(text(label),std::move(bytes));IgbOpenInvocation open(f.state,text(label));
    check(open.resume(service).value==0,"Original supplied bytes did not open.");
    IgbHeaderInvocation read(f.state);check(read.resume(service).value==0,"Original header was rejected.");
    const auto& h=metadata.header;
    check(f.state->version==h.version && f.state->entry_bytes==h.entry_buffer_size && f.state->entry_count==h.entry_count &&
        f.state->object_type_bytes==h.object_type_buffer_size && f.state->object_type_count==h.object_type_count &&
        f.state->object_bytes==h.object_buffer_size && f.state->object_count==h.object_count &&
        f.state->memory_reference_bytes==h.memory_reference_buffer_size && f.state->memory_reference_count==h.memory_reference_count &&
        f.state->field_type_bytes==h.field_type_buffer_size && f.state->field_type_count==h.field_type_count,
        "Original native header disagreed with the independent metadata reader.");
    std::cout<<"Original IGB header: "<<label<<", version "<<f.state->version<<", "<<f.state->object_count<<" objects\n";
}
void originals(const std::filesystem::path& root) {
    const auto path=std::filesystem::exists(root/"files"/"ndmw.igb") ? root/"files"/"ndmw.igb":root/"ndmw.igb";
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Missing original ndmw.igb");
    const auto size=file.tellg();if(size<0 || size>64*1024*1024)throw std::runtime_error("Original ndmw.igb size outside test profile");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!file)throw std::runtime_error("Original ndmw.igb read failed");compare_original("ndmw.igb",std::move(bytes));
    const auto resources=MountedResources::open(root);unsigned tested{};
    for(const auto& entry:resources.catalog().resources)if(entry.location && entry.logical_path.ends_with(".igb")) {
        compare_original(entry.logical_path.c_str(),resources.read(entry.logical_path));if(++tested==3)break;
    }
    check(tested==3,"Insufficient original catalog IGB entries for header integration.");
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=1 && !(argc==3 && std::string_view(argv[1])=="--data"))
            throw std::runtime_error("Usage: igb_stream_tests [--data <extracted-root>]");
        headers();scratch_and_short_reads();scratch_pool_boundaries();field_owner_prefixes();pending_header();opening();filenames_and_ownership();
        const auto synthetic=checks;if(argc==3 && std::string_view(argv[1])=="--data")originals(argv[2]);
        std::cout<<"igb_stream_tests: "<<checks<<" checks passed ("<<synthetic<<" synthetic)\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
