#ifndef SGNS_SECURE_STORAGE_HPP
#define SGNS_SECURE_STORAGE_HPP

// Compile-time override for tests: -DSGNS_USE_MEMORY_SECURE_STORAGE
// Uses in-memory storage — no keychain prompts, no file I/O, no cleanup.
#ifdef SGNS_USE_MEMORY_SECURE_STORAGE

#include "local_secure_storage/impl/MemorySecureStorage.hpp"

namespace sgns
{
    using SecureStorageImpl = MemorySecureStorage;
}

#elif defined( __ANDROID__ )

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

#endif // SGNS_SECURE_STORAGE_HPP
