#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_WASM_MODULE
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_WASM_MODULE

#include "runtime/binaryen/module/wasm_module_instance.hpp"
#include "runtime/binaryen/runtime_external_interface.hpp"

namespace sgns::runtime::binaryen {

  /**
   * A wrapper for binaryen's wasm::Module and wasm::ModuleInstance
   */
  class WasmModule {
   public:
    virtual ~WasmModule() = default;

    virtual std::unique_ptr<WasmModuleInstance> instantiate(
        const std::shared_ptr<RuntimeExternalInterface> &externalInterface)
        const = 0;
  };

}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_WASM_MODULE
