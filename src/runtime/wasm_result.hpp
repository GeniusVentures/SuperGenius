
#ifndef SUPERGENIUS_SRC_RUNTIME_WASM_RESULT_HPP
#define SUPERGENIUS_SRC_RUNTIME_WASM_RESULT_HPP

#include "runtime/types.hpp"

namespace sgns::runtime {
  /**
   * Result of a call to a Runtime API wasm function is an i64 where first 32
   * bits are the address and next 32 bits are the size of the returned buffer.
   */
  struct WasmResult {

    explicit constexpr WasmResult(WasmSpan v) {
      auto [ptr, len] = splitSpan(v);
      address = ptr;
      length = len;
    }

    constexpr WasmResult(WasmPointer ptr, WasmSize size)
        : address{ptr}, length{size} {}

    /**
     * @brief makes combined pointer-size result from address
     * @return pointer-size uint64_t value
     */
    WasmSpan combine() const {
      return static_cast<WasmSpan>(address)
             | (static_cast<WasmSpan>(length) << 32ull);
    }

    bool operator==(const WasmResult &rhs) const {
      return address == rhs.address && length == rhs.length;
    }

    WasmPointer address = 0u;  ///< address of buffer result
    WasmSize length = 0u;      ///< length of buffer result
  };
}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_WASM_RESULT_HPP
