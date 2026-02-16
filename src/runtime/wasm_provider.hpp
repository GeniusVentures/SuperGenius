#ifndef SUPERGENIUS_SRC_RUNTIME_WASM_PROVIDER_HPP
#define SUPERGENIUS_SRC_RUNTIME_WASM_PROVIDER_HPP

#include "base/buffer.hpp"

namespace sgns::runtime {
  /**
   * @brief Interface for accessing WASM state/runtime code.
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
