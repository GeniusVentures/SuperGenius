

#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_API_CORE_FACTORY_IMPL
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_API_CORE_FACTORY_IMPL

#include "runtime/binaryen/runtime_manager.hpp"
#include "runtime/core_factory.hpp"

namespace sgns::blockchain {
  class BlockHeaderRepository;
}
namespace sgns::storage::changes_trie {
  class ChangesTracker;
}

namespace sgns::runtime::binaryen {

  class CoreFactoryImpl : public CoreFactory {
   public:
    CoreFactoryImpl(
        std::shared_ptr<RuntimeManager> runtime_manager,
        std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker,
        std::shared_ptr<blockchain::BlockHeaderRepository> header_repo);
    ~CoreFactoryImpl() override = default;

    std::unique_ptr<Core> createWithCode(
        std::shared_ptr<WasmProvider> wasm_provider) override;

   private:
    std::shared_ptr<RuntimeManager> runtime_manager_;
    std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker_;
    std::shared_ptr<blockchain::BlockHeaderRepository> header_repo_;
  };

}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_API_CORE_FACTORY_IMPL
