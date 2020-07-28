

#ifndef SUPERGENIUS_SRC_PRIMITIVES_BLOCK_ID_HPP
#define SUPERGENIUS_SRC_PRIMITIVES_BLOCK_ID_HPP

#include <boost/variant.hpp>
#include "base/blob.hpp"
#include "primitives/common.hpp"

namespace sgns::primitives {
  using BlockHash = base::Hash256;

  /// Block id is the variant over BlockHash and BlockNumber
  using BlockId = boost::variant<BlockHash, BlockNumber>;
  std::ostream &operator<<(std::ostream &s,
                          const BlockId &test_struct) {
    return s;
  }
}  // namespace sgns::primitives

#endif  // SUPERGENIUS_SRC_PRIMITIVES_BLOCK_ID_HPP
