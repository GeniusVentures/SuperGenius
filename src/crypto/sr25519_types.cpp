#include "crypto/sr25519_types.hpp"

namespace sgns::crypto {

  bool VRFOutput::operator==(const VRFOutput &other) const {
    return output == other.output && proof == other.proof;
  }
  bool VRFOutput::operator!=(const VRFOutput &other) const {
    return !(*this == other);
  }

  bool SR25519Keypair::operator==(const SR25519Keypair &other) const {
    return secret_key == other.secret_key && public_key == other.public_key;
  }

  bool SR25519Keypair::operator!=(const SR25519Keypair &other) const {
    return !(*this == other);
  }

}
