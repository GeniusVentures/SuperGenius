

#ifndef SUPERGENIUS_BIP39_TYPES_HPP
#define SUPERGENIUS_BIP39_TYPES_HPP

#include "base/buffer.hpp"

namespace sgns::crypto::bip39 {
  namespace constants {
    constexpr size_t BIP39_SEED_LEN_512 = 64u;
  }  // namespace constants

  using Bip39Seed = base::Buffer;
}  // namespace sgns::crypto::bip39

#endif  // SUPERGENIUS_BIP39_TYPES_HPP
