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
    virtual ~VRFProvider() = default;

    /**
     * Generates random keypair for signing the message
     */
    virtual SR25519Keypair generateKeypair() const = 0;

    /**
     * Sign message \param msg using \param keypair. If computed value is less
     * than \param threshold then return optional containing this value and
     * proof. Otherwise none returned
     */
    virtual boost::optional<VRFOutput> sign(
        const base::Buffer &msg,
        const SR25519Keypair &keypair,
        const VRFThreshold &threshold) const = 0;

    /**
     * Verifies that \param output was derived using \param public_key on \param
     * msg
     */
    virtual VRFVerifyOutput verify(const base::Buffer &msg,
                        const VRFOutput &output,
                        const SR25519PublicKey &public_key,
                        const VRFThreshold &threshold) const = 0;
  };
}

#endif 
