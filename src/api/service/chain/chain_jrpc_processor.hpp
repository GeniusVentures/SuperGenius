
#ifndef SUPERGENIUS_SRC_CHAIN_JRPC_PROCESSOR_HPP
#define SUPERGENIUS_SRC_CHAIN_JRPC_PROCESSOR_HPP

#include "api/jrpc/jrpc_processor.hpp"
#include "api/jrpc/jrpc_server_impl.hpp"
#include "api/service/chain/chain_api.hpp"

namespace sgns::api::chain {
  /**
   * @brief extrinsic submission service implementation
   */
  class ChainJRpcProcessor : public JRpcProcessor {
   public:
    ChainJRpcProcessor(std::shared_ptr<JRpcServer> server,
                       std::shared_ptr<ChainApi> api);
    void registerHandlers() override;

    std::string GetName() override
    {
      return "ChainJRpcProcessor";
    }

   private:
    std::shared_ptr<ChainApi> api_;
    std::shared_ptr<JRpcServer> server_;
  };
}  // namespace sgns::api::chain

#endif  // SUPERGENIUS_SRC_CHAIN_JRPC_PROCESSOR_HPP
