
#ifndef SUPERGENIUS_SRC_API_STATE_API_HPP
#define SUPERGENIUS_SRC_API_STATE_API_HPP

#include <boost/optional.hpp>

#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
#include "primitives/common.hpp"
#include "primitives/version.hpp"

namespace sgns::api {

  class StateApi {
   public:
    virtual ~StateApi() = default;
    virtual outcome::result<base::Buffer> getStorage(
        const base::Buffer &key) const = 0;
    virtual outcome::result<base::Buffer> getStorage(
        const base::Buffer &key, const primitives::BlockHash &at) const = 0;
    virtual outcome::result<primitives::Version> getRuntimeVersion(
        const boost::optional<primitives::BlockHash> &at) const = 0;
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_STATE_API_HPP
