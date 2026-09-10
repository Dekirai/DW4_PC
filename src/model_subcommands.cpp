#include "dw4/model_subcommands.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dw4 {
namespace {
constexpr std::size_t max_entries=4096, max_name_bytes=1048576;
void finite(float value) {
    if(!std::isfinite(value)) throw std::invalid_argument("Model subcommand numeric input must be finite.");
}
float elapsed_sum(float a,float b) {
    const float rounded=a+b;
    // Scalar fadds under startup NI=1/RN=00 retains subnormal inputs but
    // flushes a tiny result to signed zero. Tiny sums of two binary32 inputs
    // are exactly representable, so no pre-round min-normal correction is needed.
    return std::abs(rounded)<std::numeric_limits<float>::min() ?
        std::copysign(0.0f,rounded) : rounded;
}
void name_size(const std::optional<std::string>& value) {
    if(value && value->size()>max_name_bytes)
        throw std::invalid_argument("Model subcommand name exceeds the size limit.");
}
ModelSubcommandResult needs(ModelSubcommandStop stop,const ModelSubcommandEntry& entry) {
    return {stop,entry.id};
}
} // namespace

void ModelSubcommandQueue::require_usable() const {
    if(processing_) throw std::logic_error("Recursive model subcommand operation is unsupported.");
    if(faulted_) throw std::logic_error("Model subcommand owner has an uncertain service outcome; automatic replay is forbidden.");
}
void ModelSubcommandQueue::require_appendable() const {
    if(faulted_) throw std::logic_error("Cannot append to a faulted model subcommand owner.");
    if(operation_==Operation::cleanup)
        throw std::logic_error("Cannot append during model subcommand cleanup.");
    if(entries_.size()>=max_entries || next_id_==std::numeric_limits<std::uint64_t>::max())
        throw std::length_error("Model subcommand queue limit exceeded.");
}
void ModelSubcommandQueue::transfer(ModelSubcommandQueue&& other) {
    if(other.processing_) throw std::logic_error("Cannot move an executing model subcommand owner.");
    entries_=std::move(other.entries_);
    next_id_=other.next_id_; operation_=other.operation_;
    cursor_=other.cursor_; remaining_=other.remaining_; frame_delta_=other.frame_delta_;
    context_=std::move(other.context_); current_advanced_=other.current_advanced_;
    current_start_=other.current_start_; faulted_=other.faulted_;
    other.entries_.clear(); other.operation_=Operation::none;
    other.cursor_=other.remaining_=0; other.current_advanced_=other.current_start_=false;
    other.faulted_=false;
}
ModelSubcommandQueue::ModelSubcommandQueue(ModelSubcommandQueue&& other) { transfer(std::move(other)); }
ModelSubcommandQueue& ModelSubcommandQueue::operator=(ModelSubcommandQueue&& other) {
    if(this==&other) return *this;
    require_usable();
    if(!entries_.empty() || pending())
        throw std::logic_error("Model subcommand destination requires cleanup before replacement.");
    transfer(std::move(other)); return *this;
}
std::uint64_t ModelSubcommandQueue::append_sound(float delay,std::optional<std::string> name) {
    require_appendable(); finite(delay); name_size(name);
    const auto id=next_id_;
    entries_.push_back({id,0,delay,false,ModelSoundSubcommandState{std::move(name),0}});
    ++next_id_; return id;
}
std::uint64_t ModelSubcommandQueue::append_effect(ModelEffectSubcommand command) {
    require_appendable(); finite(command.delay);
    name_size(command.model_name); name_size(command.sequence_name);
    for(float value:command.offset) finite(value);
    for(float value:command.rotation) finite(value);
    const auto id=next_id_;
    const float delay=command.delay;
    entries_.push_back({id,0,delay,false,ModelEffectSubcommandState{std::move(command),-1,false}});
    ++next_id_; return id;
}
ModelSubcommandResult ModelSubcommandQueue::update(float frames,ModelSubcommandContext context,
    const ModelSubcommandServices& services) {
    require_usable();
    if(pending()) throw std::logic_error("Resume or clean up the pending model subcommand pass before updating.");
    finite(frames);
    // Reject a non-finite native boundary before any entry or service is changed.
    for(const auto& entry:entries_) finite(elapsed_sum(entry.elapsed,frames));
    if(context.source.model_name.size()>max_name_bytes ||
       (context.attachment && context.attachment->model_name.size()>max_name_bytes))
        throw std::invalid_argument("Model subcommand owner name exceeds the size limit.");
    context_=std::move(context); frame_delta_=frames;
    cursor_=0; remaining_=entries_.size(); current_advanced_=current_start_=false;
    operation_=Operation::update;
    return process(services);
}
ModelSubcommandResult ModelSubcommandQueue::resume(const ModelSubcommandServices& services) {
    require_usable(); return process(services);
}
ModelSubcommandResult ModelSubcommandQueue::cleanup(const ModelSubcommandServices& services) {
    require_usable();
    if(operation_!=Operation::cleanup) {
        operation_=Operation::cleanup; cursor_=0; remaining_=entries_.size();
        current_advanced_=current_start_=false;
    }
    return process(services);
}
ModelSubcommandResult ModelSubcommandQueue::start(ModelSubcommandEntry& entry,
    const ModelSubcommandServices& services,bool& keep) {
    if(auto* sound=std::get_if<ModelSoundSubcommandState>(&entry.value)) {
        if(!services.play_sound) return needs(ModelSubcommandStop::sound_play_required,entry);
        const auto result=services.play_sound(sound->name);
        if(!result.available) return needs(ModelSubcommandStop::sound_play_required,entry);
        keep=result.handle!=0 && result.handle!=std::numeric_limits<std::uint64_t>::max() && result.type==4;
        if(keep) sound->handle=result.handle;
        return {};
    }
    auto& effect=std::get<ModelEffectSubcommandState>(entry.value);
    if(!effect.space_selected) {
        if(!services.find_effect_space) return needs(ModelSubcommandStop::effect_space_required,entry);
        const auto result=services.find_effect_space(effect.command.model_name);
        if(!result.available) return needs(ModelSubcommandStop::effect_space_required,entry);
        if(!result.ram_no) {keep=false;return {};}
        effect.ram_no=*result.ram_no; effect.space_selected=true;
    }
    if(!services.generate_effect) return needs(ModelSubcommandStop::effect_generate_required,entry);
    const ModelEffectGenerateRequest request{effect.command.model_name,effect.command.sequence_name,
        effect.ram_no,context_.source,context_.attachment.value_or(context_.source),
        effect.command.offset,effect.command.rotation,effect.command.position_lock};
    if(!services.generate_effect(request)) return needs(ModelSubcommandStop::effect_generate_required,entry);
    keep=effect.command.sequence_dead; return {};
}
ModelSubcommandResult ModelSubcommandQueue::clean(ModelSubcommandEntry& entry,
    const ModelSubcommandServices& services) {
    if(const auto* sound=std::get_if<ModelSoundSubcommandState>(&entry.value)) {
        if(sound->handle!=0 && (!services.sound_volume_down || !services.sound_volume_down(sound->handle)))
            return needs(ModelSubcommandStop::sound_volume_down_required,entry);
    } else {
        const auto& effect=std::get<ModelEffectSubcommandState>(entry.value);
        if(effect.command.sequence_dead && entry.started && effect.ram_no!=-1 &&
           (!services.erase_effect || !services.erase_effect(effect.command.model_name,effect.ram_no)))
            return needs(ModelSubcommandStop::effect_erase_required,entry);
    }
    return {};
}
ModelSubcommandResult ModelSubcommandQueue::process(const ModelSubcommandServices& services) {
    processing_=true;
    try {
        while(operation_!=Operation::none && remaining_!=0) {
            auto& entry=entries_.at(cursor_);
            bool keep=false;
            ModelSubcommandResult result;
            if(operation_==Operation::cleanup) result=clean(entry,services);
            else {
                keep=true;
                if(!current_advanced_) {
                    entry.elapsed=elapsed_sum(entry.elapsed,frame_delta_);
                    current_start_=!entry.started && entry.elapsed>=entry.delay;
                    if(current_start_) entry.started=true; // Original publishes before virtual start.
                    current_advanced_=true;
                }
                if(current_start_) result=start(entry,services,keep);
                // Both retained ordinary types have the original always-true update virtual.
            }
            if(!result.complete()) {processing_=false;return result;}
            if(operation_==Operation::cleanup || keep) ++cursor_;
            else entries_.erase(entries_.begin()+static_cast<std::ptrdiff_t>(cursor_));
            --remaining_; current_advanced_=current_start_=false;
        }
        if(operation_==Operation::cleanup) entries_.clear();
        operation_=Operation::none; processing_=false; return {};
    } catch(...) {
        // The callback may have committed an effect before throwing. Retain its
        // owner for diagnosis but prohibit both automatic replay and guessed cleanup.
        faulted_=true; processing_=false; throw;
    }
}
} // namespace dw4
