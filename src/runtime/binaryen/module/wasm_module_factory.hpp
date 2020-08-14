#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_MODULE_WASM_MODULE_FACTORY
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_MODULE_WASM_MODULE_FACTORY

#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
#include "runtime/binaryen/module/wasm_module.hpp"
#include "runtime/binaryen/runtime_external_interface.hpp"

namespace sgns::runtime::binaryen {

  /**
   * An abstract factory to produce WasmModules
   */
  class WasmModuleFactory {
   public:
    virtual ~WasmModuleFactory() = default;

    /**
     * A module will be created with the provided \arg code and instantiated
     * with \arg rei (runtime external interface)
     * @return the module in case of success
     */
    virtual outcome::result<std::unique_ptr<WasmModule>> createModule(
        const base::Buffer &code,
        std::shared_ptr<RuntimeExternalInterface> rei) const = 0;
  };

}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_MODULE_WASM_MODULE_FACTORY
