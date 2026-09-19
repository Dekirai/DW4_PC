#include "dw4/loader_directory.hpp"

#include <array>
#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const LoaderDirectoryError&) {return;}throw std::runtime_error(reason);
}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
std::shared_ptr<SystemStageDirectory> directory(const char* name="stage.igb",std::int32_t claims=1) {
    auto result=std::make_shared<SystemStageDirectory>();result->filename=text(name);result->claim_count=claims;return result;
}
std::shared_ptr<LoaderDirectoryList> list(std::initializer_list<std::shared_ptr<SystemStageDirectory>> rows) {
    auto result=std::make_shared<LoaderDirectoryList>();
    result->storage=std::make_shared<LoaderDirectoryList::Storage>(rows);
    result->count=result->capacity=static_cast<std::int32_t>(rows.size());return result;
}
std::shared_ptr<LoaderDirectoryServices> services() {
    auto result=std::make_shared<LoaderDirectoryServices>();
    result->filename_field=std::make_shared<LoaderDirectoryNameField>();result->mutex_success=17;return result;
}
void lookup() {
    auto loader=std::make_shared<SystemStageLoader>();auto a=directory(),b=directory();
    auto unnamed=directory();unnamed->filename.reset();
    loader->directories=list({unnamed,a,b});auto bindings=services();
    for(const auto& name:{std::string("stage.igb"),std::string("stage.igb\0ignored",17)}) {
        LoaderDirectoryLookupInvocation call(loader,text(name));
        check(call.resume(bindings).completed && call.result()==a,"Lookup did not return first exact C-string match.");
        check(call.resume().completed && call.result()==a,"Completed lookup changed result.");
    }
    for(const char* name:{"Stage.igb","stage.igb/",""}) {
        LoaderDirectoryLookupInvocation call(loader,text(name));
        check(call.resume(bindings).completed && !call.result(),"Lookup changed filename matching semantics.");
    }
    LoaderDirectoryLookupInvocation retained(loader,text("stage.igb"));
    check(!retained.resume().completed,"Missing descriptor services completed lookup.");
    loader->directories=list({b});
    check(retained.resume(bindings).completed && retained.result()==a,"Pending descriptor lookup recaptured list owner.");
    loader->directories->count=-1;loader->directories->storage.reset();
    LoaderDirectoryLookupInvocation negative(loader,text("stage.igb"));
    check(negative.resume(bindings).completed && !negative.result(),"Negative count touched backing.");
    loader->directories.reset();bindings->filename_field.reset();
    LoaderDirectoryLookupInvocation absent_field(loader,text("stage.igb"));
    check(absent_field.resume(bindings).completed && !absent_field.result(),"Null descriptor dereferenced list.");
    bindings=services();LoaderDirectoryLookupInvocation missing_list(loader,text("stage.igb"));
    rejects([&]{missing_list.resume(bindings);},"Null list was fabricated.");
    loader->directories=list({nullptr});LoaderDirectoryLookupInvocation null_row(loader,text("stage.igb"));
    rejects([&]{null_row.resume(bindings);},"Null physical row was filtered.");
    loader->directories=list({a});loader->directories->count=2;
    LoaderDirectoryLookupInvocation short_backing(loader,text("missing"));
    rejects([&]{short_backing.resume(bindings);},"Missing physical row was filtered.");
    bindings->filename_field->offset=0x18;LoaderDirectoryLookupInvocation other_field(loader,text("stage.igb"));
    rejects([&]{other_field.resume(bindings);},"Unknown metadata offset was silently treated as filename.");
    LoaderDirectoryLookupInvocation null_name(loader,{});
    rejects([&]{null_name.resume(bindings);},"Null input filename was accepted.");
}
void root_lookup() {
    auto loader=std::make_shared<SystemStageLoader>();loader->root_directory=directory("igImageDirectory");
    auto bindings=services();LoaderDirectoryLookupInvocation call(loader,text("igImageDirectory"));
    check(!call.resume(bindings).completed,"Existing root bypassed actual accessor side effects.");
    auto operation=std::make_shared<LoaderDirectoryRootOperation>();unsigned factories{},resumes{};bool ready{};
    bindings->root_directory=[&](const auto& selected) {++factories;check(selected==loader,"Wrong root owner.");return operation;};
    operation->resume=[&] {++resumes;if(!ready)return MessengerCallResult{false,0,"Prepared root accessor."};
        operation->result=loader->root_directory;return MessengerCallResult{true,0,{}};};
    check(!call.resume(bindings).completed && factories==1,"Root accessor did not pause.");
    bindings->root_directory={};operation->resume=[] {throw std::runtime_error("Replaced root callback used.");return MessengerCallResult{};};
    ready=true;check(call.resume(bindings).completed && call.result()==loader->root_directory && resumes==2,
        "Pending root accessor was redirected or replayed factory.");
    check(call.resume().completed && resumes==2,"Completed root accessor replayed.");
}
void plain_claims() {
    for(std::uint32_t word:{0u,1u,2u,0x7fffffffu,0x80000000u,0xffffffffu})
        for(auto kind:{LoaderDirectoryClaim::acquire,LoaderDirectoryClaim::release}) {
            auto value=directory("stage",std::bit_cast<std::int32_t>(word));
            LoaderDirectoryClaimInvocation call(value,kind);
            const auto expected=word+(kind==LoaderDirectoryClaim::acquire ? 1u:0xffffffffu);
            const auto result=call.resume();
            check(result.completed && result.value==expected && std::bit_cast<std::uint32_t>(value->claim_count)==expected,
                "Unprotected claim arithmetic did not wrap at32bits.");
            check(std::bit_cast<std::uint32_t>(call.value())==expected,"Claim returned a different word.");
            check(call.resume().completed && std::bit_cast<std::uint32_t>(value->claim_count)==expected,"Claim replayed mutation.");
        }
}
void locked_claims() {
    for(auto kind:{LoaderDirectoryClaim::acquire,LoaderDirectoryClaim::release})
        for(std::uint32_t status:{0u,17u,0xffffffffu}) {
            auto value=directory("stage",7);value->mutex=std::make_shared<LoaderDirectoryMutex>();
            auto next=std::make_shared<LoaderDirectoryMutex>();auto bindings=services();std::vector<int> trace;
            value->mutex->acquire=[&] {trace.push_back(1);value->claim_count=40;value->mutex=next;return MessengerCallResult{true,status,{}};};
            next->release=[&] {trace.push_back(2);value->claim_count=90;return MessengerCallResult{true,0xffffffffu,{}};};
            LoaderDirectoryClaimInvocation call(value,kind);check(call.resume(bindings).completed,"Locked claim did not complete.");
            const bool changed=kind==LoaderDirectoryClaim::release || status==17;
            check(call.value()==(changed ? kind==LoaderDirectoryClaim::acquire ? 41:39:40),"Claim result recaptured unlock mutation.");
            check(value->claim_count==(changed ? 90:40) && trace==std::vector<int>(changed ? std::initializer_list<int>{1,2}:std::initializer_list<int>{1}),
                "Acquire failure unlocked or release inspected ignored status.");
        }
    auto value=directory("stage",4);auto mutex=std::make_shared<LoaderDirectoryMutex>();value->mutex=mutex;
    unsigned locks{},unlocks{};bool ready{};auto bindings=services();bindings->mutex_success.reset();
    mutex->acquire=[&] {++locks;return MessengerCallResult{ready,17,"Prepared mutex."};};
    mutex->release=[&] {++unlocks;return MessengerCallResult{true,0,{}};};
    LoaderDirectoryClaimInvocation call(value,LoaderDirectoryClaim::acquire);
    check(!call.resume(bindings).completed && value->claim_count==4,"Pending lock mutated claim.");
    mutex->acquire=[] {throw std::runtime_error("Replaced mutex callback used.");return MessengerCallResult{};};
    ready=true;check(!call.resume(bindings).completed && locks==2 && value->claim_count==4,"Missing success global replayed lock.");
    value->claim_count=80;bindings->mutex_success=17;
    check(call.resume(bindings).completed && call.value()==81 && locks==2 && unlocks==1,"Late success binding did not read live claim.");
    auto removed=directory();removed->mutex=std::make_shared<LoaderDirectoryMutex>();
    removed->mutex->acquire=[&] {removed->mutex.reset();return MessengerCallResult{true,17,{}};};
    LoaderDirectoryClaimInvocation bad(removed,LoaderDirectoryClaim::acquire);
    rejects([&]{bad.resume(bindings);},"Successful acquire did not fault on reloaded null mutex.");
    check(removed->claim_count==2,"Null-unlock failure rolled back the increment.");
}
void removal() {
    for(std::int32_t initial:{-1,0,1,2})for(unsigned index=0;index<3;++index) {
        auto loader=std::make_shared<SystemStageLoader>();auto a=directory("a"),b=directory("b"),c=directory("c");
        const std::array<std::shared_ptr<SystemStageDirectory>,3> rows{a,b,c};
        loader->directories=list({a,b,c});auto target=rows[index];target->loader=loader;target->claim_count=initial;
        LoaderDirectoryClaimInvocation call(target,LoaderDirectoryClaim::release);
        check(call.resume().completed && call.value()==initial-1,"Directory removal claim result changed.");
        const auto& storage=*loader->directories->storage;
        if(initial<=1) {
            check(loader->directories->count==2 && !storage[2],"Directory removal did not clear trailing slot.");
            for(unsigned i=0;i<2;++i)check(storage[i]==rows[i+(i>=index)],"Directory removal changed physical order.");
        }else check(loader->directories->count==3 && storage[index]==target,"Positive remaining claim removed directory.");
    }
    auto loader=std::make_shared<SystemStageLoader>();auto target=directory();target->loader=loader;
    auto other=directory("other");loader->directories=list({target,target,other});
    LoaderDirectoryClaimInvocation duplicate(target,LoaderDirectoryClaim::release);
    check(duplicate.resume().completed && loader->directories->count==2 && (*loader->directories->storage)[0]==target,
        "Removal erased every duplicate rather than first pointer.");
    auto absent=directory();absent->loader=loader;
    LoaderDirectoryClaimInvocation missing(absent,LoaderDirectoryClaim::release);
    rejects([&]{missing.resume();},"Missing directory membership silently succeeded.");
    check(absent->claim_count==0 && loader->directories->count==2,"Missing-member failure rolled back decrement or modified list.");
    auto dangling=directory();{auto expired=std::make_shared<SystemStageLoader>();dangling->loader=expired;}
    LoaderDirectoryClaimInvocation expired(dangling,LoaderDirectoryClaim::release);
    rejects([&]{expired.resume();},"Expired nonnull loader was treated as a null parent.");
    check(dangling->claim_count==0,"Expired-parent failure rolled back decrement.");
}
void release_wrapper() {
    auto bindings=services();auto loader=std::make_shared<SystemStageLoader>();auto target=directory();
    auto other=directory("other");loader->directories=list({target,other});loader->root_directory=target;
    auto replacement=directory("replacement");target->mutex=std::make_shared<LoaderDirectoryMutex>();
    target->mutex->acquire=[] {return MessengerCallResult{true,999,{}};};
    target->mutex->release=[&] {loader->root_directory=replacement;return MessengerCallResult{true,0,{}};};
    LoaderDirectoryReleaseInvocation call(loader,text("stage.igb"));
    check(call.resume(bindings).completed && target->loader.lock()==loader && target->claim_count==0,
        "Wrapper did not install parent before releasing claim.");
    check(!loader->root_directory && loader->directories->count==1 && (*loader->directories->storage)[0]==other,
        "Wrapper recaptured root identity or cleared captured old root instead of live field.");
    check(call.resume(bindings).completed && target->claim_count==0,"Wrapper replayed claim release.");
    LoaderDirectoryReleaseInvocation absent(loader,text("absent"));
    check(absent.resume(bindings).completed && loader->directories->count==1,"Absent release changed list.");
    auto second=std::make_shared<SystemStageLoader>();target=directory();second->directories=list({target});target->loader=second;
    loader->directories=list({target});LoaderDirectoryReleaseInvocation foreign(loader,text("stage.igb"));
    check(foreign.resume(bindings).completed && target->loader.lock()==second && second->directories->count==0 && loader->directories->count==1,
        "Wrapper overwrote an existing parent or removed from wrong list.");
}
void continuation_guards() {
    auto target=directory();target->mutex=std::make_shared<LoaderDirectoryMutex>();
    auto bindings=services();bool ready{};
    target->mutex->acquire=[&] {return MessengerCallResult{ready,17,{}};};
    target->mutex->release=[] {return MessengerCallResult{true,0,{}};};
    LoaderDirectoryClaimInvocation first(target,LoaderDirectoryClaim::acquire);
    check(!first.resume(bindings).completed,"Prepared claim did not pause.");
    LoaderDirectoryClaimInvocation overlap(target,LoaderDirectoryClaim::release);
    rejects([&]{overlap.resume(bindings);},"Overlapping claim was accepted.");
    ready=true;check(first.resume(bindings).completed && target->claim_count==2,"Rejected overlap poisoned current owner.");
    target=directory();target->mutex=std::make_shared<LoaderDirectoryMutex>();
    {LoaderDirectoryClaimInvocation abandoned(target,LoaderDirectoryClaim::acquire);check(!abandoned.resume(bindings).completed,"Missing mutex operation succeeded.");}
    LoaderDirectoryClaimInvocation after(target,LoaderDirectoryClaim::acquire);
    rejects([&]{after.resume(bindings);},"Abandoned claim was restarted.");
    target=directory();target->mutex=std::make_shared<LoaderDirectoryMutex>();
    LoaderDirectoryClaimInvocation recursive(target,LoaderDirectoryClaim::acquire);
    target->mutex->acquire=[&] {try {recursive.resume(bindings);}catch(const LoaderDirectoryError&) {}return MessengerCallResult{true,17,{}};};
    rejects([&]{recursive.resume(bindings);},"Caught recursive claim continued.");
    check(target->claim_count==1,"Recursive failure published a later increment.");

    auto loader=std::make_shared<SystemStageLoader>();target=directory();
    loader->directories=list({target});target->mutex=std::make_shared<LoaderDirectoryMutex>();
    const std::weak_ptr<SystemStageDirectory> retained=target;
    target->mutex->acquire=[&] {
        loader->directories->storage->clear();loader->directories->count=0;target.reset();
        return MessengerCallResult{false,0,"Prepared callback removed the other directory owners."};
    };
    {
        LoaderDirectoryReleaseInvocation abandoned(loader,text("stage.igb"));
        check(!abandoned.resume(bindings).completed && !retained.expired(),"Pending wrapper lost its captured directory.");
    }
    check(!retained.expired(),"Abandoning the wrapper dropped its original outstanding directory temporary.");
    LoaderDirectoryReleaseInvocation poisoned(loader,text("stage.igb"));
    rejects([&]{poisoned.resume(bindings);},"Abandoned wrapper's loader state restarted.");
}
}
int main() {
    try {lookup();root_lookup();plain_claims();locked_claims();removal();release_wrapper();continuation_guards();
        std::cout<<"loader_directory_tests: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
