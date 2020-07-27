
#ifndef SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_WASM_PROVIDER_MOCK
#define SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_WASM_PROVIDER_MOCK

#include "runtime/wasm_provider.hpp"

#include <gmock/gmock.h>

namespace sgns::runtime {

  class WasmProviderMock: public WasmProvider {
   public:
    MOCK_CONST_METHOD0(getStateCode, const base::Buffer &());
  };

}

#endif  // SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_WASM_PROVIDER_MOCK
