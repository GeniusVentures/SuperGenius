

#ifndef SUPERGENIUS_SHA256_HPP
#define SUPERGENIUS_SHA256_HPP

#include <string_view>

#include <gsl/span>
#include "common/blob.hpp"

namespace sgns::crypto {
  /**
   * Take a SHA-256 hash from string
   * @param input to be hashed
   * @return hashed bytes
   */
  common::Hash256 sha256(std::string_view input);

  /**
   * Take a SHA-256 hash from bytes
   * @param input to be hashed
   * @return hashed bytes
   */
  common::Hash256 sha256(gsl::span<const uint8_t> input);
}  // namespace sgns::crypto

#endif  // SUPERGENIUS_SHA256_HPP
