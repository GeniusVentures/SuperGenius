#ifndef SUPERGENIUS_SHA256_HPP
#define SUPERGENIUS_SHA256_HPP

#include <string_view>

#include <gsl/span>
#include "base/blob.hpp"

namespace sgns::crypto {
  /**
   * Take a SHA-256 hash from string
   * @param input to be hashed
   * @return hashed bytes
   */
  base::Hash256 sha256(std::string_view input);

  /**
   * Take a SHA-256 hash from bytes
   * @param input to be hashed
   * @return hashed bytes
   */
  base::Hash256 sha256(gsl::span<const uint8_t> input);

  std::vector<uint8_t> sha256(const void* data, size_t dataSize);
}

#endif
