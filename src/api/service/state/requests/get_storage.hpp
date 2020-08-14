
#ifndef SUPERGENIUS_SRC_API_REQUEST_GET_STORAGE
#define SUPERGENIUS_SRC_API_REQUEST_GET_STORAGE

#include <jsonrpc-lean/request.h>

#include <boost/optional.hpp>

#include "api/service/state/state_api.hpp"
#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
#include "primitives/block_id.hpp"

namespace sgns::api::state::request {

  class GetStorage final {
   public:
    GetStorage(GetStorage const &) = delete;
    GetStorage &operator=(GetStorage const &) = delete;

    GetStorage(GetStorage &&) = default;
    GetStorage &operator=(GetStorage &&) = default;

    explicit GetStorage(std::shared_ptr<StateApi> api) : api_(std::move(api)){};
    ~GetStorage() = default;

    outcome::result<void> init(const jsonrpc::Request::Parameters &params);

    outcome::result<base::Buffer> execute();

   private:
    std::shared_ptr<StateApi> api_;
    base::Buffer key_;
    boost::optional<sgns::primitives::BlockHash> at_;
  };

}  // namespace sgns::api::state::request

#endif  // SUPERGENIUS_SRC_STATE_JRPC_PROCESSOR_HPP
