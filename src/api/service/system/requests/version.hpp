
#ifndef SUPERGENIUS_API_SYSTEM_REQUEST_VERSION
#define SUPERGENIUS_API_SYSTEM_REQUEST_VERSION

#include <jsonrpc-lean/request.h>

#include "api/service/system/system_api.hpp"
#include "outcome/outcome.hpp"

namespace sgns::api::system::request {

  /**
   * @brief Get the node implementation's version. Should be a semver string
   * @see https://github.com/w3f/PSPs/blob/psp-rpc-api/psp-002.md#system_version
   */
  class Version final {
   public:
    Version(const Version &) = delete;
    Version &operator=(const Version &) = delete;

    Version(Version &&) = default;
    Version &operator=(Version &&) = default;

    explicit Version(std::shared_ptr<SystemApi> api);
    ~Version() = default;

    static outcome::result<void> init( const jsonrpc::Request::Parameters &params );

    static outcome::result<std::string> execute();

private:
    std::shared_ptr<SystemApi> api_;
  };

}  // namespace sgns::api::system::request

#endif  // SUPERGENIUS_API_SYSTEM_REQUEST_VERSION
