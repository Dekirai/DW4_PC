#include "dw4/motion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace dw4 {
void MotionChannels::on_sequence_change() {
    if (!allocated()) return;
    auto candidate = states();
    for (std::size_t i=0;i<3;++i) if (candidate[i].stop_on_sequence) stop_motion(candidate[i]);
    candidate[3].active=false; candidate[3].speed=0;
    publish(candidate);
}
void MotionChannels::on_sequence_change_actual() {
    const auto first=list_owner();
    if(!first)throw MotionError("Sequence cleanup requires its actual movement list.");
    const auto count=first->size();
    if(count>4096)throw MotionError("Sequence movement cleanup exceeds its entry budget.");
    for(std::size_t i=0;i<count;++i) {
        const auto list=list_owner();
        if(!list)throw MotionError("Sequence cleanup lost its actual movement list.");
        const auto entry=list->entry_owner(i);
        if(!entry)throw MotionError("Sequence cleanup reached a null movement entry.");
        if(i==3) {entry->active=false;entry->speed=0;}
        else if(entry->stop_on_sequence)stop_motion(*entry);
    }
}
namespace {
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24);
[[noreturn]] void fail(const char* message) { throw MotionError(message); }
void finite(float value) {
    if (!std::isfinite(value)) fail("Motion parameters and arithmetic must remain finite.");
}
void validate_mode(MotionMode mode) {
    const auto number = static_cast<std::int32_t>(mode);
    if (number < 0 || number > 5) fail("Unsupported original motion mode.");
}
void validate(const MotionState& state) {
    validate_mode(state.mode);
    // Authored parameters are finite; derived runtime fields may contain IEEE
    // infinity/NaN, including after valid original zero-frame commands.
    for (const float value : {state.value,state.stop_value,state.stop_frames}) finite(value);
}
float acceleration(MotionSetup setup) {
    validate_mode(setup.mode);
    finite(setup.value); finite(setup.frames); finite(setup.end_time);
    const float squared = setup.frames * setup.frames;
    const float denominator = squared * 0.5f;
    return setup.value / denominator;
}
void configure(MotionState& state, MotionSetup setup) {
    const auto acc = acceleration(setup);
    state.mode = setup.mode; state.value = setup.value;
    state.acceleration = acc; state.end_time = setup.end_time;
    state.stop_on_sequence = false;
    if (setup.mode == MotionMode::impulse_positive) state.speed = setup.value;
    else if (setup.mode == MotionMode::impulse_negative) state.speed = -setup.value;
    state.active = true;
}
void stop(MotionState& state) {
    state.end_time = 0;
    if (state.mode == MotionMode::accelerate_positive || state.mode == MotionMode::accelerate_negative) {
        const auto mode = state.mode == MotionMode::accelerate_positive ? MotionMode::stop_positive : MotionMode::stop_negative;
        configure(state,{mode,state.stop_value,state.stop_frames,0});
    } else { state.active = false; state.speed = 0; }
    state.stop_on_sequence = false;
}
float advance(MotionState& state, float delta) {
    if (!state.active) return 0;
    if (state.end_time != 0) {
        const float remaining = state.end_time - delta;
        // 8030A324..8030A34C: fcmpo against 0 and bge stores the rest, so
        // only a rest below zero stops; an exactly reached 0 (or NaN) is
        // stored and reads as "no end" from then on. The KEIKATIMER frames
        // (0.9909 / 1.0066 on the NTSC clock) rarely land on it.
        if (remaining < 0) {
            stop(state);
            if (!state.active) return 0;
        } else state.end_time = remaining;
    }
    delta = std::min(delta,3.0f);
    const float change = delta * state.acceleration;
    float speed = state.speed;
    switch (state.mode) {
    case MotionMode::accelerate_positive:
        speed += change;
        if (speed > state.value) speed = state.value;
        break;
    case MotionMode::accelerate_negative:
        speed -= change;
        if (speed < -state.value) speed = -state.value;
        break;
    case MotionMode::stop_positive: case MotionMode::impulse_positive:
        speed -= change;
        if (speed < 0) { speed = 0; state.active = false; }
        break;
    case MotionMode::stop_negative: case MotionMode::impulse_negative:
        speed += change;
        if (speed > 0) { speed = 0; state.active = false; }
        break;
    }
    const float distance = speed * delta;
    state.speed = speed;
    return distance;
}
const IgbFieldValue& field(const IgbDocument& doc, const IgbObject& object,
    std::uint16_t slot, const char* type, std::size_t size) {
    const auto* value = doc.field(object,slot);
    if (!value || value->size != size || doc.metadata.field_types.at(value->type_index).name != type)
        fail("Unsupported serialized motion field.");
    return *value;
}
float number(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    field(doc,object,slot,"igFloatMetaField",4);
    const auto value = doc.field_f32(object,slot); finite(value); return value;
}
} // namespace

