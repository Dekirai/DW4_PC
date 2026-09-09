#include "dw4/motion.hpp"
#include "script_fixtures.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
int checks{};
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void near(float actual, float expected, const char* message) { check(std::abs(actual-expected)<0.000002f,message); }
template<class F> void rejects(F action) {
    ++checks; try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid motion input was accepted.");
}
void scalar_modes() {
    MotionState state;
    check(advance_motion(state,1)==0 && !state.active,"Inactive channel moved.");
    for (int mode = 0; mode < 6; ++mode) {
        state = {}; state.speed = mode%2==0 ? 1.0f : -1.0f;
        state.stop_value = 2; state.stop_frames = 4; state.stop_on_sequence = true;
        configure_motion(state,{static_cast<MotionMode>(mode),2,2,0});
        check(state.active && !state.stop_on_sequence && state.acceleration==1 &&
            state.stop_value==2 && state.stop_frames==4,"Configure lost stored stop parameters.");
        const float sign = mode%2==0 ? 1.0f : -1.0f;
        check(state.speed==(mode>=4 ? 2*sign : sign),"Configure did not preserve or replace speed for the original mode.");
        if (mode < 2) {
            check(advance_motion(state,1)==2*sign && state.speed==2*sign,"Acceleration does not integrate updated capped speed.");
            check(advance_motion(state,1)==2*sign && state.active,"Capped velocity deactivated or kept increasing.");
        } else {
            if (mode>=4) check(advance_motion(state,1)==sign,"Impulse did not decay before its first displacement.");
            check(advance_motion(state,1)==0 && state.active,"Exact zero incorrectly deactivated the channel.");
            check(advance_motion(state,1)==0 && !state.active,"Strict zero crossing failed to deactivate.");
        }
    }
    state = {}; configure_motion(state,{MotionMode::accelerate_positive,0.7f,6,0});
    float distance = 0;
    for (int i=0;i<18;++i) distance += advance_motion(state,1);
    near(state.speed,0.7f,"Original Agumon maximum speed differs.");
    near(distance,6.65f,"Original Agumon acceleration travel differs.");
    configure_motion(state,{MotionMode::stop_positive,1,4,0});
    float stop_distance = 0;
    for (int i=0;i<6;++i) stop_distance += advance_motion(state,1);
    near(stop_distance,1.625f,"Original Agumon stop travel differs.");
    check(!state.active && state.speed==0,"Agumon did not finish stopping.");

    MotionState full, split;
    configure_motion(full,{MotionMode::accelerate_positive,2,2,0}); split=full;
    const auto full_distance=advance_motion(full,1);
    const auto split_distance=advance_motion(split,0.5f)+advance_motion(split,0.5f);
    check(full.speed==split.speed && full_distance==1 && split_distance==0.75f,
        "Original integration was replaced by subdivision-invariant host physics.");
    state={}; configure_motion(state,{MotionMode::accelerate_positive,100,10,0});
    check(advance_motion(state,20)==18 && state.speed==6,"Delta was not capped at three before acceleration/displacement.");
    check(advance_motion(state,-1)==-4 && state.speed==4,"Signed original delta was silently clamped away.");
    state={}; state.speed=-2; configure_motion(state,{MotionMode::stop_positive,1,2,0});
    check(advance_motion(state,0)==0 && !state.active,"Zero delta must still run the original sign-crossing test.");
}
void expiry_and_failure() {
    MotionState state;
    state.stop_value=1; state.stop_frames=2;
    configure_motion(state,{MotionMode::accelerate_positive,2,2,1});
    check(advance_motion(state,1)==1 && state.end_time==0,"Exact-zero expiry incorrectly invoked stop.");
    check(advance_motion(state,1)==2 && state.active && state.mode==MotionMode::accelerate_positive,
        "Zero expiry sentinel was treated as an expired timer on the next update.");
    configure_motion(state,{MotionMode::accelerate_positive,2,2,4});
    check(advance_motion(state,5)==1.5f && state.mode==MotionMode::stop_positive && state.speed==0.5f && state.end_time==0,
        "Expiry must use the raw delta before its movement clamp, then integrate the new stop mode.");
    state={}; state.stop_value=1; state.stop_frames=2;
    configure_motion(state,{MotionMode::accelerate_negative,2,2,0}); advance_motion(state,1);
    stop_motion(state);
    check(state.mode==MotionMode::stop_negative && state.speed==-1 && state.active,"Negative stop transition lost velocity.");
    check(advance_motion(state,1)==-0.5f,"Negative stop mode accelerated in the wrong direction.");
    for (int mode=2;mode<6;++mode) {
        state={}; configure_motion(state,{static_cast<MotionMode>(mode),2,2,0.5f});
        state.speed=1; state.stop_on_sequence=true;
        check(advance_motion(state,1)==0 && !state.active && state.speed==0 && !state.stop_on_sequence,
            "Expiring non-acceleration mode did not stop immediately.");
    }
    state={}; configure_motion(state,{MotionMode::accelerate_positive,1,2,0});
    const auto saved=state;
    for (const float invalid : {std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        rejects([&]{configure_motion(state,{MotionMode::accelerate_positive,1,invalid,0});});
        check(state==saved,"Invalid setup changed the previous state.");
    }
    rejects([&]{configure_motion(state,{static_cast<MotionMode>(6),1,2,0});});
    rejects([&]{advance_motion(state,std::numeric_limits<float>::infinity());});
    check(state==saved,"Invalid mode/delta changed the previous state.");
    state={}; state.speed=std::numeric_limits<float>::max();
    configure_motion(state,{MotionMode::accelerate_positive,std::numeric_limits<float>::max(),2,0});
    check(std::isinf(advance_motion(state,3)) && state.speed==state.value,
        "Intermediate speed overflow was rejected before the original finite cap.");
}
void zero_frame_arithmetic() {
    for (int mode=0;mode<6;++mode) {
        MotionState state; state.speed=mode%2==0 ? 1.0f : -1.0f;
        configure_motion(state,{static_cast<MotionMode>(mode),2,0,0});
        check(std::isinf(state.acceleration) && state.acceleration>0,
            "Valid zero-frame command lost its original infinite acceleration.");
        const auto distance=advance_motion(state,0.5f);
        const float sign=mode%2==0 ? 1.0f : -1.0f;
        check(mode<2 ? distance==sign && state.speed==2*sign && state.active : distance==0 && !state.active,
            "Positive delta did not apply the original zero-frame cap or crossing.");

        state={}; configure_motion(state,{static_cast<MotionMode>(mode),2,0,0});
        check(std::isnan(advance_motion(state,0)) && std::isnan(state.speed) && state.active,
            "Unordered zero-times-infinity triggered a false clamp or crossing.");
        check(std::isnan(advance_motion(state,1)) && state.active,
            "Subsequent tick silently repaired the original NaN runtime state.");
        configure_motion(state,{MotionMode::impulse_positive,2,2,0});
        check(advance_motion(state,1)==1,"An impulse did not replace the previous NaN speed.");
    }
    MotionState state; configure_motion(state,{MotionMode::accelerate_positive,1,0,0.5f});
    check(std::isnan(advance_motion(state,1)) && state.active && state.mode==MotionMode::stop_positive && state.end_time==0,
        "Expiry with the original zero-initialized stop parameters did not preserve 0/0.");
    stop_motion(state);
    check(!state.active && state.speed==0,"Immediate stop did not clear an unordered runtime velocity.");
    configure_motion(state,{MotionMode::accelerate_negative,2,-0.0f,0});
    check(advance_motion(state,-1)==-std::numeric_limits<float>::infinity() && state.active,
        "Signed delta on zero-frame acceleration was replaced by an instant finite fallback.");
    state={}; configure_motion(state,{MotionMode::accelerate_positive,0,0,0});
    check(std::isnan(state.acceleration) && std::isnan(advance_motion(state,1)),"Zero value/frame was incorrectly normalized.");
    state={}; configure_motion(state,{MotionMode::accelerate_positive,1,std::numeric_limits<float>::denorm_min(),0});
    check(advance_motion(state,1)==1,"Underflow of the squared frame parameter lost its zero-denominator behavior.");
    state={}; state.speed=0.5f;
    configure_motion(state,{MotionMode::accelerate_positive,1,std::numeric_limits<float>::max(),0});
    check(state.acceleration==0 && advance_motion(state,1)==0.5f,"Infinite squared frames did not produce zero acceleration.");
    state={}; configure_motion(state,{MotionMode::accelerate_positive,2,-2,0});
    check(state.acceleration==1 && advance_motion(state,1)==1,"Signed frames were not squared as in the original.");
}
struct Fixture : dw4::test::ScriptFixture {
    std::uint32_t command{}, storage{};
    bool extended{};
    Fixture(std::string name, bool motion=false, float value=2, float frames=2) : extended(motion) {
        command=add(dw4::test::named_object); text(command,2,name);
        storage=memory(dw4::test::words({std::bit_cast<std::uint32_t>(value),std::bit_cast<std::uint32_t>(frames),
            std::bit_cast<std::uint32_t>(4.0f),1,std::bit_cast<std::uint32_t>(1.0f),std::bit_cast<std::uint32_t>(2.0f)}));
    }
    IgbDocument document() const {
        auto doc=ScriptFixture::document();
        doc.metadata.object_types[dw4::test::named_object].name=extended ? "beModelCtrlMOVEEX" : "beModelCtrlMOVE";
        doc.metadata.field_types[8].name="igFloatMetaField";
        auto& object=doc.objects.at(doc.references.at(command).value_index);
        const auto offset=doc.memories.at(doc.references.at(storage).value_index).offset;
        object.fields.push_back({3,8,offset,4}); object.fields.push_back({4,8,offset+4,4});
        if (extended) {
            object.fields.push_back({5,8,offset+8,4}); object.fields.push_back({6,4,offset+12,1});
            object.fields.push_back({7,8,offset+16,4}); object.fields.push_back({8,8,offset+20,4});
        }
        return doc;
    }
    MotionCommand decode() const { const auto doc=document(); return decode_motion_command(doc,command); }
};
void commands_and_channels() {
    const std::array names{"Move","Stop","Push","Back","Jump","Fall"};
    const std::array modes{0,2,4,5,4,1};
    for (std::size_t i=0;i<names.size();++i) {
        const auto command=Fixture(names[i]).decode();
        check(command.channel==(i<4 ? 2u : 1u) && static_cast<int>(command.setup.mode)==modes[i] &&
            command.setup.end_time==0 && !command.stop,"MOVE command routing differs from the original branches.");
    }
    for (const auto name : {"move","UNKNOWN",""}) {
        MotionChannels channels; const auto command=Fixture(name,false,0,0).decode();
        check(!command.channel,"Unknown exact MOVE name became a guessed command.");
        channels.apply(command);
        check(channels.allocated() && !channels.advance(1).translation_updated,"Verified MOVE no-op activated a channel.");
    }
    const std::array extended_names{"TXPA","TYMA","TZPD","RYMD","TXPT","TYMT"};
    const std::array axes{0u,1u,2u,3u,0u,1u};
    for (std::size_t i=0;i<extended_names.size();++i) {
        const auto command=Fixture(extended_names[i],true).decode();
        check(command.channel==axes[i] && static_cast<int>(command.setup.mode)==static_cast<int>(i) &&
            command.setup.end_time==4 && command.stop && command.stop->on_sequence && command.stop->value==1,
            "MOVEEX code/stop parameters decoded incorrectly.");
    }
    for (const auto name : {"T","TZPQ","RZPA","TWPA"}) rejects([&]{(void)Fixture(name,true).decode();});
    rejects([&]{(void)Fixture(std::string(65,'x')).decode();});
    MotionChannels instant; instant.apply(Fixture("Move",false,1,0).decode());
    check(instant.advance(1).translation[2]==1,"Serialized zero-frame MOVE was rejected or failed to cap speed.");
    instant.apply(Fixture("TXMACC",true,0.5f,0).decode());
    check(instant.advance(1).translation[0]==-0.5f,"Serialized zero-frame MOVEEX was decoded differently from MOVE.");
    rejects([&]{(void)Fixture("Move",false,std::numeric_limits<float>::infinity()).decode();});
    auto doc=Fixture("Move").document();
    doc.metadata.header.byte_order=IgbByteOrder::big; rejects([&]{decode_motion_command(doc,0);});
    doc=Fixture("Move").document(); doc.metadata.object_types[dw4::test::named_object].name="DerivedMOVE";
    rejects([&]{decode_motion_command(doc,0);});
    doc=Fixture("Move").document(); doc.objects[0].fields.pop_back(); rejects([&]{decode_motion_command(doc,0);});
    doc=Fixture("Move").document(); rejects([&]{decode_motion_command(doc,999);});
    doc=Fixture("Move").document(); doc.metadata.field_types[8].name="igUnsignedIntMetaField";
    rejects([&]{decode_motion_command(doc,0);});
    doc=Fixture("Move").document(); doc.objects[0].fields.back().size=3;
    rejects([&]{decode_motion_command(doc,0);});
    doc=Fixture("TXPA",true).document(); doc.objects[0].fields[doc.objects[0].fields.size()-3].size=4;
    rejects([&]{decode_motion_command(doc,0);});
    Fixture bad_flag("TXPA",true);
    bad_flag.records.at(bad_flag.storage).data[12]=2;
    rejects([&]{bad_flag.decode();});
    for (const std::size_t offset : {8u,16u,20u}) {
        Fixture invalid_number("TXPA",true);
        dw4::test::word(invalid_number.records.at(invalid_number.storage).data,offset,0x7f800000u);
        rejects([&]{invalid_number.decode();});
    }
    doc=Fixture("Move").document(); doc.objects[0].fields.back().offset=doc.bytes().size();
    rejects([&]{decode_motion_command(doc,0);});

    MotionChannels channels;
    channels.apply(Fixture("TXPA",true).decode()); channels.apply(Fixture("TYMA",true).decode());
    channels.apply(Fixture("Move").decode()); channels.apply(Fixture("RYPA",true).decode());
    const auto step=channels.advance(1);
    check(step.translation==std::array<float,3>{1,-1,1} && step.rotation==1 && step.translation_updated,
        "Independent local channels did not advance with the same frame delta.");
    auto copy=channels; channels.apply(Fixture("Stop").decode());
    check(copy.states()[2].mode==MotionMode::accelerate_positive && channels.states()[2].mode==MotionMode::stop_positive,
        "Copied actor channels share mutable state.");
    const auto saved=channels.states(); auto invalid=Fixture("Move").decode(); invalid.channel=4;
    rejects([&]{channels.apply(invalid);}); check(channels.states()==saved,"Bad channel changed an actor.");
    invalid.channel=2; invalid.setup.frames=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{channels.apply(invalid);}); check(channels.states()==saved,"Failed command changed an actor.");
    auto bad_stop=Fixture("TXPA",true).decode(); bad_stop.stop->frames=std::numeric_limits<float>::infinity();
    rejects([&]{channels.apply(bad_stop);});
    check(channels.states()==saved,"Failed stop-parameter validation published a partial setup.");
    rejects([&]{channels.advance(std::numeric_limits<float>::quiet_NaN());});
    check(channels.states()==saved,"A failed tick changed actor channels.");
}
void local_matrix() {
    std::array<float,16> matrix{0,0,-2,0, 0,3,0,0, 4,0,0,0, 10,20,30,1};
    translate_model_local(matrix,{1,2,3});
    check(matrix[12]==22 && matrix[13]==26 && matrix[14]==28 && matrix[15]==1,
        "Local translation ignored original rotation/scale or used row vectors.");
    matrix={1,0,0,2, 0,1,0,3, 0,0,1,4, 0,0,0,5};
    translate_model_local(matrix,{1,2,3});
    check(matrix[15]==25,"Original fourth translation component was dropped.");
    // Fused multiply-add keeps the tiny product residual after cancellation.
    matrix={}; matrix[0]=1.0f+0x1p-23f; matrix[4]=-1; matrix[15]=1;
    translate_model_local(matrix,{1.0f-0x1p-23f,1,0});
    check(matrix[12]==-0x1p-46f,"Original fused X accumulation was replaced by separate rounding.");
    const auto saved=matrix;
    rejects([&]{translate_model_local(matrix,{std::numeric_limits<float>::infinity(),0,0});});
    check(matrix==saved,"Invalid local displacement changed the matrix.");
    matrix[4]=std::numeric_limits<float>::max(); const auto before=matrix;
    rejects([&]{translate_model_local(matrix,{0,2,0});});
    check(matrix==before,"Overflow changed part of the model matrix.");
}
void timers() {
    constexpr std::uint64_t quantum=1u<<18;
    MotionTimer timer; start_motion_timer(timer,7*quantum,2*quantum);
    check(motion_timer_frames(timer,7*quantum)==1 && motion_timer_time(timer,7*quantum)==0,
        "Original timer start must cache one frame at its initial timestamp.");
    check(!motion_timer_expired(timer,9*quantum) && motion_timer_expired(timer,10*quantum),
        "Timer expiry comparison must be strictly greater than end time.");
    check(motion_timer_time(timer,8*quantum-1)==0 && motion_timer_time(timer,8*quantum)==static_cast<std::int64_t>(quantum),
        "Timer lost its 18-bit coarse quantization.");
    const auto delta=motion_timer_frames(timer,8*quantum);
    near(delta,0.01572864f,"Quantized elapsed timer conversion changed.");
    check(motion_timer_frames(timer,8*quantum)==delta,"Repeated world timestamp consumed another elapsed value.");
    check(motion_timer_frames(timer,8*quantum+1)==0,"Subquantum new timestamp did not cache zero.");
    check(motion_timer_frames(timer,1000*quantum)==3,"Timer did not cap a long elapsed update.");
    check(timer.previous_time==993*static_cast<std::int64_t>(quantum),"Capped time was left pending to catch up later.");
    near(motion_timer_frames(timer,1001*quantum),delta,"Long frame clamp introduced a backlog.");
    timer.time_scale=0;
    check(motion_timer_time(timer,1002*quantum)==-7*static_cast<std::int64_t>(quantum),"Timer scale/bias was replaced by a host stopwatch.");
    timer.time_scale=-1;
    check(motion_timer_time(timer,quantum)==-8*static_cast<std::int64_t>(quantum),"Signed timer scale was discarded.");
    timer={}; start_motion_timer(timer,0);
    check(motion_timer_time(timer,(std::uint64_t{1}<<50)+quantum)==static_cast<std::int64_t>(quantum),
        "Original truncated coarse-clock wrap was replaced by full precision time.");
    check(motion_timer_time(timer,((std::uint64_t{1}<<24)+1)*quantum)==static_cast<std::int64_t>((std::uint64_t{1}<<24)*quantum),
        "Original float rounding of coarse integer time disappeared.");
    timer.previous_time=123; timer.time_scale=0.5f; start_motion_timer(timer,8*quantum);
    check(timer.previous_time==123 && timer.time_scale==0.5f && timer.cached_frames==1,
        "Restart overwrote fields that the original start function preserves.");
    const auto before=timer; timer.time_scale=std::numeric_limits<float>::infinity();
    rejects([&]{start_motion_timer(timer,quantum);});
    check(timer.previous_time==before.previous_time && timer.time_bias==before.time_bias,"Bad timer start partially reset its state.");
    timer={}; timer.time_scale=std::numeric_limits<float>::max();
    check(motion_timer_time(timer,2*quantum)==-static_cast<std::int64_t>(quantum),
        "Positive clock overflow did not saturate before the original wrapping left shift.");
    timer.time_scale=-std::numeric_limits<float>::max();
    check(motion_timer_time(timer,2*quantum)==0,"Negative clock overflow did not saturate before the wrapping shift.");
    timer={}; timer.previous_time=1;
    check(motion_timer_frames(timer,1)<0 && timer.previous_time==0,"Negative elapsed time was silently discarded.");
    timer={}; timer.previous_time=(std::int64_t{1}<<62)+(std::int64_t{1}<<38)+1;
    check(std::bit_cast<std::uint32_t>(motion_timer_frames(timer,1))==0xd280d959u,
        "Signed elapsed conversion skipped original double rounding before float (direct conversion gives d280d95a).");

    MotionChannels empty; MotionTimer untouched; untouched.saved_world_time=7;
    const auto old=untouched; check(!empty.advance(untouched,99).translation_updated && untouched==old,
        "An unallocated movement list queried its timer.");
    MotionChannels channels; channels.apply(Fixture("Move").decode());
    timer={}; start_motion_timer(timer,5*quantum);
    check(channels.advance(timer,5*quantum).translation[2]==1 && timer.cached_frames==1,
        "Motion channels did not use the original first cached frame.");
    timer.time_scale=std::numeric_limits<float>::infinity();
    const auto saved_timer=timer; const auto saved_channels=channels.states();
    rejects([&]{channels.advance(timer,6*quantum);});
    check(timer==saved_timer && channels.states()==saved_channels,"Failed actor tick partially advanced timer or channels.");
}
} // namespace
int main() {
    try { scalar_modes(); expiry_and_failure(); zero_frame_arithmetic(); commands_and_channels(); local_matrix(); timers();
        std::cout << "Motion tests passed: " << checks << " checks\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "Motion test failure after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
