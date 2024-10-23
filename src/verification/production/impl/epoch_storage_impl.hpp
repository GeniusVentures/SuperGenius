

#ifndef SUPERGENIUS_EPOCH_STORAGE_DUMB_HPP
#define SUPERGENIUS_EPOCH_STORAGE_DUMB_HPP

#include "verification/production/epoch_storage.hpp"
#include "storage/buffer_map_types.hpp"

namespace sgns::verification {

  enum class EpochStorageError { EPOCH_DOES_NOT_EXIST = 1 };

  /**
   * Implementation of epoch storage, which stores and returns epoch descriptors
   * based on their numbers. Epoch descriptors are stored on the disk
   */
  class EpochStorageImpl : public EpochStorage {
   public:
    ~EpochStorageImpl() override = default;

    explicit EpochStorageImpl(std::shared_ptr<storage::BufferStorage> storage);

    outcome::result<void> addEpochDescriptor(
        EpochIndex epoch_number,
        const NextEpochDescriptor &epoch_descriptor) override;

    outcome::result<NextEpochDescriptor> getEpochDescriptor(
        EpochIndex epoch_number) const override;

    std::string GetName() override
    {
      return "EpochStorageImpl";
    }
   private:
    std::shared_ptr<storage::BufferStorage> storage_;
  };
}  // namespace sgns::verification

OUTCOME_HPP_DECLARE_ERROR_2(sgns::verification, EpochStorageError);

#endif  // SUPERGENIUS_EPOCH_STORAGE_DUMB_HPP
