

#ifndef SUPERGENIUS_SRC_RUNTIME_CORE_FACTORY
#define SUPERGENIUS_SRC_RUNTIME_CORE_FACTORY

#include "runtime/core.hpp"

namespace sgns::runtime {

  namespace binaryen {
    class RuntimeManager;
  }

  /**
   * An abstract factory that enables construction of Core objects over specific
   * WASM code
   */
  class CoreFactory {
   public:
    virtual ~CoreFactory() = default;

    /**
     * Creates a Core API object backed by the code that \arg wasm_provider
     * serves
     */
    virtual std::unique_ptr<Core> createWithCode(
        std::shared_ptr<WasmProvider> wasm_provider) = 0;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_CORE_FACTORY
