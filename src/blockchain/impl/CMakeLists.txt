add_proto_library(SGBlocksProto proto/SGBlocks.proto)

add_library(blockchain_common
    types.cpp
    common.hpp
    storage_util.cpp
    storage_util.hpp
    )
target_link_libraries(blockchain_common
    primitives
    Boost::boost
    in_memory_storage
    trie_storage
    trie_storage_backend
    supergenius_trie
    crdt_globaldb
    )

add_library(block_tree
    block_tree_impl.cpp
    block_tree_impl.hpp
    )
target_link_libraries(block_tree
    block_tree_error
    blockchain_common
    block_header_repository
    rocksdb
    hasher
    )
supergenius_install(blockchain_common)
supergenius_install(block_tree)