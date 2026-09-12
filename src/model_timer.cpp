#include "dw4/model_timer.hpp"
#include "dw4/model_scene.hpp"

#include <bit>
#include <exception>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw ModelSceneError(reason);}
std::shared_ptr<ModelSceneTimer> timer_fields(const std::shared_ptr<ModelSceneObject>& record) {
    auto timer=std::dynamic_pointer_cast<ModelSceneTimer>(record);
    if(!timer)fail("Original named timer field access requires the captured actual timer record.");
    return timer;
}
}
class ModelRamTimerAccess::Operation {
public:
    explicit Operation(ModelRamTimerAccess& owner):owner_(owner),exceptions_(std::uncaught_exceptions()) {
        if(owner_.executing_) {
            owner_.faulted_=true;
            fail("A named RAM timer endpoint cannot recursively enter an active helper.");
        }
        if(owner_.faulted_)fail("A named RAM timer endpoint cannot resume a faulted helper prefix.");
        owner_.executing_=true;
    }
    ~Operation() {
        if(std::uncaught_exceptions()>exceptions_)owner_.faulted_=true;
        owner_.executing_=false;
    }
    Operation(const Operation&)=delete;
    Operation& operator=(const Operation&)=delete;
private:
    ModelRamTimerAccess& owner_;
    int exceptions_;
};
std::uint64_t model_world_time(const ModelSceneRam& ram) {
    const auto manager=ram.manager.lock();
    if(!manager)fail("Original world-time access dereferences the RAM's missing current manager.");
    const auto context=manager->context;
    if(!context)fail("Original world-time access dereferences the manager's null current context.");
    return context->world_time;
}
ModelRamTimerAccess::ModelRamTimerAccess(std::shared_ptr<ModelSceneRam> ram)
    :ModelRamTimerAccess(std::move(ram),model_world_time) {}
ModelRamTimerAccess::ModelRamTimerAccess(std::shared_ptr<ModelSceneRam> ram,ModelWorldClockReader clock)
    :ram_(std::move(ram)),clock_(std::move(clock)) {
    if(!ram_ || !clock_)fail("Actual RAM timer binding requires retained RAM and a real context-clock reader.");
}
std::uint64_t ModelRamTimerAccess::world_time() {
    const auto result=clock_(*ram_);
    // A provider might catch a recursive-call exception itself. Do not permit
    // the outer operation to publish after that endpoint has been poisoned.
    if(faulted_)fail("A caught recursive timer failure invalidated the active helper prefix.");
    return result;
}
bool ModelRamTimerAccess::exists(std::string_view name) {
    Operation operation(*this);
    return bool(find_model_named(ensure_model_data(*ram_),name));
}
std::shared_ptr<ModelSceneTimer> ModelRamTimerAccess::start(std::string_view name,std::int64_t end_time) {
    Operation operation(*this);
    auto record=find_model_named(ensure_model_data(*ram_),name);
    if(!record) {
        auto allocated=std::make_shared<ModelSceneTimer>();allocated->name=std::string(name);
        ensure_model_data(*ram_)->append(allocated);record=std::move(allocated);
    }
    // 8031E038..E05C reads context time before dereferencing timer scale. A
    // wrong-type name match still reaches that clock site; existence is untyped.
    const auto world=world_time();
    const auto timer=timer_fields(record);
    const auto scaled=original_scaled_motion_clock(world,timer->timer.time_scale);
    timer->timer.time_bias=std::bit_cast<std::int64_t>(std::uint64_t{0}-scaled);
    timer->timer.saved_world_time=world;
    timer->timer.cached_frames=1;
    timer->timer.end_time=end_time;
    return timer;
}
std::int64_t ModelRamTimerAccess::time_impl(std::string_view name) {
    const auto record=find_model_named(ensure_model_data(*ram_),name);
    const auto world=world_time();
    const auto timer=timer_fields(record);
    const auto scaled=original_scaled_motion_clock(world,timer->timer.time_scale);
    const auto bias=std::bit_cast<std::uint64_t>(timer->timer.time_bias);
    return std::bit_cast<std::int64_t>(scaled+bias);
}
std::int64_t ModelRamTimerAccess::time(std::string_view name) {
    Operation operation(*this);return time_impl(name);
}
float ModelRamTimerAccess::frames(std::string_view name) {
    Operation operation(*this);
    // This helper retains T; the nested complete time helper resolves its own
    // current RAM.data and can therefore observe a different named record.
    const auto timer=timer_fields(find_model_named(ensure_model_data(*ram_),name));
    const auto saved=timer->timer.saved_world_time;
    const auto world=world_time();
    if(saved==world)return timer->timer.cached_frames;
    const auto accumulated=time_impl(name);
    return publish_motion_timer_frames(timer->timer,accumulated,world);
}
} // namespace dw4
