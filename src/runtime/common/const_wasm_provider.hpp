

#ifndef SUPERGENIUS_SRC_RUNTIME_COMMON_CONST_WASM_PROVIDER
#define SUPERGENIUS_SRC_RUNTIME_COMMON_CONST_WASM_PROVIDER

#include "runtime/wasm_provider.hpp"

namespace sgns::runtime {

  class ConstWasmProvider : public WasmProvider {
   public:
    explicit ConstWasmProvider(base::Buffer code);

    const base::Buffer &getStateCode() const override;

   private:
    base::Buffer code_;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_COMMON_CONST_WASM_PROVIDER
