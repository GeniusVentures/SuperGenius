

#ifndef SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_IMPL_PRODUCTION_DIGESTS_UTIL_HPP
#define SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_IMPL_PRODUCTION_DIGESTS_UTIL_HPP

#include <boost/optional.hpp>

#include "base/visitor.hpp"
#include "verification/production/types/production_block_header.hpp"
#include "verification/production/types/next_epoch_descriptor.hpp"
#include "verification/production/types/seal.hpp"
#include "outcome/outcome.hpp"
#include "primitives/block.hpp"

namespace sgns::verification {

  enum class DigestError {
    INVALID_DIGESTS = 1,
    MULTIPLE_EPOCH_CHANGE_DIGESTS,
    NEXT_EPOCH_DIGEST_DOES_NOT_EXIST
  };

  template <typename T, typename VarT>
  boost::optional<T> getFromVariant(VarT &&v) {
    return visit_in_place(
        v,
        [](const T &expected_val) -> boost::optional<T> {
          return boost::get<T>(expected_val);
        },
        [](const auto &) -> boost::optional<T> { return boost::none; });
  }

  outcome::result<std::pair<Seal, ProductionBlockHeader>> getProductionDigests(
      const primitives::BlockHeader &header);

  outcome::result<NextEpochDescriptor> getNextEpochDigest(
      const primitives::BlockHeader &header);

}  // namespace sgns::verification

OUTCOME_HPP_DECLARE_ERROR(sgns::verification, DigestError)

#endif  // SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_IMPL_PRODUCTION_DIGESTS_UTIL_HPP
