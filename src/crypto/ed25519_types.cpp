

#include "ed25519_types.hpp"

namespace sgns::crypto {
  bool ED25519Keypair::operator==(const ED25519Keypair &other) const {
    return private_key == other.private_key && public_key == other.public_key;
  }

  bool ED25519Keypair::operator!=(const ED25519Keypair &other) const {
    return !(*this == other);
  }
}  // namespace sgns::crypto
