#include "crypto/vrf/vrf_provider_impl.hpp"

#include <gsl/span>

#include "base/mp_utils.hpp"

namespace sgns::crypto {
  namespace vrf_constants = constants::sr25519::vrf;

  VRFProviderImpl::VRFProviderImpl(std::shared_ptr<CSPRNG> generator)
      : generator_{std::move(generator)} {}

  SR25519Keypair VRFProviderImpl::generateKeypair() const {
    auto seed = generator_->randomBytes(constants::sr25519::SEED_SIZE);

    std::array<uint8_t, constants::sr25519::KEYPAIR_SIZE> kp{};
    sr25519_keypair_from_seed(kp.data(), seed.data());

    SR25519Keypair keypair;
    std::copy(kp.begin(),
              kp.begin() + constants::sr25519::SECRET_SIZE,
              keypair.secret_key.begin());
    std::copy(kp.begin() + constants::sr25519::SECRET_SIZE,
              kp.begin() + constants::sr25519::SECRET_SIZE
                  + constants::sr25519::PUBLIC_SIZE,
              keypair.public_key.begin());
    return keypair;
  }

  boost::optional<VRFOutput> VRFProviderImpl::sign(
      const base::Buffer &msg,
      const SR25519Keypair &keypair,
      const VRFThreshold &threshold) const {
    base::Buffer keypair_buf{};
    keypair_buf.put(keypair.secret_key).put(keypair.public_key);

    std::array<uint8_t, vrf_constants::OUTPUT_SIZE + vrf_constants::PROOF_SIZE>
        out_proof{};
    auto threshold_bytes = base::uint128_t_to_bytes(threshold);
    auto sign_res =
        sr25519_vrf_sign_if_less(out_proof.data(),
                                 keypair_buf.data(),
                                 msg.data(),
                                 msg.size(),
                                 threshold_bytes.data());
    if (!sign_res.is_less || !(sign_res.result == Sr25519SignatureResult::Ok)) {
      return boost::none;
    }

    VRFOutput res;
    std::copy_n(
        out_proof.begin(), vrf_constants::OUTPUT_SIZE, res.output.begin());
    std::copy_n(out_proof.begin() + vrf_constants::OUTPUT_SIZE,
                vrf_constants::PROOF_SIZE,
                res.proof.begin());

    return res;
  }

  VRFVerifyOutput VRFProviderImpl::verify(const base::Buffer &msg,
                                          const VRFOutput &output,
                                          const SR25519PublicKey &public_key,
                                          const VRFThreshold &threshold) const {
    auto res = sr25519_vrf_verify(public_key.data(),
                                  msg.data(),
                                  msg.size(),
                                  output.output.data(),
                                  output.proof.data(),
                                  base::uint128_t_to_bytes(threshold).data());
    return VRFVerifyOutput{ res.result == Sr25519SignatureResult::Ok, res.is_less };
  }

}
