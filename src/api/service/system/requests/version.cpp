#include "api/service/system/requests/version.hpp"

namespace sgns::api::system::request {

  Version::Version(std::shared_ptr<SystemApi> api) : api_(std::move(api)) {
    BOOST_ASSERT(api_ != nullptr);
  }

  outcome::result<void> Version::init(
      const jsonrpc::Request::Parameters &params) {
    if (!params.empty()) {
      throw jsonrpc::InvalidParametersFault("Method should not have params");
    }
    return outcome::success();
  }

  outcome::result<std::string> Version::execute() {
    // TODO(xDimon): Need to replace hardcode by some config or generated code
    return "0.0.1";
  }

}  // namespace sgns::api::system::request
