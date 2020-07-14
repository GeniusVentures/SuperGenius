

#include "testutil/primitives/mp_utils.hpp"

namespace testutil {
  sgns::base::Hash256 createHash256(std::initializer_list<uint8_t> bytes) {
    sgns::base::Hash256 h;
    h.fill(0u);
    std::copy_n(bytes.begin(), bytes.size(), h.begin());
    return h;
  }
}  // namespace testutil
