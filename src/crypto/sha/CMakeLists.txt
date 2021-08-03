

add_library(sha
    sha256.hpp
    sha256.cpp
    )
set_target_properties(sha PROPERTIES PUBLIC_HEADER "sha256.hpp")
target_link_libraries(sha
    PUBLIC OpenSSL::SSL
    OpenSSL::Crypto
    buffer
    blob
    )
supergenius_install(sha)
