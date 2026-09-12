#include "dw4/model_pad.hpp"

#include <stdexcept>

namespace dw4 {
std::shared_ptr<ModelPadState> model_pad(const std::shared_ptr<ModelPadManager>& manager,
    std::int32_t number) {
    if(!manager)throw std::runtime_error("Original pad selection dereferences a null manager.");
    const auto selected=manager->redirect!=0 || number>=4 ? 4:number;
    const auto list=manager->data;
    if(!list)throw std::runtime_error("Original pad selection dereferences a null data list.");
    const auto backing=list->backing;
    if(selected<0 || !backing || static_cast<std::size_t>(selected)>=backing->size())
        throw std::runtime_error("Original pad selection has no represented physical backing slot.");
    return (*backing)[static_cast<std::size_t>(selected)];
}
} // namespace dw4
