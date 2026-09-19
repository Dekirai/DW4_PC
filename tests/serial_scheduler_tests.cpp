#include "dw4/serial_scheduler.hpp"
#include "dw4/application_frame.hpp"
#include "dw4/game_clock.hpp"

#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
MessengerCallResult done() {return {true,19,{}};}
std::shared_ptr<ModelSceneList> list(std::vector<std::shared_ptr<ModelSceneObject>> values) {
    auto result=std::make_shared<ModelSceneList>("igInfoManagerList");
    result->count=result->capacity=static_cast<std::int32_t>(values.size());
    result->backing=std::make_shared<ModelSceneList::Storage>(std::move(values));return result;
}
void empty_and_ordered_batches() {
    auto state=std::make_shared<SerialSchedulerState>();
    check(state->registered && state->registered->entries && state->cache &&
        state->registered->entries->count==0 && state->cache->count==0 && !state->cache->backing,
        "Scheduler represented empty owners differ from fresh list defaults.");
    SerialSchedulerInvocation empty(state);
    check(empty.resume().completed && !state->cache->backing,"Empty scheduler allocated storage or required manager callbacks.");
    std::vector<int> calls;
    auto first=std::make_shared<SerialSchedulerManager>([&] {calls.push_back(1);return done();});
    auto second=std::make_shared<SerialSchedulerManager>([&] {calls.push_back(2);return done();});
    state->registered->entries=list({first,second,first});
    SerialSchedulerInvocation batch(state);
    check(batch.resume().completed && calls==std::vector<int>{1,2,1},"Scheduler changed physical order or deduplicated a manager.");
    check(state->cache->count==0 && state->cache->capacity==4 && state->cache->backing->size()==4,
        "Scheduler cache growth/cleanup changed capacity or allocation identity.");
    for(std::uint32_t i=0;i<3;++i)check(!serial_scheduler_physical_object(state->cache,i),"Cache cleanup left a captured pointer word set.");
    rejects([&] {serial_scheduler_physical_object(state->cache,3);},"Fresh spare allocation word was silently treated as initialized.");
    const auto allocation=state->cache->backing;
    check(batch.resume().completed && calls.size()==3,"Completed scheduler invocation replayed the batch.");
    SerialSchedulerInvocation next(state);
    check(next.resume().completed && calls.size()==6 && state->cache->backing==allocation,
        "Next scheduler batch failed to reuse sufficient cache backing.");
}
void source_changes_wait_for_next_batch() {
    auto state=std::make_shared<SerialSchedulerState>();std::vector<int> calls;
    auto late=std::make_shared<SerialSchedulerManager>([&] {calls.push_back(3);return done();});
    auto second=std::make_shared<SerialSchedulerManager>([&] {calls.push_back(2);return done();});
    auto first=std::make_shared<SerialSchedulerManager>([&] {
        calls.push_back(1);state->registered=std::make_shared<SerialSchedulerRegistry>();
        state->registered->entries=list({late});return done();
    });
    state->registered->entries=list({first,second});
    SerialSchedulerInvocation batch(state);
    check(batch.resume().completed && calls==std::vector<int>{1,2},"Source replacement changed the already copied manager batch.");
    SerialSchedulerInvocation next(state);
    check(next.resume().completed && calls==std::vector<int>{1,2,3},"Next batch did not read the replaced registry/list/count.");
}
void live_cache_reloads_and_pending_target() {
    auto state=std::make_shared<SerialSchedulerState>();std::vector<int> calls;bool ready{};
    auto replacement_manager=std::make_shared<SerialSchedulerManager>([&] {calls.push_back(3);return done();});
    auto unused=std::make_shared<SerialSchedulerManager>([&] {calls.push_back(99);return done();});
    auto replacement=list({unused,replacement_manager});
    // Update iteration ignores this logical count and capacity; final resize
    // uses the count that is live when cleanup is reached.
    replacement->count=0;replacement->capacity=-1;
    auto first=std::make_shared<SerialSchedulerManager>([&] {
        calls.push_back(1);state->cache=replacement;
        return MessengerCallResult{ready,7,"Prepared manager remains pending."};
    });
    state->registered->entries=list({first,unused});const auto original_cache=state->cache;
    SerialSchedulerInvocation batch(state);
    check(!batch.resume().completed && calls==std::vector<int>{1},"Manager suspension incorrectly completed or advanced.");
    first->update=[&] {calls.push_back(98);return done();};
    SerialSchedulerInvocation overlap(state);
    rejects([&] {overlap.resume();},"Overlapping scheduler batch was accepted.");
    check(!state->faulted(),"Rejected overlap poisoned the original pending scheduler.");
    ready=true;
    check(batch.resume().completed && calls==std::vector<int>{1,1,3},"Pending target changed or next manager used the stale cache.");
    check(original_cache->count==2 && serial_scheduler_physical_object(original_cache,0)==first,
        "Final cleanup cleared the initial cache instead of reloading the live cache owner.");
    check(replacement->count==0 && serial_scheduler_physical_object(replacement,1)==replacement_manager,
        "Zero-count cleanup fabricated clearing beyond its current logical range.");
}
void missing_and_null_callbacks() {
    auto state=std::make_shared<SerialSchedulerState>();unsigned calls{};
    auto pending=std::make_shared<SerialSchedulerManager>();
    state->registered->entries=list({pending});
    SerialSchedulerInvocation batch(state);
    check(!batch.resume().completed && state->cache->count==1,"Missing manager callback was treated as a no-op.");
    pending->update=[&] {++calls;return done();};
    check(batch.resume().completed && calls==1 && state->cache->count==0,"Installing the actual pending manager callback did not resume.");
    auto null_state=std::make_shared<SerialSchedulerState>();
    auto first=std::make_shared<SerialSchedulerManager>([&] {++calls;return done();});
    null_state->registered->entries=list({first,{}});
    SerialSchedulerInvocation null_entry(null_state);
    rejects([&] {null_entry.resume();},"Null manager slot was skipped instead of faulting at virtual dispatch.");
    check(calls==2 && null_state->cache->count==2 && null_state->faulted(),"Null later manager changed the earlier call or cleanup prefix.");
}
void resize_and_failure_prefixes() {
    {
        auto incoming=std::make_shared<SerialSchedulerManager>(done);
        auto missing_slot=std::make_shared<SerialSchedulerState>();
        missing_slot->registered->entries=list({incoming});
        missing_slot->cache->count=missing_slot->cache->capacity=1;
        check(incoming.use_count()==2,"Prepared retained-source fixture has unexpected ownership.");
        {SerialSchedulerInvocation retain_prefix(missing_slot);
            rejects([&] {retain_prefix.resume();},"Missing destination slot was accepted after source retain.");}
        check(incoming.use_count()==3 && missing_slot->cache->count==1 && !missing_slot->cache->backing,
            "A later copy failure undid the already acquired source reference.");
    }
    auto manager=std::make_shared<SerialSchedulerManager>(done);
    auto truncated=std::make_shared<SerialSchedulerState>();
    truncated->cache=list({manager});truncated->cache->count=2;truncated->cache->capacity=2;
    SerialSchedulerInvocation release_prefix(truncated);
    rejects([&] {release_prefix.resume();},"Truncated release backing was accepted.");
    check(truncated->cache->count==2 && manager.use_count()==1,"Release prefix did not drop its reached owner before the later failure.");
    check(serial_scheduler_physical_object(truncated->cache,0)==manager,
        "Release-before-zero prefix lost the original still-readable pointer word.");

    auto raw_zero=std::make_shared<SerialSchedulerState>();
    raw_zero->registered->entries=list({manager});raw_zero->cache=list({manager});raw_zero->cache->count=0;
    SerialSchedulerInvocation zero_prefix(raw_zero);
    rejects([&] {zero_prefix.resume();},"Raw memset silently released an owned spare slot.");
    check(raw_zero->cache->count==1 && serial_scheduler_physical_object(raw_zero->cache,0)==manager,
        "Growing-within-capacity failure lost count publication before the raw zero.");

    auto bad_copy=std::make_shared<SerialSchedulerState>();
    bad_copy->registered->entries=list({manager,manager,manager,manager});
    bad_copy->cache=list({});bad_copy->cache->capacity=3;
    const auto old=bad_copy->cache->backing;
    SerialSchedulerInvocation capacity_copy(bad_copy);
    rejects([&] {capacity_copy.resume();},"Malformed old-capacity copy was accepted.");
    check(bad_copy->cache->backing!=old && bad_copy->cache->backing->size()==4 &&
        bad_copy->cache->capacity==3 && bad_copy->cache->count==0,
        "Allocation failure prefix did not preserve publication before capacity copy.");

    auto negative=std::make_shared<SerialSchedulerState>();negative->cache->count=-1;
    SerialSchedulerInvocation negative_old(negative);
    rejects([&] {negative_old.resume();},"Negative old count reached an invented safe memset.");
    check(negative->cache->count==0,"Growing from negative count lost the prior count-zero publication.");
    auto negative_source=std::make_shared<SerialSchedulerState>();negative_source->registered->entries->count=-1;
    SerialSchedulerInvocation negative_new(negative_source);
    rejects([&] {negative_new.resume();},"Negative captured source count was treated as an empty batch.");
    check(negative_source->cache->count==0 && !negative_source->cache->backing,"Negative shrink changed fields before its first bad pointer read.");

    auto source_short=std::make_shared<SerialSchedulerState>();source_short->registered->entries=list({manager});
    source_short->registered->entries->count=2;unsigned callbacks{};
    manager->update=[&] {++callbacks;return done();};
    SerialSchedulerInvocation copy_prefix(source_short);
    rejects([&] {copy_prefix.resume();},"Truncated source copied a fabricated manager.");
    check(callbacks==0 && source_short->cache->count==2 && serial_scheduler_physical_object(source_short->cache,0)==manager &&
        !serial_scheduler_physical_object(source_short->cache,1),"Copy failure ran updates early or rolled back an earlier cache store.");

    auto final_release=std::make_shared<SerialSchedulerState>();
    final_release->cache=list({std::make_shared<SerialSchedulerManager>(done)});
    SerialSchedulerInvocation unsupported_destructor(final_release);
    rejects([&] {unsupported_destructor.resume();},"Unknown final manager destructor was replaced with silent success.");
    check(final_release->cache->count==1 && serial_scheduler_physical_object(final_release->cache,0),
        "Unsupported final destructor cleared fields after its reached boundary.");
}
void alias_and_fault_contracts() {
    auto state=std::make_shared<SerialSchedulerState>();unsigned calls{};
    auto manager=std::make_shared<SerialSchedulerManager>([&] {++calls;return done();});
    auto aliased=list({manager});state->registered->entries=aliased;state->cache=aliased;
    SerialSchedulerInvocation alias(state);
    check(alias.resume().completed && calls==1 && aliased->count==0 && !serial_scheduler_physical_object(aliased,0),
        "Aliased source/cache changed retain-before-release or final clear semantics.");

    auto throwing=std::make_shared<SerialSchedulerState>();
    auto bad=std::make_shared<SerialSchedulerManager>([]() -> MessengerCallResult {throw SerialSchedulerError("Prepared manager failure.");});
    throwing->registered->entries=list({bad,manager});SerialSchedulerInvocation failure(throwing);
    rejects([&] {failure.resume();},"Manager exception was swallowed.");
    check(throwing->faulted() && throwing->cache->count==2 && calls==1,"Manager exception reached later calls or cleanup.");
    SerialSchedulerInvocation restart(throwing);rejects([&] {restart.resume();},"Faulted scheduler replayed its batch.");

    auto recursive_state=std::make_shared<SerialSchedulerState>();SerialSchedulerInvocation recursive(recursive_state);
    auto recurse=std::make_shared<SerialSchedulerManager>([&] {try {recursive.resume();}catch(const SerialSchedulerError&) {}return done();});
    recursive_state->registered->entries=list({recurse});
    rejects([&] {recursive.resume();},"Caught recursive update completed its original outer call.");
    check(recursive_state->faulted() && recursive_state->cache->count==1,"Recursive failure cleared the retained cache.");
    auto abandoned=std::make_shared<SerialSchedulerState>();abandoned->registered->entries=list({std::make_shared<SerialSchedulerManager>()});
    {SerialSchedulerInvocation pending(abandoned);check(!pending.resume().completed,"Missing callback completed abandonment fixture.");}
    check(abandoned->faulted(),"Abandoned incomplete scheduler could start a later batch.");
}
void application_service_binding() {
    auto scheduler=std::make_shared<SerialSchedulerState>();unsigned samples{},updates{};
    bool ready{};
    auto manager=std::make_shared<SerialSchedulerManager>([&] {++updates;return MessengerCallResult{ready,0,"Prepared manager is pending."};});
    scheduler->registered->entries=list({manager});
    auto clock=std::make_shared<NativeGameClock>([&] {return std::uint64_t(++samples)*100;});clock->start();
    auto application=std::make_shared<ApplicationFrameState>(clock);initialize_application_frame_factor(*application);
    application->dispatcher=done;application->scheduler=make_serial_scheduler_update(scheduler);
    ApplicationFrameInvocation first(application);
    check(!first.resume().completed && samples==2 && updates==1 && application->world_time==100,
        "Application-to-scheduler suspension changed the clock/update prefix.");
    ready=true;
    check(first.resume().completed && samples==2 && updates==2,"Scheduler resumption replayed application timing.");
    ApplicationFrameInvocation second(application);
    check(second.resume().completed && samples==3 && updates==3 && application->world_time==200,
        "Completed scheduler service failed to create one new batch for the next application update.");
}
} // namespace
int main() {
    try {
        empty_and_ordered_batches();source_changes_wait_for_next_batch();live_cache_reloads_and_pending_target();
        missing_and_null_callbacks();resize_and_failure_prefixes();alias_and_fault_contracts();application_service_binding();
        std::cout<<"Passed "<<checks<<" native serial scheduler checks.\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"Serial scheduler test failed: "<<error.what()<<'\n';return 1;}
}
