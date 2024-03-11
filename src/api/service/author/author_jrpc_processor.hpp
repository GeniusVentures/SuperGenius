
#ifndef SUPERGENIUS_SRC_SERVICE_EXTRINSICS_SUBMISSION_SERVICE_HPP
#define SUPERGENIUS_SRC_SERVICE_EXTRINSICS_SUBMISSION_SERVICE_HPP

#include "api/jrpc/jrpc_processor.hpp"
#include "api/jrpc/jrpc_server_impl.hpp"
#include "api/service/author/author_api.hpp"

namespace sgns::api::author {

  /**
   * @brief extrinsic submission service implementation
   */
  class AuthorJRpcProcessor : public JRpcProcessor {
   public:
    AuthorJRpcProcessor(std::shared_ptr<JRpcServer> server,
                        std::shared_ptr<AuthorApi> api);

    std::string GetName() override
    {
      return "AuthorJRpcProcessor";
    }

   private:
    void registerHandlers() override;

    std::shared_ptr<AuthorApi> api_;
    std::shared_ptr<JRpcServer> server_;
  };

}  // namespace sgns::api::author

#endif  // SUPERGENIUS_SRC_SERVICE_EXTRINSICS_SUBMISSION_SERVICE_HPP
