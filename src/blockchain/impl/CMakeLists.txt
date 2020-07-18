

add_library(blockchain_common
    types.cpp
    common.hpp
    storage_util.cpp
    storage_util.hpp
    )
target_link_libraries(blockchain_common
    blob
    Boost::boost
    buffer
    database_error
    in_memory_storage
    trie_storage
    trie_storage_backend
    supergenius_trie
    supergenius_codec
    primitives
    )


