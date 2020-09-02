
#ifndef SUPERGENIUS_SRC_API_JRPC_JRPC_METHOD_HPP
#define SUPERGENIUS_SRC_API_JRPC_JRPC_METHOD_HPP

#include <jsonrpc-lean/dispatcher.h>
#include <jsonrpc-lean/request.h>

#include <memory>
#include <type_traits>

#include "api/jrpc/value_converter.hpp"

namespace sgns::api {

  template <typename RequestType, typename Api>
  class Method {
   private:
    std::weak_ptr<Api> api_;

   public:
    explicit Method(const std::shared_ptr<Api> &api) : api_(api) {}

    jsonrpc::Value operator()(const jsonrpc::Request::Parameters &params) {
      auto api = api_.lock();
      if (! api) {
        throw jsonrpc::Fault("API not available");
      }

      RequestType request(api);

      // Init request
      if (auto &&init_result = request.init(params); ! init_result) {
        throw jsonrpc::Fault(init_result.error().message());
      }

      // Execute request
      auto &&result = request.execute();

      // Handle of failure
      if (not result) {
        throw jsonrpc::Fault(result.error().message());
      }

      if constexpr (std::is_same_v<decltype(result.value()), void>) {
        return {};
      } else {
        return makeValue(result.value());
      }
    }
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_JRPC_JRPC_METHOD_HPP
