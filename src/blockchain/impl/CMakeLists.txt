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

add_library(block_tree
    block_tree_impl.cpp
    block_tree_impl.hpp
)
target_link_libraries(block_tree
    PUBLIC
    block_tree_error
    blockchain_common
    block_header_repository
    hasher
)
supergenius_install(block_tree)