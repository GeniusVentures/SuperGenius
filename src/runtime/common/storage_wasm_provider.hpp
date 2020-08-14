#ifndef SUPERGENIUS_SRC_RUNTIME_STORAGE_WASM_PROVIDER_HPP
#define SUPERGENIUS_SRC_RUNTIME_STORAGE_WASM_PROVIDER_HPP

#include "runtime/wasm_provider.hpp"

#include "storage/trie/trie_storage.hpp"

namespace sgns::runtime {

  // key for accessing runtime from storage(hex representation of ":code")
  inline const base::Buffer kRuntimeKey =
      base::Buffer::fromHex("3a636f6465").value();

  class StorageWasmProvider : public WasmProvider {
   public:
    ~StorageWasmProvider() override = default;

    explicit StorageWasmProvider(
        std::shared_ptr<const storage::trie::TrieStorage> storage);

    const base::Buffer &getStateCode() const override;

   private:
    std::shared_ptr<const storage::trie::TrieStorage> storage_;
    mutable base::Buffer state_code_;
    mutable base::Buffer last_state_root_;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_STORAGE_WASM_PROVIDER_HPP
