
#ifndef SUPERGENIUS_SRC_STATE_JRPC_PROCESSOR_HPP
#define SUPERGENIUS_SRC_STATE_JRPC_PROCESSOR_HPP

#include "api/jrpc/jrpc_processor.hpp"

#include "api/jrpc/jrpc_server_impl.hpp"
#include "api/service/state/state_api.hpp"

namespace sgns::api::state {

  class StateJrpcProcessor : public JRpcProcessor {
   public:
    StateJrpcProcessor(std::shared_ptr<JRpcServer> server,
                       std::shared_ptr<StateApi> api);
    ~StateJrpcProcessor() override = default;

    void registerHandlers() override;

   private:
    std::shared_ptr<StateApi> api_;
    std::shared_ptr<JRpcServer> server_;
  };

}  // namespace sgns::api
#endif  // SUPERGENIUS_SRC_STATE_JRPC_PROCESSOR_HPP
