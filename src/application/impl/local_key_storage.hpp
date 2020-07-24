
#ifndef SUPERGENIUS_APPLICATION_LOCAL_KEY_STORAGE_HPP
#define SUPERGENIUS_APPLICATION_LOCAL_KEY_STORAGE_HPP

#include "application/key_storage.hpp"

#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>

#include "base/buffer.hpp"

namespace sgns::application {

  class LocalKeyStorage : public KeyStorage {
   public:
    static outcome::result<std::shared_ptr<LocalKeyStorage>> create(
        const std::string &keystore_path);

    ~LocalKeyStorage() override = default;

    crypto::SR25519Keypair getLocalSr25519Keypair() const override;
    crypto::ED25519Keypair getLocalEd25519Keypair() const override;
    libp2p::crypto::KeyPair getP2PKeypair() const override;

   private:
    LocalKeyStorage() = default;

    /**
     * Loads a keystore from the provided file
     */
    outcome::result<void> loadFromJson(const std::string &file_path);

    outcome::result<void> loadSR25519Keys(
        const boost::property_tree::ptree &tree);
    outcome::result<void> loadED25519Keys(
        const boost::property_tree::ptree &tree);
    outcome::result<void> loadP2PKeys(const boost::property_tree::ptree &tree);

    crypto::SR25519Keypair sr_25519_keypair_;
    crypto::ED25519Keypair ed_25519_keypair_;
    libp2p::crypto::KeyPair p2p_keypair_;
  };

}  // namespace sgns::application

#endif  // SUPERGENIUS_APPLICATION_LOCAL_KEY_STORAGE_HPP
