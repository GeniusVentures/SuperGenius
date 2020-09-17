
#include "runtime/binaryen/runtime_environment.hpp"

#include "crypto/hasher.hpp"
#include "runtime/binaryen/module/wasm_module.hpp"

namespace sgns::runtime::binaryen {

  outcome::result<RuntimeEnvironment> RuntimeEnvironment::create(
      const std::shared_ptr<RuntimeExternalInterface> &rei,
      const std::shared_ptr<WasmModule> &module,

      const base::Buffer &state_code) {

    return RuntimeEnvironment{
        module->instantiate(rei), rei->memory(), boost::none};
  }

}  // namespace sgns::runtime::binaryen
