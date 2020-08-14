

#include "crypto/pbkdf2/impl/pbkdf2_provider_impl.hpp"

#include <openssl/evp.h>

namespace sgns::crypto {

  outcome::result<base::Buffer> Pbkdf2ProviderImpl::deriveKey(
      gsl::span<const uint8_t> data,
      gsl::span<const uint8_t> salt,
      size_t iterations,
      size_t key_length) const {
    base::Buffer out(key_length, 0);
    const auto *digest = EVP_sha512();

    std::string pass(data.begin(), data.end());

    int res = PKCS5_PBKDF2_HMAC(pass.data(),
                                pass.size(),
                                salt.data(),
                                salt.size(),
                                iterations,
                                digest,
                                key_length,
                                out.data());
    if (res != 1) {
      return Pbkdf2ProviderError::KEY_DERIVATION_FAILED;
    }

    return out;
  }
}  // namespace sgns::crypto

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::crypto, Pbkdf2ProviderError, error) {
  using Error = sgns::crypto::Pbkdf2ProviderError;
  switch (error) {
    case Error::KEY_DERIVATION_FAILED:
      return "failed to derive key";
  }
  return "unknown Pbkdf2ProviderError";
}
