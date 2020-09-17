#ifndef SUPERGENIUS_API_REQUEST_SUBSCRIBE_STORAGE
#define SUPERGENIUS_API_REQUEST_SUBSCRIBE_STORAGE

#include <jsonrpc-lean/request.h>

#include <boost/optional.hpp>

#include "api/service/state/state_api.hpp"
#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
#include "primitives/block_id.hpp"

namespace sgns::api::state::request {

  class SubscribeStorage final {
   public:
    SubscribeStorage(const SubscribeStorage &) = delete;
    SubscribeStorage &operator=(const SubscribeStorage &) = delete;

    SubscribeStorage(SubscribeStorage &&) = default;
    SubscribeStorage &operator=(SubscribeStorage &&) = default;

    explicit SubscribeStorage(std::shared_ptr<StateApi> api)
        : api_(std::move(api)){};
    ~SubscribeStorage() = default;

    outcome::result<void> init(const jsonrpc::Request::Parameters &params);
    outcome::result<uint32_t> execute();

   private:
    std::shared_ptr<StateApi> api_;
    std::vector<base::Buffer> key_buffers_;
  };

}  // namespace sgns::api::state::request

#endif  // SUPERGENIUS_API_REQUEST_SUBSCRIBE_STORAGE