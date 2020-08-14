

#include "crypto/sha/sha256.hpp"

#include <openssl/sha.h>

namespace sgns::crypto {
  base::Hash256 sha256(std::string_view input) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *bytes_ptr = reinterpret_cast<const uint8_t *>(input.data());
    return sha256(gsl::make_span(bytes_ptr, input.length()));
  }

  base::Hash256 sha256(gsl::span<const uint8_t> input) {
    base::Hash256 out;
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, input.data(), input.size());
    SHA256_Final(out.data(), &ctx);
    return out;
  }
}  // namespace sgns::crypto
