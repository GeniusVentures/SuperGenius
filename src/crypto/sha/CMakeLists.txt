add_library(sha
    sha256.hpp
    sha256.cpp
)
target_link_libraries(sha
    PUBLIC
    blob
    PRIVATE
    OpenSSL::Crypto
)
supergenius_install(sha)
