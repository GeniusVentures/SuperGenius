#ifdef __ANDROID__

#include "local_secure_storage/impl/Android.hpp"

namespace sgns
{
    using SecureStorageImpl = AndroidSecureStorage;
}

#elif defined( __linux__ )

#include "local_secure_storage/impl/Linux.hpp"

namespace sgns
{
    using SecureStorageImpl = LinuxSecureStorage;
}

#elif defined( _WIN32 )

#include "local_secure_storage/impl/Windows.hpp"

namespace sgns
{
    using SecureStorageImpl = WindowsSecureStorage;
}

#elif defined( __APPLE__ )

#include "local_secure_storage/impl/Apple.hpp"

namespace sgns
{
    using SecureStorageImpl = AppleSecureStorage;
}

#endif
