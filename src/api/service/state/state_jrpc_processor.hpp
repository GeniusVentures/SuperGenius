
#ifndef SUPERGENIUS_SRC_STATE_JRPC_PROCESSOR_HPP
#define SUPERGENIUS_SRC_STATE_JRPC_PROCESSOR_HPP

#include "api/jrpc/jrpc_processor.hpp"

#include "api/jrpc/jrpc_server_impl.hpp"
#include "api/service/state/state_api.hpp"

namespace sgns::api::state {

  class StateJRpcProcessor : public JRpcProcessor {
   public:
    StateJRpcProcessor(std::shared_ptr<JRpcServer> server,
                       std::shared_ptr<StateApi> api);
    ~StateJRpcProcessor() override = default;

    void registerHandlers() override;

    std::string GetName() override
    {
      return "StateJRpcProcessor";
    }

   private:
    std::shared_ptr<StateApi> api_;
    std::shared_ptr<JRpcServer> server_;
  };

}  // namespace sgns::api
#endif  // SUPERGENIUS_SRC_STATE_JRPC_PROCESSOR_HPP
