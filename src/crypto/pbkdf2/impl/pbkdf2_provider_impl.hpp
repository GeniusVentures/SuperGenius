#ifndef SUPERGENIUS_CRYPTO_PBKDF2_PROVIDER_IMPL_HPP
#define SUPERGENIUS_CRYPTO_PBKDF2_PROVIDER_IMPL_HPP

#include "crypto/pbkdf2/pbkdf2_provider.hpp"

namespace sgns::crypto {

  class Pbkdf2ProviderImpl : public Pbkdf2Provider {
   public:
    ~Pbkdf2ProviderImpl() override = default;

    outcome::result<base::Buffer> deriveKey(gsl::span<const uint8_t> data,
                                              gsl::span<const uint8_t> salt,
                                              size_t iterations,
                                              size_t key_length) const override;
  };

}

#endif
