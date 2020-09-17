#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_WASM_MODULE_IMPL
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_WASM_MODULE_IMPL

#include "base/buffer.hpp"
#include "runtime/binaryen/module/wasm_module.hpp"

namespace wasm {
  using namespace ::wasm;  // NOLINT(google-build-using-namespace)
  class Module;
}  // namespace wasm

namespace sgns::runtime::binaryen {

  /**
   * Stores a wasm::Module and a wasm::Module instance which contains the module
   * and the provided runtime external interface
   */
  class WasmModuleImpl final : public WasmModule {
   public:
    enum class Error { EMPTY_STATE_CODE = 1, INVALID_STATE_CODE };

    WasmModuleImpl(WasmModuleImpl&&) = default;
    WasmModuleImpl& operator=(WasmModuleImpl&&) = default;

    WasmModuleImpl(const WasmModuleImpl&) = delete;
    WasmModuleImpl& operator=(const WasmModuleImpl&) = delete;

    ~WasmModuleImpl() override = default;

    static outcome::result<std::unique_ptr<WasmModuleImpl>> createFromCode(
        const base::Buffer &code,
        const std::shared_ptr<RuntimeExternalInterface> &rei);

    std::unique_ptr<WasmModuleInstance> instantiate(
        const std::shared_ptr<RuntimeExternalInterface> &externalInterface)
        const override;

   private:
    explicit WasmModuleImpl(std::unique_ptr<wasm::Module> &&module);

    std::unique_ptr<wasm::Module> module_;
  };

}  // namespace sgns::runtime::binaryen

OUTCOME_HPP_DECLARE_ERROR_2(sgns::runtime::binaryen, WasmModuleImpl::Error);

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_WASM_MODULE_IMPL
