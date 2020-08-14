

#ifndef SUPERGENIUS_PRODUCTION_LOTTERY_IMPL_HPP
#define SUPERGENIUS_PRODUCTION_LOTTERY_IMPL_HPP

#include <memory>
#include <vector>

#include "base/logger.hpp"
#include "verification/production/production_lottery.hpp"
#include "crypto/hasher.hpp"
#include "crypto/vrf_provider.hpp"

namespace sgns::verification {
  class ProductionLotteryImpl : public ProductionLottery {
   public:
    ProductionLotteryImpl(std::shared_ptr<crypto::VRFProvider> vrf_provider,
                    std::shared_ptr<crypto::Hasher> hasher);

    SlotsLeadership slotsLeadership(
        const Epoch &epoch,
        const Threshold &threshold,
        const crypto::SR25519Keypair &keypair) const override;

    Randomness computeRandomness(const Randomness &last_epoch_randomness,
                                 EpochIndex last_epoch_index) override;

    void submitVRFValue(const crypto::VRFPreOutput &value) override;

   private:
    std::shared_ptr<crypto::VRFProvider> vrf_provider_;
    std::shared_ptr<crypto::Hasher> hasher_;

    /// also known as "rho" (greek letter) in the spec
    std::vector<crypto::VRFPreOutput> last_epoch_vrf_values_;
    base::Logger logger_;
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_PRODUCTION_LOTTERY_IMPL_HPP
