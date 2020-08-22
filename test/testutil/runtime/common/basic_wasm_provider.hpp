
#ifndef SUPERGENIUS_TEST_TESTUTIL_RUNTIME_BASIC_WASM_PROVIDER_HPP
#define SUPERGENIUS_TEST_TESTUTIL_RUNTIME_BASIC_WASM_PROVIDER_HPP

#include "runtime/wasm_provider.hpp"

namespace sgns::runtime {

  class BasicWasmProvider : public sgns::runtime::WasmProvider {
   public:
    explicit BasicWasmProvider(std::string_view path);

    ~BasicWasmProvider() override = default;

    const sgns::base::Buffer &getStateCode() const override;

   private:
    void initialize(std::string_view path);

    sgns::base::Buffer buffer_;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_TEST_TESTUTIL_RUNTIME_BASIC_WASM_PROVIDER_HPP
