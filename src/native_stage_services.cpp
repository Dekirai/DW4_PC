#include "dw4/native_stage_services.hpp"

namespace dw4 {
SystemStageFactory make_native_stage_factory(std::weak_ptr<LoaderRequestServices> loader_services,
    SystemStageFactory remaining) {
    return [loader_services=std::move(loader_services),remaining=std::move(remaining)](const SystemStageRequest& request) {
        auto operation=std::make_shared<SystemStageOperation>();
        switch(request.kind) {
        case SystemStageServiceKind::register_info:
            operation->resume=make_context_info_registration(request.context,request.info);break;
        case SystemStageServiceKind::load_directory: {
            const auto services=loader_services.lock();
            if(!services)throw SystemStageError("Native stage directory request requires a live loader-service owner.");
            const auto call=std::make_shared<LoaderDirectoryRequestInvocation>(request.loader,request.filename);
            const std::weak_ptr<SystemStageOperation> output=operation;
            operation->resume=[services,call,output] {
                const auto result=call->resume(services);
                if(result.completed) {
                    const auto destination=output.lock();
                    if(!destination)throw SystemStageError("Native stage directory result owner was destroyed.");
                    destination->directory_result=call->result();
                }
                return result;
            };break;
        }
        case SystemStageServiceKind::release_claim: {
            const auto services=loader_services.lock();
            if(!services)throw SystemStageError("Native stage directory release requires a live loader-service owner.");
            const auto call=std::make_shared<LoaderDirectoryReleaseInvocation>(request.loader,request.filename);
            operation->resume=[services,call] {return call->resume(services->directory);};break;
        }
        default:
            if(remaining)return remaining(request);
            operation->resume=[] {return MessengerCallResult{false,0,
                "Native stage requires the reached pool, loader lifecycle or string-pool operation."};};
            break;
        }
        return operation;
    };
}
} // namespace dw4
