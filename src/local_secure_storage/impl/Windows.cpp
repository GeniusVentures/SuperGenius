#include "Windows.hpp"

namespace sgns
{
    outcome::result<ISecureStorage::SecureBufferType> WindowsSecureStorage::Load( const std::string &key ) {}

    outcome::result<void> WindowsSecureStorage::Save( const std::string &key, const SecureBufferType &buffer ) {}

    outcome::result<bool> WindowsSecureStorage::DeleteKey( const std::string &key ) {}
}
