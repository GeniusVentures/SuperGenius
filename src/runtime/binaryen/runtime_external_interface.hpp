
#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_EXTERNAL_INTERFACE_HPP
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_EXTERNAL_INTERFACE_HPP

#include <binaryen/shell-interface.h>

#include "base/logger.hpp"
#include "extensions/extension_factory.hpp"
#include "runtime/wasm_memory.hpp"
#include "runtime/trie_storage_provider.hpp"

namespace sgns::runtime::binaryen {

  class RuntimeExternalInterface : public wasm::ShellExternalInterface {
   public:
    explicit RuntimeExternalInterface(
        const std::shared_ptr<extensions::ExtensionFactory>& extension_factory,
        std::shared_ptr<TrieStorageProvider> storage_provider);

    wasm::Literal callImport(wasm::Function *import,
                             wasm::LiteralList &arguments) override;

    inline std::shared_ptr<WasmMemory> memory() const {
      return extension_->memory();
    }

   private:
    /**
     * Checks that the number of arguments is as expected and terminates the
     * program if it is not
     */
    void checkArguments(std::string_view extern_name,
                        size_t expected,
                        size_t actual);

    std::shared_ptr<extensions::Extension> extension_;
    base::Logger logger_ = base::createLogger(kDefaultLoggerTag);

    constexpr static auto kDefaultLoggerTag = "Runtime external interface";
  };

}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_RUNTIME_EXTERNAL_INTERFACE_HPP
