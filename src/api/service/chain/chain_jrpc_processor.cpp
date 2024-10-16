
#include "api/service/chain/chain_jrpc_processor.hpp"

#include "api/jrpc/jrpc_method.hpp"
#include "api/jrpc/value_converter.hpp"
#include "api/service/chain/requests/get_block_hash.hpp"

namespace sgns::api::chain {

  ChainJRpcProcessor::ChainJRpcProcessor(std::shared_ptr<JRpcServer> server,
                                         std::shared_ptr<ChainApi> api)
      : api_{std::move(api)}, server_{std::move(server)} {
    BOOST_ASSERT(api_ != nullptr);
    BOOST_ASSERT(server_ != nullptr);
  }

  template <typename Request>
  using Handler = Method<Request, ChainApi>;

  void ChainJRpcProcessor::registerHandlers() {
    server_->registerHandler("chain_getBlockHash",
                             Handler<request::GetBlockhash>(api_));
  }

}  // namespace sgns::api::chain
