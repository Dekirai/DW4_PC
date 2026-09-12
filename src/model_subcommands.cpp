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

struct ModelSubcommandWrapper::State {
    enum class Operation { none, update, cleanup };
    std::shared_ptr<ModelSubcommandList> list=std::make_shared<ModelSubcommandList>();
    std::uint64_t next_id_{};
    Operation operation_=Operation::none;
    std::size_t cursor_{},remaining_{};
    float frame_delta_{};
    ModelSubcommandContext context_;
    std::optional<ModelEffectGenerateRequest> current_effect_request_;
    bool current_advanced_{},current_start_{},processing_{},faulted_{},hitbox_check_pending_{};
    bool pending() const noexcept {return operation_!=Operation::none;}
    void require_usable() const;
    void require_cleanup_startable() const;
    void require_appendable() const;
    std::uint64_t append_sound(float,std::optional<std::string>);
    std::uint64_t append_effect(ModelEffectSubcommand);
    std::uint64_t append_hitbox(ModelHitboxSubcommand);
    void clear_hitboxes();
    ModelSubcommandResult update(float,ModelSubcommandContext,const ModelSubcommandServices&);
    ModelSubcommandResult resume(const ModelSubcommandServices&);
    ModelSubcommandResult cleanup(const ModelSubcommandServices&);
    ModelSubcommandResult process(const ModelSubcommandServices&);
    ModelSubcommandResult start(ModelSubcommandEntry&,const ModelSubcommandServices&,bool&);
    ModelSubcommandResult check_hitbox(ModelSubcommandEntry&,const ModelSubcommandServices&);
    ModelSubcommandResult clean(ModelSubcommandEntry&,const ModelSubcommandServices&);
};
ModelSubcommandWrapper::ModelSubcommandWrapper():state_(std::make_unique<State>()) {}
ModelSubcommandWrapper::~ModelSubcommandWrapper()=default;
std::shared_ptr<ModelSubcommandList> ModelSubcommandWrapper::list_owner() noexcept {return state_->list;}
std::shared_ptr<const ModelSubcommandList> ModelSubcommandWrapper::list_owner() const noexcept {return state_->list;}
ModelSubcommandQueue::ModelSubcommandQueue():wrapper_(std::make_shared<ModelSubcommandWrapper>()) {}
ModelSubcommandQueue::ModelSubcommandQueue(std::shared_ptr<ModelSubcommandWrapper> wrapper):wrapper_(std::move(wrapper)) {
    if(!wrapper_)throw std::invalid_argument("Model subcommands require their actual wrapper owner.");
}
ModelSubcommandQueue::ModelSubcommandQueue(std::shared_ptr<std::shared_ptr<ModelSubcommandWrapper>> slot):slot_(std::move(slot)) {
    if(!slot_)throw std::invalid_argument("Model subcommands require their actual work reference-field owner.");
}
ModelSubcommandQueue::ModelSubcommandQueue(ModelSubcommandQueue&& other):ModelSubcommandQueue() {
    other.require_not_executing();
    wrapper_.swap(other.wrapper_);slot_.swap(other.slot_);captured_.swap(other.captured_);
}
ModelSubcommandQueue& ModelSubcommandQueue::operator=(ModelSubcommandQueue&& other) {
    if(this==&other)return *this;
    require_not_executing();require_live_wrapper()->state_->require_usable();
    if(!entries().empty() || pending())throw std::logic_error("Model subcommand destination requires cleanup before replacement.");
    other.require_not_executing();
    auto replacement=std::make_shared<ModelSubcommandWrapper>();
    wrapper_=std::exchange(other.wrapper_,std::move(replacement));
    slot_=std::move(other.slot_);captured_=std::move(other.captured_);return *this;
}
std::shared_ptr<ModelSubcommandWrapper> ModelSubcommandQueue::live_wrapper() const noexcept {return slot_ ? *slot_ : wrapper_;}
std::shared_ptr<ModelSubcommandWrapper> ModelSubcommandQueue::require_live_wrapper() const {
    auto owner=live_wrapper();
    if(!owner)throw std::invalid_argument("Model subcommands require the non-null actual work wrapper at this operation.");
    return owner;
}
std::shared_ptr<ModelSubcommandWrapper> ModelSubcommandQueue::retained_wrapper() const noexcept {
    if(captured_ && (captured_->state_->pending() || captured_->state_->processing_ || captured_->state_->faulted_))return captured_;
    return {};
}
void ModelSubcommandQueue::require_not_executing() const {
    const auto current=live_wrapper();
    if((captured_ && captured_->state_->processing_) || (current && current->state_->processing_))
        throw std::logic_error("Cannot move or replace an executing model subcommand facade.");
}
void ModelSubcommandQueue::release_completed_capture() noexcept {
    if(captured_ && !captured_->state_->pending() && !captured_->state_->processing_ && !captured_->state_->faulted_)captured_.reset();
}
std::shared_ptr<ModelSubcommandWrapper> ModelSubcommandQueue::wrapper_owner() noexcept {return live_wrapper();}
std::shared_ptr<const ModelSubcommandWrapper> ModelSubcommandQueue::wrapper_owner() const noexcept {return live_wrapper();}
std::shared_ptr<ModelSubcommandList> ModelSubcommandQueue::list_owner() {return require_live_wrapper()->list_owner();}
std::shared_ptr<const ModelSubcommandList> ModelSubcommandQueue::list_owner() const {return require_live_wrapper()->list_owner();}
const ModelSubcommandList& ModelSubcommandQueue::entries() const {return *require_live_wrapper()->state_->list;}
std::shared_ptr<ModelSubcommandWrapper> ModelSubcommandQueue::pending_wrapper_owner() noexcept {
    if(auto retained=retained_wrapper())return retained;
    auto current=live_wrapper();return current && current->state_->pending() ? current : nullptr;
}
std::shared_ptr<const ModelSubcommandWrapper> ModelSubcommandQueue::pending_wrapper_owner() const noexcept {
    if(auto retained=retained_wrapper())return retained;
    auto current=live_wrapper();return current && current->state_->pending() ? current : nullptr;
}
bool ModelSubcommandQueue::pending() const noexcept {
    const auto owner=pending_wrapper_owner();return owner && owner->state_->pending();
}
bool ModelSubcommandQueue::faulted() const noexcept {
    const auto retained=retained_wrapper(),current=live_wrapper();
    return (retained && retained->state_->faulted_) || (current && current->state_->faulted_);
}
std::uint64_t ModelSubcommandQueue::append_sound(float delay,std::optional<std::string> name) {
    const auto owner=require_live_wrapper();return owner->state_->append_sound(delay,std::move(name));
}
std::uint64_t ModelSubcommandQueue::append_effect(ModelEffectSubcommand command) {
    const auto owner=require_live_wrapper();return owner->state_->append_effect(std::move(command));
}
std::uint64_t ModelSubcommandQueue::append_hitbox(ModelHitboxSubcommand command) {
    const auto owner=require_live_wrapper();return owner->state_->append_hitbox(std::move(command));
}
void ModelSubcommandQueue::clear_hitboxes() {
    const auto owner=require_live_wrapper();owner->state_->clear_hitboxes();release_completed_capture();
}
ModelSubcommandResult ModelSubcommandQueue::update(float frames,ModelSubcommandContext context,const ModelSubcommandServices& services) {
    if(const auto retained=retained_wrapper()) {
        retained->state_->require_usable();
        throw std::logic_error("Resume or clean up the retained model subcommand pass before updating.");
    }
    captured_=require_live_wrapper();
    try {const auto result=captured_->state_->update(frames,std::move(context),services);release_completed_capture();return result;}
    catch(...) {release_completed_capture();throw;}
}
ModelSubcommandResult ModelSubcommandQueue::resume(const ModelSubcommandServices& services) {
    captured_=retained_wrapper() ? captured_ : require_live_wrapper();
    try {const auto result=captured_->state_->resume(services);release_completed_capture();return result;}
    catch(...) {release_completed_capture();throw;}
}
void ModelSubcommandQueue::require_cleanup_startable() const {
    const auto owner=require_live_wrapper();
    if(const auto retained=retained_wrapper();retained && retained!=owner)
        throw std::logic_error("A replaced model subcommand slot requires a separate facade while its earlier pass is pending.");
    owner->state_->require_cleanup_startable();
}
ModelSubcommandResult ModelSubcommandQueue::cleanup(const ModelSubcommandServices& services) {
    require_cleanup_startable();
    const auto owner=require_live_wrapper();
    captured_=owner;
    try {const auto result=captured_->state_->cleanup(services);release_completed_capture();return result;}
    catch(...) {release_completed_capture();throw;}
}

