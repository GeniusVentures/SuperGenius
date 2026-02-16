#ifndef SUPERGENIUS_SRC_CRYPTO_VRF_VRF_PROVIDER_HPP
#define SUPERGENIUS_SRC_CRYPTO_VRF_VRF_PROVIDER_HPP

#include <boost/optional.hpp>

#include "base/buffer.hpp"
#include "crypto/sr25519_types.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::crypto {
  /**
   * SR25519 based verifiable random function implementation
   */
  class VRFProvider : public IComponent {
   public:
       ~VRFProvider() override = default;

    /**
     * Generates random keypair for signing the message
     */
    virtual SR25519Keypair generateKeypair() const = 0;

    /**
     * @brief Sign message using the given keypair and threshold.
     * @param msg Message bytes.
     * @param keypair SR25519 keypair.
     * @param threshold VRF threshold.
     * @return Optional VRF output if threshold satisfied.
     */
    virtual boost::optional<VRFOutput> sign(
        const base::Buffer &msg,
        const SR25519Keypair &keypair,
        const VRFThreshold &threshold) const = 0;

    /**
     * @brief Verify VRF output against message and public key.
     * @param msg Message bytes.
     * @param output VRF output to verify.
     * @param public_key Public key to verify against.
     * @param threshold VRF threshold.
     * @return Verification output.
     */
    virtual VRFVerifyOutput verify(const base::Buffer &msg,
                        const VRFOutput &output,
                        const SR25519PublicKey &public_key,
                        const VRFThreshold &threshold) const = 0;
  };
}

#endif 
