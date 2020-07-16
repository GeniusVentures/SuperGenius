

#ifndef SUPERGENIUS_CRYPTO_PBKDF2_PROVIDER_HPP
#define SUPERGENIUS_CRYPTO_PBKDF2_PROVIDER_HPP

#include <gsl/span>
#include "base/buffer.hpp"
#include "outcome/outcome.hpp"

namespace sgns::crypto {

  enum class Pbkdf2ProviderError { KEY_DERIVATION_FAILED = 1 };

  /**
   * @class Pbkdf2Provider provides key derivation functionality
   */
  class Pbkdf2Provider {
   public:
    virtual ~Pbkdf2Provider() = default;

    /**
     * @brief derives key from password and salt
     * @param data entropy or password
     * @param salt salt
     * @param iterations number of iterations
     * @param key_length length of generated key
     * @return derived key
     */
    virtual outcome::result<base::Buffer> deriveKey(
        gsl::span<const uint8_t> data,
        gsl::span<const uint8_t> salt,
        size_t iterations,
        size_t key_length) const = 0;
  };
}  // namespace sgns::crypto

OUTCOME_HPP_DECLARE_ERROR_2(sgns::crypto, Pbkdf2ProviderError);

#endif  // SUPERGENIUS_CRYPTO_PBKDF2_PROVIDER_HPP
