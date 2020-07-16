

#include "verification/production/impl/production_lottery_impl.hpp"

#include <unordered_set>

#include <boost/assert.hpp>
#include "base/buffer.hpp"
#include "base/mp_utils.hpp"

namespace sgns::verification {
  using base::Buffer;
  namespace vrf_constants = crypto::constants::sr25519::vrf;

  ProductionLotteryImpl::ProductionLotteryImpl(
      std::shared_ptr<crypto::VRFProvider> vrf_provider,
      std::shared_ptr<crypto::Hasher> hasher)
      : vrf_provider_{std::move(vrf_provider)},
        hasher_{std::move(hasher)},
        logger_{base::createLogger("ProductionLottery")} {
    BOOST_ASSERT(vrf_provider_);
    BOOST_ASSERT(hasher_);
    BOOST_ASSERT(logger_);
  }

  ProductionLottery::SlotsLeadership ProductionLotteryImpl::slotsLeadership(
      const Epoch &epoch,
      const Threshold &threshold,
      const crypto::SR25519Keypair &keypair) const {
    ProductionLottery::SlotsLeadership result;
    result.reserve(epoch.epoch_duration);

    // randomness || slot number
    Buffer vrf_input(vrf_constants::OUTPUT_SIZE + 8, 0);

    // the first part - randomness - is always the same, while the slot number
    // obviously changes depending on the slot we are computing for
    std::copy(
        epoch.randomness.begin(), epoch.randomness.end(), vrf_input.begin());

    auto slot_number_begin = vrf_input.begin() + vrf_constants::OUTPUT_SIZE;
    for (ProductionSlotNumber i = epoch.epoch_index * epoch.epoch_duration;
         i < (epoch.epoch_index + 1) * epoch.epoch_duration;
         ++i) {
      auto slot_bytes = base::uint64_t_to_bytes(i);
      std::copy(slot_bytes.begin(), slot_bytes.end(), slot_number_begin);
      auto sign_opt = vrf_provider_->sign(vrf_input, keypair, threshold);
      result.push_back(sign_opt);
    }

    return result;
  }

  Randomness ProductionLotteryImpl::computeRandomness(
      const Randomness &last_epoch_randomness, EpochIndex last_epoch_index) {
    static std::unordered_set<EpochIndex> computed_epochs_randomnesses{};

    // the function must never be called twice for the same epoch
    BOOST_ASSERT(computed_epochs_randomnesses.insert(last_epoch_index).second);

    // randomness || epoch_index || rho
    Buffer new_randomness(
        vrf_constants::OUTPUT_SIZE + 8 + last_epoch_vrf_values_.size() * 32, 0);

    std::copy(last_epoch_randomness.begin(),
              last_epoch_randomness.end(),
              new_randomness.begin());

    auto epoch_index_bytes = base::uint64_t_to_bytes(last_epoch_index);
    std::copy(epoch_index_bytes.begin(),
              epoch_index_bytes.end(),
              new_randomness.begin() + vrf_constants::OUTPUT_SIZE);

    auto new_vrf_value_begin =
        new_randomness.begin() + vrf_constants::OUTPUT_SIZE + 8;
    // NOLINTNEXTLINE
    for (size_t i = 0; i < last_epoch_vrf_values_.size(); ++i) {
      auto const &value_bytes = last_epoch_vrf_values_[i];
      std::copy(value_bytes.begin(), value_bytes.end(), new_vrf_value_begin);
      new_vrf_value_begin += 32;
    }
    last_epoch_vrf_values_.clear();

    return hasher_->blake2b_256(new_randomness);
  }

  void ProductionLotteryImpl::submitVRFValue(const crypto::VRFPreOutput &value) {
    last_epoch_vrf_values_.push_back(value);
  }
}  // namespace sgns::verification
