#ifndef SUPERGENIUS_TEST_SRC_EXTENSIONS_MOCK_EXTENSION_FACTORY_HPP
#define SUPERGENIUS_TEST_SRC_EXTENSIONS_MOCK_EXTENSION_FACTORY_HPP

#include "extensions/extension_factory.hpp"

#include <gmock/gmock.h>

namespace sgns::extensions {

  class ExtensionFactoryMock : public ExtensionFactory {
   public:
    MOCK_CONST_METHOD2(createExtension,
                       std::unique_ptr<Extension>(
                           std::shared_ptr<runtime::WasmMemory>,
                           std::shared_ptr<runtime::TrieStorageProvider> storage));
  };

}  // namespace sgns::extensions

#endif  // SUPERGENIUS_TEST_SRC_EXTENSIONS_MOCK_EXTENSION_FACTORY_HPP
