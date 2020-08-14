

#ifndef SUPERGENIUS_SRC_CRYPTO_ED25519_ED25519_PROVIDER_IMPL_HPP
#define SUPERGENIUS_SRC_CRYPTO_ED25519_ED25519_PROVIDER_IMPL_HPP

#include "crypto/ed25519_provider.hpp"

namespace sgns::crypto {

  class ED25519ProviderImpl : public ED25519Provider {
   public:
    ~ED25519ProviderImpl() override = default;

    outcome::result<ED25519Keypair> generateKeypair() const override;

    ED25519Keypair generateKeypair(const ED25519Seed &seed) const override;

    outcome::result<sgns::crypto::ED25519Signature> sign(
        const ED25519Keypair &keypair,
        gsl::span<uint8_t> message) const override;

    outcome::result<bool> verify(
        const ED25519Signature &signature,
        gsl::span<uint8_t> message,
        const ED25519PublicKey &public_key) const override;
  };

}  // namespace sgns::crypto

#endif  // SUPERGENIUS_SRC_CRYPTO_ED25519_ED25519_PROVIDER_IMPL_HPP
