
#ifndef SUPERGENIUS_SRC_MISC_EXTENSION_HPP
#define SUPERGENIUS_SRC_MISC_EXTENSION_HPP

#include <cstdint>
#include <functional>
#include <memory>

#include "base/logger.hpp"
#include "outcome/outcome.hpp"
#include "runtime/types.hpp"
#include "runtime/wasm_result.hpp"

namespace sgns::runtime {
  class WasmMemory;
  class CoreFactory;
  class Core;
  class WasmProvider;
}  // namespace sgns::runtime

namespace sgns::extensions {
  /**
   * Implements miscellaneous extension functions
   */
  class MiscExtension final {
   public:
    using CoreFactoryMethod =
        std::function<std::unique_ptr<runtime::Core>(
            std::shared_ptr<runtime::WasmProvider>)>;

    MiscExtension(uint64_t chain_id,
                  std::shared_ptr<runtime::WasmMemory> memory,
                  CoreFactoryMethod core_factory_method);

    ~MiscExtension() = default;

    /**
     * @return id (a 64-bit unsigned integer) of the current chain
     */
    uint64_t ext_chain_id() const;

    runtime::WasmResult ext_misc_runtime_version_version_1(
        runtime::WasmSpan data) const;

   private:
    CoreFactoryMethod core_factory_method_;
    std::shared_ptr<runtime::WasmMemory> memory_;
    base::Logger logger_;
    const uint64_t chain_id_ = 42;
  };
}  // namespace sgns::extensions

#endif  // SUPERGENIUS_SRC_MISC_EXTENSION_HPP
