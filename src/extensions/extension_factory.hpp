#ifndef SUPERGENIUS_SRC_EXTENSIONS_EXTENSION_FACTORY_HPP
#define SUPERGENIUS_SRC_EXTENSIONS_EXTENSION_FACTORY_HPP

#include "extensions/extension.hpp"

#include "runtime/trie_storage_provider.hpp"
namespace sgns::runtime {
  class CoreFactory;
}
namespace sgns::extensions {

  /**
   * Creates extension containing provided wasm memory
   */
  class ExtensionFactory {
   public:
    virtual ~ExtensionFactory() = default;

    /**
     * Takes \param memory and creates \return extension using this memory
     */
    virtual std::unique_ptr<Extension> createExtension(
        std::shared_ptr<runtime::WasmMemory> memory,
        std::shared_ptr<runtime::TrieStorageProvider> storage_provider) const = 0;
  };

}

#endif
