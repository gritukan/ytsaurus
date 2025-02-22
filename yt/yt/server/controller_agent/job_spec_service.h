#pragma once

#include "public.h"

#include <yt/yt/core/rpc/public.h>

namespace NYT::NControllerAgent {

////////////////////////////////////////////////////////////////////

NRpc::IServicePtr CreateJobSpecService(TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
