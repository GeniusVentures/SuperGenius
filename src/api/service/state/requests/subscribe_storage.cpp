
#include "api/service/state/requests/subscribe_storage.hpp"

namespace sgns::api::state::request {

  outcome::result<void> SubscribeStorage::init(
      const jsonrpc::Request::Parameters &params) {
    if (params.size() > 1 || params.empty()) {
      throw jsonrpc::InvalidParametersFault("Incorrect number of params");
    }
    auto &keys = params[0];
    if (!keys.IsArray()) {
      throw jsonrpc::InvalidParametersFault(
          "Parameter 'params' must be a string array of the storage keys");
    }

    auto &key_str_array = keys.AsArray();
    key_buffers_.clear();
    key_buffers_.reserve(key_str_array.size());

    for (auto &key_str : key_str_array) {
      if (!key_str.IsString())
        throw jsonrpc::InvalidParametersFault(
            "Parameter 'params' must be a string array of the storage keys");

      OUTCOME_TRY((auto &&, key), base::unhexWith0x(key_str.AsString()));
      key_buffers_.emplace_back(std::move(key));
    }
    return outcome::success();
  }

  outcome::result<uint32_t> SubscribeStorage::execute() {
    return api_->subscribeStorage(key_buffers_);
  }

}  // namespace sgns::api::state::request