MotionChannels::MotionChannels() : MotionChannels(std::make_shared<MotionChannelList>()) {}
MotionChannels::MotionChannels(std::shared_ptr<MotionChannelList> actual_list)
    : slot_(std::make_shared<std::shared_ptr<MotionChannelList>>(std::move(actual_list))) { require_owner(); }
MotionChannels::MotionChannels(std::shared_ptr<std::shared_ptr<MotionChannelList>> actual_slot)
    :slot_(std::move(actual_slot)) { require_slot(); }
void MotionChannels::require_slot() const {
    if(!slot_)fail("Motion channels have transferred their movement reference slot.");
}
void MotionChannels::require_owner() const {
    require_slot();
    if (!*slot_) fail("Motion channels require their actual movement list owner.");
}
void MotionChannels::validate_shape() const {
    require_owner();
    const auto list=*slot_;
    const auto count=list->size();
    if (count!=0 && count!=4) fail("Movement list must be empty or contain exactly four channel objects.");
    for (std::size_t i=0;i<count;++i) {
        const auto entry=list->entry_owner(i);
        if (!entry) fail("Movement list contains a null channel object.");
        for (std::size_t j=0;j<i;++j)
            if (entry==list->entry_owner(j)) fail("Aliased movement channel entries are outside the supported list profile.");
    }
}
bool MotionChannels::allocated() const { validate_shape(); return (*slot_)->size()!=0; }
MotionChannels::States MotionChannels::states() const {
    validate_shape(); States result{};
    const auto list=*slot_;
    for (std::size_t i=0;i<list->size();++i) result[i]=*list->entry_owner(i);
    return result;
}
std::shared_ptr<MotionChannelList> MotionChannels::list_owner() { require_slot(); return *slot_; }
std::shared_ptr<const MotionChannelList> MotionChannels::list_owner() const { require_slot(); return *slot_; }
void MotionChannels::publish(const States& values) {
    validate_shape();
    const auto list=*slot_;
    auto& entries=list->mutable_entries();
    if (entries.empty()) {
        MotionChannelList::Entries created; created.reserve(values.size());
        for (const auto& value:values) created.push_back(std::make_shared<MotionState>(value));
        entries=std::move(created);
    } else {
        for (std::size_t i=0;i<values.size();++i) *entries[i]=values[i];
    }
}
void MotionChannels::publish_from(const MotionChannels& other) {
    other.require_slot();
    if(!slot_) {
        MotionChannels restored(other);slot_=std::move(restored.slot_);return;
    }
    const auto source_list=*other.slot_,destination=*slot_;
    if (destination==source_list) return;
    if(!destination || !source_list)fail("Movement publication cannot replace an actual list reference.");
    constexpr std::size_t copy_limit=4096;
    if (source_list->size()>copy_limit || destination->size()>copy_limit)
        fail("Movement list snapshot exceeds 4096 retained entries.");
    MotionChannelList::Entries entries;entries.reserve(source_list->size());
    std::vector<std::pair<std::shared_ptr<MotionState>,MotionState>> writes;
    writes.reserve(source_list->size());
    std::unordered_map<const MotionState*,std::shared_ptr<MotionState>> aliases;
    std::unordered_set<const MotionState*> reused;
    for (std::size_t i=0;i<source_list->size();++i) {
        const auto source=source_list->entry_owner(i);
        if (!source) { entries.push_back({}); continue; }
        if (const auto found=aliases.find(source.get());found!=aliases.end()) {
            entries.push_back(found->second);continue;
        }
        auto entry=i<destination->size() ? destination->entry_owner(i) : nullptr;
        if (!entry || reused.contains(entry.get())) entry=std::make_shared<MotionState>();
        reused.insert(entry.get());aliases.emplace(source.get(),entry);
        writes.emplace_back(entry,*source);entries.push_back(std::move(entry));
    }
    // Every allocation and source value capture precedes publication. In
    // particular, source/destination lists may share some entry allocations.
    for (const auto& [entry,value]:writes) *entry=value;
    destination->mutable_entries()=std::move(entries);
}
MotionChannels::MotionChannels(const MotionChannels& other)
    :slot_(std::make_shared<std::shared_ptr<MotionChannelList>>()) {
    other.require_slot();
    if(*other.slot_) *slot_=std::make_shared<MotionChannelList>();
    publish_from(other);
}
MotionChannels& MotionChannels::operator=(const MotionChannels& other) {
    publish_from(other);return *this;
}
MotionChannels& MotionChannels::operator=(MotionChannels&& other) {
    // Moving a candidate into a live work publishes its values, never rebinds
    // the destination to the detached candidate's list or channel allocations.
    return operator=(static_cast<const MotionChannels&>(other));
}

