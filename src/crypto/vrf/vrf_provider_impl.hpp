

#ifndef SUPERGENIUS_SRC_VERIFICATION_VRF_VRF_HPP
#define SUPERGENIUS_SRC_VERIFICATION_VRF_VRF_HPP

#include "crypto/vrf_provider.hpp"

#include <boost/optional.hpp>
#include "base/buffer.hpp"
#include "crypto/random_generator.hpp"

namespace sgns::crypto {

  class VRFProviderImpl : public VRFProvider {
   public:
    explicit VRFProviderImpl(std::shared_ptr<CSPRNG> generator);

    ~VRFProviderImpl() override = default;

    SR25519Keypair generateKeypair() const override;

    boost::optional<VRFOutput> sign(const base::Buffer &msg,
                                    const SR25519Keypair &keypair,
                                    const VRFThreshold &threshold) const override;

    VRFVerifyOutput verify(const base::Buffer &msg,
                const VRFOutput &output,
                const SR25519PublicKey &public_key,
                const VRFThreshold &threshold) const override;

   private:
    std::shared_ptr<CSPRNG> generator_;
  };
}  // namespace sgns::crypto

#endif  // SUPERGENIUS_SRC_VERIFICATION_VRF_VRF_HPP
