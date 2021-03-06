

#ifndef SUPERGENIUS_SR25519_UTILS_HPP
#define SUPERGENIUS_SR25519_UTILS_HPP

#include "crypto/sr25519_types.hpp"

namespace sr25519_constants = sgns::crypto::constants::sr25519;

/**
 * Generate a SR25519 with some seed
 */
sgns::crypto::SR25519Keypair generateSR25519Keypair() {
  std::array<uint8_t, sr25519_constants::SEED_SIZE> seed{};
  seed.fill(1);
  std::array<uint8_t, sr25519_constants::KEYPAIR_SIZE> kp;
  sr25519_keypair_from_seed(kp.data(), seed.data());
  sgns::crypto::SR25519Keypair keypair;
  std::copy(kp.begin(),
            kp.begin() + sr25519_constants::SECRET_SIZE,
            keypair.secret_key.begin());
  std::copy(kp.begin() + sr25519_constants::SECRET_SIZE,
            kp.begin() + sr25519_constants::SECRET_SIZE
                + sr25519_constants::PUBLIC_SIZE,
            keypair.public_key.begin());
  return keypair;
}

#endif  // SUPERGENIUS_SR25519_UTILS_HPP