void configure_motion(MotionState& state, MotionSetup setup) {
    validate(state); auto candidate = state; configure(candidate,setup); state = candidate;
}
void stop_motion(MotionState& state) {
    validate(state); auto candidate = state; stop(candidate); state = candidate;
}
float advance_motion(MotionState& state, float delta_frames) {
    validate(state); finite(delta_frames);
    auto candidate = state; const auto distance = advance(candidate,delta_frames); state = candidate; return distance;
}
float advance_motion_actual(MotionState& state,float delta_frames) {
    if(!state.active)return 0;
    finite(delta_frames);
    return advance(state,delta_frames);
}

MotionCommand decode_motion_command(const IgbDocument& doc, std::uint32_t reference) {
    if (doc.metadata.header.byte_order != IgbByteOrder::little) fail("Big-endian motion records are unverified.");
    const auto& object = doc.object(reference);
    const auto& type = doc.metadata.object_types.at(object.type_index).name;
    if (type != "beModelCtrlMOVE" && type != "beModelCtrlMOVEEX") fail("Expected exact MOVE or MOVEEX object.");
    const auto* name = doc.field(object,2);
    if (!name || name->size < 4 || name->size > 72 ||
        doc.metadata.field_types.at(name->type_index).name != "igStringMetaField") fail("Unsupported motion command name schema.");
    const auto text = doc.field_string(object,2);
    if (text.size() > 64 || text.find('\0') != std::string_view::npos) fail("Invalid motion command name.");
    MotionCommand result; result.reference_index = reference; result.type = type; result.name = text;
    result.setup.value = number(doc,object,3); result.setup.frames = number(doc,object,4);
    if (type == "beModelCtrlMOVE") {
        if (text == "Move") { result.channel = 2; result.setup.mode = MotionMode::accelerate_positive; }
        else if (text == "Stop") { result.channel = 2; result.setup.mode = MotionMode::stop_positive; }
        else if (text == "Push") { result.channel = 2; result.setup.mode = MotionMode::impulse_positive; }
        else if (text == "Back") { result.channel = 2; result.setup.mode = MotionMode::impulse_negative; }
        else if (text == "Jump") { result.channel = 1; result.setup.mode = MotionMode::impulse_positive; }
        else if (text == "Fall") { result.channel = 1; result.setup.mode = MotionMode::accelerate_negative; }
    } else {
        if (text.size() < 4) fail("MOVEEX control code needs four bytes.");
        const int channel = static_cast<unsigned char>(text[1]) - 'X' + (text[0] == 'T' ? 0 : 2);
        if (channel < 0 || channel > 3) fail("MOVEEX control code selects an invalid channel.");
        int mode;
        if (text[3] == 'A') mode = 0;
        else if (text[3] == 'D') mode = 2;
        else if (text[3] == 'T') mode = 4;
        else fail("MOVEEX control code has an unverified mode.");
        if (text[2] == 'M') ++mode;
        result.channel = static_cast<std::uint32_t>(channel);
        result.setup.mode = static_cast<MotionMode>(mode);
        result.setup.end_time = number(doc,object,5);
        const auto& flag = field(doc,object,6,"igBoolMetaField",1);
        const auto enabled = doc.field_bytes(flag)[0];
        if (enabled > 1) fail("Unsupported MOVEEX sequence-stop flag.");
        result.stop = MotionStop{enabled != 0,number(doc,object,7),number(doc,object,8)};
    }
    return result;
}

