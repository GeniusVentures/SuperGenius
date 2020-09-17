
#ifndef SUPERGENIUS_API_REQUEST_UNSUBSCRIBE_STORAGE
#define SUPERGENIUS_API_REQUEST_UNSUBSCRIBE_STORAGE

#include <jsonrpc-lean/request.h>

#include <boost/optional.hpp>

#include "api/service/state/state_api.hpp"
#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
#include "primitives/block_id.hpp"

namespace sgns::api::state::request {

  class UnsubscribeStorage final {
   public:
    UnsubscribeStorage(const UnsubscribeStorage &) = delete;
    UnsubscribeStorage &operator=(const UnsubscribeStorage &) = delete;

    UnsubscribeStorage(UnsubscribeStorage &&) = default;
    UnsubscribeStorage &operator=(UnsubscribeStorage &&) = default;

    explicit UnsubscribeStorage(std::shared_ptr<StateApi> api)
        : api_(std::move(api)) { };
    ~UnsubscribeStorage() = default;

    outcome::result<void> init(const jsonrpc::Request::Parameters &params);
    outcome::result<void> execute();

   private:
    std::shared_ptr<StateApi> api_;
    std::vector<uint32_t> subscriber_id_;
  };

}  // namespace sgns::api::state::request

#endif  // SUPERGENIUS_API_REQUEST_UNSUBSCRIBE_STORAGE