

#ifndef SUPERGENIUS_SRC_CRYPTO_VRF_TYPES
#define SUPERGENIUS_SRC_CRYPTO_VRF_TYPES

extern "C" {
#include <sr25519/sr25519.h>
}
#include <boost/multiprecision/cpp_int.hpp>
#include <gsl/span>

#include "base/blob.hpp"
#include "base/mp_utils.hpp"

namespace sgns::crypto {
  namespace constants::sr25519 {
    /**
     * Important constants to deal with sr25519
     */
    enum {
      KEYPAIR_SIZE = SR25519_KEYPAIR_SIZE,
      SECRET_SIZE = SR25519_SECRET_SIZE,
      PUBLIC_SIZE = SR25519_PUBLIC_SIZE,
      SIGNATURE_SIZE = SR25519_SIGNATURE_SIZE,
      SEED_SIZE = SR25519_SEED_SIZE
    };

    namespace vrf {
      /**
       * Important constants to deal with vrf
       */
      enum {
        PROOF_SIZE = SR25519_VRF_PROOF_SIZE,
        OUTPUT_SIZE = SR25519_VRF_OUTPUT_SIZE
      };
    }  // namespace vrf

  }  // namespace constants::sr25519

  using VRFPreOutput =
      std::array<uint8_t, constants::sr25519::vrf::OUTPUT_SIZE>;
  using VRFThreshold = boost::multiprecision::uint128_t;
  using VRFProof = std::array<uint8_t, constants::sr25519::vrf::PROOF_SIZE>;

  /**
   * Output of a verifiable random function.
   * Consists of pre-output, which is an internal representation of the
   * generated random value, and the proof to this value that servers as the
   * verification of its randomness.
   */
  struct VRFOutput {
    // an internal representation of the generated random value
    VRFPreOutput output{};
    // the proof to the output, serves as the verification of its randomness
    VRFProof proof{};

    bool operator==(const VRFOutput &other) const;
    bool operator!=(const VRFOutput &other) const;
    //added to fix gtest link error
    friend std::ostream &operator<<(std::ostream &out, const VRFOutput &test_struct)
    {
      out << test_struct.output.size();
      // for(auto it = test_struct.output.begin(); it != test_struct.output.end() ; it++)
      // {
      //   out << *it ;
      // }
      
      out << test_struct.proof.size(); 
      // for(auto it = test_struct.proof.begin(); it != test_struct.proof.end() ; it++)
      // {
      //   out << *it ;
      // }  
      return out;
    }
    //end
  };

  /**
   * Output of a verifiable random function verification.
   */
  struct VRFVerifyOutput {
    // indicates if the proof is valid
    bool is_valid;
    // indicates if the value is less than the provided threshold
    bool is_less;
    //added to fix gtest link error
    friend std::ostream &operator<<(std::ostream &out, const VRFVerifyOutput &test_struct)
    {
      return out << test_struct.is_valid << test_struct.is_less; 
    }
    //end
  };

  using SR25519SecretKey = base::Blob<constants::sr25519::SECRET_SIZE>;

  using SR25519PublicKey = base::Blob<constants::sr25519::PUBLIC_SIZE>;

  using SR25519Seed = base::Blob<constants::sr25519::SEED_SIZE>;

  struct SR25519Keypair {
    SR25519SecretKey secret_key;
    SR25519PublicKey public_key;

    SR25519Keypair() = default;

    bool operator==(const SR25519Keypair &other) const;
    bool operator!=(const SR25519Keypair &other) const;

    friend std::ostream &operator<<(std::ostream &out, const SR25519Keypair &test_struct)
    {
      return out << test_struct.secret_key << test_struct.public_key; 
    }

  };

  using SR25519Signature = base::Blob<constants::sr25519::SIGNATURE_SIZE>;

  /**
   * @brief outputs object of type VRFOutput to stream
   * @tparam Stream output stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const VRFOutput &o) {
    return s << o.output << o.proof;
  }

  /**
   * @brief decodes object of type VRFOutput from stream
   * @tparam Stream input stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, VRFOutput &o) {
    return s >> o.output >> o.proof;
  }

}  // namespace sgns::crypto

#endif  // SUPERGENIUS_SRC_CRYPTO_VRF_TYPES
