add_library(trustedpeer
    TrustedPeerRegistry.cpp
)
target_link_libraries(trustedpeer
    PUBLIC
    securecrdt
)
supergenius_install(trustedpeer)
