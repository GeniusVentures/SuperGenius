add_library(trustedpeer
    CanonicalTrustCodec.cpp
    genesis_tool/GenesisCeremony.cpp
    GenesisManifest.cpp
    QuorumPolicy.cpp
    TrustStateStore.cpp
    TrustedPeerRegistry.cpp
)
if(WIN32)
    target_sources(trustedpeer PRIVATE genesis_tool/GenesisCeremonyPlatformWindows.cpp)
else()
    target_sources(trustedpeer PRIVATE genesis_tool/GenesisCeremonyPlatformPosix.cpp)
endif()
target_link_libraries(trustedpeer
    PUBLIC
    securecrdt
    sgns_genius_account
    PRIVATE
    hexutil
    sha
    rocksdb
    OpenSSL::Crypto
)
if(WIN32)
    target_link_libraries(trustedpeer PRIVATE Advapi32)
endif()
supergenius_install(trustedpeer)
add_subdirectory(genesis_tool)
