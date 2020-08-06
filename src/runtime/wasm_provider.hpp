#ifndef SUPERGENIUS_SRC_RUNTIME_WASM_PROVIDER_HPP
#define SUPERGENIUS_SRC_RUNTIME_WASM_PROVIDER_HPP

#include "base/buffer.hpp"

namespace sgns::runtime {
  /**
   * @class WasmProvider keeps and provides wasm state code
   */
  class WasmProvider {
   public:
    virtual ~WasmProvider() = default;

    /**
     * @return wasm runtime code
     */
    virtual const base::Buffer &getStateCode() const = 0;
  };
}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_WASM_PROVIDER_HPP
