
#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_API_RUNTIME_MANAGER
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_API_RUNTIME_MANAGER

#include "base/blob.hpp"
#include "base/logger.hpp"
#include "crypto/hasher.hpp"
#include "extensions/extension_factory.hpp"
#include "outcome/outcome.hpp"
#include "runtime/binaryen/module/wasm_module_factory.hpp"
#include "runtime/binaryen/runtime_environment.hpp"
#include "runtime/binaryen/runtime_external_interface.hpp"
#include "runtime/trie_storage_provider.hpp"
#include "runtime/wasm_provider.hpp"
#include "storage/trie/trie_batches.hpp"
#include "storage/trie/trie_storage.hpp"

namespace sgns::runtime::binaryen {

  /**
   * @brief RuntimeManager is a mechanism to prepare environment for launching
   * execute() function of runtime APIs. It supports in-memory cache to reuse
   * existing environments, avoid hi-load operations.
   */
  class RuntimeManager {
   public:
    enum class Error { EMPTY_STATE_CODE = 1 };

    RuntimeManager(
        std::shared_ptr<extensions::ExtensionFactory> extension_factory,
        std::shared_ptr<WasmModuleFactory> module_factory,
        std::shared_ptr<TrieStorageProvider> storage_provider,
        std::shared_ptr<crypto::Hasher> hasher);

    outcome::result<RuntimeEnvironment> createPersistentRuntimeEnvironment(
        const base::Buffer &state_code);

    outcome::result<RuntimeEnvironment> createEphemeralRuntimeEnvironment(
        const base::Buffer &state_code);

    /**
     * @warning calling this with an \arg state_root older than the current root
     * will reset the storage to an older state once changes are committed
     */
    outcome::result<RuntimeEnvironment> createPersistentRuntimeEnvironmentAt(
        const base::Buffer &state_code, const base::Hash256 &state_root);

    outcome::result<RuntimeEnvironment> createEphemeralRuntimeEnvironmentAt(
        const base::Buffer &state_code, const base::Hash256 &state_root);

   private:
    outcome::result<RuntimeEnvironment> createRuntimeEnvironment(
        const base::Buffer &state_code);

    base::Logger logger_ = base::createLogger("Runtime manager");

    std::shared_ptr<TrieStorageProvider> storage_provider_;
    std::shared_ptr<extensions::ExtensionFactory> extension_factory_;
    std::shared_ptr<WasmModuleFactory> module_factory_;
    std::shared_ptr<crypto::Hasher> hasher_;

    std::mutex modules_mutex_;
    std::map<base::Hash256, std::shared_ptr<WasmModule>> modules_;

    static thread_local std::shared_ptr<RuntimeExternalInterface>
        external_interface_;
  };

}  // namespace sgns::runtime::binaryen

OUTCOME_HPP_DECLARE_ERROR_2(sgns::runtime::binaryen, RuntimeManager::Error);

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_API_RUNTIME_MANAGER
