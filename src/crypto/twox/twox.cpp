#include "crypto/twox/twox.hpp"

#include <xxhash.h>

namespace sgns::crypto {
  void make_twox64(const uint8_t *in, uint32_t len, uint8_t *out) {
    auto *ptr = reinterpret_cast<uint64_t *>(out);
    ptr[0] = XXH64(in, len, 0);
  }

  base::Hash64 make_twox64(gsl::span<const uint8_t> buf) {
    base::Hash64 hash{};
    make_twox64(buf.data(), buf.size(), hash.data());
    return hash;
  }

  void make_twox128(const uint8_t *in, uint32_t len, uint8_t *out) {
    auto *ptr = reinterpret_cast<uint64_t *>(out);
    ptr[0] = XXH64(in, len, 0);
    ptr[1] = XXH64(in, len, 1);
  }

  base::Hash128 make_twox128(gsl::span<const uint8_t> buf) {
    base::Hash128 hash{};
    make_twox128(buf.data(), buf.size(), hash.data());
    return hash;
  }

  void make_twox256(const uint8_t *in, uint32_t len, uint8_t *out) {
    auto *ptr = reinterpret_cast<uint64_t *>(out);
    ptr[0] = XXH64(in, len, 0);
    ptr[1] = XXH64(in, len, 1);
    ptr[2] = XXH64(in, len, 2);
    ptr[3] = XXH64(in, len, 3);
  }

  base::Hash256 make_twox256(gsl::span<const uint8_t> buf) {
    base::Hash256 hash{};
    make_twox256(buf.data(), buf.size(), hash.data());
    return hash;
  }
}
