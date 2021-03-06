
#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_ENVIRONMENT
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_ENVIRONMENT

#include <memory>

#include <boost/optional.hpp>

#include "outcome/outcome.hpp"

namespace sgns::base {
  class Buffer;
}

namespace sgns::storage::trie {
  class TopperTrieBatch;
  class TrieBatch;
}  // namespace sgns::storage::trie

namespace sgns::runtime {
  class WasmMemory;
}

namespace sgns::runtime::binaryen {
  class RuntimeExternalInterface;
  class WasmModuleInstance;
  class WasmModule;

  /**
   * Runtime environment is a structure that contains data necessary to operate
   * with Runtime: memory, WASM module, storage batch
   */
  class RuntimeEnvironment {
   public:
    static outcome::result<RuntimeEnvironment> create(
        const std::shared_ptr<RuntimeExternalInterface> &rei,
        const std::shared_ptr<WasmModule> &module,
        const base::Buffer &state_code);

    RuntimeEnvironment(RuntimeEnvironment &&) = default;
    RuntimeEnvironment &operator=(RuntimeEnvironment &&) = default;

    RuntimeEnvironment(const RuntimeEnvironment &) = delete;
    RuntimeEnvironment &operator=(const RuntimeEnvironment &) = delete;

    ~RuntimeEnvironment() = default;

    std::shared_ptr<WasmModuleInstance> module_instance;
    std::shared_ptr<WasmMemory> memory;
    boost::optional<std::shared_ptr<storage::trie::TopperTrieBatch>>
        batch;  // in persistent environments all changes of a call must be
                // either applied together or discarded in case of failure
  };

}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_ENVIRONMENT
