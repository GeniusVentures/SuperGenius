
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
      if (auto api = api_.lock()) {
        RequestType request(api);

        if (auto &&resust = request.init(params); ! resust) {
          throw jsonrpc::Fault(resust.error().message());
        }

        if (auto &&result = request.execute(); ! result) {
          throw jsonrpc::Fault(result.error().message());
          // NOLINTNEXTLINE
        } else if constexpr (std::is_same_v<decltype(result.value()), void>) {
          return {};
          // NOLINTNEXTLINE
        } else {
          return makeValue(result.value());
        }

      } else {
        throw jsonrpc::Fault("API not available");
      }
    }
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_JRPC_JRPC_METHOD_HPP
