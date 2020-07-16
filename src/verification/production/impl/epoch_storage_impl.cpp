

#include "verification/production/impl/epoch_storage_impl.hpp"

#include "scale/scale.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::verification, EpochStorageError, e) {
  using E = sgns::verification::EpochStorageError;

  switch (e) {
    case E::EPOCH_DOES_NOT_EXIST:
      return "Requested epoch does not exist";
  }
  return "Unknown error";
}

namespace sgns::verification {

  const auto EPOCH_PREFIX = base::Blob<8>::fromString("epchdcr0").value();

  EpochStorageImpl::EpochStorageImpl(
      std::shared_ptr<sgns::storage::BufferStorage> storage)
      : storage_{std::move(storage)} {
    BOOST_ASSERT(storage_);
  }

  outcome::result<void> EpochStorageImpl::addEpochDescriptor(
      EpochIndex epoch_number, const NextEpochDescriptor &epoch_descriptor) {
    auto key = base::Buffer{EPOCH_PREFIX}.putUint64(epoch_number);
    auto val = base::Buffer{scale::encode(epoch_descriptor).value()};
    return storage_->put(key, val);
  }

  outcome::result<NextEpochDescriptor> EpochStorageImpl::getEpochDescriptor(
      EpochIndex epoch_number) const {
    auto key = base::Buffer{EPOCH_PREFIX}.putUint64(epoch_number);
    OUTCOME_TRY(encoded_ed, storage_->get(key));
    return scale::decode<NextEpochDescriptor>(encoded_ed);
  }
}  // namespace sgns::verification
