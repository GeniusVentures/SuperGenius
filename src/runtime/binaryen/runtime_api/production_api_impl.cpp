
#include "runtime/binaryen/runtime_api/production_api_impl.hpp"

namespace sgns::runtime::binaryen {

  ProductionApiImpl::ProductionApiImpl(
      const std::shared_ptr<RuntimeManager> &runtime_manager)
      : RuntimeApi(runtime_manager) {}

  outcome::result<primitives::ProductionConfiguration> ProductionApiImpl::configuration() {
    return execute<primitives::ProductionConfiguration>("ProductionApi_configuration",
                                                  CallPersistency::EPHEMERAL);
  }

}  // namespace sgns::runtime::binaryen
