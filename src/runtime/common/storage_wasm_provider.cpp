
#include "runtime/common/storage_wasm_provider.hpp"

namespace sgns::runtime {

  StorageWasmProvider::StorageWasmProvider(
      std::shared_ptr<const storage::trie::TrieStorage> storage)
      : storage_{std::move(storage)} {
    BOOST_ASSERT(storage_ != nullptr);

    last_state_root_ = storage_->getRootHash();
    auto batch = storage_->getEphemeralBatch();
    BOOST_ASSERT_MSG(batch.has_value(),
                     "Error getting a batch of the storage");
    auto state_code_res = batch.value()->get(kRuntimeKey);
    BOOST_ASSERT_MSG(state_code_res.has_value(),
                     "Runtime code does not exist in the storage");
    state_code_ = state_code_res.value();
  }

  const base::Buffer &StorageWasmProvider::getStateCode() const {
    auto current_state_root = storage_->getRootHash();
    if (last_state_root_ == current_state_root) {
      return state_code_;
    }
    last_state_root_ = current_state_root;

    auto batch = storage_->getEphemeralBatch();
    BOOST_ASSERT_MSG(batch.has_value(),
                     "Error getting a batch of the storage");
    auto state_code_res = batch.value()->get(kRuntimeKey);
    BOOST_ASSERT_MSG(state_code_res.has_value(),
                     "Runtime code does not exist in the storage");
    state_code_ = state_code_res.value();
    return state_code_;
  }

}  // namespace sgns::runtime