void MotionChannels::apply(const MotionCommand& command) {
    if (command.type != "beModelCtrlMOVE" && command.type != "beModelCtrlMOVEEX") fail("Unsupported native motion command type.");
    auto candidate = states();
    if (command.channel) {
        if (*command.channel >= candidate.size()) fail("Motion command channel is outside the four-channel set.");
        auto& state = candidate[*command.channel]; configure_motion(state,command.setup);
        if (command.stop) {
            finite(command.stop->value); finite(command.stop->frames);
            state.stop_on_sequence = command.stop->on_sequence;
            state.stop_value = command.stop->value; state.stop_frames = command.stop->frames;
        }
    }
    publish(candidate);
}
void MotionChannels::allocate() {
    if (!allocated()) publish(States{});
}
MotionStep MotionChannels::advance(float delta_frames) {
    finite(delta_frames); MotionStep result;
    if (!allocated()) return result;
    auto candidate = states();
    if (candidate[3].active) result.rotation = advance_motion(candidate[3],delta_frames);
    result.translation_updated = candidate[0].active || candidate[1].active || candidate[2].active;
    if (result.translation_updated) for (std::size_t i = 0; i < 3; ++i)
        result.translation[i] = advance_motion(candidate[i],delta_frames);
    publish(candidate); return result;
}
MotionStep MotionChannels::advance(MotionTimer& timer, std::uint64_t world_time) {
    if (!allocated()) return {}; // Original empty channel list does not query the timer.
    auto candidate = timer;
    const auto result = advance(motion_timer_frames(candidate,world_time));
    timer = candidate; return result;
}
namespace {
void turn_finite(float value) {
    if (!std::isfinite(value)) throw MotionError("Model motion cannot publish a non-finite pose.");
}
float euler_single(double value) {
    // Original frsp checks tininess before rounding the returned double trig
    // value. Trig itself remains the explicitly shared host-libm boundary.
    return std::abs(value)<std::numeric_limits<float>::min() ?
        std::copysign(0.0f,static_cast<float>(value)) : static_cast<float>(value);
}
float euler_product(float a,float b) {
    const float rounded=a*b;
    const float magnitude=std::abs(rounded),minimum=std::numeric_limits<float>::min();
    if (magnitude<minimum || (magnitude==minimum &&
        std::abs(static_cast<double>(a)*static_cast<double>(b))<minimum))
        return std::copysign(0.0f,rounded);
    return rounded;
}
float euler_fused(float a,float b,float addend) {
    const float rounded=std::fma(a,b,addend);
    const float magnitude=std::abs(rounded),minimum=std::numeric_limits<float>::min();
    if (magnitude<minimum) return std::copysign(0.0f,rounded);
    if (magnitude!=minimum) return rounded;
    // A binary32 product is exact in double. TwoSum preserves a deficit that
    // a double-only sum would lose at the pre-round min-normal boundary.
    const double product=static_cast<double>(a)*static_cast<double>(b);
    const double sum=product+static_cast<double>(addend);
    const double virtual_addend=sum-product;
    const double error=(product-(sum-virtual_addend))+(static_cast<double>(addend)-virtual_addend);
    if (std::abs(sum)<minimum || (std::abs(sum)==minimum &&
        ((sum>0 && error<0) || (sum<0 && error>0))))
        return std::copysign(0.0f,rounded);
    return rounded;
}
void euler_basis(std::array<float,16>& matrix, const std::array<float,3>& angles) {
    for (const auto value : angles) turn_finite(value);
    for (std::size_t i=12;i<15;++i) turn_finite(matrix[i]);
    // 801292D4 special-cases exact zero before calling double sin/cos. Keep
    // those branches, their positive-zero products and the original FMA order.
    const float sx = angles[0] == 0 ? 0 : euler_single(std::sin(static_cast<double>(angles[0])));
    const float cx = angles[0] == 0 ? 1 : euler_single(std::cos(static_cast<double>(angles[0])));
    const float sy = angles[1] == 0 ? 0 : euler_single(std::sin(static_cast<double>(angles[1])));
    const float cy = angles[1] == 0 ? 1 : euler_single(std::cos(static_cast<double>(angles[1])));
    const float sxsy = angles[1] == 0 ? 0 : euler_product(sx,sy);
    const float sz = angles[2] == 0 ? 0 : euler_single(std::sin(static_cast<double>(angles[2])));
    const float cz = angles[2] == 0 ? 1 : euler_single(std::cos(static_cast<double>(angles[2])));
    const float cxsz = angles[2] == 0 ? 0 : euler_product(cx,sz);
    const float cxcz=euler_product(cx,cz),sxsz=euler_product(sx,sz),sxcz=euler_product(sx,cz);
    matrix[0]=euler_product(cy,cz);matrix[1]=euler_product(cy,sz);matrix[2]=-sy;
    matrix[4]=euler_fused(sxsy,cz,-cxsz);
    matrix[5]=euler_fused(sxsy,sz,cxcz);matrix[6]=euler_product(sx,cy);
    matrix[8]=euler_fused(cxcz,sy,sxsz);
    matrix[9]=euler_fused(cxsz,sy,-sxcz);matrix[10]=euler_product(cx,cy);
    // 80129134 resets these components from globals initialized at 8012F57C;
    // 8030C85C then restores only translation XYZ with 80128D90.
    matrix[3] = 0; matrix[7] = 0; matrix[11] = 0; matrix[15] = 1;
    for (const auto value : matrix) turn_finite(value);
}
} // namespace

void original_model_turn_basis(std::array<float,16>& matrix, const std::array<float,3>& angles) {
    euler_basis(matrix,angles);
}
void translate_model_local(std::array<float,16>& matrix, const std::array<float,3>& translation) {
    for (const auto value : matrix) finite(value);
    for (const auto value : translation) finite(value);
    auto candidate = matrix;
    for (std::size_t row = 0; row < 4; ++row) {
        const float y = matrix[4+row] * translation[1]; finite(y);
        const float xy = std::fma(matrix[row],translation[0],y); finite(xy);
        const float xyz = std::fma(matrix[8+row],translation[2],xy); finite(xyz);
        candidate[12+row] = matrix[12+row] + xyz; finite(candidate[12+row]);
    }
    matrix = candidate;
}
} // namespace dw4