void ModelSubcommandWrapper::State::require_usable() const {
    if(processing_) throw std::logic_error("Recursive model subcommand operation is unsupported.");
    if(faulted_) throw std::logic_error("Model subcommand owner has an uncertain service outcome; automatic replay is forbidden.");
}
void ModelSubcommandWrapper::State::require_appendable() const {
    if(faulted_) throw std::logic_error("Cannot append to a faulted model subcommand owner.");
    if(operation_==Operation::cleanup)
        throw std::logic_error("Cannot append during model subcommand cleanup.");
    if(list->size()>=max_entries || next_id_==std::numeric_limits<std::uint64_t>::max())
        throw std::length_error("Model subcommand queue limit exceeded.");
}
std::uint64_t ModelSubcommandWrapper::State::append_sound(float delay,std::optional<std::string> name) {
    require_appendable(); finite(delay); name_size(name);
    const auto id=next_id_;
    list->entries_.push_back(std::make_shared<ModelSubcommandEntry>(ModelSubcommandEntry{id,0,delay,false,ModelSoundSubcommandState{std::move(name),0}}));
    ++next_id_; return id;
}
std::uint64_t ModelSubcommandWrapper::State::append_effect(ModelEffectSubcommand command) {
    require_appendable(); finite(command.delay);
    name_size(command.model_name); name_size(command.sequence_name);
    for(float value:command.offset) finite(value);
    for(float value:command.rotation) finite(value);
    const auto id=next_id_;
    const float delay=command.delay;
    list->entries_.push_back(std::make_shared<ModelSubcommandEntry>(ModelSubcommandEntry{id,0,delay,false,ModelEffectSubcommandState{std::move(command),-1,false}}));
    ++next_id_; return id;
}
std::uint64_t ModelSubcommandWrapper::State::append_hitbox(ModelHitboxSubcommand command) {
    require_appendable(); finite(command.delay); finite(command.length);
    for(float value:command.offset) finite(value);
    for(float value:command.box_size) finite(value);
    const float end=command.length==-1.0f ? 100000000.0f : elapsed_sum(command.delay,command.length);
    finite(end);
    const auto quotient=command.kind/10000; // Signed truncation toward zero, including negative kinds.
    const std::int32_t mode=quotient==0 ? (command.kind<1000 ? 0 : 2) :
        quotient==6 ? 1 : (quotient==7 || quotient==8 ? 2 : 0);
    const auto id=next_id_;
    const float delay=command.delay;
    list->entries_.push_back(std::make_shared<ModelSubcommandEntry>(ModelSubcommandEntry{id,0,delay,false,ModelHitboxSubcommandState{std::move(command),mode,end}}));
    ++next_id_; return id;
}
void ModelSubcommandWrapper::State::clear_hitboxes() {
    require_usable();
    if(hitbox_check_pending_)
        throw std::logic_error("Complete the pending hitbox check before clearing model subcommands.");
    // Removing an earlier entry shifts the retained native cursor. Entries
    // appended after an update's count snapshot do not belong to its remainder.
    auto& entries=list->entries_;
    for(std::size_t index=0;index<entries.size();) {
        if(!std::holds_alternative<ModelHitboxSubcommandState>(entries[index]->value)) {++index;continue;}
        if(operation_!=Operation::none) {
            if(index<cursor_) --cursor_;
            else if(index-cursor_<remaining_) {
                --remaining_;
                if(index==cursor_) {current_advanced_=current_start_=false;current_effect_request_.reset();}
            }
        }
        entries.erase(entries.begin()+static_cast<std::ptrdiff_t>(index));
    }
    if(operation_==Operation::update && remaining_==0) operation_=Operation::none;
}
ModelSubcommandResult ModelSubcommandWrapper::State::update(float frames,ModelSubcommandContext context,
    const ModelSubcommandServices& services) {
    require_usable();
    if(pending()) throw std::logic_error("Resume or clean up the pending model subcommand pass before updating.");
    finite(frames);
    // Reject a non-finite native boundary before any entry or service is changed.
    for(const auto& entry:*list) finite(elapsed_sum(entry.elapsed,frames));
    if(context.source.model_name.size()>max_name_bytes ||
       (!context.resolve_attachment && context.attachment && context.attachment->model_name.size()>max_name_bytes))
        throw std::invalid_argument("Model subcommand owner name exceeds the size limit.");
    context_=std::move(context); frame_delta_=frames;
    cursor_=0; remaining_=list->size(); current_advanced_=current_start_=false;
    current_effect_request_.reset();
    operation_=Operation::update;
    return process(services);
}
ModelSubcommandResult ModelSubcommandWrapper::State::resume(const ModelSubcommandServices& services) {
    require_usable(); return process(services);
}
void ModelSubcommandWrapper::State::require_cleanup_startable() const {
    require_usable();
    if(hitbox_check_pending_)
        throw std::logic_error("Complete the pending hitbox check before model subcommand cleanup.");
}
ModelSubcommandResult ModelSubcommandWrapper::State::cleanup(const ModelSubcommandServices& services) {
    require_cleanup_startable();
    if(operation_!=Operation::cleanup) {
        operation_=Operation::cleanup; cursor_=0; remaining_=list->size();
        current_advanced_=current_start_=false;
        current_effect_request_.reset();
    }
    return process(services);
}
ModelSubcommandResult ModelSubcommandWrapper::State::start(ModelSubcommandEntry& entry,
    const ModelSubcommandServices& services,bool& keep) {
    if(auto* sound=std::get_if<ModelSoundSubcommandState>(&entry.value)) {
        if(!services.play_sound) return needs(ModelSubcommandStop::sound_play_required,entry);
        const auto result=services.play_sound(sound->name);
        if(!result.available) return needs(ModelSubcommandStop::sound_play_required,entry);
        keep=result.handle!=0 && result.handle!=std::numeric_limits<std::uint64_t>::max() && result.type==4;
        if(keep) sound->handle=result.handle;
        return {};
    }
    if(std::holds_alternative<ModelHitboxSubcommandState>(entry.value)) return check_hitbox(entry,services);
    auto& effect=std::get<ModelEffectSubcommandState>(entry.value);
    if(!effect.space_selected) {
        if(!services.find_effect_space) return needs(ModelSubcommandStop::effect_space_required,entry);
        const auto result=services.find_effect_space(effect.command.model_name);
        if(!result.available) return needs(ModelSubcommandStop::effect_space_required,entry);
        if(!result.ram_no) {keep=false;return {};}
        effect.ram_no=*result.ram_no; effect.space_selected=true;
    }
    if(!current_effect_request_) {
        // 8031037C completes FIND_SPACE before 803104A8/AC/F0 reads the
        // current ONE work's attachment. Retain the built packet, not a
        // resolver that would observe a later mutation on GENERATE retry.
        const auto attachment=context_.resolve_attachment ? context_.resolve_attachment() : context_.attachment;
        if(attachment && attachment->model_name.size()>max_name_bytes)
            throw std::invalid_argument("Model effect attachment name exceeds the size limit.");
        current_effect_request_=ModelEffectGenerateRequest{effect.command.model_name,effect.command.sequence_name,
            effect.ram_no,context_.source,attachment.value_or(context_.source),
            effect.command.offset,effect.command.rotation,effect.command.position_lock};
    }
    if(!services.generate_effect) return needs(ModelSubcommandStop::effect_generate_required,entry);
    if(!services.generate_effect(*current_effect_request_)) return needs(ModelSubcommandStop::effect_generate_required,entry);
    keep=effect.command.sequence_dead; return {};
}
ModelSubcommandResult ModelSubcommandWrapper::State::check_hitbox(ModelSubcommandEntry& entry,
    const ModelSubcommandServices& services) {
    if(!services.check_hitbox) return needs(ModelSubcommandStop::hitbox_check_required,entry);
    const auto& hitbox=std::get<ModelHitboxSubcommandState>(entry.value);
    const ModelHitboxCheckRequest request{entry.id,hitbox.command,hitbox.bullet_mode,context_};
    hitbox_check_pending_=true;
    if(!services.check_hitbox(request)) return needs(ModelSubcommandStop::hitbox_check_required,entry);
    hitbox_check_pending_=false;
    return {};
}
ModelSubcommandResult ModelSubcommandWrapper::State::clean(ModelSubcommandEntry& entry,
    const ModelSubcommandServices& services) {
    if(const auto* sound=std::get_if<ModelSoundSubcommandState>(&entry.value)) {
        if(sound->handle!=0 && (!services.sound_volume_down || !services.sound_volume_down(sound->handle)))
            return needs(ModelSubcommandStop::sound_volume_down_required,entry);
    } else if(const auto* effect=std::get_if<ModelEffectSubcommandState>(&entry.value)) {
        if(effect->command.sequence_dead && entry.started && effect->ram_no!=-1 &&
           (!services.erase_effect || !services.erase_effect(effect->command.model_name,effect->ram_no)))
            return needs(ModelSubcommandStop::effect_erase_required,entry);
    }
    // SCHitBox's cleanup virtual 802E93B4 is empty.
    return {};
}
ModelSubcommandResult ModelSubcommandWrapper::State::process(const ModelSubcommandServices& services) {
    processing_=true;
    try {
        while(operation_!=Operation::none && remaining_!=0) {
            auto& entries=list->entries_;
            const auto entry_owner=entries.at(cursor_);
            auto& entry=*entry_owner;
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
                else if(entry.started) {
                    if(const auto* hitbox=std::get_if<ModelHitboxSubcommandState>(&entry.value)) {
                        if(entry.elapsed>=hitbox->end_frame) keep=false;
                        else result=check_hitbox(entry,services);
                    }
                    // Sound/effect retained types have the always-true update virtual.
                }
            }
            if(!result.complete()) {processing_=false;return result;}
            if(operation_==Operation::cleanup || keep) ++cursor_;
            else entries.erase(entries.begin()+static_cast<std::ptrdiff_t>(cursor_));
            --remaining_; current_advanced_=current_start_=false;current_effect_request_.reset();
        }
        if(operation_==Operation::cleanup) list->entries_.clear();
        operation_=Operation::none; processing_=false; return {};
    } catch(...) {
        // The callback may have committed an effect before throwing. Retain its
        // owner for diagnosis but prohibit both automatic replay and guessed cleanup.
        faulted_=true; processing_=false; throw;
    }
}
} // namespace dw4
