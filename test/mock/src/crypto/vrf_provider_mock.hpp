

#ifndef SUPERGENIUS_VRF_PROVIDER_MOCK_HPP
#define SUPERGENIUS_VRF_PROVIDER_MOCK_HPP

#include <gmock/gmock.h>

#include "crypto/vrf_provider.hpp"

namespace sgns::crypto {
  //added to fix link error
  std::ostream &operator<<(std::ostream &out, const boost::optional<VRFOutput> &test_struct)
  {
    return out << test_struct.get();
  }
  //end
  struct VRFProviderMock : public VRFProvider {
    MOCK_CONST_METHOD0(generateKeypair, SR25519Keypair());

    MOCK_CONST_METHOD3(sign,
                       boost::optional<VRFOutput>(const base::Buffer &,
                                                  const SR25519Keypair &,
                                                  const VRFThreshold &));
    MOCK_CONST_METHOD4(verify,
                       VRFVerifyOutput(const base::Buffer &,
                            const VRFOutput &,
                            const SR25519PublicKey &,
                            const VRFThreshold &));

    MOCK_METHOD0(GetName, std::string());
  };
}  // namespace sgns::crypto

#endif  // SUPERGENIUS_VRF_PROVIDER_MOCK_HPP
