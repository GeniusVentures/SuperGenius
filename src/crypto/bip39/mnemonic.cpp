

#include "crypto/bip39/mnemonic.hpp"

#include <codecvt>
#include <locale>

#include <boost/algorithm/string.hpp>
#include "base/logger.hpp"
#include "crypto/bip39/bip39_types.hpp"

namespace sgns::crypto::bip39 {

  namespace {
    constexpr auto kMnemonicLoggerString = "Mnemonic";
    /**
     * @return true if string s is a valid utf-8, false otherwise
     */
      bool isValidUtf8(const std::string &s) {
          const unsigned char* bytes = reinterpret_cast<const unsigned char*>(s.data());
          size_t len = s.length();

          for (size_t i = 0; i < len; i++) {
              // Single byte character (0xxxxxxx)
              if (bytes[i] <= 0x7F) {
                  continue;
              }

              // Start of a 2-byte character (110xxxxx)
              else if ((bytes[i] & 0xE0) == 0xC0) {
                  // Check if we have at least one more byte
                  if (i + 1 >= len) return false;

                  // Check if the next byte is a continuation byte (10xxxxxx)
                  if ((bytes[i + 1] & 0xC0) != 0x80) return false;

                  i += 1;
              }

              // Start of a 3-byte character (1110xxxx)
              else if ((bytes[i] & 0xF0) == 0xE0) {
                  // Check if we have at least two more bytes
                  if (i + 2 >= len) return false;

                  // Check if the next two bytes are continuation bytes (10xxxxxx)
                  if ((bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80) return false;

                  i += 2;
              }

              // Start of a 4-byte character (11110xxx)
              else if ((bytes[i] & 0xF8) == 0xF0) {
                  // Check if we have at least three more bytes
                  if (i + 3 >= len) return false;

                  // Check if the next three bytes are continuation bytes (10xxxxxx)
                  if ((bytes[i + 1] & 0xC0) != 0x80 ||
                      (bytes[i + 2] & 0xC0) != 0x80 ||
                      (bytes[i + 3] & 0xC0) != 0x80) return false;

                  i += 3;
              }

              // Invalid start byte
              else {
                  return false;
              }
          }

          return true;
      }
  }  // namespace

  outcome::result<Mnemonic> Mnemonic::parse(std::string_view phrase) {
    if (phrase.empty() || !isValidUtf8(std::string(phrase))) {
      return bip39::MnemonicError::INVALID_MNEMONIC;
    }

    auto password_pos = phrase.find("///");
    std::string_view mnemonic_list;
    std::string_view password;
    if (password_pos != std::string_view::npos) {
      // need to normalize password utf8 nfkd
      password = phrase.substr(password_pos + 3);
      mnemonic_list = phrase.substr(0, password_pos);
    } else {
      mnemonic_list = phrase;
    }

    if ( mnemonic_list.find( '/' ) != std::string_view::npos )
    {
        base::createLogger( kMnemonicLoggerString )->error( "junctions are not supported yet" );
        return bip39::MnemonicError::INVALID_MNEMONIC;
    }

    // split word list into separate words
    std::vector<std::string> word_list;
    boost::split(word_list, mnemonic_list, boost::algorithm::is_space());

    Mnemonic mnemonic{std::move(word_list), std::string(password)};
    return mnemonic;
    //
  }
}  // namespace sgns::crypto::bip39

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::crypto::bip39, MnemonicError, e) {
  using Error = sgns::crypto::bip39::MnemonicError;
  switch (e) {
    case Error::INVALID_MNEMONIC:
      return "Mnemonic provided is not valid";
  }
  return "unknown MnemonicError";
}
