
#include "api/service/author/requests/submit_extrinsic.hpp"

#include "base/hexutil.hpp"
#include "primitives/extrinsic.hpp"
#include "scale/scale.hpp"

namespace sgns::api::author::request {

  outcome::result<void> SubmitExtrinsic::init(
      const jsonrpc::Request::Parameters &params) {
    if (params.size() != 1) {
      throw jsonrpc::InvalidParametersFault("incorrect number of arguments");
    }

    const auto &arg0 = params[0];
    if (!arg0.IsString()) {
      throw jsonrpc::InvalidParametersFault("invalid argument");
    }

    auto &&hexified_extrinsic = arg0.AsString();
    OUTCOME_TRY((auto &&, buffer), base::unhexWith0x(hexified_extrinsic));
    OUTCOME_TRY((auto &&, extrinsic), scale::decode<primitives::Extrinsic>(buffer));

    extrinsic_ = std::move(extrinsic);

    return outcome::success();
  }

  outcome::result<base::Hash256> SubmitExtrinsic::execute() {
    return api_->submitExtrinsic(extrinsic_);
  }

}  // namespace sgns::api::author::request
