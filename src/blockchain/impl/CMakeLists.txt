add_proto_library(SGBlocksProto proto/SGBlocks.proto)

add_library(blockchain_common
    types.cpp
    common.hpp
    storage_util.cpp
    storage_util.hpp
)
target_link_libraries(blockchain_common
    PUBLIC
    primitives
    crdt_globaldb
    PRIVATE
    Boost::headers
    in_memory_storage
    trie_storage
    trie_storage_backend
    supergenius_trie
)
supergenius_install(blockchain_common)
