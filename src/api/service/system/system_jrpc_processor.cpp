
#include "api/service/system/system_jrpc_processor.hpp"

#include "api/jrpc/jrpc_method.hpp"
#include "api/service/system/requests/chain.hpp"
#include "api/service/system/requests/name.hpp"
#include "api/service/system/requests/properties.hpp"
#include "api/service/system/requests/version.hpp"

namespace sgns::api::system {

  SystemJRpcProcessor::SystemJRpcProcessor(std::shared_ptr<JRpcServer> server,
                                           std::shared_ptr<SystemApi> api)
      : api_{std::move(api)}, server_{std::move(server)} {
    BOOST_ASSERT(api_ != nullptr);
    BOOST_ASSERT(server_ != nullptr);
  }

  template <typename Request>
  using Handler = sgns::api::Method<Request, SystemApi>;

  void SystemJRpcProcessor::registerHandlers() {
    server_->registerHandler("system_name", Handler<request::Name>(api_));

    server_->registerHandler("system_version", Handler<request::Version>(api_));

    server_->registerHandler("system_chain", Handler<request::Chain>(api_));

    server_->registerHandler("system_properties",
                             Handler<request::Properties>(api_));
  }

}  // namespace sgns::api::system
