#ifndef SUPERGENIUS_CRDT_KEYPAIR_FILE_STORAGE_HPP
#define SUPERGENIUS_CRDT_KEYPAIR_FILE_STORAGE_HPP

#include <outcome/outcome.hpp>
#include <base/logger.hpp>
#include <boost/filesystem/path.hpp>
#include <libp2p/crypto/key.hpp>

namespace sgns::crdt
{
class KeyPairFileStorage
{
public:
    KeyPairFileStorage( boost::filesystem::path keyPath );

    [[nodiscard]] outcome::result<libp2p::crypto::KeyPair> GetKeyPair() const;

private:
    boost::filesystem::path m_keyPath;
    sgns::base::Logger m_logger = sgns::base::createLogger("KeyPairFileStorage");
};
}

#endif // SUPERGENIUS_CRDT_KEYPAIR_FILE_STORAGE_HPP
