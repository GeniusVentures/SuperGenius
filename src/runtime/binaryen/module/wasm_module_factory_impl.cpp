#include "runtime/binaryen/module/wasm_module_factory_impl.hpp"

#include "runtime/binaryen/module/wasm_module_impl.hpp"

namespace sgns::runtime::binaryen {

  outcome::result<std::unique_ptr<WasmModule>>
  WasmModuleFactoryImpl::createModule(
      const base::Buffer &code,
      std::shared_ptr<RuntimeExternalInterface> rei) const {
    auto res = WasmModuleImpl::createFromCode(code, rei);
    if (res.has_value()) {
      return std::unique_ptr<WasmModule>(res.value().release());
    }
    return res.error();
  }

}  // namespace sgns::runtime::binaryen
