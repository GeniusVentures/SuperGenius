#ifndef SUPERGENIUS_BIP39_PROVIDER_HPP
#define SUPERGENIUS_BIP39_PROVIDER_HPP

#include "crypto/bip39/bip39_types.hpp"

namespace sgns::crypto {

  /**
   * @brief Interface for creating BIP-39 entropy and seeds from mnemonic
   *        wordlists.
   */
  class Bip39Provider {
   public:
    virtual ~Bip39Provider() = default;

    /**
     * @brief Calculates entropy from a mnemonic word list.
     * @param word_list Mnemonic words in order.
     * @return Entropy value.
     */
    virtual outcome::result<std::vector<uint8_t>> calculateEntropy(
        const std::vector<std::string> &word_list) = 0;

    /**
     * @brief Derives a seed from entropy and an optional password.
     * @param entropy Entropy bytes.
     * @param password Optional passphrase used for seed derivation.
     * @return Seed bytes.
     */
    virtual outcome::result<bip39::Bip39Seed> makeSeed(
        gsl::span<const uint8_t> entropy, std::string_view password) = 0;
  };

}  // namespace sgns::crypto

#endif
