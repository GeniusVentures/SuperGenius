
#ifndef SUPERGENIUS_SRC_API_REQUEST_GET_VERSION
#define SUPERGENIUS_SRC_API_REQUEST_GET_VERSION

#include <jsonrpc-lean/request.h>

#include "api/service/state/state_api.hpp"
#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
#include "primitives/block_id.hpp"

namespace sgns::api::state::request {

  class GetRuntimeVersion final {
   public:
    GetRuntimeVersion(GetRuntimeVersion const &) = delete;
    GetRuntimeVersion &operator=(GetRuntimeVersion const &) = delete;

    GetRuntimeVersion(GetRuntimeVersion &&) = default;
    GetRuntimeVersion &operator=(GetRuntimeVersion &&) = default;

    explicit GetRuntimeVersion(std::shared_ptr<StateApi> api);
    ~GetRuntimeVersion() = default;

    outcome::result<void> init(jsonrpc::Request::Parameters const &params);
    outcome::result<primitives::Version> execute();

   private:
    std::shared_ptr<StateApi> api_;
    boost::optional<primitives::BlockHash> at_;
  };

}  // namespace sgns::api::state::request

#endif  // SUPERGENIUS_SRC_API_REQUEST_GET_VERSION
