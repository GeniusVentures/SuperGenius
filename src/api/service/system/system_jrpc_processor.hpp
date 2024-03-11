
#ifndef SUPERGENIUS_API_SYSTEM_SYSTEMJRPCPROCESSOR
#define SUPERGENIUS_API_SYSTEM_SYSTEMJRPCPROCESSOR

#include "api/jrpc/jrpc_processor.hpp"

#include "api/jrpc/jrpc_server_impl.hpp"
#include "api/service/system/system_api.hpp"

namespace sgns::api::system {

  class SystemJRpcProcessor : public JRpcProcessor {
   public:
    SystemJRpcProcessor(std::shared_ptr<JRpcServer> server,
                        std::shared_ptr<SystemApi> api);
    ~SystemJRpcProcessor() override = default;

    void registerHandlers() override;

    std::string GetName() override
    {
      return "SystemJRpcProcessor";
    }

   private:
    std::shared_ptr<SystemApi> api_;
    std::shared_ptr<JRpcServer> server_;
  };

}  // namespace sgns::api::system
#endif  // SUPERGENIUS_API_SYSTEM_SYSTEMJRPCPROCESSOR
