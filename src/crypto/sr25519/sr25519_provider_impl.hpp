#ifndef SUPERGENIUS_SRC_CRYPTO_SR25519_SR25519_PROVIDER_IMPL_HPP
#define SUPERGENIUS_SRC_CRYPTO_SR25519_SR25519_PROVIDER_IMPL_HPP

#include "crypto/sr25519_provider.hpp"

namespace libp2p::crypto::random {
  class CSPRNG;
}

namespace sgns::crypto {
  class SR25519ProviderImpl : public SR25519Provider {
    using CSPRNG = libp2p::crypto::random::CSPRNG;

   public:
    explicit SR25519ProviderImpl(std::shared_ptr<CSPRNG> generator);

    ~SR25519ProviderImpl() override = default;

    SR25519Keypair generateKeypair() const override;

    SR25519Keypair generateKeypair(const SR25519Seed &seed) const override;

    outcome::result<SR25519Signature> sign(
        const SR25519Keypair &keypair,
        gsl::span<const uint8_t> message) const override;

    outcome::result<bool> verify(
        const SR25519Signature &signature,
        gsl::span<const uint8_t> message,
        const SR25519PublicKey &public_key) const override;
    std::string GetName() override
    {
      return "SR25519ProviderImpl";
    }

   private:
    std::shared_ptr<CSPRNG> generator_;
  };

}

#endif
