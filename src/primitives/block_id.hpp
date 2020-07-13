

#ifndef SUPERGENIUS_CORE_PRIMITIVES_BLOCK_ID_HPP
#define SUPERGENIUS_CORE_PRIMITIVES_BLOCK_ID_HPP

#include <boost/variant.hpp>
#include "common/blob.hpp"
#include "primitives/common.hpp"

namespace sgns::primitives {
  using BlockHash = common::Hash256;

  /// Block id is the variant over BlockHash and BlockNumber
  using BlockId = boost::variant<BlockHash, BlockNumber>;
}  // namespace sgns::primitives

#endif  // SUPERGENIUS_CORE_PRIMITIVES_BLOCK_ID_HPP
