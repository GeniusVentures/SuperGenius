

#ifndef SUPERGENIUS_TEST_MOCK_CORE_CRYPTO_ED25519_PROVIDER_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_CORE_CRYPTO_ED25519_PROVIDER_MOCK_HPP

#include "crypto/ed25519_provider.hpp"

#include <gmock/gmock.h>

namespace sgns::crypto {

  class ED25519ProviderMock : public ED25519Provider {
   public:
    MOCK_CONST_METHOD0(generateKeypair, outcome::result<ED25519Keypair>());
    MOCK_CONST_METHOD1(generateKeypair,
                       outcome::result<ED25519Keypair>(const ED25519Seed &));
    MOCK_CONST_METHOD2(sign,
                       outcome::result<ED25519Signature>(const ED25519Keypair &,
                                                         gsl::span<uint8_t>));
    MOCK_CONST_METHOD3(
        verify,
        outcome::result<bool>(const ED25519Signature &signature,
                              gsl::span<uint8_t> message,
                              const ED25519PublicKey &public_key));

    MOCK_METHOD0(GetName,std::string());
  };

}  // namespace sgns::crypto

#endif  // SUPERGENIUS_TEST_MOCK_CORE_CRYPTO_ED25519_PROVIDER_MOCK_HPP
