#include "runtime/binaryen/wasm_executor.hpp"

#include <utility>

#include <binaryen/shell-interface.h>
#include <binaryen/wasm-binary.h>

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::runtime::binaryen, WasmExecutor::Error, e) {
  using sgns::runtime::binaryen::WasmExecutor;
  switch (e) {
    case WasmExecutor::Error::EXECUTION_ERROR:
      return "An error occurred during an export call execution";
  }
}

namespace sgns::runtime::binaryen {

  outcome::result<wasm::Literal> WasmExecutor::call(
      WasmModuleInstance &module_instance,
      wasm::Name method_name,
      const std::vector<wasm::Literal> &args) {
    try {
      return module_instance.callExport(wasm::Name(method_name), args);
    } catch (wasm::ExitException &e) {
      return Error::EXECUTION_ERROR;
    } catch (wasm::TrapException &e) {
      return Error::EXECUTION_ERROR;
    }
  }

}  // namespace sgns::runtime::binaryen
