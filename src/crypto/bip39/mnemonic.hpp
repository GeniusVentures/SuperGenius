

#ifndef SUPERGENIUS_CRYPTO_BIP39_MNEMONIC_HPP
#define SUPERGENIUS_CRYPTO_BIP39_MNEMONIC_HPP

#include <string>
#include <vector>

#include "common/outcome.hpp"

namespace sgns::crypto::bip39 {
  enum class MnemonicError {
    INVALID_MNEMONIC = 1,
  };

  struct Mnemonic {
    std::vector<std::string> words;
    std::string password;
    /**
     * @brief parse mnemonic from phrase
     * @param phrase valid utf8 list of words from bip-39 word list
     * @return Mnemonic instance
     */
    static outcome::result<Mnemonic> parse(std::string_view phrase);
  };
}  // namespace sgns::crypto::bip39

OUTCOME_HPP_DECLARE_ERROR_2(sgns::crypto::bip39, MnemonicError);

#endif  // SUPERGENIUS_CRYPTO_BIP39_MNEMONIC_HPP
