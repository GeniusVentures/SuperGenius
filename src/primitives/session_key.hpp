

#ifndef SUPERGENIUS_CORE_PRIMITIVES_SESSION_KEY_HPP
#define SUPERGENIUS_CORE_PRIMITIVES_SESSION_KEY_HPP

#include "base/blob.hpp"

namespace sgns::primitives {
  // TODO(akvinikym): must be a SR25519 key
  using SessionKey = base::Blob<32>;
}  // namespace sgns::primitives

#endif  // SUPERGENIUS_CORE_PRIMITIVES_SESSION_KEY_HPP
