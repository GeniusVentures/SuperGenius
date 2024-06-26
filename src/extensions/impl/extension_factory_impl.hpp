#ifndef SUPERGENIUS_SRC_EXTENSIONS_IMPL_EXTENSION_FACTORY_IMPL_HPP
#define SUPERGENIUS_SRC_EXTENSIONS_IMPL_EXTENSION_FACTORY_IMPL_HPP

#include "extensions/extension_factory.hpp"

#include "extensions/impl/misc_extension.hpp"
#include "crypto/bip39/bip39_provider.hpp"
#include "crypto/crypto_store.hpp"
#include "crypto/ed25519_provider.hpp"
#include "crypto/hasher.hpp"
#include "crypto/secp256k1_provider.hpp"
#include "crypto/sr25519_provider.hpp"
#include "storage/changes_trie/changes_tracker.hpp"

namespace sgns::extensions {

  class ExtensionFactoryImpl : public ExtensionFactory {
   public:
    ~ExtensionFactoryImpl() override = default;

    ExtensionFactoryImpl(
        std::shared_ptr<storage::changes_trie::ChangesTracker> tracker,
        std::shared_ptr<crypto::SR25519Provider> sr25519_provider,
        std::shared_ptr<crypto::ED25519Provider> ed25519_provider,
        std::shared_ptr<crypto::Secp256k1Provider> secp256k1_provider,
        std::shared_ptr<crypto::Hasher> hasher,
        std::shared_ptr<crypto::CryptoStore> crypto_store,
        std::shared_ptr<crypto::Bip39Provider> bip39_provider,
        MiscExtension::CoreFactoryMethod core_factory_method);

    std::unique_ptr<Extension> createExtension(
        std::shared_ptr<runtime::WasmMemory> memory,
        std::shared_ptr<runtime::TrieStorageProvider> storage_provider)
        const override;

   private:
    std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker_;
    std::shared_ptr<crypto::SR25519Provider> sr25519_provider_;
    std::shared_ptr<crypto::ED25519Provider> ed25519_provider_;
    std::shared_ptr<crypto::Secp256k1Provider> secp256k1_provider_;
    std::shared_ptr<crypto::Hasher> hasher_;
    std::shared_ptr<crypto::CryptoStore> crypto_store_;
    std::shared_ptr<crypto::Bip39Provider> bip39_provider_;
    MiscExtension::CoreFactoryMethod core_factory_method_;
  };

}

#endif
