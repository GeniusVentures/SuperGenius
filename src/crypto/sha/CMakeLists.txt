

add_library(sha
    sha256.hpp
    sha256.cpp
    )
target_link_libraries(sha
    PUBLIC OpenSSL::SSL
    OpenSSL::Crypto
    buffer
    blob
    )
supergenius_install(sha)
