

#ifndef SUPERGENIUS_SRC_CRYPTO_SR25519_PROVIDER_HPP
#define SUPERGENIUS_SRC_CRYPTO_SR25519_PROVIDER_HPP

#include <gsl/span>
#include "outcome/outcome.hpp"
#include "crypto/sr25519_types.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::crypto {

  /**
   * sr25519 provider error codes
   */
  enum class SR25519ProviderError {
    SIGN_UNKNOWN_ERROR = 1,  // unknown error occured during call to `sign`
                             // method of bound function
    VERIFY_UNKNOWN_ERROR     // unknown error occured during call to `verify`
                             // method of bound function
  };

  class SR25519Provider : public IComponent {
   public:
       ~SR25519Provider() override = default;

    /**
     * Generates random keypair for signing the message
     */
    virtual SR25519Keypair generateKeypair() const = 0;

    /**
     * Generate random keypair from seed
     */
    virtual SR25519Keypair generateKeypair(const SR25519Seed &seed) const = 0;

    /**
     * Sign message \param msg using \param keypair. If computed value is less
     * than \param threshold then return optional containing this value and
     * proof. Otherwise none returned
     * @param keypair pair of public and secret sr25519 keys
     * @param message bytes to be signed
     * @return signed message
     */
    virtual outcome::result<SR25519Signature> sign(
        const SR25519Keypair &keypair, gsl::span<const uint8_t> message) const = 0;

    /**
     * Verifies that \param message was derived using \param public_key on
     * \param signature
     */
    virtual outcome::result<bool> verify(
        const SR25519Signature &signature,
        gsl::span<const uint8_t> message,
        const SR25519PublicKey &public_key) const = 0;
  };
}  // namespace sgns::crypto

OUTCOME_HPP_DECLARE_ERROR_2(sgns::crypto, SR25519ProviderError)

#endif  // SUPERGENIUS_SRC_CRYPTO_SR25519_PROVIDER_HPP
