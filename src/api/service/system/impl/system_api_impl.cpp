
#include "api/service/system/impl/system_api_impl.hpp"

#include <jsonrpc-lean/request.h>

namespace sgns::api {

  SystemApiImpl::SystemApiImpl(
      std::shared_ptr<application::ConfigurationStorage> config)
      : config_(std::move(config)) {
    BOOST_ASSERT(config_ != nullptr);
  }

  std::shared_ptr<application::ConfigurationStorage> SystemApiImpl::getConfig()
      const {
    if (! config_) {
      throw jsonrpc::InternalErrorFault(
          "Internal error. Configuration storage not initialized.");
    }
    return config_;
  }

}  // namespace sgns::api
