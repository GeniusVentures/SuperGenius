
#include "runtime/binaryen/runtime_api/production_api_impl.hpp"

namespace sgns::runtime::binaryen {

  ProductionApiImpl::ProductionApiImpl(
      const std::shared_ptr<WasmProvider> &wasm_provider,
      const std::shared_ptr<RuntimeManager> &runtime_manager)
      : RuntimeApi(wasm_provider, runtime_manager),
	  logger_{ base::createLogger("ProductionApiImpl")} {}

  outcome::result<primitives::ProductionConfiguration> ProductionApiImpl::configuration() {
	  logger_->debug("ProductionApi_configuration");
    primitives::ProductionConfiguration result;
	primitives::ProductionDuration duration{30000000};  //0x0000000001c9c380
	result.slot_duration = duration;
	result.epoch_length = 0x00000000000000c8; //200
	result.leadership_rate = { 0x0000000000000001,0x0000000000000004 };
	result.genesis_authorities = { primitives::Authority{{}, 1} , primitives::Authority{{}, 0x00000137ddc9a180} , primitives::Authority{{}, 0x00000137ddc9a180} };
	//return result;


//     return execute<primitives::ProductionConfiguration>("ProductionApi_configuration",
//                                                   CallPersistency::EPHEMERAL);
	return execute<primitives::ProductionConfiguration>("BabeApi_configuration",
		CallPersistency::EPHEMERAL);
  }

}  // namespace sgns::runtime::binaryen
