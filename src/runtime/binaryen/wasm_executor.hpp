#ifndef SUPERGENIUS_SRC_RUNTIME_WASM_EXECUTOR_IMPL_HPP
#define SUPERGENIUS_SRC_RUNTIME_WASM_EXECUTOR_IMPL_HPP

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "runtime/binaryen/module/wasm_module.hpp"

namespace sgns::runtime::binaryen {

  /**
   * @brief WasmExecutor is the helper to execute export functions from wasm
   * runtime
   * @note This class is implementation detail and should never be used outside
   * this directory
   */
  class WasmExecutor {
   public:
    enum class Error { EXECUTION_ERROR = 1 };

    outcome::result<wasm::Literal> call(WasmModule &module_instance,
                                        wasm::Name method_name,
                                        const std::vector<wasm::Literal> &args);
  };

}  // namespace sgns::runtime::binaryen

OUTCOME_HPP_DECLARE_ERROR_2(sgns::runtime::binaryen, WasmExecutor::Error);

#endif  // SUPERGENIUS_SRC_RUNTIME_WASM_EXECUTOR_IMPL_HPP
