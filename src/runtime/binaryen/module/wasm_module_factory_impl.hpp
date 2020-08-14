#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_MODULE_WASM_MODULE_FACTORY_IMPL
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_MODULE_WASM_MODULE_FACTORY_IMPL

#include "runtime/binaryen/module/wasm_module_factory.hpp"

namespace sgns::runtime::binaryen {

  class WasmModuleFactoryImpl final : public WasmModuleFactory {
   public:
    ~WasmModuleFactoryImpl() override = default;

    outcome::result<std::unique_ptr<WasmModule>> createModule(
        const base::Buffer &code,
        std::shared_ptr<RuntimeExternalInterface> rei) const override;
  };

}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_MODULE_WASM_MODULE_FACTORY_IMPL
