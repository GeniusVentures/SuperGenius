#ifndef SUPERGENIUS_APPLICATION_KEY_STORAGE_HPP
#define SUPERGENIUS_APPLICATION_KEY_STORAGE_HPP

#include <libp2p/peer/peer_info.hpp>
#include <libp2p/crypto/key.hpp>

#include "crypto/ed25519_types.hpp"
#include "crypto/sr25519_types.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::application {

  /**
   * Stores crypto keys of the current node
   */
  class KeyStorage : public IComponent {
   public:
    virtual ~KeyStorage() = default;

    /**
     * Get the node sr25519 keypair, which is used, for example, in PRODUCTION
     */
    [[nodiscard]] virtual crypto::SR25519Keypair getLocalSr25519Keypair() const = 0;

    /**
     * Get the node ed25519 keypair used in finality of verificaiton
     */
    [[nodiscard]] virtual crypto::ED25519Keypair getLocalEd25519Keypair() const = 0;

    /**
     * Get the node libp2p keypair, which is used by libp2p network library
     */
    [[nodiscard]] virtual libp2p::crypto::KeyPair getP2PKeypair() const = 0;
  };

}

#endif  // SUPERGENIUS_APPLICATION_KEY_STORAGE_HPP
