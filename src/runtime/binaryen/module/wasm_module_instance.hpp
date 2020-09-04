
#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_MODULE_WASM_MODULE_INSTANCE
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_MODULE_WASM_MODULE_INSTANCE

#include <binaryen/literal.h>
#include <binaryen/support/name.h>

namespace sgns::runtime::binaryen {

  /**
   * Wrapper for wasm::ModuleInstance
   */
  class WasmModuleInstance {
   public:
    virtual ~WasmModuleInstance() = default;

    /**
     * @param name the name of a wasm function to call
     * @param arguments the list of arguments to pass to the function
     * @return whatever the export function returns
     */
    virtual wasm::Literal callExport(
        wasm::Name name, const std::vector<wasm::Literal> &arguments) = 0;
  };
}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_MODULE_WASM_MODULE_INSTANCE
