#pragma once

#include "dw4/context_info.hpp"
#include "dw4/loader_request.hpp"

namespace dw4 {
// Routes stage load_directory, register_info and release_claim to the actual
// recovered native bodies. Other kinds require the supplied retained fallback;
// missing implementations remain pending. Keep loader services alive in the
// application. Weak factory capture avoids a cycle through services->globals.
SystemStageFactory make_native_stage_factory(std::weak_ptr<LoaderRequestServices>,
    SystemStageFactory remaining_operations={});
} // namespace dw4
