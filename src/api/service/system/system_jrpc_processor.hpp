
#ifndef SUPERGENIUS_API_SYSTEM_SYSTEMJRPCPROCESSOR
#define SUPERGENIUS_API_SYSTEM_SYSTEMJRPCPROCESSOR

#include "api/jrpc/jrpc_processor.hpp"

#include "api/jrpc/jrpc_server_impl.hpp"
#include "api/service/system/system_api.hpp"

namespace sgns::api::system {

  class SystemJrpcProcessor : public JRpcProcessor {
   public:
    SystemJrpcProcessor(std::shared_ptr<JRpcServer> server,
                        std::shared_ptr<SystemApi> api);
    ~SystemJrpcProcessor() override = default;

    void registerHandlers() override;

   private:
    std::shared_ptr<SystemApi> api_;
    std::shared_ptr<JRpcServer> server_;
  };

}  // namespace sgns::api::system
#endif  // SUPERGENIUS_API_SYSTEM_SYSTEMJRPCPROCESSOR
