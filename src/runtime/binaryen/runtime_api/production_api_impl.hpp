
#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_API_PRODUCTION_API_IMPL_HPP
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_API_PRODUCTION_API_IMPL_HPP

#include "runtime/production_api.hpp"
#include "runtime/binaryen/runtime_api/runtime_api.hpp"
#include "base/logger.hpp"

namespace sgns::runtime::binaryen {

  class ProductionApiImpl : public RuntimeApi, public ProductionApi {
   public:
    ~ProductionApiImpl() override = default;

    explicit ProductionApiImpl(
        const std::shared_ptr<WasmProvider> &wasm_provider,
        const std::shared_ptr<RuntimeManager> &runtime_manager);

    outcome::result<primitives::ProductionConfiguration> configuration() override;
	base::Logger logger_;
  };

}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_API_PRODUCTION_API_IMPL_HPP
