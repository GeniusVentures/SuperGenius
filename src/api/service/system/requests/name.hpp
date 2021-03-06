
#ifndef SUPERGENIUS_API_SYSTEM_REQUEST_NAME
#define SUPERGENIUS_API_SYSTEM_REQUEST_NAME

#include <jsonrpc-lean/request.h>

#include "api/service/system/system_api.hpp"
#include "outcome/outcome.hpp"

namespace sgns::api::system::request {

  /**
   * @brief Get the node's implementation name
   * @see https://github.com/w3f/PSPs/blob/psp-rpc-api/psp-002.md#system_name
   */
  class Name final {
   public:
    Name(const Name &) = delete;
    Name &operator=(const Name &) = delete;

    Name(Name &&) = default;
    Name &operator=(Name &&) = default;

    explicit Name(std::shared_ptr<SystemApi> api);
    ~Name() = default;

    outcome::result<void> init(const jsonrpc::Request::Parameters &params);

    outcome::result<std::string> execute();

   private:
    std::shared_ptr<SystemApi> api_;
  };

}  // namespace sgns::api::system::request

#endif  // SUPERGENIUS_API_SYSTEM_REQUEST_NAME
