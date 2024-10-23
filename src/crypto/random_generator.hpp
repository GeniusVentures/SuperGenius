#ifndef SUPERGENIUS_SRC_CRYPTO_RANDOM_GENERATOR_HPP
#define SUPERGENIUS_SRC_CRYPTO_RANDOM_GENERATOR_HPP

#include <libp2p/crypto/random_generator.hpp>

namespace sgns::crypto {
  using RandomGenerator = libp2p::crypto::random::RandomGenerator;
  using CSPRNG = libp2p::crypto::random::CSPRNG;
}

#endif
