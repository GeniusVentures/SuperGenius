
#include "runtime/common/const_wasm_provider.hpp"

namespace sgns::runtime {

  ConstWasmProvider::ConstWasmProvider(base::Buffer code)
      : code_{std::move(code)} {}

  const base::Buffer &ConstWasmProvider::getStateCode() const {
    return code_;
  }

}  // namespace sgns::runtime
